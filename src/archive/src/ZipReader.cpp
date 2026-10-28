#include "kestrel/archive/ZipReader.hpp"

#define ZLIB_CONST
#include <zlib.h>

#include <algorithm>
#include <array>
#include <limits>
#include <set>

namespace kestrel::archive
{

namespace
{

constexpr std::uint32_t kSigLocal = 0x04034b50;
constexpr std::uint32_t kSigCentral = 0x02014b50;
constexpr std::uint32_t kSigEocd = 0x06054b50;
constexpr std::uint32_t kSigEocd64 = 0x06064b50;
constexpr std::uint32_t kSigEocd64Locator = 0x07064b50;

constexpr std::size_t kLocalHeaderSize = 30;
constexpr std::size_t kCentralHeaderSize = 46;
constexpr std::size_t kEocdSize = 22;
constexpr std::size_t kEocd64LocatorSize = 20;
constexpr std::size_t kEocd64Size = 56;
constexpr std::size_t kMaxCommentSize = 0xFFFF;

constexpr std::uint16_t kExtraZip64 = 0x0001;
constexpr std::uint16_t kExtraUnicodePath = 0x7075;

/// Little-endian reader over a bounded window. Reads past the end set the failure flag and return 0.
class LeReader
{
	ByteView m_data;
	std::size_t m_pos = 0;
	bool m_failed = false;

public:
	explicit LeReader(ByteView data, std::size_t pos = 0) : m_data(data), m_pos(pos)
	{
		if (pos > data.size())
		{
			m_failed = true;
		}
	}

	std::uint64_t le(std::size_t width)
	{
		if (m_failed || m_data.size() - m_pos < width)
		{
			m_failed = true;
			return 0;
		}
		std::uint64_t value = 0;
		for (std::size_t i = 0; i < width; ++i)
		{
			value |= std::uint64_t{m_data[m_pos + i]} << (8 * i);
		}
		m_pos += width;
		return value;
	}

	std::uint8_t u8() { return static_cast<std::uint8_t>(le(1)); }
	std::uint16_t u16() { return static_cast<std::uint16_t>(le(2)); }
	std::uint32_t u32() { return static_cast<std::uint32_t>(le(4)); }
	std::uint64_t u64() { return le(8); }

	ByteView take(std::size_t count)
	{
		if (m_failed || m_data.size() - m_pos < count)
		{
			m_failed = true;
			return {};
		}
		const ByteView view = m_data.subspan(m_pos, count);
		m_pos += count;
		return view;
	}

