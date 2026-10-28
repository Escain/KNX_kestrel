#include "kestrel/catalog/Knxprod.hpp"

#include "Parser.hpp"

#include "kestrel/archive/ZipReader.hpp"

#include <QFile>
#include <QSet>
#include <QXmlStreamReader>

namespace kestrel::catalog
{

using namespace Qt::StringLiterals;

namespace
{

constexpr std::size_t kMaxWarnings = 1000;

/// "M-0083", "M-FFFF": the per-manufacturer folder of a product file.
bool isManufacturerFolder(QStringView folder)
{
	if (folder.size() != 6 || !folder.startsWith(u"M-"))
	{
		return false;
	}
	for (const QChar c : folder.mid(2))
	{
		if (!(c.isDigit() || (c >= u'A' && c <= u'F') || (c >= u'a' && c <= u'f')))
		{
			return false;
		}
	}
	return true;
}

enum class FileRole
{
	Ignored,
	MasterData,
	Hardware,
	Catalog,
	Program, ///< M-XXXX/M-XXXX_A-*.xml
	Other,   ///< any other M-XXXX/*.xml: parsed leniently
};

FileRole roleOf(const QString& path)
{
	const QStringList parts = path.split(u'/');
	if (parts.size() == 1 && parts[0].compare(u"knx_master.xml"_s, Qt::CaseInsensitive) == 0)
	{
		return FileRole::MasterData;
	}
	if (parts.size() != 2 || !isManufacturerFolder(parts[0]) || !parts[1].endsWith(u".xml"_s, Qt::CaseInsensitive))
	{
		return FileRole::Ignored;
	}
	const QString& file = parts[1];
	if (file.compare(u"Hardware.xml"_s, Qt::CaseInsensitive) == 0)
	{
		return FileRole::Hardware;
	}
	if (file.compare(u"Catalog.xml"_s, Qt::CaseInsensitive) == 0)
	{
		return FileRole::Catalog;
	}
	if (file.compare(u"Baggages.xml"_s, Qt::CaseInsensitive) == 0)
	{
		return FileRole::Ignored;
	}
	if (file.startsWith(parts[0] + u"_A-"_s, Qt::CaseInsensitive))
	{
		return FileRole::Program;
	}
	return FileRole::Other;
}

/// Translation units go to the application program whose id prefixes the unit's RefId, else to the package.
void distributeTranslations(detail::ParseState& state)
{
	for (auto& [unit, translations] : state.unitTranslations)
	{
		Translations* target = &state.database.translations;
		for (ApplicationProgram& app : state.database.applicationPrograms)
		{
			if (unit.startsWith(app.id))
			{
				target = &app.translations;
				break;
			}
		}
		target->merge(translations);
	}
	state.unitTranslations.clear();
}

/// Dangling references become warnings (the file is still usable for everything else).
void checkReferences(detail::ParseState& state)
{
	const ProductDatabase& db = state.database;
	auto warn = [&state](const QString& file, const QString& message) {
		state.warnings.push_back({file, 0, message});
	};
	for (const Hardware& hardware : db.hardware)
	{
		for (const Hardware2Program& link : hardware.hardware2Programs)
		{
			for (const QString& ref : link.applicationProgramRefs)
			{
				if (!db.findApplicationProgram(ref))
				{
					warn(QString(), u"application program %1 (of %2) is not in this product file"_s.arg(ref, link.id));
				}
			}
		}
	}
	for (const CatalogItem& item : db.catalogItems)
	{
		if (!item.productRefId.isEmpty() && !db.findProduct(item.productRefId))
		{
			warn(QString(), u"catalog item %1 refers to unknown product %2"_s.arg(item.id, item.productRefId));
		}
	}
	for (const ApplicationProgram& app : db.applicationPrograms)
	{
		QSet<QString> objects;
		for (const ComObject& object : app.comObjects)
		{
			objects.insert(object.id);
		}
		for (const ComObjectRef& ref : app.comObjectRefs)
		{
			if (!objects.contains(ref.refId))
			{
				warn(app.id, u"ComObjectRef %1 refers to unknown ComObject %2"_s.arg(ref.id, ref.refId));
			}
		}
		QSet<QString> parameters;
		for (const Parameter& parameter : app.parameters)
		{
			parameters.insert(parameter.id);
		}
		for (const ParameterRef& ref : app.parameterRefs)
		{
			if (!parameters.contains(ref.refId))
			{
				warn(app.id, u"ParameterRef %1 refers to unknown Parameter %2"_s.arg(ref.id, ref.refId));
			}
		}
	}
}

} // namespace

std::ostream& operator<<(std::ostream& os, const LoadedDatabase& value)
{
	os << "LoadedDatabase{" << value.database << " warnings=[";
	for (std::size_t i = 0; i < value.warnings.size(); ++i)
	{
		os << (i ? "; " : "") << value.warnings[i];
	}
	return os << "]}";
}

namespace
{

LoadControl readLoadControl(QXmlStreamReader& xml)
{
	LoadControl control;
	control.element = xml.name().toString();
	for (const QXmlStreamAttribute& attribute : xml.attributes())
	{
		control.attributes[attribute.name().toString()] = attribute.value().toString();
	}
	while (xml.readNextStartElement())
	{
		control.children.push_back(readLoadControl(xml));
	}
	return control;
}

/// knx_master.xml: MasterData/MaskVersions/MaskVersion with their Procedures (below HawkConfigurationData).
/// Everything else is skipped.
Result<std::vector<MaskVersionInfo>> parseMasterData(const QByteArray& content)
{
	std::vector<MaskVersionInfo> masks;
	QXmlStreamReader xml(content);
	while (!xml.atEnd())
	{
		xml.readNext();
		if (!xml.isStartElement() || xml.name() != u"MaskVersion")
		{
			continue;
		}
		MaskVersionInfo mask;
		const auto attributes = xml.attributes();
		mask.id = attributes.value(u"Id").toString();
		mask.maskVersion = attributes.value(u"MaskVersion").toUInt();
		mask.name = attributes.value(u"Name").toString();
		mask.managementModel = attributes.value(u"ManagementModel").toString();
		// Procedures sit below HawkConfigurationData; accept them at any depth inside the MaskVersion.
		int depth = 1;
		while (depth > 0 && !xml.atEnd())
		{
			xml.readNext();
			if (xml.isEndElement())
			{
				--depth;
				continue;
			}
			if (!xml.isStartElement())
			{
				continue;
			}
			if (xml.name() != u"Procedure")
			{
				++depth;
				continue;
			}
			MaskProcedure procedure;
			const auto procedureAttributes = xml.attributes();
			procedure.type = procedureAttributes.value(u"ProcedureType").toString();
			procedure.subType = procedureAttributes.value(u"ProcedureSubType").toString();
			procedure.access = procedureAttributes.value(u"Access").toString();
			while (xml.readNextStartElement())
			{
				procedure.controls.push_back(readLoadControl(xml));
			}
			mask.procedures.push_back(std::move(procedure));
		}
		masks.push_back(std::move(mask));
	}
	if (xml.hasError())
	{
		return makeError(ErrorCode::ParseError,
			u"knx_master.xml line %1: %2"_s.arg(xml.lineNumber()).arg(xml.errorString()).toStdString());
	}
	return masks;
}

} // namespace

Result<LoadedDatabase> parsePackage(const PackageFiles& files)
{
	std::vector<QString> ordered[4]; // Hardware, Catalog, Program, Other
	bool masterData = false;
	for (const auto& [path, content] : files)
	{
		switch (roleOf(path))
		{
			case FileRole::MasterData: masterData = true; break;
			case FileRole::Hardware: ordered[0].push_back(path); break;
			case FileRole::Catalog: ordered[1].push_back(path); break;
			case FileRole::Program: ordered[2].push_back(path); break;
			case FileRole::Other: ordered[3].push_back(path); break;
			case FileRole::Ignored: break;
		}
	}
	if (ordered[0].empty())
	{
		return makeError(ErrorCode::NotFound, "not a product file: no M-XXXX/Hardware.xml");
	}

	detail::ParseState state;
	state.database.hasMasterData = masterData;
	for (const auto& [path, content] : files)
	{
		if (roleOf(path) == FileRole::MasterData && !content.isEmpty())
		{
			auto masks = parseMasterData(content);
			if (masks)
			{
				state.database.maskVersions = std::move(*masks);
			}
			else
			{
				state.warnings.push_back({path, 0, QString::fromStdString(masks.error().message)});
			}
		}
	}
	for (int role = 0; role < 4; ++role)
	{
		for (const QString& path : ordered[role])
		{
			const Status parsed = detail::parseDocument(path, files.at(path), state);
			if (!parsed)
			{
				if (role < 3)
				{
					return Unexpected{parsed.error()};
				}
				state.warnings.push_back({path, 0, u"ignored: %1"_s.arg(QString::fromStdString(parsed.error().message))});
				continue;
			}
			state.database.files.append(path);
		}
	}
	if (ordered[1].empty())
	{
		state.warnings.push_back({QString(), 0, u"no M-XXXX/Catalog.xml: products have no catalog items"_s});
	}
	if (state.database.applicationPrograms.empty())
	{
		state.warnings.push_back({QString(), 0, u"no application program in this product file"_s});
	}
	distributeTranslations(state);
	checkReferences(state);
	if (state.warnings.size() > kMaxWarnings)
	{
		const std::size_t dropped = state.warnings.size() - kMaxWarnings;
		state.warnings.resize(kMaxWarnings);
		state.warnings.push_back({QString(), 0, u"%1 more warnings suppressed"_s.arg(dropped)});
	}
	return LoadedDatabase{std::move(state.database), std::move(state.warnings)};
}

Result<LoadedDatabase> loadKnxprod(Bytes data)
{
	KESTREL_TRY(const archive::ZipReader zip, archive::ZipReader::open(std::move(data)));
	PackageFiles files;
	for (const archive::ZipEntry& entry : zip.entries())
	{
		if (entry.isDirectory())
		{
			continue;
		}
		const QString path = QString::fromStdString(entry.name);
		switch (roleOf(path))
		{
			case FileRole::Ignored: continue;
			default: break;
		}
		KESTREL_TRY(const Bytes content, zip.read(entry));
		files[path] = QByteArray(reinterpret_cast<const char*>(content.data()), static_cast<qsizetype>(content.size()));
	}
	return parsePackage(files);
}

Result<LoadedDatabase> loadKnxprodFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		return makeError(ErrorCode::IoError, u"cannot open %1: %2"_s.arg(path, file.errorString()).toStdString());
	}
	const QByteArray content = file.readAll();
	if (file.error() != QFileDevice::NoError)
	{
		return makeError(ErrorCode::IoError, u"cannot read %1: %2"_s.arg(path, file.errorString()).toStdString());
	}
	return loadKnxprod(Bytes(content.begin(), content.end()));
}

