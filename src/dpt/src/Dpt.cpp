// DptId, the DptValue printer, the unit table and the dispatcher over the codec families.

#include "Detail.hpp"

#include <array>
#include <charconv>
#include <limits>
#include <system_error>
#include <type_traits>

namespace kestrel::dpt
{

using namespace detail;

namespace
{

/// Parses all of `text` as an unsigned 16-bit number (no sign, no whitespace).
std::optional<std::uint16_t> parseU16(std::string_view text)
{
	std::uint16_t value = 0;
	const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (text.empty() || ec != std::errc{} || ptr != text.data() + text.size())
	{
		return std::nullopt;
	}
	return value;
}

struct UnitEntry
{
	std::uint16_t main;
	std::uint16_t sub;
	std::string_view unit;
};

// Unit symbols from KNX 3/7/2. Subtypes whose wire value is scaled (7.003, 8.010 ...) are left
// out on purpose: we keep their raw value, and a unit next to it would mislead.
constexpr std::array kUnits = {
	UnitEntry{5, 1, "%"},
	UnitEntry{5, 3, "°"},
	UnitEntry{5, 4, "%"},
	UnitEntry{6, 1, "%"},
	UnitEntry{7, 2, "ms"},
	UnitEntry{7, 5, "s"},
	UnitEntry{7, 6, "min"},
	UnitEntry{7, 7, "h"},
	UnitEntry{7, 11, "mm"},
	UnitEntry{7, 12, "mA"},
	UnitEntry{7, 13, "lx"},
	UnitEntry{7, 600, "K"},
	UnitEntry{8, 2, "ms"},
	UnitEntry{8, 5, "s"},
	UnitEntry{8, 6, "min"},
	UnitEntry{8, 7, "h"},
	UnitEntry{8, 11, "°"},
	UnitEntry{8, 12, "m"},
	UnitEntry{9, 1, "°C"},
	UnitEntry{9, 2, "K"},
	UnitEntry{9, 3, "K/h"},
	UnitEntry{9, 4, "lx"},
	UnitEntry{9, 5, "m/s"},
	UnitEntry{9, 6, "Pa"},
	UnitEntry{9, 7, "%"},
	UnitEntry{9, 8, "ppm"},
	UnitEntry{9, 9, "m³/h"},
	UnitEntry{9, 10, "s"},
	UnitEntry{9, 11, "ms"},
	UnitEntry{9, 20, "mV"},
	UnitEntry{9, 21, "mA"},
	UnitEntry{9, 22, "W/m²"},
	UnitEntry{9, 23, "K/%"},
	UnitEntry{9, 24, "kW"},
	UnitEntry{9, 25, "l/h"},
	UnitEntry{9, 26, "l/m²"},
	UnitEntry{9, 27, "°F"},
	UnitEntry{9, 28, "km/h"},
	UnitEntry{9, 29, "g/m³"},
	UnitEntry{9, 30, "µg/m³"},
	UnitEntry{12, 100, "s"},
	UnitEntry{12, 101, "min"},
	UnitEntry{12, 102, "h"},
	UnitEntry{12, 1200, "l"},
	UnitEntry{12, 1201, "m³"},
	UnitEntry{13, 10, "Wh"},
	UnitEntry{13, 11, "VAh"},
	UnitEntry{13, 12, "VARh"},
	UnitEntry{13, 13, "kWh"},
	UnitEntry{13, 14, "kVAh"},
	UnitEntry{13, 15, "kVARh"},
	UnitEntry{13, 16, "MWh"},
	UnitEntry{13, 100, "s"},
	UnitEntry{13, 1200, "l"},
	UnitEntry{13, 1201, "m³"},
	UnitEntry{14, 0, "m/s²"},
	UnitEntry{14, 7, "°"},
	UnitEntry{14, 19, "A"},
	UnitEntry{14, 27, "V"},
	UnitEntry{14, 28, "V"},
	UnitEntry{14, 31, "J"},
	UnitEntry{14, 33, "Hz"},
	UnitEntry{14, 39, "m"},
	UnitEntry{14, 51, "kg"},
	UnitEntry{14, 56, "W"},
	UnitEntry{14, 58, "Pa"},
	UnitEntry{14, 65, "m/s"},
	UnitEntry{14, 68, "°C"},
	UnitEntry{14, 69, "K"},
	UnitEntry{14, 70, "K"},
	UnitEntry{14, 74, "s"},
	UnitEntry{14, 76, "m³"},
	UnitEntry{14, 77, "m³/s"},
	UnitEntry{14, 80, "VA"},
	UnitEntry{14, 1200, "m³/h"},
	UnitEntry{14, 1201, "l/s"},
};

const Codec* findCodec(std::uint16_t main)
{
	if (const Codec* codec = findBitCodec(main))
	{
		return codec;
	}
	if (const Codec* codec = findNumericCodec(main))
	{
		return codec;
	}
	return findTextCodec(main);
}

Unexpected unsupported(DptId id)
{
	return makeError(ErrorCode::Unsupported, "DPT " + id.toString() + ": main type not supported");
}

/// Plain rendering for values that do not fit the DPT (format() must never fail).
std::string formatRaw(const DptValue& value)
{
	return std::visit(
		[](const auto& v) -> std::string
		{
			using T = std::decay_t<decltype(v)>;
			if constexpr (std::is_same_v<T, bool>)
			{
				return v ? "true" : "false";
			}
			else if constexpr (std::is_same_v<T, double>)
			{
				return formatFixed(v, 6);
			}
			else if constexpr (std::is_same_v<T, std::string>)
			{
				return v;
			}
			else
			{
				return std::to_string(v);
			}
		},
		value);
}

std::string formatGeneric(const Codec& codec, DptId id, const DptValue& value)
{
	const std::string_view unit = unitOf(id);
	switch (codec.kind)
	{
		case Codec::Kind::Signed:
			if (auto v = toSigned(value, std::numeric_limits<std::int64_t>::min(),
								  std::numeric_limits<std::int64_t>::max()))
			{
				return withUnit(std::to_string(*v), unit);
			}
			break;
		case Codec::Kind::Unsigned:
			if (auto v = toUnsigned(value, std::numeric_limits<std::uint64_t>::max()))
			{
				return withUnit(std::to_string(*v), unit);
			}
			break;
		case Codec::Kind::Real:
			if (auto v = toReal(value))
			{
				return withUnit(formatFixed(*v, 2), unit);
			}
			break;
		case Codec::Kind::Bool:
		case Codec::Kind::Text:
			break;
	}
	return formatRaw(value);
}

Result<DptValue> parseGeneric(const Codec& codec, DptId id, std::string_view text)
{
	const std::string_view number = stripUnit(text, unitOf(id));
	switch (codec.kind)
	{
		case Codec::Kind::Signed:
			if (auto v = parseSigned(number))
			{
				return DptValue{*v};
			}
			break;
		case Codec::Kind::Unsigned:
			if (auto v = parseUnsigned(number))
			{
				return DptValue{*v};
			}
			break;
		case Codec::Kind::Real:
			if (auto v = parseReal(number))
			{
				return DptValue{*v};
			}
			break;
		case Codec::Kind::Text:
			return DptValue{std::string(text)};
		case Codec::Kind::Bool:
			break;
	}
	return invalid(id, "cannot parse '" + std::string(text) + "'");
}

} // namespace

// ----- DptId -------------------------------------------------------------------------------

Result<DptId> DptId::parse(std::string_view text)
{
	const std::string original(text);
	auto fail = [&original]() { return makeError(ErrorCode::InvalidArgument, "invalid DPT '" + original + "'"); };

	text = trim(text);
	bool requireSub = false;
	if (startsWithNoCase(text, "DPST-"))
	{
		text.remove_prefix(5);
		requireSub = true;
	}
	else if (startsWithNoCase(text, "DPT-"))
	{
		text.remove_prefix(4);
	}
	else if (startsWithNoCase(text, "DPT"))
	{
		text = trim(text.substr(3)); // "DPT9.001", "DPT 9.001"
	}

	const std::size_t separator = text.find_first_of(".-");
	const auto mainPart = parseU16(text.substr(0, separator));
	if (!mainPart || *mainPart == 0)
	{
		return fail();
	}
	DptId id;
	id.main = *mainPart;
	if (separator == std::string_view::npos)
	{
		if (requireSub)
		{
			return fail();
		}
		return id;
	}
	const auto subPart = parseU16(text.substr(separator + 1));
	if (!subPart)
	{
		return fail();
	}
	id.sub = *subPart;
	return id;
}

std::string DptId::toString() const
{
	std::string subText = std::to_string(sub);
	if (subText.size() < 3)
	{
		subText.insert(0, 3 - subText.size(), '0');
	}
	return std::to_string(main) + "." + subText;
}

std::string DptId::toEtsString() const
{
	if (sub == 0)
	{
		return "DPT-" + std::to_string(main);
	}
	return "DPST-" + std::to_string(main) + "-" + std::to_string(sub);
}

std::ostream& operator<<(std::ostream& os, const DptId& id)
{
	return os << id.toString();
}

std::ostream& operator<<(std::ostream& os, const DptValue& value)
{
	std::visit(
		[&os](const auto& v)
		{
			using T = std::decay_t<decltype(v)>;
			if constexpr (std::is_same_v<T, bool>)
			{
				os << "bool:" << (v ? "true" : "false");
			}
			else if constexpr (std::is_same_v<T, std::int64_t>)
			{
				os << "i64:" << v;
			}
			else if constexpr (std::is_same_v<T, std::uint64_t>)
			{
				os << "u64:" << v;
			}
			else if constexpr (std::is_same_v<T, double>)
			{
				// Shortest round-trip form, so that failing ASSERT_EQs show the real difference.
				std::array<char, 64> buffer{};
				const auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v);
				os << "f64:" << (ec == std::errc{} ? std::string_view(buffer.data(), ptr) : std::string_view("?"));
			}
			else
			{
				os << "str:\"" << v << '"';
			}
		},
		value);
	return os;
}