	std::size_t position() const { return m_pos; }
	std::size_t remaining() const { return m_failed ? 0 : m_data.size() - m_pos; }
	bool failed() const { return m_failed; }
};

std::uint32_t le32At(ByteView data, std::size_t pos)
{
	LeReader reader(data, pos);
	return reader.u32();
}

std::string toString(ByteView bytes)
{
	return std::string(bytes.begin(), bytes.end());
}

// clang-format off
constexpr std::array<char16_t, 128> kCp437High = {
	0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
	0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
	0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
	0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
	0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
	0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
	0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
};
// clang-format on

void appendUtf8(std::string& out, char16_t cp)
{
	if (cp < 0x80)
	{
		out.push_back(static_cast<char>(cp));
	}
	else if (cp < 0x800)
	{
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
	else
	{
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
}

std::string cp437ToUtf8(std::string_view raw)
{
	std::string out;
	out.reserve(raw.size());
	for (const char c : raw)
	{
		const auto byte = static_cast<std::uint8_t>(c);
		if (byte < 0x80)
		{
			out.push_back(c);
		}
		else
		{
			appendUtf8(out, kCp437High[byte - 0x80u]);
		}
	}
	return out;
}

bool isAscii(std::string_view text)
{
	return std::all_of(text.begin(), text.end(), [](char c) { return static_cast<std::uint8_t>(c) < 0x80; });
}

/// Well-formed UTF-8 (no overlongs, no surrogates, max U+10FFFF).
bool isValidUtf8(std::string_view text)
{
	std::size_t i = 0;
	while (i < text.size())
	{
		const auto b0 = static_cast<std::uint8_t>(text[i]);
		std::size_t length = 0;
		std::uint32_t cp = 0;
		std::uint32_t minimum = 0;
		if (b0 < 0x80)
		{
			++i;
			continue;
		}
		if ((b0 & 0xE0) == 0xC0)
		{
			length = 2;
			cp = b0 & 0x1Fu;
			minimum = 0x80;
		}
		else if ((b0 & 0xF0) == 0xE0)
		{
			length = 3;
			cp = b0 & 0x0Fu;
			minimum = 0x800;
		}
		else if ((b0 & 0xF8) == 0xF0)
		{
			length = 4;
			cp = b0 & 0x07u;
			minimum = 0x10000;
		}
		else
		{
			return false;
		}
		if (text.size() - i < length)
		{
			return false;
		}
		for (std::size_t k = 1; k < length; ++k)
		{
			const auto b = static_cast<std::uint8_t>(text[i + k]);
			if ((b & 0xC0) != 0x80)
			{
				return false;
			}
			cp = (cp << 6) | (b & 0x3Fu);
		}
		if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
		{
			return false;
		}
		i += length;
	}
	return true;
}

/// Rejects names that would escape an extraction directory or confuse path handling.
bool isSafeName(std::string_view name)
{
	if (name.empty() || name.front() == '/' || name.find('\0') != std::string_view::npos)
	{
		return false;
	}
	if (name.size() >= 2 && name[1] == ':')
	{
		return false; // "C:..." drive letter
	}
	std::size_t start = 0;
	while (start <= name.size())
	{
		const std::size_t end = std::min(name.find('/', start), name.size());
		if (name.substr(start, end - start) == "..")
		{
			return false;
		}
		start = end + 1;
	}
	return true;
}

Unexpected parseError(std::string message)
{
	return makeError(ErrorCode::ParseError, std::move(message));
}

/// Raw deflate (no zlib/gzip header). Output must be exactly `expected` bytes.
Result<Bytes> inflateRaw(ByteView input, std::uint64_t expected, const std::string& name)
{
	z_stream stream{};
	if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
	{
		return makeError(ErrorCode::IoError, "zlib: inflateInit2 failed");
	}
	struct Guard
	{
		z_stream* stream;
		~Guard() { inflateEnd(stream); }
	} guard{&stream};

	constexpr std::size_t kChunk = std::size_t{1} << 30; // well below uInt's range
	Bytes out(static_cast<std::size_t>(expected));
	std::size_t inPos = 0;
	std::size_t outPos = 0;
	std::uint8_t probe = 0;
	bool probing = false;
	while (true)
	{
		if (stream.avail_in == 0 && inPos < input.size())
		{
			const std::size_t chunk = std::min(input.size() - inPos, kChunk);
			stream.next_in = input.data() + inPos;
			stream.avail_in = static_cast<uInt>(chunk);
			inPos += chunk;
		}
		if (stream.avail_out == 0)
		{
			if (outPos < out.size())
			{
				const std::size_t chunk = std::min(out.size() - outPos, kChunk);
				stream.next_out = out.data() + outPos;
				stream.avail_out = static_cast<uInt>(chunk);
				outPos += chunk;
			}
			else
			{
				// Output is full: give inflate one spare byte to detect data beyond the declared size.
				stream.next_out = &probe;
				stream.avail_out = 1;
				probing = true;
			}
		}
		const int rc = inflate(&stream, Z_NO_FLUSH);
		if (probing && stream.avail_out == 0)
		{
			return parseError("size mismatch in '" + name + "': more data than declared");
		}
		if (rc == Z_STREAM_END)
		{
			break;
		}
		if (rc == Z_BUF_ERROR)
		{
			if (stream.avail_in == 0 && inPos >= input.size())
			{
				return parseError("truncated deflate data in '" + name + "'");
			}
			if (stream.avail_out != 0)
			{
				return parseError("corrupt deflate data in '" + name + "'");
			}
			continue;
		}
		if (rc != Z_OK)
		{
			return parseError("corrupt deflate data in '" + name + "'" + (stream.msg ? std::string(": ") + stream.msg : ""));
		}
	}
	const std::size_t produced = probing ? out.size() : outPos - stream.avail_out;
	if (produced != out.size())
	{
		return parseError("size mismatch in '" + name + "': less data than declared");
	}
	return out;
}

} // namespace

std::uint32_t crc32(ByteView data)
{
	return static_cast<std::uint32_t>(::crc32_z(0L, data.data(), data.size()));
}

std::ostream& operator<<(std::ostream& os, const ZipEntry& entry)
{
	return os << "ZipEntry{\"" << entry.name << "\" method=" << entry.method << " compressed=" << entry.compressedSize
			  << " uncompressed=" << entry.uncompressedSize << " crc=" << std::hex << entry.crc32 << " flags=" << entry.flags
			  << std::dec << " offset=" << entry.localHeaderOffset << '}';
}

std::ostream& operator<<(std::ostream& os, const ZipLimits& limits)
{
	return os << "ZipLimits{entry=" << limits.maxEntrySize << " total=" << limits.maxTotalSize
			  << " entries=" << limits.maxEntries << '}';
}

const ZipEntry* ZipReader::find(std::string_view name) const
{
	for (const ZipEntry& entry : m_entries)
	{
		if (entry.name == name)
		{
			return &entry;
		}
	}
	return nullptr;
}

Result<ZipReader> ZipReader::open(Bytes data, ZipLimits limits)
{
	ZipReader zip;
	zip.m_data = std::move(data);
	zip.m_limits = limits;
	const ByteView all(zip.m_data);
	if (all.size() < kEocdSize)
	{
		return parseError("not a ZIP archive (too short)");
	}

	// --- End of central directory: scan backwards over the maximum comment length. Prefer a record whose
	// comment ends exactly at the end of the data; tolerate trailing bytes otherwise.
	std::size_t eocd = std::numeric_limits<std::size_t>::max();
	std::size_t tolerated = eocd;
	const std::size_t last = all.size() - kEocdSize;
	const std::size_t first = last > kMaxCommentSize ? last - kMaxCommentSize : 0;
	for (std::size_t pos = last + 1; pos-- > first;)
	{
		if (le32At(all, pos) != kSigEocd)
		{
			continue;
		}
		LeReader commentLength(all, pos + 20);
		const std::size_t end = pos + kEocdSize + commentLength.u16();
		if (end == all.size())
		{
			eocd = pos;
			break;
		}
		if (end < all.size() && tolerated == std::numeric_limits<std::size_t>::max())
		{
			tolerated = pos;
		}
	}
	if (eocd == std::numeric_limits<std::size_t>::max())
	{
		eocd = tolerated;
	}
	if (eocd == std::numeric_limits<std::size_t>::max())
	{
		return parseError("not a ZIP archive (end of central directory not found)");
	}

	LeReader eocdReader(all, eocd + 4);
	std::uint64_t diskNumber = eocdReader.u16();
	std::uint64_t cdDisk = eocdReader.u16();
	std::uint64_t entriesOnDisk = eocdReader.u16();
	std::uint64_t totalEntries = eocdReader.u16();
	std::uint64_t cdSize = eocdReader.u32();
	std::uint64_t cdOffset = eocdReader.u32();
	const std::uint16_t commentLength = eocdReader.u16();
	const ByteView comment = eocdReader.take(commentLength);
	if (eocdReader.failed())
	{
		return parseError("truncated end of central directory");
	}
	std::uint64_t directoryEnd = eocd; // the central directory must end before this

	// --- ZIP64 end of central directory (locator right before the EOCD).
	if (eocd >= kEocd64LocatorSize && le32At(all, eocd - kEocd64LocatorSize) == kSigEocd64Locator)
	{
		LeReader locator(all, eocd - kEocd64LocatorSize + 4);
		const std::uint32_t eocd64Disk = locator.u32();
		const std::uint64_t eocd64Offset = locator.u64();
		const std::uint32_t diskCount = locator.u32();
		if (eocd64Disk != 0 || diskCount > 1)
		{
			return makeError(ErrorCode::Unsupported, "multi-disk archive");
		}
		const std::uint64_t locatorPos = eocd - kEocd64LocatorSize;
		if (eocd64Offset > locatorPos || locatorPos - eocd64Offset < kEocd64Size)
		{
			return parseError("ZIP64 end of central directory out of bounds");
		}
		const auto recordPos = static_cast<std::size_t>(eocd64Offset);
		if (le32At(all, recordPos) != kSigEocd64)
		{
			return parseError("bad ZIP64 end of central directory signature");
		}
		LeReader record(all, recordPos + 4);
		record.u64(); // size of record
		record.u16(); // version made by
		record.u16(); // version needed
		diskNumber = record.u32();
		cdDisk = record.u32();
		entriesOnDisk = record.u64();
		totalEntries = record.u64();
		cdSize = record.u64();
		cdOffset = record.u64();
		if (record.failed())
		{
			return parseError("truncated ZIP64 end of central directory");
		}
		directoryEnd = recordPos;
	}

	if (diskNumber != 0 || cdDisk != 0 || entriesOnDisk != totalEntries)
	{
		return makeError(ErrorCode::Unsupported, "multi-disk archive");
	}
	if (cdOffset > directoryEnd || directoryEnd - cdOffset < cdSize)
	{
		return parseError("central directory out of bounds");
	}
	if (totalEntries > limits.maxEntries)
	{
		return makeError(ErrorCode::Unsupported,
			"too many entries: " + std::to_string(totalEntries) + " (limit " + std::to_string(limits.maxEntries) + ")");
	}
	if (totalEntries > cdSize / kCentralHeaderSize)
	{
		return parseError("central directory too small for " + std::to_string(totalEntries) + " entries");
	}

	zip.m_comment = toString(comment);
	zip.m_dataEnd = cdOffset;
	zip.m_entries.reserve(static_cast<std::size_t>(totalEntries));
	zip.m_locations.reserve(static_cast<std::size_t>(totalEntries));

	const ByteView directory = all.subspan(static_cast<std::size_t>(cdOffset), static_cast<std::size_t>(cdSize));
	LeReader cd(directory);
	std::set<std::string> names;
	std::uint64_t declaredTotal = 0;
	for (std::uint64_t index = 0; index < totalEntries; ++index)
	{
		if (cd.u32() != kSigCentral)
		{
			return parseError("bad central directory signature at entry " + std::to_string(index));
		}
		cd.u16(); // version made by
		cd.u16(); // version needed
		ZipEntry entry;
		entry.flags = cd.u16();
		entry.method = cd.u16();
		cd.u16(); // time
		cd.u16(); // date
		entry.crc32 = cd.u32();
		entry.compressedSize = cd.u32();
		entry.uncompressedSize = cd.u32();
		const std::uint16_t nameLength = cd.u16();
		const std::uint16_t extraLength = cd.u16();
		const std::uint16_t entryCommentLength = cd.u16();
		std::uint32_t diskStart = cd.u16();
		cd.u16(); // internal attributes
		cd.u32(); // external attributes
		entry.localHeaderOffset = cd.u32();
		const std::string rawName = toString(cd.take(nameLength));
		const ByteView extra = cd.take(extraLength);
		cd.take(entryCommentLength);
		if (cd.failed())
		{
			return parseError("truncated central directory at entry " + std::to_string(index));
		}

		// Extra fields: ZIP64 sizes/offset and the Info-ZIP Unicode path.
		std::string unicodeName;
		LeReader fields(extra);
		while (fields.remaining() >= 4)
		{
			const std::uint16_t id = fields.u16();
			const std::uint16_t size = fields.u16();
			const ByteView body = fields.take(size);
			if (fields.failed())
			{
				return parseError("corrupt extra field in '" + rawName + "'");
			}
			LeReader field(body);
			if (id == kExtraZip64)
			{
				if (entry.uncompressedSize == 0xFFFFFFFF)
				{
					entry.uncompressedSize = field.u64();
				}
				if (entry.compressedSize == 0xFFFFFFFF)
				{
					entry.compressedSize = field.u64();
				}
				if (entry.localHeaderOffset == 0xFFFFFFFF)
				{
					entry.localHeaderOffset = field.u64();
				}
				if (diskStart == 0xFFFF)
				{
					diskStart = field.u32();
				}
				if (field.failed())
				{
					return parseError("truncated ZIP64 extra field in '" + rawName + "'");
				}
			}
			else if (id == kExtraUnicodePath && body.size() > 5)
			{
				const std::uint8_t version = field.u8();
				const std::uint32_t nameCrc = field.u32();
				const ByteView raw(reinterpret_cast<const std::uint8_t*>(rawName.data()), rawName.size());
				if (version == 1 && nameCrc == crc32(raw))
				{
					unicodeName = toString(field.take(field.remaining()));
				}
			}
		}
		if (diskStart != 0)
		{
			return makeError(ErrorCode::Unsupported, "multi-disk archive");
		}

		if (!unicodeName.empty() && isValidUtf8(unicodeName))
		{
			entry.name = unicodeName;
		}
		else if ((entry.flags & kFlagUtf8) != 0 || isAscii(rawName))
		{
			if (!isValidUtf8(rawName))
			{
				return parseError("invalid UTF-8 entry name at entry " + std::to_string(index));
			}
			entry.name = rawName;
		}
		else
		{
			entry.name = cp437ToUtf8(rawName);
		}
		std::replace(entry.name.begin(), entry.name.end(), '\\', '/');
		if (!isSafeName(entry.name))
		{
			return parseError("unsafe entry name '" + entry.name + "'");
		}
		if (!names.insert(entry.name).second)
		{
			return parseError("duplicate entry name '" + entry.name + "'");
		}

		if (entry.uncompressedSize > limits.maxTotalSize - std::min(declaredTotal, limits.maxTotalSize))
		{
			return makeError(ErrorCode::Unsupported,
				"archive too large: declared uncompressed size exceeds " + std::to_string(limits.maxTotalSize) + " bytes");
		}
		declaredTotal += entry.uncompressedSize;

		zip.m_entries.push_back(std::move(entry));
		zip.m_locations.push_back(Location{rawName});
		auto located = zip.entryData(zip.m_entries.size() - 1);
		if (!located)
		{
			return Unexpected{located.error()};
		}
	}
	return zip;
}

Result<ByteView> ZipReader::entryData(std::size_t index) const
{
	const ZipEntry& entry = m_entries[index];
	const Location& location = m_locations[index];
	const ByteView all(m_data);
	if (entry.localHeaderOffset > m_dataEnd || m_dataEnd - entry.localHeaderOffset < kLocalHeaderSize)
	{
		return parseError("local header of '" + entry.name + "' out of bounds");
	}
	const auto headerPos = static_cast<std::size_t>(entry.localHeaderOffset);
	LeReader local(all.first(static_cast<std::size_t>(m_dataEnd)), headerPos);
	if (local.u32() != kSigLocal)
	{
		return parseError("bad local header signature for '" + entry.name + "'");
	}
	local.u16(); // version needed
	const std::uint16_t flags = local.u16();
	local.u16(); // method
	local.u16(); // time
	local.u16(); // date
	local.u32(); // crc (may be zero with a data descriptor)
	local.u32(); // compressed size
	local.u32(); // uncompressed size
	const std::uint16_t nameLength = local.u16();
	const std::uint16_t extraLength = local.u16();
	const ByteView name = local.take(nameLength);
	local.take(extraLength);
	if (local.failed())
	{
		return parseError("truncated local header for '" + entry.name + "'");
	}
	if (std::string_view(reinterpret_cast<const char*>(name.data()), name.size()) != location.rawName)
	{
		return parseError("local and central names differ for '" + entry.name + "'");
	}
	if (((flags ^ entry.flags) & kFlagEncrypted) != 0)
	{
		return parseError("local and central encryption flags differ for '" + entry.name + "'");
	}
	if (entry.compressedSize > local.remaining())
	{
		return parseError("data of '" + entry.name + "' out of bounds");
	}
	return local.take(static_cast<std::size_t>(entry.compressedSize));
}

Result<Bytes> ZipReader::read(std::string_view name) const
{
	const ZipEntry* entry = find(name);
	if (!entry)
	{
		return makeError(ErrorCode::NotFound, "no entry '" + std::string(name) + "' in archive");
	}
	if (entry->isEncrypted())
	{
		return makeError(ErrorCode::Unsupported, "encrypted entry '" + entry->name + "'");
	}
	if (entry->method != kMethodStored && entry->method != kMethodDeflate)
	{
		return makeError(ErrorCode::Unsupported,
			"unsupported compression method " + std::to_string(entry->method) + " for '" + entry->name + "'");
	}
	if (entry->uncompressedSize > m_limits.maxEntrySize)
	{
		return makeError(ErrorCode::Unsupported, "entry '" + entry->name + "' exceeds the size limit ("
			+ std::to_string(entry->uncompressedSize) + " > " + std::to_string(m_limits.maxEntrySize) + " bytes)");
	}
	const auto index = static_cast<std::size_t>(entry - m_entries.data());
	KESTREL_TRY(const ByteView data, entryData(index));

	Bytes out;
	if (entry->method == kMethodStored)
	{
		if (entry->compressedSize != entry->uncompressedSize)
		{
			return parseError("size mismatch in stored entry '" + entry->name + "'");
		}
		out.assign(data.begin(), data.end());
	}
	else
	{
		KESTREL_TRY(out, inflateRaw(data, entry->uncompressedSize, entry->name));
	}
	if (crc32(out) != entry->crc32)
	{
		return parseError("CRC mismatch in '" + entry->name + "'");
	}
	return out;
}

} // namespace kestrel::archive
