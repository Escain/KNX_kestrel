// Product index (specs/07-catalog.md §6): parsing, search, and taking the .knxprod out of a manufacturer's
// download (as is, zipped, or in a nested zip).

#include "CatalogFixture.hpp"

#include "kestrel/archive/ZipReader.hpp"
#include "kestrel/catalog/ProductIndex.hpp"

#include <Printers.hpp>
#include <QtPrinters.hpp>
#include <UnitTests.hpp>

using namespace kestrel;
using namespace Qt::StringLiterals;

namespace
{

void putU16(QByteArray& out, unsigned value)
{
	out.append(static_cast<char>(value & 0xFF));
	out.append(static_cast<char>((value >> 8) & 0xFF));
}

void putU32(QByteArray& out, std::uint32_t value)
{
	putU16(out, value & 0xFFFF);
	putU16(out, value >> 16);
}

/// A zip with stored (uncompressed) entries.
QByteArray storedZip(const std::vector<std::pair<QString, QByteArray>>& entries)
{
	QByteArray out;
	QByteArray directory;
	for (const auto& [name, data] : entries)
	{
		const QByteArray utf8 = name.toUtf8();
		const std::uint32_t crc = archive::crc32(ByteView(reinterpret_cast<const std::uint8_t*>(data.constData()),
			static_cast<std::size_t>(data.size())));
		const auto offset = static_cast<std::uint32_t>(out.size());
		auto header = [&](QByteArray& target, bool central)
		{
			putU32(target, central ? 0x02014b50 : 0x04034b50);
			if (central)
				putU16(target, 20); // made by
			putU16(target, 20);     // needed
			putU16(target, 0);      // flags
			putU16(target, 0);      // stored
			putU16(target, 0);      // time
			putU16(target, 0);      // date
			putU32(target, crc);
			putU32(target, static_cast<std::uint32_t>(data.size()));
			putU32(target, static_cast<std::uint32_t>(data.size()));
			putU16(target, static_cast<unsigned>(utf8.size()));
			putU16(target, 0); // extra
			if (central)
			{
				putU16(target, 0); // comment
				putU16(target, 0); // disk
				putU16(target, 0); // internal attributes
				putU32(target, 0); // external attributes
				putU32(target, offset);
			}
			target.append(utf8);
		};
		header(out, false);
		out.append(data);
		header(directory, true);
	}
	const auto directoryOffset = static_cast<std::uint32_t>(out.size());
	out.append(directory);
	putU32(out, 0x06054b50);
	putU16(out, 0);
	putU16(out, 0);
	putU16(out, static_cast<unsigned>(entries.size()));
	putU16(out, static_cast<unsigned>(entries.size()));
	putU32(out, static_cast<std::uint32_t>(directory.size()));
	putU32(out, directoryOffset);
	putU16(out, 0);
	return out;
}

QByteArray fixtureBytes()
{
	QFile file(fixture::knxprodPath());
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

std::vector<catalog::IndexedProduct> sampleIndex()
{
	const QByteArray json = R"json({"version": 1, "products": [
		{"manufacturer": "Merten (Schneider Electric)", "manufacturerId": "M-0064", "orderNumber": "MTN670804",
		 "product": "Push-button interface, 4-gang plus", "category": "binary input", "url": "https://example.invalid/a",
		 "sha256": "ABCD", "mask": "MV-0021", "size": 459336},
		{"manufacturer": "MDT", "manufacturerId": "M-0083", "orderNumber": "AKS-0816.03",
		 "product": "Switch Actuator 8-fold, 6TE, 16A", "category": "switch actuator", "url": "https://example.invalid/b"},
		{"manufacturer": "Gira", "manufacturerId": "M-0008", "orderNumber": "5004 00",
		 "product": "Push button sensor 4 Komfort 1-gang", "category": "push button", "url": "https://example.invalid/c"}
	]})json";
	return catalog::parseProductIndex(json).value();
}

} // namespace

UNIT_TEST(FR_CAT_4__productIndexParses)
{
	const auto index = sampleIndex();
	ASSERT_EQ(index.size(), std::size_t{3});
	ASSERT_EQ(index[0].orderNumber, u"MTN670804"_s);
	ASSERT_EQ(index[0].sha256, u"abcd"_s); // lower case
	ASSERT_EQ(index[0].size, qint64{459336});
	ASSERT(!catalog::parseProductIndex(R"({"version": 1, "products": [{"manufacturer": "X"}]})").has_value());
	ASSERT(!catalog::parseProductIndex(R"({"version": 2, "products": []})").has_value());
	ASSERT(!catalog::parseProductIndex("not json").has_value());
	ASSERT(!catalog::builtinProductIndex().empty()); // the resource is linked in
}

UNIT_TEST(FR_CAT_4__productIndexSearch)
{
	const auto index = sampleIndex();
	using V = std::vector<std::size_t>;
	ASSERT_EQ(catalog::searchProductIndex(index, u"mergten 670804"_s), V{0}); // typo in the brand
	ASSERT_EQ(catalog::searchProductIndex(index, u"mtn-670804"_s), V{0});    // punctuation in the order number
	ASSERT_EQ(catalog::searchProductIndex(index, u"aks0816"_s), V{1});
	ASSERT_EQ(catalog::searchProductIndex(index, u"500400"_s), V{2});
	ASSERT_EQ(catalog::searchProductIndex(index, u"actuator"_s), V{1});
	ASSERT_EQ(catalog::searchProductIndex(index, u"push"_s), (V{2, 0})); // the push button before the interface
	ASSERT_EQ(catalog::searchProductIndex(index, u"4-gang plus"_s), V{0}); // "4", "gang" and "plus"
	ASSERT(catalog::searchProductIndex(index, u"merten actuator"_s).empty()); // every word must match
	ASSERT_EQ(catalog::searchProductIndex(index, {}), (V{0, 1, 2}));
}

UNIT_TEST(FR_CAT_4__productFilesAreTakenOutOfDownloads)
{
	const QByteArray knxprod = fixtureBytes();
	ASSERT(!knxprod.isEmpty());

	// The download is the product file.
	auto files = catalog::extractProductFiles(knxprod, u"M-FFFF"_s);
	ASSERT(files.has_value());
	ASSERT_EQ(files->size(), std::size_t{1});
	ASSERT_EQ(files->front().name, u"M-FFFF.knxprod"_s);
	ASSERT(files->front().data == knxprod);

	// A zip holding it (with a manual), and a zip in a zip.
	const QByteArray zip = storedZip({{u"docs/manual.pdf"_s, "%PDF"}, {u"ETS/M-FFFF.KNXPROD"_s, knxprod}});
	files = catalog::extractProductFiles(zip, u"download.zip"_s);
	ASSERT(files.has_value());
	ASSERT_EQ(files->size(), std::size_t{1});
	ASSERT_EQ(files->front().name, u"M-FFFF.knxprod"_s); // the extension in lower case (catalog folders scan for it)
	ASSERT(files->front().data == knxprod);
	const QByteArray nested = storedZip({{u"inner.zip"_s, zip}});
	files = catalog::extractProductFiles(nested, u"outer.zip"_s);
	ASSERT(files.has_value());
	ASSERT_EQ(files->size(), std::size_t{1});

	// Nothing usable.
	ASSERT_EQ(catalog::extractProductFiles(storedZip({{u"readme.txt"_s, "hello"}}), u"x.zip"_s).error().code,
		ErrorCode::NotFound);
	ASSERT_EQ(catalog::extractProductFiles("<html>Access denied</html>", u"x"_s).error().code, ErrorCode::ParseError);
}
