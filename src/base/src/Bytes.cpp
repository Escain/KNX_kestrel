#include "kestrel/base/Bytes.hpp"

namespace kestrel
{

std::string toHex(ByteView bytes)
{
	static constexpr char kDigits[] = "0123456789abcdef";
	std::string out;
	out.reserve(bytes.size() * 3);
	for (std::size_t i = 0; i < bytes.size(); ++i)
	{
		if (i != 0)
		{
			out.push_back(' ');
		}
		out.push_back(kDigits[bytes[i] >> 4]);
		out.push_back(kDigits[bytes[i] & 0x0F]);
	}
	return out;
}

Result<Bytes> fromHex(std::string_view text)
{
	auto nibble = [](char c) -> int
	{
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	};

	Bytes out;
	int high = -1;
	for (char c : text)
	{
		if (c == ' ' || c == ':' || c == '-')
		{
			continue;
		}
		const int value = nibble(c);
		if (value < 0)
		{
			return makeError(ErrorCode::ParseError, std::string("invalid hex character '") + c + "'");
		}
		if (high < 0)
		{
			high = value;
		}
		else
		{
			out.push_back(static_cast<std::uint8_t>((high << 4) | value));
			high = -1;
		}
	}
	if (high >= 0)
	{
		return makeError(ErrorCode::ParseError, "odd number of hex digits");
	}
	return out;
}

std::uint8_t ByteReader::u8()
{
	if (m_failed || m_pos + 1 > m_data.size())
	{
		m_failed = true;
		return 0;
	}
	return m_data[m_pos++];
}

std::uint16_t ByteReader::u16()
{
	const auto high = u8();
	const auto low = u8();
	return static_cast<std::uint16_t>((high << 8) | low);
}

std::uint32_t ByteReader::u32()
{
	const std::uint32_t high = u16();
	const std::uint32_t low = u16();
	return (high << 16) | low;
}

ByteView ByteReader::take(std::size_t count)
{
	if (m_failed || m_pos + count > m_data.size())
	{
		m_failed = true;
		return {};
	}
	auto out = m_data.subspan(m_pos, count);
	m_pos += count;
	return out;
}

void ByteReader::skip(std::size_t count)
{
	(void)take(count);
}

void ByteWriter::u16(std::uint16_t value)
{
	m_out.push_back(static_cast<std::uint8_t>(value >> 8));
	m_out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void ByteWriter::u32(std::uint32_t value)
{
	u16(static_cast<std::uint16_t>(value >> 16));
	u16(static_cast<std::uint16_t>(value & 0xFFFF));
}

void ByteWriter::patchU16(std::size_t offset, std::uint16_t value)
{
	m_out.at(offset) = static_cast<std::uint8_t>(value >> 8);
	m_out.at(offset + 1) = static_cast<std::uint8_t>(value & 0xFF);
}

} // namespace kestrel
