#include "Parser.hpp"

#include <QSet>
#include <QXmlStreamReader>

#include <array>

namespace kestrel::catalog::detail
{

using namespace Qt::StringLiterals;

namespace
{

constexpr QStringView kNamespacePrefix = u"http://knx.org/xml/project/";
constexpr int kMaxDepth = 256; ///< nesting guard for the recursive sections (Dynamic, load controls)

/// Attributes whose value is a user-visible text that a TranslationElement may override.
constexpr std::array<QStringView, 6> kTranslatable = {
	u"Text", u"FunctionText", u"Name", u"SuffixText", u"VisibleDescription", u"Description"};

QString attr(const AttributeMap& attributes, const QString& name)
{
	const auto it = attributes.find(name);
	return it == attributes.end() ? QString() : it->second;
}

std::optional<QString> optionalAttr(const AttributeMap& attributes, const QString& name)
{
	const auto it = attributes.find(name);
	return it == attributes.end() ? std::nullopt : std::optional<QString>(it->second);
}

DynamicKind classifyDynamic(QStringView name)
{
	if (name == u"ChannelIndependentBlock")
		return DynamicKind::ChannelIndependentBlock;
	if (name == u"Channel")
		return DynamicKind::Channel;
	if (name == u"ParameterBlock")
		return DynamicKind::ParameterBlock;
	if (name == u"choose")
		return DynamicKind::Choose;
	if (name == u"when")
		return DynamicKind::When;
	if (name == u"ParameterRefRef")
		return DynamicKind::ParameterRefRef;
	if (name == u"ComObjectRefRef")
		return DynamicKind::ComObjectRefRef;
	return DynamicKind::Other;
}

ParameterTypeKind classifyType(QStringView name)
{
	if (name == u"TypeNumber")
		return ParameterTypeKind::Number;
	if (name == u"TypeRestriction")
		return ParameterTypeKind::Restriction;
	if (name == u"TypeText")
		return ParameterTypeKind::Text;
	if (name == u"TypeFloat")
		return ParameterTypeKind::Float;
	if (name == u"TypeNone")
		return ParameterTypeKind::None;
	return ParameterTypeKind::Unsupported;
}

/// Builds the per-channel index while the Dynamic tree is read.
struct ChannelIndex
{
	std::vector<Channel> channels;
	std::vector<QSet<QString>> seenObjects;
	std::vector<QSet<QString>> seenParameters;
	std::optional<std::size_t> independent;

	std::size_t add(Channel channel)
	{
		channels.push_back(std::move(channel));
		seenObjects.emplace_back();
		seenParameters.emplace_back();
		return channels.size() - 1;
	}

	std::size_t independentIndex()
	{
		if (!independent)
		{
			Channel channel;
			channel.independent = true;
			independent = add(std::move(channel));
		}
		return *independent;
	}
};

class DocumentParser
{
public:
	DocumentParser(const QString& path, const QByteArray& content, ParseState& state)
		: m_path(path), m_xml(content), m_state(state)
	{
	}

	Status run();

private:
	QString m_path;
	QXmlStreamReader m_xml;
	ParseState& m_state;

	ProductDatabase& db() { return m_state.database; }

	void warn(const QString& message) { m_state.warnings.push_back({m_path, m_xml.lineNumber(), message}); }
	bool is(QStringView name) const { return m_xml.name() == name; }
	void skip() { m_xml.skipCurrentElement(); }

	AttributeMap attributes() const
	{
		AttributeMap out;
		for (const QXmlStreamAttribute& attribute : m_xml.attributes())
		{
			out.emplace(attribute.name().toString(), attribute.value().toString());
		}
		return out;
	}

	/// Records the default (untranslated) texts of an element with an Id.
	static void remember(Translations& translations, const AttributeMap& attributes)
	{
		const QString id = attr(attributes, u"Id"_s);
		if (id.isEmpty())
		{
			return;
		}
		for (const QStringView name : kTranslatable)
		{
			const auto it = attributes.find(name.toString());
			if (it != attributes.end())
			{
				translations.setDefault(id, it->first, it->second);
			}
		}
	}

