#pragma once

// Byte buffer helpers for wire formats. All KNX multi-byte fields are big-endian.

#include "kestrel/base/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kestrel
{

using Bytes = std::vector<std::uint8_t>;
using ByteView = std::span<const std::uint8_t>;

/// "06 10 02 05" style hex dump (lowercase, space separated).
std::string toHex(ByteView bytes);

/// Parses "06 10 02 05", "06100205" or "06:10:02:05".
Result<Bytes> fromHex(std::string_view text);

/// Sequential big-endian reader with bounds checking. Reads past the end set
/// the failure flag and return 0, so decoders can check once at the end.
class ByteReader
{
	ByteView m_data;
	std::size_t m_pos = 0;
	bool m_failed = false;

public:
	explicit ByteReader(ByteView data) : m_data(data) {}

	std::uint8_t u8();
	std::uint16_t u16();
	std::uint32_t u32();
	ByteView take(std::size_t count);
	void skip(std::size_t count);

	std::size_t position() const { return m_pos; }
	std::size_t remaining() const { return m_failed ? 0 : m_data.size() - m_pos; }
	ByteView rest() const { return m_failed ? ByteView{} : m_data.subspan(m_pos); }
	bool failed() const { return m_failed; }
	bool atEnd() const { return m_pos >= m_data.size(); }
};

/// Sequential big-endian writer appending to a Bytes buffer.
class ByteWriter
{
	Bytes& m_out;

public:
	explicit ByteWriter(Bytes& out) : m_out(out) {}

	void u8(std::uint8_t value) { m_out.push_back(value); }
	void u16(std::uint16_t value);
	void u32(std::uint32_t value);
	void append(ByteView bytes) { m_out.insert(m_out.end(), bytes.begin(), bytes.end()); }
	std::size_t size() const { return m_out.size(); }
	/// Overwrite a previously written byte (e.g. a length field).
	void patchU8(std::size_t offset, std::uint8_t value) { m_out.at(offset) = value; }
	void patchU16(std::size_t offset, std::uint16_t value);
};

} // namespace kestrel
