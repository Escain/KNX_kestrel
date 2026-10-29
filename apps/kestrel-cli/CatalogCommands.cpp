// Catalog commands: read-only inspection of manufacturer product files (.knxprod).

#include "Cli.hpp"

#include "kestrel/app/ProductFetcher.hpp"
#include "kestrel/catalog/Knxprod.hpp"
#include "kestrel/catalog/ProductIndex.hpp"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QRegularExpression>

namespace kestrel::cli
{

using namespace Qt::StringLiterals;

namespace
{

QString hex(unsigned value, int digits)
{
	return u"0x"_s + QString::number(value, 16).toUpper().rightJustified(digits, u'0');
}

/// Left-aligned columns separated by two spaces; the last column is not padded.
QStringList table(const QList<QStringList>& rows, const QString& indent)
{
	QList<qsizetype> widths;
	for (const QStringList& row : rows)
	{
		for (qsizetype i = 0; i < row.size(); ++i)
		{
			if (widths.size() <= i)
			{
				widths.append(0);
			}
			widths[i] = std::max(widths[i], row[i].size());
		}
	}
	QStringList lines;
	for (const QStringList& row : rows)
	{
		QString line = indent;
		for (qsizetype i = 0; i < row.size(); ++i)
		{
			line += i + 1 < row.size() ? row[i].leftJustified(widths[i] + 2) : row[i];
		}
		lines.append(line.trimmed().isEmpty() ? QString() : line);
	}
	return lines;
}

QJsonObject warningJson(const catalog::Diagnostic& warning)
{
	return {{u"file"_s, warning.file}, {u"line"_s, static_cast<qint64>(warning.line)}, {u"message"_s, warning.message}};
}

int catalogInspect(Invocation& inv)
{
	const QString path = inv.args[0];
	const QString language = inv.value(u"language"_s);
	auto loaded = catalog::loadKnxprodFile(path);
	if (!loaded)
	{
		return inv.out.failure(kExitIo, loaded.error());
	}
	const catalog::ProductDatabase& db = loaded->database;

	QStringList text;
	text.append(u"manufacturer  %1  (%2)"_s.arg(db.manufacturerId, db.schemaNamespace));

	// --- products
	QJsonArray products;
	QList<QStringList> productRows{{u"order"_s, u"product"_s, u"application programs"_s}};
	for (const catalog::ProductSummary& product : catalog::listProducts(db, language))
	{
		QJsonArray programs;
		QStringList programIds;
		for (const catalog::ProgramSummary& program : product.programs)
		{
			programs.append(QJsonObject{{u"id"_s, program.id}, {u"available"_s, program.available}});
			programIds.append(program.available ? program.id : program.id + u" (not in file)"_s);
		}
		products.append(QJsonObject{{u"id"_s, product.productId}, {u"text"_s, product.text},
			{u"orderNumber"_s, product.orderNumber}, {u"hardwareId"_s, product.hardwareId},
			{u"hardwareName"_s, product.hardwareName}, {u"applicationPrograms"_s, programs}});
		productRows.append({product.orderNumber, product.text, programIds.join(u", "_s)});
	}
	text.append(QString());
	text.append(u"products"_s);
	text.append(table(productRows, u"  "_s));

	// --- application programs
	QJsonArray applications;
	for (const catalog::ApplicationProgram& app : db.applicationPrograms)
	{
		const QString name = app.text(app.id, u"Name"_s, language);
		text.append(QString());
		text.append(u"application %1  \"%2\""_s.arg(app.id, name));
		text.append(u"  mask %1  number %2  version %3 (%4)  load procedure %5"_s.arg(app.maskVersion,
			hex(app.applicationNumber, 4), hex(app.applicationVersion, 2))
						.arg(app.applicationVersion)
						.arg(app.loadProcedureStyle.isEmpty() ? u"-"_s : app.loadProcedureStyle));

		QJsonArray channels;
		QList<QStringList> channelRows{{u"number"_s, u"channel"_s, u"objects"_s, u"parameters"_s}};
		for (const catalog::Channel& channel : app.channels)
		{
			const QString channelText = channel.independent ? u"(channel-independent)"_s
															: app.text(channel.id, u"Text"_s, language);
			channels.append(QJsonObject{{u"id"_s, channel.id}, {u"number"_s, channel.number}, {u"name"_s, channel.name},
				{u"text"_s, channelText}, {u"independent"_s, channel.independent},
				{u"comObjects"_s, QJsonArray::fromStringList(channel.comObjectRefs)},
				{u"parameterCount"_s, static_cast<int>(channel.parameterRefs.size())}});
			channelRows.append({channel.independent ? u"-"_s : channel.number, channelText,
				QString::number(channel.comObjectRefs.size()), QString::number(channel.parameterRefs.size())});
		}
		text.append(u"  channels"_s);
		text.append(table(channelRows, u"    "_s));

		QJsonArray objects;
		QList<QStringList> objectRows{
			{u"#"_s, u"channel"_s, u"text"_s, u"function"_s, u"size"_s, u"flags"_s, u"DPT"_s}};
		for (const catalog::EffectiveComObject& object : app.effectiveComObjects(language))
		{
			const catalog::Channel* channel = app.findChannel(object.channelId);
			const QString channelText = channel ? app.text(channel->id, u"Text"_s, language) : QString();
			objects.append(QJsonObject{{u"refId"_s, object.refId}, {u"comObjectId"_s, object.comObjectId},
				{u"number"_s, static_cast<int>(object.number)}, {u"channel"_s, object.channelId},
				{u"channelText"_s, channelText}, {u"text"_s, object.text}, {u"functionText"_s, object.functionText},
				{u"objectSize"_s, object.objectSize}, {u"sizeInBit"_s, static_cast<int>(object.sizeInBit)},
				{u"flags"_s, object.flags.toString()}, {u"dpt"_s, object.datapointType},
				{u"referenced"_s, object.referenced}});
			objectRows.append({QString::number(object.number), channelText.isEmpty() ? u"-"_s : channelText,
				object.text, object.functionText, object.objectSize, object.flags.toString(),
				object.datapointType.isEmpty() ? u"-"_s : object.datapointType});
		}
		text.append(u"  com objects"_s);
		text.append(table(objectRows, u"    "_s));

		applications.append(QJsonObject{{u"id"_s, app.id}, {u"name"_s, name}, {u"maskVersion"_s, app.maskVersion},
			{u"applicationNumber"_s, static_cast<int>(app.applicationNumber)},
			{u"applicationVersion"_s, static_cast<int>(app.applicationVersion)},
			{u"loadProcedureStyle"_s, app.loadProcedureStyle}, {u"channels"_s, channels}, {u"comObjects"_s, objects}});
	}

	QJsonArray warnings;
	for (const catalog::Diagnostic& warning : loaded->warnings)
	{
		warnings.append(warningJson(warning));
		inv.out.note(u"warning: %1%2%3"_s.arg(warning.file,
			warning.line > 0 ? u":%1"_s.arg(warning.line) : QString(),
			warning.file.isEmpty() ? warning.message : u": "_s + warning.message));
	}
	return inv.out.success(
		{
			{u"file"_s, path},
			{u"manufacturer"_s, db.manufacturerId},
			{u"schema"_s, db.schemaNamespace},
			{u"language"_s, language},
			{u"products"_s, products},
			{u"applicationPrograms"_s, applications},
			{u"warnings"_s, warnings},
		},
		text.join(u'\n'));
}

/// The built-in index, or the one of --index.
std::optional<std::vector<catalog::IndexedProduct>> productIndex(Invocation& inv, int& exitCode)
{
	if (!inv.isSet(u"index"_s))
		return catalog::builtinProductIndex();
	QFile file(inv.value(u"index"_s));
	if (!file.open(QIODevice::ReadOnly))
	{
		exitCode = inv.out.failure(kExitIo, u"cannot read %1"_s.arg(file.fileName()), u"IoError"_s);
		return std::nullopt;
	}
	auto parsed = catalog::parseProductIndex(file.readAll());
	if (!parsed)
	{
		exitCode = inv.out.failure(kExitFindings, parsed.error());
		return std::nullopt;
	}
	return std::move(*parsed);
}

QJsonObject productJson(const catalog::IndexedProduct& p)
{
	return {{u"manufacturer"_s, p.manufacturer}, {u"orderNumber"_s, p.orderNumber}, {u"product"_s, p.product},
		{u"category"_s, p.category}, {u"mask"_s, p.mask}, {u"url"_s, p.url}};
}

int catalogSearch(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto index = productIndex(inv, exitCode);
	if (!index)
		return exitCode;
	QJsonArray found;
	QList<QStringList> rows{{u"order"_s, u"manufacturer"_s, u"product"_s, u"kind"_s, u"mask"_s}};
	for (const std::size_t i : catalog::searchProductIndex(*index, inv.args[0]))
	{
		const catalog::IndexedProduct& p = (*index)[i];
		found.append(productJson(p));
		rows.append({p.orderNumber, p.manufacturer, p.product, p.category, p.mask});
	}
	const QString text = found.isEmpty() ? u"no product matches \"%1\""_s.arg(inv.args[0])
										 : table(rows, {}).join(u'\n');
	return inv.out.success({{u"products"_s, found}}, text);
}

int catalogFetch(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto index = productIndex(inv, exitCode);
	if (!index)
		return exitCode;
	// The order number exactly (punctuation and case ignored), else a search with a single hit.
	const auto fold = [](QString text) { return text.remove(QRegularExpression(u"[^A-Za-z0-9]"_s)).toLower(); };
	const catalog::IndexedProduct* product = nullptr;
	for (const catalog::IndexedProduct& p : *index)
	{
		if (fold(p.orderNumber) == fold(inv.args[0]))
			product = &p;
	}
	if (!product)
	{
		const auto hits = catalog::searchProductIndex(*index, inv.args[0]);
		if (hits.size() != 1)
		{
			return inv.out.failure(kExitFindings,
				hits.empty() ? u"no product matches \"%1\""_s.arg(inv.args[0])
							 : u"%1 products match \"%2\"; give the order number (catalog search)"_s.arg(hits.size()).arg(inv.args[0]),
				u"NotFound"_s);
		}
		product = &(*index)[hits.front()];
	}
	const QDir target(inv.isSet(u"to"_s) ? inv.value(u"to"_s) : QDir::currentPath());
	if (!QDir().mkpath(target.path()))
		return inv.out.failure(kExitIo, u"cannot create %1"_s.arg(target.path()), u"IoError"_s);

	inv.out.note(u"downloading %1 %2 from %3"_s.arg(product->manufacturer, product->orderNumber, product->url));
	app::ProductFetcher fetcher;
	QEventLoop loop;
	std::optional<Result<app::FetchedProduct>> result;
	fetcher.fetch(*product,
		[&](Result<app::FetchedProduct> fetched)
		{
			result = std::move(fetched);
			loop.quit();
		});
	if (!result)
		loop.exec();
	if (!*result)
		return inv.out.failure(kExitIo, result->error());
	if (!(*result)->matchesIndex)
		inv.out.note(u"note: the file differs from the one the index was made with (updated by the manufacturer?)"_s);
	QJsonArray files;
	QStringList lines;
	for (const catalog::ExtractedFile& file : (*result)->files)
	{
		QFile out(target.filePath(file.name));
		if (!out.open(QIODevice::WriteOnly) || out.write(file.data) != file.data.size())
			return inv.out.failure(kExitIo, u"cannot write %1"_s.arg(out.fileName()), u"IoError"_s);
		files.append(out.fileName());
		lines.append(u"saved %1"_s.arg(QDir::toNativeSeparators(out.fileName())));
	}
	return inv.out.success({{u"product"_s, productJson(*product)}, {u"files"_s, files}, {u"sha256"_s, (*result)->sha256}},
		lines.join(u'\n'));
}

} // namespace

std::vector<Command> catalogCommands()
{
	const QCommandLineOption language(u"language"_s,
		u"Text language, e.g. de-DE (default en-US; falls back to the same language, then the file's default)."_s,
		u"lang"_s, u"en-US"_s);
	const QCommandLineOption indexOption(u"index"_s, u"Product index JSON to use instead of the built-in one."_s, u"file"_s);
	const QCommandLineOption to(u"to"_s, u"Folder for the downloaded .knxprod files (default: the current folder)."_s, u"dir"_s);
	return {
		{{u"catalog"_s, u"inspect"_s}, {u"file"_s},
			u"List the products, application programs, channels and com objects of a .knxprod (FR-CAT-1)."_s,
			{language}, catalogInspect},
		{{u"catalog"_s, u"search"_s}, {u"query"_s},
			u"Search the product index for products whose file can be downloaded (words: brand, order number, kind)."_s,
			{indexOption}, catalogSearch},
		{{u"catalog"_s, u"fetch"_s}, {u"orderNumber"_s},
			u"Download a product's file from its manufacturer (product index) and save the .knxprod."_s,
			{indexOption, to}, catalogFetch},
	};
}

} // namespace kestrel::cli
