// Catalog commands: read-only inspection of manufacturer product files (.knxprod).

#include "Cli.hpp"

#include "kestrel/catalog/Knxprod.hpp"

#include <QJsonArray>

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

} // namespace

std::vector<Command> catalogCommands()
{
	const QCommandLineOption language(u"language"_s,
		u"Text language, e.g. de-DE (default en-US; falls back to the same language, then the file's default)."_s,
		u"lang"_s, u"en-US"_s);
	return {
		{{u"catalog"_s, u"inspect"_s}, {u"file"_s},
			u"List the products, application programs, channels and com objects of a .knxprod (FR-CAT-1)."_s,
			{language}, catalogInspect},
	};
}

} // namespace kestrel::cli
