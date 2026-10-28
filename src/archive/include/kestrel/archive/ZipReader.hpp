#pragma once

// Own in-memory ZIP reader (ADR-004): C++ standard library plus system zlib.
//
// Supported: end-of-central-directory search (with archive comment), central directory and local
// headers, ZIP64 (EOCD64 record/locator and the 0x0001 extra field), methods 0 (stored) and 8
// (raw deflate), CRC-32 verification, UTF-8 names (flag bit 11 or the Info-ZIP 0x7075 extra field;
// other non-ASCII names are decoded as CP437), data descriptors (sizes come from the central directory).
//
// Rejected cleanly (never UB): truncated or corrupt input, multi-disk archives, unsafe names
// ("..", absolute paths, drive letters, NUL), duplicate names, size lies, and archives whose declared
// uncompressed total exceeds ZipLimits::maxTotalSize (open fails). Encrypted entries (flag bit 0 or 6,
// WinZip-AES method 99), unknown methods and entries above ZipLimits::maxEntrySize are listed but
// read() fails with ErrorCode::Unsupported.

#include "kestrel/base/Bytes.hpp"
#include "kestrel/base/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace kestrel::archive
{

inline constexpr std::uint16_t kMethodStored = 0;
inline constexpr std::uint16_t kMethodDeflate = 8;
inline constexpr std::uint16_t kMethodWinZipAes = 99;

inline constexpr std::uint16_t kFlagEncrypted = 0x0001;
inline constexpr std::uint16_t kFlagDataDescriptor = 0x0008;
inline constexpr std::uint16_t kFlagStrongEncryption = 0x0040;
inline constexpr std::uint16_t kFlagUtf8 = 0x0800;

/// One central-directory record.
struct ZipEntry
{
	std::string name; ///< UTF-8, '/' separated ('\' is normalised to '/'); directories end with '/'
	std::uint64_t compressedSize = 0;
	std::uint64_t uncompressedSize = 0;
	std::uint16_t method = kMethodStored;
	std::uint32_t crc32 = 0;
	std::uint16_t flags = 0;
	std::uint64_t localHeaderOffset = 0;

	bool isDirectory() const { return !name.empty() && name.back() == '/'; }
	bool isEncrypted() const
	{
		return (flags & (kFlagEncrypted | kFlagStrongEncryption)) != 0 || method == kMethodWinZipAes;
	}

	friend bool operator==(const ZipEntry&, const ZipEntry&) = default;
};

std::ostream& operator<<(std::ostream& os, const ZipEntry& entry);

/// Zip-bomb guards.
struct ZipLimits
{
	std::uint64_t maxEntrySize = std::uint64_t{256} << 20; ///< per entry, uncompressed (read() fails above)
	std::uint64_t maxTotalSize = std::uint64_t{1} << 30;   ///< sum of declared uncompressed sizes (open() fails above)
	std::size_t maxEntries = 100000;                       ///< open() fails above

	friend bool operator==(const ZipLimits&, const ZipLimits&) = default;
};

std::ostream& operator<<(std::ostream& os, const ZipLimits& limits);

class ZipReader
{
public:
	/// Parses the central directory and checks every local header. The archive bytes are kept.
	static Result<ZipReader> open(Bytes data, ZipLimits limits = {});

	/// Central-directory order.
	const std::vector<ZipEntry>& entries() const { return m_entries; }
	/// Exact (case-sensitive) name match, nullptr if absent.
	const ZipEntry* find(std::string_view name) const;
	bool contains(std::string_view name) const { return find(name) != nullptr; }

	/// Archive comment (raw bytes, usually ASCII/UTF-8).
	const std::string& comment() const { return m_comment; }
	const ZipLimits& limits() const { return m_limits; }

	/// Decompresses and CRC-checks one entry. NotFound if absent, Unsupported for encrypted entries,
	/// unknown methods and entries above the size limit, ParseError for corrupt data.
	Result<Bytes> read(std::string_view name) const;
	Result<Bytes> read(const ZipEntry& entry) const { return read(entry.name); }

private:
	ZipReader() = default;

	struct Location
	{
		std::string rawName; ///< name bytes as stored in the central directory (checked against the local header)
	};

	Bytes m_data;
	std::vector<ZipEntry> m_entries;
	std::vector<Location> m_locations; ///< parallel to m_entries
	std::uint64_t m_dataEnd = 0;       ///< start of the central directory: entry data must end before it
	std::string m_comment;
	ZipLimits m_limits;

	Result<ByteView> entryData(std::size_t index) const;
};

/// CRC-32 (ISO 3309, as used by ZIP).
std::uint32_t crc32(ByteView data);

} // namespace kestrel::archive
