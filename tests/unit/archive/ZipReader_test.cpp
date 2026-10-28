// ZIP reader tests. Fixtures in tests/data/archive/ come from make_fixtures.py (Python zipfile + a raw writer).

#include "kestrel/archive/ZipReader.hpp"

#include <Printers.hpp>
#include <UnitTests.hpp>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

using namespace kestrel;
using namespace kestrel::archive;

namespace
{

Bytes fixture(const std::string& name)
{
	std::ifstream file(std::string(KESTREL_TEST_DATA_DIR) + "/archive/" + name, std::ios::binary);
	return Bytes(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

Bytes text(std::string_view value)
{
	return Bytes(value.begin(), value.end());
}

std::string lorem()
{
	std::string out;
	for (int i = 0; i < 200; ++i)
	{
		char line[80];
		std::snprintf(line, sizeof line, "Kestrel line %03d: the quick brown fox jumps over the lazy dog.\n", i);
		out += line;
	}
	return out;
}

/// Opens and reads every entry; returns how many entries read successfully (-1 if open failed).
int exercise(Bytes data, ZipLimits limits = {})
{
	auto zip = ZipReader::open(std::move(data), limits);
	if (!zip)
	{
		return -1;
	}
	int ok = 0;
	for (const ZipEntry& entry : zip->entries())
	{
		auto content = zip->read(entry);
		if (content)
		{
			ok += 1;
		}
	}
	return ok;
}

} // namespace

UNIT_TEST(FR_CAT_1__zipListsEntriesAndComment)
{
	auto zip = ZipReader::open(fixture("basic.zip"));
	ASSERT(zip.has_value());
	ASSERT_EQ(zip->comment(), std::string("Kestrel test archive"));
	ASSERT_EQ(zip->entries().size(), std::size_t{5});
	const ZipEntry& hello = zip->entries()[0];
	ASSERT_EQ(hello.name, std::string("hello.txt"));
	ASSERT_EQ(hello.method, kMethodStored);
	ASSERT_EQ(hello.uncompressedSize, std::uint64_t{16});
	ASSERT_EQ(hello.compressedSize, std::uint64_t{16});
	ASSERT_EQ(hello.crc32, crc32(text("Hello, Kestrel!\n")));
	ASSERT_FALSE(hello.isDirectory());
	ASSERT(zip->entries()[1].isDirectory());
	ASSERT_EQ(zip->entries()[2].method, kMethodDeflate);
	ASSERT(zip->entries()[2].compressedSize < zip->entries()[2].uncompressedSize);
	ASSERT(zip->contains("dir/lorem.txt"));
	ASSERT_FALSE(zip->contains("DIR/LOREM.TXT"));
	ASSERT(zip->find("missing") == nullptr);
}

UNIT_TEST(FR_CAT_1__zipReadsStoredAndDeflated)
{
	auto zip = ZipReader::open(fixture("basic.zip"));
	ASSERT(zip.has_value());
	auto hello = zip->read("hello.txt");
	ASSERT(hello.has_value());
	ASSERT_EQ(*hello, text("Hello, Kestrel!\n"));
	auto loremData = zip->read("dir/lorem.txt");
	ASSERT(loremData.has_value());
	ASSERT_EQ(loremData->size(), std::size_t{12600});
	ASSERT(*loremData == text(lorem()));
}

UNIT_TEST(FR_CAT_1__zipReadsDirectoryAndEmptyFile)
{
	auto zip = ZipReader::open(fixture("basic.zip"));
	ASSERT(zip.has_value());
	auto directory = zip->read("dir/");
	ASSERT(directory.has_value());
	ASSERT(directory->empty());
	auto empty = zip->read("empty.txt");
	ASSERT(empty.has_value());
	ASSERT(empty->empty());
}

UNIT_TEST(FR_CAT_1__zipUtf8Name)
{
	auto zip = ZipReader::open(fixture("basic.zip"));
	ASSERT(zip.has_value());
	const ZipEntry* entry = zip->find("dir/gr\xC3\xBC\xC3\x9F" "e.txt");
	ASSERT(entry != nullptr);
	ASSERT((entry->flags & kFlagUtf8) != 0);
	auto content = zip->read(*entry);
	ASSERT(content.has_value());
	ASSERT_EQ(*content, text("Gr\xC3\xBC\xC3\x9F" "e aus M-FFFF\n"));
}

UNIT_TEST(FR_CAT_1__zipCp437NameIsConverted)
{
	auto zip = ZipReader::open(fixture("cp437.zip"));
	ASSERT(zip.has_value());
	ASSERT_EQ(zip->entries().at(0).name, std::string("caf\xC3\xA9.txt"));
	auto content = zip->read("caf\xC3\xA9.txt");
	ASSERT(content.has_value());
	ASSERT_EQ(*content, text("cp437\n"));
}

UNIT_TEST(FR_CAT_1__zipDataDescriptors)
{
	auto zip = ZipReader::open(fixture("streamed.zip"));
	ASSERT(zip.has_value());
	ASSERT_EQ(zip->entries().size(), std::size_t{3});
	ASSERT((zip->entries()[0].flags & kFlagDataDescriptor) != 0);
	auto loremData = zip->read("dir/lorem.txt");
	ASSERT(loremData.has_value());
	ASSERT(*loremData == text(lorem()));
	ASSERT_EQ(zip->read("hello.txt").value(), text("Hello, Kestrel!\n"));
}

UNIT_TEST(FR_CAT_1__zipZip64)
{
	auto zip = ZipReader::open(fixture("zip64.zip"));
	ASSERT(zip.has_value());
	ASSERT_EQ(zip->comment(), std::string("zip64"));
	ASSERT_EQ(zip->entries().size(), std::size_t{2});
	ASSERT_EQ(zip->entries()[1].uncompressedSize, std::uint64_t{12600});
	ASSERT_EQ(zip->read("a.txt").value(), text("zip64 stored\n"));
	auto loremData = zip->read("b.txt");
	ASSERT(loremData.has_value());
	ASSERT(*loremData == text(lorem()));
}

UNIT_TEST(FR_CAT_1__zipRejectsCorruptCrc)
{
	auto zip = ZipReader::open(fixture("bad-crc.zip"));
	ASSERT(zip.has_value());
	auto hello = zip->read("hello.txt");
	ASSERT_FALSE(hello.has_value());
	ASSERT_EQ(hello.error().code, ErrorCode::ParseError);
	ASSERT(hello.error().message.find("CRC") != std::string::npos);
	ASSERT(zip->read("dir/lorem.txt").has_value()); // the other entries are fine
}

UNIT_TEST(FR_CAT_1__zipRejectsTruncatedArchive)
{
	auto zip = ZipReader::open(fixture("truncated.zip"));
	ASSERT_FALSE(zip.has_value());
	ASSERT_EQ(zip.error().code, ErrorCode::ParseError);

	// Every prefix of a valid archive fails cleanly.
	const Bytes basic = fixture("basic.zip");
	for (std::size_t length = 0; length < basic.size(); ++length)
	{
		auto prefix = ZipReader::open(Bytes(basic.begin(), basic.begin() + static_cast<std::ptrdiff_t>(length)));
		ASSERT_FALSE(prefix.has_value());
	}
	ASSERT_FALSE(ZipReader::open(Bytes{}).has_value());
	ASSERT_FALSE(ZipReader::open(text("not a zip file at all, just some text that is long enough")).has_value());
}

UNIT_TEST(FR_CAT_1__zipToleratesTrailingBytes)
{
	Bytes data = fixture("zip64.zip");
	data.push_back(0);
	data.push_back(0);
	auto zip = ZipReader::open(std::move(data));
	ASSERT(zip.has_value());
	ASSERT(zip->read("b.txt").has_value());
}

UNIT_TEST(FR_CAT_1__zipEncryptedEntryIsUnsupported)
{
	for (const char* name : {"encrypted.zip", "aes.zip"})
	{
		auto zip = ZipReader::open(fixture(name));
		ASSERT(zip.has_value());
		ASSERT_EQ(zip->entries().size(), std::size_t{1});
		ASSERT(zip->entries()[0].isEncrypted());
		auto content = zip->read("secret.txt");
		ASSERT_FALSE(content.has_value());
		ASSERT_EQ(content.error().code, ErrorCode::Unsupported);
		ASSERT(content.error().message.find("encrypted entry") != std::string::npos);
	}
}

UNIT_TEST(FR_CAT_1__zipUnknownMethodIsUnsupported)
{
	auto zip = ZipReader::open(fixture("unknown-method.zip"));
	ASSERT(zip.has_value());
	auto content = zip->read("a.lzma");
	ASSERT_FALSE(content.has_value());
	ASSERT_EQ(content.error().code, ErrorCode::Unsupported);
	ASSERT(content.error().message.find("method 14") != std::string::npos);
}

UNIT_TEST(FR_CAT_1__zipRejectsPathTraversal)
{
	for (const char* name : {"traversal.zip", "absolute.zip"})
	{
		auto zip = ZipReader::open(fixture(name));
		ASSERT_FALSE(zip.has_value());
		ASSERT_EQ(zip.error().code, ErrorCode::ParseError);
		ASSERT(zip.error().message.find("unsafe entry name") != std::string::npos);
	}
}

UNIT_TEST(FR_CAT_1__zipRejectsSizeLies)
{
	for (const char* name : {"size-lie-small.zip", "size-lie-large.zip"})
	{
		auto zip = ZipReader::open(fixture(name));
		ASSERT(zip.has_value());
		auto content = zip->read("lorem.txt");
		ASSERT_FALSE(content.has_value());
		ASSERT_EQ(content.error().code, ErrorCode::ParseError);
		ASSERT(content.error().message.find("size mismatch") != std::string::npos);
	}
}

UNIT_TEST(FR_CAT_1__zipBombLimits)
{
	// Default limits: 4 MiB is fine.
	auto zip = ZipReader::open(fixture("bomb.zip"));
	ASSERT(zip.has_value());
	ASSERT_EQ(zip->read("zeros.bin").value().size(), std::size_t{4} << 20);

	// Per-entry limit: listed, but read() refuses before inflating.
	ZipLimits entryLimit;
	entryLimit.maxEntrySize = 1 << 20;
	auto limited = ZipReader::open(fixture("bomb.zip"), entryLimit);
	ASSERT(limited.has_value());
	auto content = limited->read("zeros.bin");
	ASSERT_FALSE(content.has_value());
	ASSERT_EQ(content.error().code, ErrorCode::Unsupported);

	// Total limit: open() refuses.
	ZipLimits totalLimit;
	totalLimit.maxTotalSize = 1 << 20;
	auto refused = ZipReader::open(fixture("bomb.zip"), totalLimit);
	ASSERT_FALSE(refused.has_value());
	ASSERT_EQ(refused.error().code, ErrorCode::Unsupported);
	ASSERT_EQ(exercise(fixture("basic.zip"), totalLimit), 5);

	ZipLimits entryCount;
	entryCount.maxEntries = 4;
	ASSERT_EQ(exercise(fixture("basic.zip"), entryCount), -1);
}

UNIT_TEST(FR_CAT_1__zipMissingEntryIsNotFound)
{
	auto zip = ZipReader::open(fixture("basic.zip"));
	ASSERT(zip.has_value());
	auto content = zip->read("nope.txt");
	ASSERT_FALSE(content.has_value());
	ASSERT_EQ(content.error().code, ErrorCode::NotFound);
}

UNIT_TEST(FR_CAT_1__zipCrc32KnownValue)
{
	ASSERT_EQ(crc32(text("123456789")), std::uint32_t{0xCBF43926});
	ASSERT_EQ(crc32(ByteView{}), std::uint32_t{0});
}

// Deterministic mutation fuzzing: byte flips, field smashes, truncations and insertions of valid archives.
// Nothing may crash or trip the sanitizers; most mutants fail cleanly at open() or read().
UNIT_TEST(FR_CAT_1__zipSurvivesRandomMutations)
{
	int opened = 0;
	int total = 0;
	for (const char* name : {"basic.zip", "zip64.zip", "streamed.zip"})
	{
		const Bytes original = fixture(name);
		ASSERT(!original.empty());
		for (std::uint32_t seed = 1; seed <= 400; ++seed)
		{
			std::mt19937 rng(seed); // the engine's output is specified; no distributions (they are not)
			Bytes data = original;
			const std::uint32_t mutations = 1 + rng() % 4;
			for (std::uint32_t m = 0; m < mutations && !data.empty(); ++m)
			{
				const std::size_t pos = rng() % data.size();
				switch (rng() % 6)
				{
					case 0: data[pos] = static_cast<std::uint8_t>(data[pos] ^ (1u << (rng() % 8))); break;
					case 1: data[pos] = static_cast<std::uint8_t>(rng()); break;
					case 2: // smash a 32-bit field with an extreme value
						for (std::size_t k = pos; k < std::min(pos + 4, data.size()); ++k)
						{
							data[k] = (rng() % 2) ? 0xFF : 0x00;
						}
						break;
					case 3: data.resize(pos); break;
					case 4: data.insert(data.begin() + static_cast<std::ptrdiff_t>(pos), static_cast<std::uint8_t>(rng())); break;
					default: data.erase(data.begin() + static_cast<std::ptrdiff_t>(pos)); break;
				}
			}
			ZipLimits limits;
			limits.maxEntrySize = 1 << 20;
			limits.maxTotalSize = 8 << 20;
			if (exercise(std::move(data), limits) >= 0)
			{
				opened += 1;
			}
			total += 1;
		}
	}
	ASSERT_EQ(total, 1200);
	ASSERT(opened > 0); // some mutants (e.g. in file data) still open
}
