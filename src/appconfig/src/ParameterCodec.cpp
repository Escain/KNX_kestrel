#include "kestrel/appconfig/Images.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace kestrel::appconfig
{

using namespace Qt::StringLiterals;
using catalog::ParameterTypeKind;

namespace
{

/// Field width in bits of a parameter type (0 = nothing to write).
unsigned widthOf(const catalog::ParameterType& type)
{
	switch (type.kind)
	{
	case ParameterTypeKind::Float:
		if (type.encoding == u"DPT 9")
		{
			return 16;
		}
		if (type.encoding.contains(u"Double"))
		{
			return 64;
		}
		return 32; // IEEE-754 Single
	case ParameterTypeKind::Number:
	case ParameterTypeKind::Restriction:
	case ParameterTypeKind::Text: return type.sizeInBit;
	case ParameterTypeKind::None:
	case ParameterTypeKind::Unsupported: return 0;
	}
	return 0;
}

Status checkRange(const Bytes& segment, const catalog::MemoryLocation& location, unsigned width)
{
	const std::size_t bytes = (location.bitOffset + width + 7) / 8;
	if (location.offset + bytes > segment.size())
	{
		return makeError(ErrorCode::InvalidArgument, "parameter memory beyond the segment");
	}
	return ok();
}

void writeBits(Bytes& segment, std::size_t offset, unsigned bitOffset, unsigned width, std::uint64_t value)
{
	// Bit 0 is the most significant bit of the first octet; multi-octet fields are big endian.
	for (unsigned i = 0; i < width; ++i)
	{
		const unsigned bit = bitOffset + i;
		const std::size_t byte = offset + bit / 8;
		const auto mask = static_cast<std::uint8_t>(0x80u >> (bit % 8));
		const bool set = (value >> (width - 1 - i)) & 1u;
		segment[byte] = static_cast<std::uint8_t>(set ? (segment[byte] | mask) : (segment[byte] & ~mask));
	}
}

std::uint64_t readBits(const Bytes& segment, std::size_t offset, unsigned bitOffset, unsigned width)
{
	std::uint64_t value = 0;
	for (unsigned i = 0; i < width; ++i)
	{
		const unsigned bit = bitOffset + i;
		value = (value << 1) | ((segment[offset + bit / 8] >> (7 - bit % 8)) & 1u);
	}
	return value;
}

QString formatDouble(double value)
{
	// The same canonical form the product files use, e.g. "1.0E+001" is not reproduced, but the number is.
	return QString::number(value, 'g', 17);
}

} // namespace

std::uint16_t encodeDpt9(double value)
{
	// Smallest exponent whose rounded mantissa fits the 12-bit two's complement range.
	const double scaled = value * 100.0;
	int exponent = 0;
	long mantissa = std::lround(scaled);
	while ((mantissa < -2048 || mantissa > 2047) && exponent < 15)
	{
		++exponent;
		mantissa = std::lround(scaled / std::ldexp(1.0, exponent));
	}
	mantissa = std::clamp(mantissa, -2048L, 2047L);
	const unsigned sign = mantissa < 0 ? 0x8000u : 0u;
	return static_cast<std::uint16_t>(
		sign | (static_cast<unsigned>(exponent) << 11) | (static_cast<unsigned long>(mantissa) & 0x7FFu));
}

double decodeDpt9(std::uint16_t raw)
{
	const int exponent = (raw >> 11) & 0x0F;
	int mantissa = raw & 0x7FF;
	if (raw & 0x8000)
	{
		mantissa -= 2048;
	}
	return 0.01 * mantissa * std::ldexp(1.0, exponent);
}

Status writeParameter(Bytes& segment, const catalog::ParameterType& type, const catalog::MemoryLocation& location,
	const QString& value)
{
	const unsigned width = widthOf(type);
	if (width == 0)
	{
		if (type.kind == ParameterTypeKind::None)
		{
			return ok();
		}
		return makeError(ErrorCode::Unsupported,
			"parameter type " + type.element.toStdString() + " is not supported yet");
	}
	KESTREL_CHECK(checkRange(segment, location, width));

	switch (type.kind)
	{
	case ParameterTypeKind::Text:
	{
		if (location.bitOffset != 0)
		{
			return makeError(ErrorCode::Unsupported, "text parameter not octet aligned");
		}
		const QByteArray text = value.toLatin1();
		const std::size_t length = width / 8;
		for (std::size_t i = 0; i < length; ++i)
		{
			segment[location.offset + i] = i < static_cast<std::size_t>(text.size()) ? static_cast<std::uint8_t>(text[static_cast<qsizetype>(i)]) : 0;
		}
		return ok();
	}
	case ParameterTypeKind::Float:
	{
		bool ok = false;
		const double number = value.toDouble(&ok);
		if (!ok)
		{
			return makeError(ErrorCode::InvalidArgument, "not a number: " + value.toStdString());
		}
		std::uint64_t raw = 0;
		if (width == 16)
		{
			raw = encodeDpt9(number);
		}
		else if (width == 32)
		{
			raw = std::bit_cast<std::uint32_t>(static_cast<float>(number));
		}
		else
		{
			raw = std::bit_cast<std::uint64_t>(number);
		}
		writeBits(segment, location.offset, location.bitOffset, width, raw);
		return kestrel::ok();
	}
	case ParameterTypeKind::Number:
	case ParameterTypeKind::Restriction:
	{
		bool ok = false;
		// Integers may be written as "10" or "1.0E+001" in product files.
		const double number = value.trimmed().toDouble(&ok);
		if (!ok)
		{
			return makeError(ErrorCode::InvalidArgument, "not a number: " + value.toStdString());
		}
		const auto integer = static_cast<std::int64_t>(std::llround(number));
		const std::uint64_t mask = width >= 64 ? ~std::uint64_t{0} : ((std::uint64_t{1} << width) - 1);
		writeBits(segment, location.offset, location.bitOffset, width, static_cast<std::uint64_t>(integer) & mask);
		return kestrel::ok();
	}
	case ParameterTypeKind::None:
	case ParameterTypeKind::Unsupported: break;
	}
	return ok();
}

Result<QString> readParameter(const Bytes& segment, const catalog::ParameterType& type,
	const catalog::MemoryLocation& location)
{
	const unsigned width = widthOf(type);
	if (width == 0)
	{
		return makeError(ErrorCode::Unsupported, "parameter type has no memory representation");
	}
	KESTREL_CHECK(checkRange(segment, location, width));
	switch (type.kind)
	{
	case ParameterTypeKind::Text:
	{
		QByteArray text;
		for (std::size_t i = 0; i < width / 8; ++i)
		{
			const std::uint8_t c = segment[location.offset + i];
			if (c == 0)
			{
				break;
			}
			text.append(static_cast<char>(c));
		}
		return QString::fromLatin1(text);
	}
	case ParameterTypeKind::Float:
	{
		const std::uint64_t raw = readBits(segment, location.offset, location.bitOffset, width);
		if (width == 16)
		{
			return formatDouble(decodeDpt9(static_cast<std::uint16_t>(raw)));
		}
		if (width == 32)
		{
			return formatDouble(std::bit_cast<float>(static_cast<std::uint32_t>(raw)));
		}
		return formatDouble(std::bit_cast<double>(raw));
	}
	case ParameterTypeKind::Number:
	case ParameterTypeKind::Restriction:
	{
		std::uint64_t raw = readBits(segment, location.offset, location.bitOffset, width);
		if (type.numberType == u"signedInt" && width < 64 && (raw >> (width - 1)) & 1u)
		{
			return QString::number(static_cast<std::int64_t>(raw) - (std::int64_t{1} << width));
		}
		return QString::number(raw);
	}
	case ParameterTypeKind::None:
	case ParameterTypeKind::Unsupported: break;
	}
	return makeError(ErrorCode::Unsupported, "parameter type has no memory representation");
}

} // namespace kestrel::appconfig