	std::optional<qint64> integer(const AttributeMap& attributes, const QString& name)
	{
		const auto it = attributes.find(name);
		if (it == attributes.end())
		{
			return std::nullopt;
		}
		bool ok = false;
		const qint64 value = QStringView(it->second).trimmed().toLongLong(&ok);
		if (!ok)
		{
			warn(u"invalid number %1=\"%2\""_s.arg(name, it->second));
			return std::nullopt;
		}
		return value;
	}

	std::optional<unsigned> optionalUnsigned(const AttributeMap& attributes, const QString& name)
	{
		const auto it = attributes.find(name);
		if (it == attributes.end())
		{
			return std::nullopt;
		}
		bool ok = false;
		const uint value = QStringView(it->second).trimmed().toUInt(&ok);
		if (!ok)
		{
			warn(u"invalid unsigned number %1=\"%2\""_s.arg(name, it->second));
			return std::nullopt;
		}
		return value;
	}

	unsigned toUnsigned(const AttributeMap& attributes, const QString& name)
	{
		return optionalUnsigned(attributes, name).value_or(0);
	}

	std::optional<double> optionalDouble(const AttributeMap& attributes, const QString& name)
	{
		const auto it = attributes.find(name);
		if (it == attributes.end())
		{
			return std::nullopt;
		}
		bool ok = false;
		const double value = QStringView(it->second).trimmed().toDouble(&ok);
		if (!ok)
		{
			warn(u"invalid number %1=\"%2\""_s.arg(name, it->second));
			return std::nullopt;
		}
		return value;
	}

	/// xs:boolean ("true"/"false"/"1"/"0") and Enable_t ("Enabled"/"Disabled").
	std::optional<bool> optionalBool(const AttributeMap& attributes, const QString& name)
	{
		const auto it = attributes.find(name);
		if (it == attributes.end())
		{
			return std::nullopt;
		}
		const QStringView value = QStringView(it->second).trimmed();
		if (value.compare(u"Enabled", Qt::CaseInsensitive) == 0 || value.compare(u"true", Qt::CaseInsensitive) == 0
			|| value == u"1")
		{
			return true;
		}
		if (value.compare(u"Disabled", Qt::CaseInsensitive) == 0 || value.compare(u"false", Qt::CaseInsensitive) == 0
			|| value == u"0")
		{
			return false;
		}
		warn(u"invalid flag %1=\"%2\""_s.arg(name, it->second));
		return std::nullopt;
	}

	std::optional<unsigned> objectSize(const std::optional<QString>& text)
	{
		if (!text)
		{
			return std::nullopt;
		}
		const auto bits = parseObjectSize(*text);
		if (!bits)
		{
			warn(u"unknown ObjectSize \"%1\""_s.arg(*text));
		}
		return bits;
	}

	MemoryLocation memory(const AttributeMap& attributes)
	{
		return MemoryLocation{attr(attributes, u"CodeSegment"_s), toUnsigned(attributes, u"Offset"_s),
			toUnsigned(attributes, u"BitOffset"_s)};
	}

	PropertyLocation property(const AttributeMap& attributes)
	{
		return PropertyLocation{optionalUnsigned(attributes, u"ObjectIndex"_s),
			optionalUnsigned(attributes, u"ObjectType"_s), toUnsigned(attributes, u"PropertyId"_s),
			toUnsigned(attributes, u"Offset"_s), toUnsigned(attributes, u"BitOffset"_s)};
	}

	TablePlacement table(const AttributeMap& attributes)
	{
		return TablePlacement{attr(attributes, u"CodeSegment"_s), toUnsigned(attributes, u"Offset"_s),
			optionalUnsigned(attributes, u"MaxEntries"_s), attributes};
	}

