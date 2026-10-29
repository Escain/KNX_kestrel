#include "kestrel/catalog/ProductIndex.hpp"

#include "kestrel/archive/ZipReader.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace kestrel::catalog
{

namespace
{

/// Lower case letters and digits only ("MTN-670804" → "mtn670804").
QString folded(const QString& text)
{
	QString out;
	out.reserve(text.size());
	for (const QChar c : text)
	{
		if (c.isLetterOrNumber())
			out.append(c.toLower());
	}
	return out;
}

QStringList wordsOf(const QString& text)
{
	static const QRegularExpression separators(u"[^\\p{L}\\p{N}]+"_s);
	return text.toLower().split(separators, Qt::SkipEmptyParts);
}

/// True if a and b differ by at most one insertion, deletion or substitution.
bool oneEditApart(const QString& a, const QString& b)
{
	if (std::abs(a.size() - b.size()) > 1)
		return false;
	qsizetype i = 0;
	qsizetype j = 0;
	bool edited = false;
	while (i < a.size() && j < b.size())
	{
		if (a[i] == b[j])
		{
			++i;
			++j;
			continue;
		}
		if (edited)
			return false;
		edited = true;
		if (a.size() > b.size())
			++i;
		else if (b.size() > a.size())
			++j;
		else
		{
			++i;
			++j;
		}
	}
	return !edited || (i == a.size() && j == b.size());
}

/// How well one query word matches a product (0 = not at all).
int score(const IndexedProduct& product, const QString& word)
{
	const QString folding = folded(word);
	const QString order = folded(product.orderNumber);
	if (!folding.isEmpty() && order == folding)
		return 100;
	if (folding.size() >= 3 && order.contains(folding))
		return 60;
	// Each field adds its best match: a word found in the product and its category ranks first.
	int total = 0;
	const std::pair<const QString*, int> fields[] = {
		{&product.manufacturer, 40}, {&product.product, 30}, {&product.category, 20}};
	for (const auto& [field, weight] : fields)
	{
		int best = 0;
		for (const QString& candidate : wordsOf(*field))
		{
			if (candidate == word)
				best = std::max(best, weight + 5);
			else if (candidate.startsWith(word))
				best = std::max(best, weight);
			else if (candidate.contains(word))
				best = std::max(best, weight / 2);
			else if (word.size() >= 5 && oneEditApart(candidate, word))
				best = std::max(best, weight / 3);
		}
		if (best == 0 && field->toLower().contains(word))
			best = weight / 2; // across word boundaries ("4-gang")
		total += best;
	}
	return total;
}

QString stringOf(const QJsonObject& object, const QString& key)
{
	return object.value(key).toString().trimmed();
}

bool isZip(const QByteArray& data)
{
	return data.startsWith("PK\x03\x04") || data.startsWith("PK\x05\x06");
}

Bytes toBytes(const QByteArray& data)
{
	return Bytes(reinterpret_cast<const std::uint8_t*>(data.constData()),
		reinterpret_cast<const std::uint8_t*>(data.constData()) + data.size());
}

QByteArray toQByteArray(const Bytes& data)
{
	return QByteArray(reinterpret_cast<const char*>(data.data()), static_cast<qsizetype>(data.size()));
}

/// A .knxprod: a zip with knx_master.xml or a manufacturer folder ("M-0064/...").
bool isProductFile(const archive::ZipReader& zip)
{
	static const QRegularExpression folder(u"^M-[0-9A-Fa-f]{4}/"_s);
	for (const archive::ZipEntry& entry : zip.entries())
	{
		const QString name = QString::fromStdString(entry.name);
		if (name == u"knx_master.xml" || folder.match(name).hasMatch())
			return true;
	}
	return false;
}

QString baseName(const std::string& path)
{
	const QString name = QString::fromStdString(path);
	return name.mid(name.lastIndexOf(u'/') + 1);
}

/// "PS_TYA606E.KNXPROD" → "PS_TYA606E.knxprod" (catalog folders are scanned for "*.knxprod").
QString productFileName(QString name)
{
	if (name.endsWith(u".knxprod"_s, Qt::CaseInsensitive))
		name.chop(8);
	return name + u".knxprod"_s;
}

Status collect(const QByteArray& data, const QString& name, int depth, std::vector<ExtractedFile>& out)
{
	if (!isZip(data))
		return makeError(ErrorCode::ParseError, name.toStdString() + " is neither a .knxprod nor a zip");
	KESTREL_TRY(const archive::ZipReader zip, archive::ZipReader::open(toBytes(data)));
	if (isProductFile(zip))
	{
		out.push_back({productFileName(name), data});
		return ok();
	}
	for (const archive::ZipEntry& entry : zip.entries())
	{
		if (entry.isDirectory())
			continue;
		const QString entryName = baseName(entry.name);
		const bool product = entryName.endsWith(u".knxprod"_s, Qt::CaseInsensitive);
		const bool nested = depth == 0 && entryName.endsWith(u".zip"_s, Qt::CaseInsensitive);
		if (!product && !nested)
			continue;
		KESTREL_TRY(const Bytes content, zip.read(entry));
		if (product)
			out.push_back({productFileName(entryName), toQByteArray(content)});
		else
			KESTREL_CHECK(collect(toQByteArray(content), entryName, depth + 1, out));
	}
	return ok();
}

} // namespace

Result<std::vector<IndexedProduct>> parseProductIndex(const QByteArray& json)
{
	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject())
		return makeError(ErrorCode::ParseError, "product index: " + error.errorString().toStdString());
	const QJsonObject root = document.object();
	if (root.value(u"version"_s).toInt() != 1)
		return makeError(ErrorCode::Unsupported, "product index: unknown version");
	std::vector<IndexedProduct> products;
	for (const QJsonValue& value : root.value(u"products"_s).toArray())
	{
		const QJsonObject o = value.toObject();
		IndexedProduct p;
		p.manufacturer = stringOf(o, u"manufacturer"_s);
		p.manufacturerId = stringOf(o, u"manufacturerId"_s);
		p.orderNumber = stringOf(o, u"orderNumber"_s);
		p.product = stringOf(o, u"product"_s);
		p.category = stringOf(o, u"category"_s);
		p.url = stringOf(o, u"url"_s);
		p.page = stringOf(o, u"page"_s);
		p.format = stringOf(o, u"format"_s);
		p.size = o.value(u"size"_s).toInteger();
		p.sha256 = stringOf(o, u"sha256"_s).toLower();
		p.applicationId = stringOf(o, u"applicationId"_s);
		p.mask = stringOf(o, u"mask"_s);
		p.verified = stringOf(o, u"verified"_s);
		if (p.url.isEmpty() || p.manufacturer.isEmpty() || p.product.isEmpty())
		{
			return makeError(ErrorCode::ParseError,
				"product index: entry " + std::to_string(products.size() + 1) + " needs manufacturer, product and url");
		}
		products.push_back(std::move(p));
	}
	return products;
}