// ----- public codec API --------------------------------------------------------------------

Result<Bytes> encode(DptId id, const DptValue& value)
{
	const Codec* codec = findCodec(id.main);
	if (!codec)
	{
		return unsupported(id);
	}
	return codec->encode(id, value);
}

Result<DptValue> decode(DptId id, ByteView bytes)
{
	const Codec* codec = findCodec(id.main);
	if (!codec)
	{
		return unsupported(id);
	}
	return codec->decode(id, bytes);
}

std::string format(DptId id, const DptValue& value)
{
	const Codec* codec = findCodec(id.main);
	if (!codec)
	{
		return formatRaw(value);
	}
	if (!codec->format)
	{
		return formatGeneric(*codec, id, value);
	}
	return codec->format(id, value).value_or(formatRaw(value));
}

Result<DptValue> parse(DptId id, std::string_view text)
{
	const Codec* codec = findCodec(id.main);
	if (!codec)
	{
		return unsupported(id);
	}
	KESTREL_TRY(DptValue value, codec->parse ? codec->parse(id, text) : parseGeneric(*codec, id, text));
	// Same range rules as encode(): parse() only hands out values that can go on the bus.
	KESTREL_TRY([[maybe_unused]] Bytes bytes, codec->encode(id, value));
	return value;
}

std::string_view unitOf(DptId id)
{
	for (const UnitEntry& entry : kUnits)
	{
		if (entry.main == id.main && entry.sub == id.sub)
		{
			return entry.unit;
		}
	}
	return {};
}

std::size_t sizeInBit(DptId id)
{
	const Codec* codec = findCodec(id.main);
	return codec ? codec->bits : 0;
}

bool isShortForm(DptId id)
{
	const std::size_t bits = sizeInBit(id);
	return bits > 0 && bits <= 6;
}

} // namespace kestrel::dpt
