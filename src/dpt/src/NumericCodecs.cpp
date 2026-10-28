// Numeric DPTs (KNX 3/7/2 §3.5-3.14, §3.20):
//   5   U8   (5.001 scaled 0..100 %, 5.003 angle 0..360 °)
//   6   V8   7   U16   8   V16   12   U32   13   V32   20   N8 (raw)
//   9   F16  KNX 2-byte float
//   14  F32  IEEE 754 single precision

#include "Detail.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace kestrel::dpt::detail
{

namespace
{

// ----- plain integers, N bytes big-endian (two's complement for the signed ones) ------------

template <std::size_t N>
Result<Bytes> encodeUnsignedN(DptId id, const DptValue& value)
{
	constexpr std::uint64_t kMax = (std::uint64_t{1} << (8 * N)) - 1;
	auto raw = toUnsigned(value, kMax);
	if (!raw)
	{
		return invalid(id, raw.error().message);
	}
	return writeBigEndian(*raw, N);
}

template <std::size_t N>
Result<DptValue> decodeUnsignedN(DptId id, ByteView bytes)
{
	KESTREL_CHECK(checkSize(id, bytes, N));
	return DptValue{readBigEndian(bytes)};
}

template <std::size_t N>
Result<Bytes> encodeSignedN(DptId id, const DptValue& value)
{
	constexpr std::int64_t kMax = (std::int64_t{1} << (8 * N - 1)) - 1;
	auto v = toSigned(value, -kMax - 1, kMax);
	if (!v)
	{
		return invalid(id, v.error().message);
	}
	// Conversion to unsigned is modular, i.e. exactly the two's complement bit pattern.
	return writeBigEndian(static_cast<std::uint64_t>(*v), N);
}

template <std::size_t N>
Result<DptValue> decodeSignedN(DptId id, ByteView bytes)
{
	KESTREL_CHECK(checkSize(id, bytes, N));
	constexpr std::uint64_t kSignBit = std::uint64_t{1} << (8 * N - 1);
	const std::uint64_t raw = readBigEndian(bytes);
	auto v = static_cast<std::int64_t>(raw);
	if (raw & kSignBit)
	{
		v -= static_cast<std::int64_t>(kSignBit << 1);
	}
	return DptValue{v};
}

// ----- DPT 5: 5.001 and 5.003 are scaled to physical values, the rest stays raw ------------

/// Physical full scale of the scaled 5.xxx subtypes, 0 for raw ones.
double scaleOf5(DptId id)
{
	switch (id.sub)
	{
		case 1: return 100.0; // DPT_Scaling, %
		case 3: return 360.0; // DPT_Angle, °
		default: return 0.0;
	}
}

Result<Bytes> encodeDpt5(DptId id, const DptValue& value)
{
	const double scale = scaleOf5(id);
	if (scale == 0.0)
	{
		return encodeUnsignedN<1>(id, value);
	}
	auto v = toReal(value);
	if (!v)
	{
		return invalid(id, v.error().message);
	}
	if (*v < 0.0 || *v > scale)
	{
		return invalid(id, "value " + formatFixed(*v, 3) + " out of range 0.." + formatFixed(scale, 0));
	}
	// std::round: half away from zero, so 50 % -> 127.5 -> 128.
	return Bytes{static_cast<std::uint8_t>(std::round(*v * 255.0 / scale))};
}

Result<DptValue> decodeDpt5(DptId id, ByteView bytes)
{
	const double scale = scaleOf5(id);
	if (scale == 0.0)
	{
		return decodeUnsignedN<1>(id, bytes);
	}
	KESTREL_CHECK(checkSize(id, bytes, 1));
	return DptValue{static_cast<double>(bytes[0]) * scale / 255.0};
}

std::optional<std::string> formatDpt5(DptId id, const DptValue& value)
{
	if (scaleOf5(id) != 0.0)
	{
		// One decimal: a raw step is 0.39 % / 1.41 °, more digits would be noise.
		auto v = toReal(value);
		if (!v)
		{
			return std::nullopt;
		}
		return withUnit(formatFixed(*v, 1), unitOf(id));
	}
	auto raw = toUnsigned(value, std::numeric_limits<std::uint64_t>::max());
	if (!raw)
	{
		return std::nullopt;
	}
	return withUnit(std::to_string(*raw), unitOf(id));
}

Result<DptValue> parseDpt5(DptId id, std::string_view text)
{
	const std::string_view number = stripUnit(text, unitOf(id));
	if (scaleOf5(id) != 0.0)
	{
		if (auto v = parseReal(number))
		{
			return DptValue{*v};
		}
	}
	else if (auto raw = parseUnsigned(number))
	{
		return DptValue{*raw};
	}
	return invalid(id, "cannot parse '" + std::string(text) + "'");
}

// ----- DPT 9: KNX 2-byte float -------------------------------------------------------------
// Bit layout MEEE EMMM MMMM MMMM: value = 0.01 * M * 2^E, where M is a 12-bit two's complement
// number (sign = bit 15, low 11 bits = bits 10..0) and E a 4-bit exponent (bits 14..11).

constexpr std::uint64_t kDpt9Invalid = 0x7FFF; // "invalid data" marker (3/7/2 §3.10)

Result<Bytes> encodeDpt9(DptId id, const DptValue& value)
{
	auto v = toReal(value);
	if (!v)
	{
		return invalid(id, v.error().message);
	}
	const double hundredths = *v * 100.0;
	// Smallest exponent whose rounded mantissa fits gives the best resolution.
	for (int exponent = 0; exponent <= 15; ++exponent)
	{
		const double mantissa = std::round(std::ldexp(hundredths, -exponent));
		if (mantissa < -2048.0 || mantissa > 2047.0)
		{
			continue;
		}
		const auto m = static_cast<std::int32_t>(mantissa);
		const std::uint64_t raw = (m < 0 ? 0x8000u : 0u) | (static_cast<std::uint64_t>(exponent) << 11)
								  | (static_cast<std::uint32_t>(m) & 0x07FFu);
		if (raw == kDpt9Invalid)
		{
			break; // 2047 * 2^15 would be read back as "invalid data"
		}
		return writeBigEndian(raw, 2);
	}
	return invalid(id, "value " + formatFixed(*v, 2) + " out of range -671088.64..670433.28");
}

Result<DptValue> decodeDpt9(DptId id, ByteView bytes)
{
	KESTREL_CHECK(checkSize(id, bytes, 2));
	const std::uint64_t raw = readBigEndian(bytes);
	if (raw == kDpt9Invalid)
	{
		return makeError(ErrorCode::ProtocolError, "DPT " + id.toString() + ": invalid data (0x7FFF)");
	}
	const std::uint64_t exponent = (raw >> 11) & 0x0F;
	auto mantissa = static_cast<std::int64_t>(raw & 0x07FF);
	if (raw & 0x8000)
	{
		mantissa -= 2048;
	}
	// Integer product first, then one division: the result is the double nearest to the exact value.
	return DptValue{static_cast<double>(mantissa * (std::int64_t{1} << exponent)) / 100.0};
}

// ----- DPT 14: IEEE 754 float32 ------------------------------------------------------------

Result<Bytes> encodeDpt14(DptId id, const DptValue& value)
{
	auto v = toReal(value);
	if (!v)
	{
		return invalid(id, v.error().message);
	}
	if (std::fabs(*v) > static_cast<double>(std::numeric_limits<float>::max()))
	{
		return invalid(id, "value out of float32 range");
	}
	return writeBigEndian(std::bit_cast<std::uint32_t>(static_cast<float>(*v)), 4);
}

Result<DptValue> decodeDpt14(DptId id, ByteView bytes)
{
	KESTREL_CHECK(checkSize(id, bytes, 4));
	const auto bits = static_cast<std::uint32_t>(readBigEndian(bytes));
	return DptValue{static_cast<double>(std::bit_cast<float>(bits))};
}

std::optional<std::string> formatDpt14(DptId id, const DptValue& value)
{
	auto v = toReal(value);
	if (!v)
	{
		return std::nullopt;
	}
	// Shortest float text: 0.1f decodes to 0.100000001490116 but prints as "0.1".
	return withUnit(formatShortestFloat(*v), unitOf(id));
}

constexpr std::array kCodecs = {
	Codec{5, 8, Codec::Kind::Unsigned, encodeDpt5, decodeDpt5, formatDpt5, parseDpt5},
	Codec{6, 8, Codec::Kind::Signed, encodeSignedN<1>, decodeSignedN<1>, nullptr, nullptr},
	Codec{7, 16, Codec::Kind::Unsigned, encodeUnsignedN<2>, decodeUnsignedN<2>, nullptr, nullptr},
	Codec{8, 16, Codec::Kind::Signed, encodeSignedN<2>, decodeSignedN<2>, nullptr, nullptr},
	Codec{9, 16, Codec::Kind::Real, encodeDpt9, decodeDpt9, nullptr, nullptr},
	Codec{12, 32, Codec::Kind::Unsigned, encodeUnsignedN<4>, decodeUnsignedN<4>, nullptr, nullptr},
	Codec{13, 32, Codec::Kind::Signed, encodeSignedN<4>, decodeSignedN<4>, nullptr, nullptr},
	Codec{14, 32, Codec::Kind::Real, encodeDpt14, decodeDpt14, formatDpt14, nullptr},
	Codec{20, 8, Codec::Kind::Unsigned, encodeUnsignedN<1>, decodeUnsignedN<1>, nullptr, nullptr},
};

} // namespace

const Codec* findNumericCodec(std::uint16_t main)
{
	for (const Codec& codec : kCodecs)
	{
		if (codec.main == main)
		{
			return &codec;
		}
	}
	return nullptr;
}

} // namespace kestrel::dpt::detail