const std::vector<IndexedProduct>& builtinProductIndex()
{
	static const std::vector<IndexedProduct> index = []
	{
		QFile file(u":/kestrel/catalog/product-index.json"_s);
		if (!file.open(QIODevice::ReadOnly))
			return std::vector<IndexedProduct>{};
		auto parsed = parseProductIndex(file.readAll());
		return parsed ? std::move(*parsed) : std::vector<IndexedProduct>{};
	}();
	return index;
}

std::vector<std::size_t> searchProductIndex(const std::vector<IndexedProduct>& index, const QString& query)
{
	const QStringList words = wordsOf(query);
	std::vector<std::pair<int, std::size_t>> hits;
	for (std::size_t i = 0; i < index.size(); ++i)
	{
		int total = 0;
		bool all = true;
		for (const QString& word : words)
		{
			const int s = score(index[i], word);
			if (s == 0)
			{
				all = false;
				break;
			}
			total += s;
		}
		if (all)
			hits.emplace_back(total, i);
	}
	std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
	std::vector<std::size_t> out;
	out.reserve(hits.size());
	for (const auto& hit : hits)
		out.push_back(hit.second);
	return out;
}

Result<std::vector<ExtractedFile>> extractProductFiles(const QByteArray& download, const QString& downloadName)
{
	std::vector<ExtractedFile> files;
	KESTREL_CHECK(collect(download, downloadName, 0, files));
	if (files.empty())
		return makeError(ErrorCode::NotFound, downloadName.toStdString() + " holds no .knxprod");
	return files;
}

} // namespace kestrel::catalog
