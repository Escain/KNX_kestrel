// DPTs whose C++ value is text, plus RGB (KNX 3/7/2 §3.11, 3.12, 3.16, 3.29 / 232):
//   10   N3N5r2N6r2N6  time of day        "Mon 14:30:00" / "14:30:00"
//   11   r3N5r4N4r1U7  date               "2024-03-15"
//   16   A112          14-character string (16.000 ASCII, 16.001 ISO 8859-1)
//   232  U8U8U8        RGB                0xRRGGBB, formatted "#RRGGBB"

#include "Detail.hpp"

#include <array>
#include <charconv>
#include <system_error>

namespace kestrel::dpt::detail
{

namespace
{

std::string twoDigits(unsigned value)
{
	return std::string{static_cast<char>('0' + value / 10 % 10), static_cast<char>('0' + value % 10)};
}

/// Splits "a:b:c" (or with '-') into at most `maxParts` unsigned fields; nullopt if malformed.
template <std::size_t MaxParts>
std::optional<std::array<std::uint64_t, MaxParts>> splitNumbers(std::string_view text, char separator,
																std::size_t& count)
{
	std::array<std::uint64_t, MaxParts> parts{};
	count = 0;
	while (true)
	{
		if (count == MaxParts)
		{
			return std::nullopt;
		}
		const std::size_t pos = text.find(separator);
		const std::string_view field = text.substr(0, pos);
		// Plain digits only: parseUnsigned would also accept "+5" or " 5".
		if (field.empty() || field.find_first_not_of("0123456789") != std::string_view::npos)
		{
			return std::nullopt;
		}
		auto value = parseUnsigned(field);
		if (!value)
		{
			return std::nullopt;
		}
		parts[count++] = *value;
		if (pos == std::string_view::npos)
		{
			return parts;
		}
		text.remove_prefix(pos + 1);
	}
}

// ----- DPT 10: time of day -----------------------------------------------------------------

constexpr std::array<std::string_view, 8> kWeekdays = {"", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

struct TimeOfDay
{
	unsigned day = 0; // 0 = no day, 1 = Monday .. 7 = Sunday
	unsigned hour = 0;
	unsigned minute = 0;
	unsigned second = 0;

	bool valid() const { return day <= 7 && hour <= 23 && minute <= 59 && second <= 59; }

	std::string toString() const
	{
		std::string text = day ? std::string(kWeekdays[day]) + " " : std::string();
		return text + twoDigits(hour) + ":" + twoDigits(minute) + ":" + twoDigits(second);
	}
};

/// "[Ddd ]H:MM[:SS]", Ddd = Mon..Sun or a digit 0..7.
std::optional<TimeOfDay> parseTimeOfDay(std::string_view text)
{
	text = trim(text);
	TimeOfDay time;
	if (const std::size_t space = text.find(' '); space != std::string_view::npos)
	{
		const std::string_view dayText = text.substr(0, space);
		bool found = false;
		for (unsigned day = 1; day < kWeekdays.size(); ++day)
		{
			if (equalsNoCase(dayText, kWeekdays[day]))
			{
				time.day = day;
				found = true;
			}
		}
		if (!found)
		{
			if (dayText.size() != 1 || dayText[0] < '0' || dayText[0] > '7')
			{
				return std::nullopt;
			}
			time.day = static_cast<unsigned>(dayText[0] - '0');
		}
		text = trim(text.substr(space + 1));
	}
	std::size_t count = 0;
	auto parts = splitNumbers<3>(text, ':', count);
	if (!parts || count < 2 || (*parts)[0] > 23 || (*parts)[1] > 59 || (*parts)[2] > 59)
	{
		return std::nullopt;
	}
	time.hour = static_cast<unsigned>((*parts)[0]);
	time.minute = static_cast<unsigned>((*parts)[1]);
	time.second = static_cast<unsigned>((*parts)[2]);
	return time;
}

Result<Bytes> encodeDpt10(DptId id, const DptValue& value)
{
	auto text = toText(value);
	if (!text)
	{
		return invalid(id, text.error().message);
	}
	auto time = parseTimeOfDay(*text);
	if (!time)
	{
		return invalid(id, "invalid time of day '" + *text + "' (expected '[Mon ]HH:MM:SS')");
	}
	return Bytes{static_cast<std::uint8_t>((time->day << 5) | time->hour), static_cast<std::uint8_t>(time->minute),
				 static_cast<std::uint8_t>(time->second)};
}

Result<DptValue> decodeDpt10(DptId id, ByteView bytes)
{
	KESTREL_CHECK(checkSize(id, bytes, 3));
	// Reserved bits (the top two of minutes and seconds) are ignored.
	const TimeOfDay time{static_cast<unsigned>(bytes[0] >> 5), bytes[0] & 0x1Fu, bytes[1] & 0x3Fu, bytes[2] & 0x3Fu};
	if (!time.valid())
	{
		return makeError(ErrorCode::ParseError, "DPT " + id.toString() + ": time field out of range");
	}
	return DptValue{time.toString()};
}

/// Canonicalises: "8:05" -> "08:05:00", "mon 8:05" -> "Mon 08:05:00".
Result<DptValue> parseDpt10(DptId id, std::string_view text)
{
	auto time = parseTimeOfDay(text);
	if (!time)
	{
		return invalid(id, "invalid time of day '" + std::string(text) + "' (expected '[Mon ]HH:MM:SS')");
	}
	return DptValue{time->toString()};
}

// ----- DPT 11: date ------------------------------------------------------------------------

struct Date
{
	unsigned year = 2000;
	unsigned month = 1;
	unsigned day = 1;

	/// 3/7/2 §3.12: the wire year 0..99 covers 1990..2089.
	bool valid() const
	{
		if (year < 1990 || year > 2089 || month < 1 || month > 12 || day < 1)
		{
			return false;
		}
		constexpr std::array<unsigned, 12> kDays = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
		const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
		return day <= kDays[month - 1] + ((month == 2 && leap) ? 1u : 0u);
	}

	std::string toString() const { return std::to_string(year) + "-" + twoDigits(month) + "-" + twoDigits(day); }
};

std::optional<Date> parseDate(std::string_view text)
{
	std::size_t count = 0;
	auto parts = splitNumbers<3>(trim(text), '-', count);
	if (!parts || count != 3 || (*parts)[0] > 9999 || (*parts)[1] > 12 || (*parts)[2] > 31)
	{
		return std::nullopt;
	}
	const Date date{static_cast<unsigned>((*parts)[0]), static_cast<unsigned>((*parts)[1]),
					static_cast<unsigned>((*parts)[2])};
	if (!date.valid())
	{
		return std::nullopt;
	}
	return date;
}

Result<Bytes> encodeDpt11(DptId id, const DptValue& value)
{
	auto text = toText(value);
	if (!text)
	{
		return invalid(id, text.error().message);
	}
	auto date = parseDate(*text);
	if (!date)
	{
		return invalid(id, "invalid date '" + *text + "' (expected YYYY-MM-DD, years 1990..2089)");
	}
	return Bytes{static_cast<std::uint8_t>(date->day), static_cast<std::uint8_t>(date->month),
				 static_cast<std::uint8_t>(date->year % 100)};
}

Result<DptValue> decodeDpt11(DptId id, ByteView bytes)
{
	KESTREL_CHECK(checkSize(id, bytes, 3));
	const unsigned yy = bytes[2] & 0x7Fu;
	const Date date{yy < 90 ? 2000 + yy : 1900 + yy, bytes[1] & 0x0Fu, bytes[0] & 0x1Fu};
	if (yy > 99 || !date.valid())
	{
		return makeError(ErrorCode::ParseError, "DPT " + id.toString() + ": date field out of range");
	}
	return DptValue{date.toString()};
}

Result<DptValue> parseDpt11(DptId id, std::string_view text)
{
	auto date = parseDate(text);
	if (!date)
	{
		return invalid(id, "invalid date '" + std::string(text) + "' (expected YYYY-MM-DD, years 1990..2089)");
	}
	return DptValue{date->toString()};
}

// ----- DPT 16: 14-byte string --------------------------------------------------------------

constexpr std::size_t kStringBytes = 14;

/// UTF-8 -> single-byte charset. 16.001 is ISO 8859-1 (code points <= U+00FF, which in UTF-8 are
/// ASCII or a 0xC2/0xC3 lead byte plus one continuation byte); everything else is ASCII-only.
Result<Bytes> encodeDpt16(DptId id, const DptValue& value)
{
	auto text = toText(value);
	if (!text)
	{
		return invalid(id, text.error().message);
	}
	const bool latin1 = id.sub == 1;
	Bytes out;
	for (std::size_t i = 0; i < text->size(); ++i)
	{
		const auto byte = static_cast<std::uint8_t>((*text)[i]);
		if (byte == 0)
		{
			return invalid(id, "text must not contain NUL");
		}
		if (byte < 0x80)
		{
			out.push_back(byte);
			continue;
		}
		const bool latin1Lead = byte == 0xC2 || byte == 0xC3;
		if (!latin1 || !latin1Lead || i + 1 >= text->size())
		{
			return invalid(id, latin1 ? "character not in ISO 8859-1" : "character not in ASCII");
		}
		const auto next = static_cast<std::uint8_t>((*text)[++i]);
		if ((next & 0xC0) != 0x80)
		{
			return invalid(id, "malformed UTF-8");
		}
		out.push_back(static_cast<std::uint8_t>(((byte & 0x03u) << 6) | (next & 0x3Fu)));
	}
	if (out.size() > kStringBytes)
	{
		return invalid(id, "text longer than 14 characters");
	}
	out.resize(kStringBytes, 0); // NUL padding
	return out;
}

/// Bytes are read as ISO 8859-1 for both subtypes (a superset of ASCII, so nothing is lost).
Result<DptValue> decodeDpt16(DptId id, ByteView bytes)
{
	KESTREL_CHECK(checkSize(id, bytes, kStringBytes));
	std::string text;
	for (std::uint8_t byte : bytes)
	{
		if (byte == 0)
		{
			break;
		}
		if (byte < 0x80)
		{
			text.push_back(static_cast<char>(byte));
		}
		else
		{
			text.push_back(static_cast<char>(0xC0 | (byte >> 6)));
			text.push_back(static_cast<char>(0x80 | (byte & 0x3F)));
		}
	}
	return DptValue{std::move(text)};
}

// ----- DPT 232: RGB ------------------------------------------------------------------------

constexpr std::uint64_t kRgbMax = 0xFFFFFF;

Result<Bytes> encodeDpt232(DptId id, const DptValue& value)
{
	auto rgb = toUnsigned(value, kRgbMax);
	if (!rgb)
	{
		return invalid(id, rgb.error().message);
	}
	return writeBigEndian(*rgb, 3);
}

Result<DptValue> decodeDpt232(DptId id, ByteView bytes)
{
	KESTREL_CHECK(checkSize(id, bytes, 3));
	return DptValue{readBigEndian(bytes)};
}

std::optional<std::string> formatDpt232(DptId, const DptValue& value)
{
	auto rgb = toUnsigned(value, kRgbMax);
	if (!rgb)
	{
		return std::nullopt;
	}
	static constexpr char kDigits[] = "0123456789ABCDEF";
	std::string text = "#";
	for (int shift = 20; shift >= 0; shift -= 4)
	{
		text.push_back(kDigits[(*rgb >> shift) & 0x0F]);
	}
	return text;
}

/// "#FF8000", "0xFF8000" or a decimal number.
Result<DptValue> parseDpt232(DptId id, std::string_view text)
{
	text = trim(text);
	std::string_view hex;
	if (text.starts_with('#'))
	{
		hex = text.substr(1);
	}
	else if (startsWithNoCase(text, "0x"))
	{
		hex = text.substr(2);
	}
	else if (auto rgb = parseUnsigned(text))
	{
		return DptValue{*rgb};
	}
	std::uint64_t rgb = 0;
	const auto [ptr, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), rgb, 16);
	if (hex.size() != 6 || ec != std::errc{} || ptr != hex.data() + hex.size())
	{
		return invalid(id, "cannot parse '" + std::string(text) + "' (expected #RRGGBB)");
	}
	return DptValue{rgb};
}

constexpr std::array kCodecs = {
	Codec{10, 24, Codec::Kind::Text, encodeDpt10, decodeDpt10, nullptr, parseDpt10},
	Codec{11, 24, Codec::Kind::Text, encodeDpt11, decodeDpt11, nullptr, parseDpt11},
	Codec{16, 112, Codec::Kind::Text, encodeDpt16, decodeDpt16, nullptr, nullptr},
	Codec{232, 24, Codec::Kind::Unsigned, encodeDpt232, decodeDpt232, formatDpt232, parseDpt232},
};

} // namespace

const Codec* findTextCodec(std::uint16_t main)
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