	void parseManufacturerData();
	void parseManufacturer();
	void parseCatalog(const QStringList& sectionPath);
	void parseHardwareList();
	void parseHardware();
	void parseApplicationProgram();
	void parseStatic(ApplicationProgram& app);
	void parseCode(ApplicationProgram& app);
	void parseParameterType(ApplicationProgram& app);
	void parseParameters(ApplicationProgram& app);
	QString parseParameter(ApplicationProgram& app, std::optional<UnionMember> member);
	void parseUnion(ApplicationProgram& app);
	void parseComObject(ApplicationProgram& app);
	void parseComObjectRef(ApplicationProgram& app);
	void parseLoadProcedures(ApplicationProgram& app);
	LoadControl parseLoadControl(int depth);
	void parseDynamic(ApplicationProgram& app);
	void parseDynamicChildren(DynamicNode& parent, ApplicationProgram& app, ChannelIndex& index,
		std::optional<std::size_t> channel, int depth);
	void parseLanguages();
};

Status DocumentParser::run()
{
	if (!m_xml.readNextStartElement())
	{
		return makeError(ErrorCode::ParseError, u"%1:%2: %3"_s
			.arg(m_path)
			.arg(m_xml.lineNumber())
			.arg(m_xml.hasError() ? m_xml.errorString() : u"no root element"_s)
			.toStdString());
	}
	if (!is(u"KNX"))
	{
		return makeError(ErrorCode::ParseError,
			u"%1: root element is <%2>, expected <KNX>"_s.arg(m_path, m_xml.name()).toStdString());
	}
	const QString ns = m_xml.namespaceUri().toString();
	if (!ns.startsWith(kNamespacePrefix))
	{
		return makeError(ErrorCode::ParseError,
			u"%1: unsupported namespace \"%2\" (expected %3NN)"_s.arg(m_path, ns, kNamespacePrefix).toStdString());
	}
	if (db().schemaNamespace.isEmpty())
	{
		db().schemaNamespace = ns;
	}
	else if (db().schemaNamespace != ns)
	{
		warn(u"namespace %1 differs from %2"_s.arg(ns, db().schemaNamespace));
	}

	while (m_xml.readNextStartElement())
	{
		if (is(u"ManufacturerData"))
		{
			parseManufacturerData();
		}
		else
		{
			skip();
		}
	}
	while (!m_xml.atEnd())
	{
		m_xml.readNext();
	}
	if (m_xml.hasError())
	{
		return makeError(ErrorCode::ParseError, u"%1:%2:%3: %4"_s
			.arg(m_path)
			.arg(m_xml.lineNumber())
			.arg(m_xml.columnNumber())
			.arg(m_xml.errorString())
			.toStdString());
	}
	return ok();
}

void DocumentParser::parseManufacturerData()
{
	while (m_xml.readNextStartElement())
	{
		if (is(u"Manufacturer"))
		{
			parseManufacturer();
		}
		else
		{
			skip();
		}
	}
}

void DocumentParser::parseManufacturer()
{
	const QString refId = attr(attributes(), u"RefId"_s);
	if (db().manufacturerId.isEmpty())
	{
		db().manufacturerId = refId;
	}
	else if (!refId.isEmpty() && refId != db().manufacturerId)
	{
		warn(u"several manufacturers in one product file (%1, %2)"_s.arg(db().manufacturerId, refId));
	}
	while (m_xml.readNextStartElement())
	{
		if (is(u"Catalog"))
		{
			parseCatalog({});
		}
		else if (is(u"Hardware"))
		{
			parseHardwareList();
		}
		else if (is(u"ApplicationPrograms"))
		{
			while (m_xml.readNextStartElement())
			{
				if (is(u"ApplicationProgram"))
				{
					parseApplicationProgram();
				}
				else
				{
					skip();
				}
			}
		}
		else if (is(u"Languages"))
		{
			parseLanguages();
		}
		else
		{
			skip(); // Baggages, Products (older schemas list nothing else we need), unknown elements
		}
	}
}

void DocumentParser::parseCatalog(const QStringList& sectionPath)
{
	while (m_xml.readNextStartElement())
	{
		if (is(u"CatalogSection"))
		{
			const AttributeMap attributes = this->attributes();
			remember(db().translations, attributes);
			parseCatalog(sectionPath + QStringList{attr(attributes, u"Name"_s)});
		}
		else if (is(u"CatalogItem"))
		{
			CatalogItem item;
			item.attributes = attributes();
			item.id = attr(item.attributes, u"Id"_s);
			item.name = attr(item.attributes, u"Name"_s);
			item.number = attr(item.attributes, u"Number"_s);
			item.productRefId = attr(item.attributes, u"ProductRefId"_s);
			item.hardware2ProgramRefId = attr(item.attributes, u"Hardware2ProgramRefId"_s);
			item.sectionPath = sectionPath;
			remember(db().translations, item.attributes);
			db().catalogItems.push_back(std::move(item));
			skip();
		}
		else
		{
			skip();
		}
	}
}

void DocumentParser::parseHardwareList()
{
	while (m_xml.readNextStartElement())
	{
		if (is(u"Hardware"))
		{
			parseHardware();
		}
		else
		{
			skip();
		}
	}
}

void DocumentParser::parseHardware()
{
	Hardware hardware;
	hardware.attributes = attributes();
	hardware.id = attr(hardware.attributes, u"Id"_s);
	hardware.name = attr(hardware.attributes, u"Name"_s);
	hardware.serialNumber = attr(hardware.attributes, u"SerialNumber"_s);
	hardware.versionNumber = attr(hardware.attributes, u"VersionNumber"_s);
	remember(db().translations, hardware.attributes);
	while (m_xml.readNextStartElement())
	{
		if (is(u"Products"))
		{
			while (m_xml.readNextStartElement())
			{
				if (is(u"Product"))
				{
					Product product;
					product.attributes = attributes();
					product.id = attr(product.attributes, u"Id"_s);
					product.text = attr(product.attributes, u"Text"_s);
					product.orderNumber = attr(product.attributes, u"OrderNumber"_s);
					remember(db().translations, product.attributes);
					hardware.products.push_back(std::move(product));
				}
				skip();
			}
		}
		else if (is(u"Hardware2Programs"))
		{
			while (m_xml.readNextStartElement())
			{
				if (!is(u"Hardware2Program"))
				{
					skip();
					continue;
				}
				Hardware2Program link;
				link.attributes = attributes();
				link.id = attr(link.attributes, u"Id"_s);
				while (m_xml.readNextStartElement())
				{
					if (is(u"ApplicationProgramRef"))
					{
						const QString ref = attr(attributes(), u"RefId"_s);
						if (!ref.isEmpty() && !link.applicationProgramRefs.contains(ref))
						{
							link.applicationProgramRefs.append(ref);
						}
					}
					skip();
				}
				hardware.hardware2Programs.push_back(std::move(link));
			}
		}
		else
		{
			skip();
		}
	}
	if (hardware.id.isEmpty())
	{
		warn(u"Hardware without Id ignored"_s);
		return;
	}
	db().hardware.push_back(std::move(hardware));
}

void DocumentParser::parseApplicationProgram()
{
	ApplicationProgram app;
	app.attributes = attributes();
	const AttributeMap& a = app.attributes;
	app.id = attr(a, u"Id"_s);
	app.name = attr(a, u"Name"_s);
	app.applicationNumber = toUnsigned(a, u"ApplicationNumber"_s);
	app.applicationVersion = toUnsigned(a, u"ApplicationVersion"_s);
	app.programType = attr(a, u"ProgramType"_s);
	app.maskVersion = attr(a, u"MaskVersion"_s);
	app.loadProcedureStyle = attr(a, u"LoadProcedureStyle"_s);
	app.peiType = toUnsigned(a, u"PeiType"_s);
	app.dynamicTableManagement = optionalBool(a, u"DynamicTableManagement"_s).value_or(false);
	app.defaultLanguage = attr(a, u"DefaultLanguage"_s);
	remember(app.translations, a);
	while (m_xml.readNextStartElement())
	{
		if (is(u"Static"))
		{
			parseStatic(app);
		}
		else if (is(u"Dynamic"))
		{
			parseDynamic(app);
		}
		else if (is(u"ModuleDefs"))
		{
			app.hasModuleDefs = true;
			warn(u"%1: ModuleDefs are not supported yet; channels and objects defined in modules are not listed"_s.arg(
				app.id));
			skip();
		}
		else
		{
			skip();
		}
	}
	if (app.id.isEmpty())
	{
		warn(u"ApplicationProgram without Id ignored"_s);
		return;
	}
	db().applicationPrograms.push_back(std::move(app));
}

void DocumentParser::parseStatic(ApplicationProgram& app)
{
	while (m_xml.readNextStartElement())
	{
		if (is(u"Code"))
		{
			parseCode(app);
		}
		else if (is(u"ParameterTypes"))
		{
			while (m_xml.readNextStartElement())
			{
				if (is(u"ParameterType"))
				{
					parseParameterType(app);
				}
				else
				{
					skip();
				}
			}
		}
		else if (is(u"Parameters"))
		{
			parseParameters(app);
		}
		else if (is(u"ParameterRefs"))
		{
			while (m_xml.readNextStartElement())
			{
				if (is(u"ParameterRef"))
				{
					ParameterRef ref;
					ref.attributes = attributes();
					ref.id = attr(ref.attributes, u"Id"_s);
					ref.refId = attr(ref.attributes, u"RefId"_s);
					ref.text = optionalAttr(ref.attributes, u"Text"_s);
					ref.value = optionalAttr(ref.attributes, u"Value"_s);
					ref.access = optionalAttr(ref.attributes, u"Access"_s);
					remember(app.translations, ref.attributes);
					app.parameterRefs.push_back(std::move(ref));
				}
				skip();
			}
		}
		else if (is(u"ComObjectTable"))
		{
			app.comObjectTable = table(attributes());
			while (m_xml.readNextStartElement())
			{
				if (is(u"ComObject"))
				{
					parseComObject(app);
				}
				else
				{
					skip();
				}
			}
		}
		else if (is(u"ComObjectRefs"))
		{
			while (m_xml.readNextStartElement())
			{
				if (is(u"ComObjectRef"))
				{
					parseComObjectRef(app);
				}
				else
				{
					skip();
				}
			}
		}
		else if (is(u"AddressTable"))
		{
			app.addressTable = table(attributes());
			skip();
		}
		else if (is(u"AssociationTable"))
		{
			app.associationTable = table(attributes());
			skip();
		}
		else if (is(u"LoadProcedures"))
		{
			parseLoadProcedures(app);
		}
		else
		{
			skip();
		}
	}
}

void DocumentParser::parseCode(ApplicationProgram& app)
{
	while (m_xml.readNextStartElement())
	{
		if (is(u"AbsoluteSegment"))
		{
			AbsoluteSegment segment;
			segment.attributes = attributes();
			segment.id = attr(segment.attributes, u"Id"_s);
			segment.address = toUnsigned(segment.attributes, u"Address"_s);
			segment.size = toUnsigned(segment.attributes, u"Size"_s);
			while (m_xml.readNextStartElement())
			{
				if (is(u"Data"))
				{
					QByteArray base64 = m_xml.readElementText(QXmlStreamReader::SkipChildElements).toLatin1();
					base64.removeIf([](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; });
					auto decoded = QByteArray::fromBase64Encoding(base64, QByteArray::AbortOnBase64DecodingErrors);
					if (decoded)
					{
						segment.data = *decoded;
					}
					else
					{
						warn(u"%1: invalid base64 in <Data>"_s.arg(segment.id));
					}
				}
				else
				{
					skip();
				}
			}
			app.absoluteSegments.push_back(std::move(segment));
		}
		else if (is(u"RelativeSegment"))
		{
			RelativeSegment segment;
			segment.attributes = attributes();
			segment.id = attr(segment.attributes, u"Id"_s);
			segment.name = attr(segment.attributes, u"Name"_s);
			segment.offset = toUnsigned(segment.attributes, u"Offset"_s);
			segment.size = toUnsigned(segment.attributes, u"Size"_s);
			segment.loadStateMachine = toUnsigned(segment.attributes, u"LoadStateMachine"_s);
			while (m_xml.readNextStartElement())
			{
				if (is(u"Data"))
				{
					QByteArray base64 = m_xml.readElementText(QXmlStreamReader::SkipChildElements).toLatin1();
					base64.removeIf([](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; });
					auto decoded = QByteArray::fromBase64Encoding(base64, QByteArray::AbortOnBase64DecodingErrors);
					if (decoded)
					{
						segment.data = *decoded;
					}
					else
					{
						warn(u"%1: invalid base64 in <Data>"_s.arg(segment.id));
					}
				}
				else
				{
					skip();
				}
			}
			app.relativeSegments.push_back(std::move(segment));
		}
		else
		{
			skip();
		}
	}
}

void DocumentParser::parseParameterType(ApplicationProgram& app)
{
	ParameterType type;
	const AttributeMap outer = attributes();
	type.id = attr(outer, u"Id"_s);
	type.name = attr(outer, u"Name"_s);
	bool haveType = false;
	while (m_xml.readNextStartElement())
	{
		if (haveType || !m_xml.name().startsWith(u"Type"))
		{
			skip();
			continue;
		}
		haveType = true;
		type.element = m_xml.name().toString();
		type.kind = classifyType(m_xml.name());
		type.attributes = attributes();
		const AttributeMap& a = type.attributes;
		type.sizeInBit = toUnsigned(a, u"SizeInBit"_s);
		switch (type.kind)
		{
			case ParameterTypeKind::Number:
				type.numberType = attr(a, u"Type"_s);
				type.minimum = integer(a, u"minInclusive"_s);
				type.maximum = integer(a, u"maxInclusive"_s);
				break;
			case ParameterTypeKind::Float:
				type.encoding = attr(a, u"Encoding"_s);
				type.minimumFloat = optionalDouble(a, u"minInclusive"_s);
				type.maximumFloat = optionalDouble(a, u"maxInclusive"_s);
				break;
			case ParameterTypeKind::Restriction: type.base = attr(a, u"Base"_s); break;
			default: break;
		}
		if (type.kind != ParameterTypeKind::Restriction)
		{
			skip();
			continue;
		}
		while (m_xml.readNextStartElement())
		{
			if (is(u"Enumeration"))
			{
				EnumerationEntry entry;
				entry.attributes = attributes();
				entry.id = attr(entry.attributes, u"Id"_s);
				entry.text = attr(entry.attributes, u"Text"_s);
				entry.value = attr(entry.attributes, u"Value"_s);
				remember(app.translations, entry.attributes);
				type.enumeration.push_back(std::move(entry));
			}
			skip();
		}
	}
	if (!haveType)
	{
		warn(u"ParameterType %1 has no type element"_s.arg(type.id));
	}
	app.parameterTypes.push_back(std::move(type));
}

void DocumentParser::parseParameters(ApplicationProgram& app)
{
	while (m_xml.readNextStartElement())
	{
		if (is(u"Parameter"))
		{
			parseParameter(app, std::nullopt);
		}
		else if (is(u"Union"))
		{
			parseUnion(app);
		}
		else
		{
			skip();
		}
	}
}

QString DocumentParser::parseParameter(ApplicationProgram& app, std::optional<UnionMember> member)
{
	Parameter parameter;
	parameter.attributes = attributes();
	const AttributeMap& a = parameter.attributes;
	parameter.id = attr(a, u"Id"_s);
	parameter.name = attr(a, u"Name"_s);
	parameter.parameterType = attr(a, u"ParameterType"_s);
	parameter.text = attr(a, u"Text"_s);
	parameter.value = attr(a, u"Value"_s);
	parameter.access = attr(a, u"Access"_s);
	remember(app.translations, a);
	if (member)
	{
		member->offset = toUnsigned(a, u"Offset"_s);
		member->bitOffset = toUnsigned(a, u"BitOffset"_s);
		member->isDefault = optionalBool(a, u"DefaultUnionParameter"_s).value_or(false);
		parameter.unionMember = member;
	}
	while (m_xml.readNextStartElement())
	{
		if (is(u"Memory"))
		{
			parameter.memory = memory(attributes());
		}
		else if (is(u"Property"))
		{
			parameter.property = property(attributes());
		}
		skip();
	}
	QString id = parameter.id;
	app.parameters.push_back(std::move(parameter));
	return id;
}

void DocumentParser::parseUnion(ApplicationProgram& app)
{
	Union group;
	group.attributes = attributes();
	group.sizeInBit = toUnsigned(group.attributes, u"SizeInBit"_s);
	const std::size_t index = app.unions.size();
	app.unions.emplace_back(); // reserve the index the members refer to
	while (m_xml.readNextStartElement())
	{
		if (is(u"Memory"))
		{
			group.memory = memory(attributes());
			skip();
		}
		else if (is(u"Property"))
		{
			group.property = property(attributes());
			skip();
		}
		else if (is(u"Parameter"))
		{
			group.parameters.append(parseParameter(app, UnionMember{index, 0, 0, false}));
		}
		else
		{
			skip();
		}
	}
	app.unions[index] = std::move(group);
}

void DocumentParser::parseComObject(ApplicationProgram& app)
{
	ComObject object;
	object.attributes = attributes();
	const AttributeMap& a = object.attributes;
	object.id = attr(a, u"Id"_s);
	object.name = attr(a, u"Name"_s);
	object.text = attr(a, u"Text"_s);
	object.functionText = attr(a, u"FunctionText"_s);
	object.number = toUnsigned(a, u"Number"_s);
	object.objectSize = attr(a, u"ObjectSize"_s);
	object.sizeInBit = objectSize(optionalAttr(a, u"ObjectSize"_s)).value_or(0);
	object.flags.communication = optionalBool(a, u"CommunicationFlag"_s).value_or(false);
	object.flags.read = optionalBool(a, u"ReadFlag"_s).value_or(false);
	object.flags.write = optionalBool(a, u"WriteFlag"_s).value_or(false);
	object.flags.transmit = optionalBool(a, u"TransmitFlag"_s).value_or(false);
	object.flags.update = optionalBool(a, u"UpdateFlag"_s).value_or(false);
	object.flags.readOnInit = optionalBool(a, u"ReadOnInitFlag"_s).value_or(false);
	object.datapointType = attr(a, u"DatapointType"_s);
	remember(app.translations, a);
	app.comObjects.push_back(std::move(object));
	skip();
}

void DocumentParser::parseComObjectRef(ApplicationProgram& app)
{
	ComObjectRef ref;
	ref.attributes = attributes();
	const AttributeMap& a = ref.attributes;
	ref.id = attr(a, u"Id"_s);
	ref.refId = attr(a, u"RefId"_s);
	ref.name = optionalAttr(a, u"Name"_s);
	ref.text = optionalAttr(a, u"Text"_s);
	ref.functionText = optionalAttr(a, u"FunctionText"_s);
	ref.objectSize = optionalAttr(a, u"ObjectSize"_s);
	ref.sizeInBit = objectSize(ref.objectSize);
	ref.communication = optionalBool(a, u"CommunicationFlag"_s);
	ref.read = optionalBool(a, u"ReadFlag"_s);
	ref.write = optionalBool(a, u"WriteFlag"_s);
	ref.transmit = optionalBool(a, u"TransmitFlag"_s);
	ref.update = optionalBool(a, u"UpdateFlag"_s);
	ref.readOnInit = optionalBool(a, u"ReadOnInitFlag"_s);
	ref.datapointType = optionalAttr(a, u"DatapointType"_s);
	remember(app.translations, a);
	app.comObjectRefs.push_back(std::move(ref));
	skip();
}

void DocumentParser::parseLoadProcedures(ApplicationProgram& app)
{
	std::optional<std::size_t> implicit; // LdCtrl* written directly under <LoadProcedures>
	while (m_xml.readNextStartElement())
	{
		if (is(u"LoadProcedure"))
		{
			LoadProcedure procedure;
			procedure.attributes = attributes();
			procedure.mergeId = optionalUnsigned(procedure.attributes, u"MergeId"_s);
			while (m_xml.readNextStartElement())
			{
				procedure.controls.push_back(parseLoadControl(1));
			}
			app.loadProcedures.push_back(std::move(procedure));
		}
		else if (m_xml.name().startsWith(u"LdCtrl"))
		{
			if (!implicit)
			{
				implicit = app.loadProcedures.size();
				app.loadProcedures.emplace_back();
			}
			app.loadProcedures[*implicit].controls.push_back(parseLoadControl(1));
		}
		else
		{
			skip();
		}
	}
}

LoadControl DocumentParser::parseLoadControl(int depth)
{
	LoadControl control{m_xml.name().toString(), attributes(), {}};
	if (depth >= kMaxDepth)
	{
		warn(u"load procedure nested too deeply"_s);
		skip();
		return control;
	}
	while (m_xml.readNextStartElement())
	{
		control.children.push_back(parseLoadControl(depth + 1));
	}
	return control;
}

void DocumentParser::parseDynamic(ApplicationProgram& app)
{
	DynamicNode root;
	root.kind = DynamicKind::Root;
	root.element = m_xml.name().toString();
	root.attributes = attributes();
	ChannelIndex index;
	parseDynamicChildren(root, app, index, std::nullopt, 1);
	app.dynamic = std::move(root);
	// The channel-independent part (if any) first, then the channels in document order.
	if (index.independent && *index.independent != 0)
	{
		Channel independent = std::move(index.channels[*index.independent]);
		index.channels.erase(index.channels.begin() + static_cast<std::ptrdiff_t>(*index.independent));
		index.channels.insert(index.channels.begin(), std::move(independent));
	}
	app.channels = std::move(index.channels);
}

void DocumentParser::parseDynamicChildren(DynamicNode& parent, ApplicationProgram& app, ChannelIndex& index,
	std::optional<std::size_t> channel, int depth)
{
	while (m_xml.readNextStartElement())
	{
		if (depth >= kMaxDepth)
		{
			warn(u"Dynamic section nested too deeply; subtree ignored"_s);
			skip();
			continue;
		}
		DynamicNode node;
		node.element = m_xml.name().toString();
		node.kind = classifyDynamic(m_xml.name());
		node.attributes = attributes();
		remember(app.translations, node.attributes);

		std::optional<std::size_t> childChannel = channel;
		const QString id = node.attribute(u"Id"_s);
		const QString refId = node.attribute(u"RefId"_s);
		switch (node.kind)
		{
			case DynamicKind::Channel:
			{
				Channel info;
				info.id = id;
				info.name = node.attribute(u"Name"_s);
				info.text = node.attribute(u"Text"_s);
				info.number = node.attribute(u"Number"_s);
				childChannel = index.add(std::move(info));
				break;
			}
			case DynamicKind::ChannelIndependentBlock: childChannel = index.independentIndex(); break;
			case DynamicKind::ParameterBlock:
			{
				const std::size_t target = channel ? *channel : index.independentIndex();
				index.channels[target].parameterBlocks.push_back(
					ParameterBlockInfo{id, node.attribute(u"Name"_s), node.attribute(u"Text"_s)});
				break;
			}
			case DynamicKind::ComObjectRefRef:
			{
				const std::size_t target = channel ? *channel : index.independentIndex();
				if (!refId.isEmpty() && !index.seenObjects[target].contains(refId))
				{
					index.seenObjects[target].insert(refId);
					index.channels[target].comObjectRefs.append(refId);
				}
				break;
			}
			case DynamicKind::ParameterRefRef:
			{
				const std::size_t target = channel ? *channel : index.independentIndex();
				if (!refId.isEmpty() && !index.seenParameters[target].contains(refId))
				{
					index.seenParameters[target].insert(refId);
					index.channels[target].parameterRefs.append(refId);
				}
				break;
			}
			default: break;
		}
		parseDynamicChildren(node, app, index, childChannel, depth + 1);
		parent.children.push_back(std::move(node));
	}
}

void DocumentParser::parseLanguages()
{
	while (m_xml.readNextStartElement())
	{
		if (!is(u"Language"))
		{
			skip();
			continue;
		}
		const QString language = attr(attributes(), u"Identifier"_s);
		while (m_xml.readNextStartElement())
		{
			if (!is(u"TranslationUnit"))
			{
				skip();
				continue;
			}
			Translations& unit = m_state.unitTranslations[attr(attributes(), u"RefId"_s)];
			while (m_xml.readNextStartElement())
			{
				if (!is(u"TranslationElement"))
				{
					skip();
					continue;
				}
				const QString element = attr(attributes(), u"RefId"_s);
				while (m_xml.readNextStartElement())
				{
					if (is(u"Translation"))
					{
						const AttributeMap a = attributes();
						unit.add(language, element, attr(a, u"AttributeName"_s), attr(a, u"Text"_s));
					}
					skip();
				}
			}
		}
	}
}

} // namespace

Status parseDocument(const QString& path, const QByteArray& content, ParseState& state)
{
	DocumentParser parser(path, content, state);
	return parser.run();
}

} // namespace kestrel::catalog::detail