// --- listing -----------------------------------------------------------------------------------------------

std::ostream& operator<<(std::ostream& os, const ProgramSummary& value)
{
	return os << "Program{" << value.id.toStdString() << " \"" << value.name.toStdString() << "\" mask="
			  << value.maskVersion.toStdString() << " number=" << value.applicationNumber
			  << " version=" << value.applicationVersion << (value.available ? "" : " (missing)") << '}';
}

std::ostream& operator<<(std::ostream& os, const ProductSummary& value)
{
	os << "ProductSummary{" << value.productId.toStdString() << " \"" << value.text.toStdString() << "\" order="
	   << value.orderNumber.toStdString() << " hardware=" << value.hardwareId.toStdString() << " programs=[";
	for (std::size_t i = 0; i < value.programs.size(); ++i)
	{
		os << (i ? ", " : "") << value.programs[i];
	}
	return os << "]}";
}

std::vector<ProductSummary> listProducts(const ProductDatabase& database, const QString& language)
{
	std::vector<ProductSummary> out;
	for (const Hardware& hardware : database.hardware)
	{
		std::vector<ProgramSummary> programs;
		QSet<QString> seen;
		for (const Hardware2Program& link : hardware.hardware2Programs)
		{
			for (const QString& ref : link.applicationProgramRefs)
			{
				if (seen.contains(ref))
				{
					continue;
				}
				seen.insert(ref);
				ProgramSummary program;
				program.id = ref;
				if (const ApplicationProgram* app = database.findApplicationProgram(ref))
				{
					program.available = true;
					program.name = app->text(app->id, u"Name"_s, language);
					program.maskVersion = app->maskVersion;
					program.applicationNumber = app->applicationNumber;
					program.applicationVersion = app->applicationVersion;
				}
				programs.push_back(std::move(program));
			}
		}
		for (const Product& product : hardware.products)
		{
			ProductSummary summary;
			summary.productId = product.id;
			summary.text = database.translations.text(product.id, u"Text"_s, language);
			summary.orderNumber = product.orderNumber;
			summary.hardwareId = hardware.id;
			summary.hardwareName = database.translations.text(hardware.id, u"Name"_s, language);
			summary.programs = programs;
			out.push_back(std::move(summary));
		}
	}
	return out;
}

} // namespace kestrel::catalog
