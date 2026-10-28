#include "Detail.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <system_error>

namespace kestrel::dpt::detail
{

namespace
{

// 2^63 and 2^64 are exactly representable as double; values below them convert safely.
constexpr double kTwoPow63 = 9223372036854775808.0;
constexpr double kTwoPow64 = 18446744073709551616.0;

char toLowerAscii(char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/// Trims and drops a leading '+' (std::from_chars rejects it). Returns false on empty input.
bool prepareNumber(std::string_view& text)
{
	text = trim(text);
	if (!text.empty() && text.front() == '+')
	{
		text.remove_prefix(1);
		// "+-5" must stay invalid
		if (!text.empty() && text.front() == '-')
		{
			return false;
		}
	}
	return !text.empty();
}

} // namespace

// ----- value conversions -------------------------------------------------------------------

Result<bool> toBool(const DptValue& value)
{
	if (const bool* b = std::get_if<bool>(&value))
	{
		return *b;
	}
	if (const std::int64_t* i = std::get_if<std::int64_t>(&value); i && (*i == 0 || *i == 1))
	{
		return *i == 1;
	}
	if (const std::uint64_t* u = std::get_if<std::uint64_t>(&value); u && *u <= 1)
	{
		return *u == 1;
	}
	if (const double* d = std::get_if<double>(&value); d && (*d == 0.0 || *d == 1.0))
	{
		return *d == 1.0;
	}
	return makeError(ErrorCode::InvalidArgument, "expected a boolean (or 0/1)");
}

Result<std::int64_t> toSigned(const DptValue& value, std::int64_t min, std::int64_t max)
{
	std::int64_t result = 0;
	if (const bool* b = std::get_if<bool>(&value))
	{
		result = *b ? 1 : 0;
	}
	else if (const std::int64_t* i = std::get_if<std::int64_t>(&value))
	{
		result = *i;
	}
	else if (const std::uint64_t* u = std::get_if<std::uint64_t>(&value))
	{
		if (*u > static_cast<std::uint64_t>(max < 0 ? 0 : max))
		{
			return makeError(ErrorCode::InvalidArgument, "value out of range");
		}
		result = static_cast<std::int64_t>(*u);
	}
	else if (const double* d = std::get_if<double>(&value))
	{
		// Only exact integers convert: 3.0 is fine, 3.5 is not.
		if (!std::isfinite(*d) || std::trunc(*d) != *d || *d < -kTwoPow63 || *d >= kTwoPow63)
		{
			return makeError(ErrorCode::InvalidArgument, "expected an integer");
		}
		result = static_cast<std::int64_t>(*d);
	}
	else
	{
		return makeError(ErrorCode::InvalidArgument, "expected an integer, got text");
	}
	if (result < min || result > max)
	{
		return makeError(ErrorCode::InvalidArgument,
						 "value " + std::to_string(result) + " out of range " + std::to_string(min) + ".."
							 + std::to_string(max));
	}
	return result;
}

Result<std::uint64_t> toUnsigned(const DptValue& value, std::uint64_t max)
{
	std::uint64_t result = 0;
	if (const bool* b = std::get_if<bool>(&value))
	{
		result = *b ? 1u : 0u;
	}
	else if (const std::int64_t* i = std::get_if<std::int64_t>(&value))
	{
		if (*i < 0)
		{
			return makeError(ErrorCode::InvalidArgument, "value " + std::to_string(*i) + " must not be negative");
		}
		result = static_cast<std::uint64_t>(*i);
	}
	else if (const std::uint64_t* u = std::get_if<std::uint64_t>(&value))
	{
		result = *u;
	}
	else if (const double* d = std::get_if<double>(&value))
	{
		if (!std::isfinite(*d) || std::trunc(*d) != *d || *d < 0.0 || *d >= kTwoPow64)
		{
			return makeError(ErrorCode::InvalidArgument, "expected a non-negative integer");
		}
		result = static_cast<std::uint64_t>(*d);
	}
	else
	{
		return makeError(ErrorCode::InvalidArgument, "expected an integer, got text");
	}
	if (result > max)
	{
		return makeError(ErrorCode::InvalidArgument,
						 "value " + std::to_string(result) + " out of range 0.." + std::to_string(max));
	}
	return result;
}

Result<double> toReal(const DptValue& value)
{
	if (const bool* b = std::get_if<bool>(&value))
	{
		return *b ? 1.0 : 0.0;
	}
	if (const std::int64_t* i = std::get_if<std::int64_t>(&value))
	{
		return static_cast<double>(*i);
	}
	if (const std::uint64_t* u = std::get_if<std::uint64_t>(&value))
	{
		return static_cast<double>(*u);
	}
	if (const double* d = std::get_if<double>(&value))
	{
		if (!std::isfinite(*d))
		{
			return makeError(ErrorCode::InvalidArgument, "value is not finite");
		}
		return *d;
	}
	return makeError(ErrorCode::InvalidArgument, "expected a number, got text");
}

Result<std::string> toText(const DptValue& value)
{
	if (const std::string* s = std::get_if<std::string>(&value))
	{
		return *s;
	}
	return makeError(ErrorCode::InvalidArgument, "expected text");
}

// ----- byte helpers ------------------------------------------------------------------------

Status checkSize(DptId id, ByteView bytes, std::size_t expected)
{
	if (bytes.size() != expected)
	{
		return makeError(ErrorCode::ParseError, "DPT " + id.toString() + ": expected " + std::to_string(expected)
													 + " byte(s), got " + std::to_string(bytes.size()));
	}
	return ok();
}

std::uint64_t readBigEndian(ByteView bytes)
{
	std::uint64_t result = 0;
	for (std::uint8_t byte : bytes)
	{
		result = (result << 8) | byte;
	}
	return result;
}

Bytes writeBigEndian(std::uint64_t value, std::size_t count)
{
	Bytes out(count);
	for (std::size_t i = 0; i < count; ++i)
	{
		out[count - 1 - i] = static_cast<std::uint8_t>(value & 0xFF);
		value >>= 8;
	}
	return out;
}

// ----- text helpers ------------------------------------------------------------------------

std::string_view trim(std::string_view text)
{
	constexpr std::string_view kSpace = " \t\r\n\f\v";
	const std::size_t first = text.find_first_not_of(kSpace);
	if (first == std::string_view::npos)
	{
		return {};
	}
	const std::size_t last = text.find_last_not_of(kSpace);
	return text.substr(first, last - first + 1);
}

bool equalsNoCase(std::string_view a, std::string_view b)
{
	if (a.size() != b.size())
	{
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		if (toLowerAscii(a[i]) != toLowerAscii(b[i]))
		{
			return false;
		}
	}
	return true;
}

bool startsWithNoCase(std::string_view text, std::string_view prefix)
{
	return text.size() >= prefix.size() && equalsNoCase(text.substr(0, prefix.size()), prefix);
}

std::string_view stripUnit(std::string_view text, std::string_view unit)
{
	text = trim(text);
	if (!unit.empty() && text.size() >= unit.size() && text.substr(text.size() - unit.size()) == unit)
	{
		return trim(text.substr(0, text.size() - unit.size()));
	}
	return text;
}

std::optional<double> parseReal(std::string_view text)
{
	if (!prepareNumber(text))
	{
		return std::nullopt;
	}
	// Accept a decimal comma ("21,5") as typed in many European locales.
	std::string buffer(text);
	if (buffer.find('.') == std::string::npos)
	{
		if (const std::size_t comma = buffer.find(','); comma != std::string::npos && buffer.rfind(',') == comma)
		{
			buffer[comma] = '.';
		}
	}
	double value = 0.0;
	const char* end = buffer.data() + buffer.size();
	const auto [ptr, ec] = std::from_chars(buffer.data(), end, value);
	if (ec != std::errc{} || ptr != end || !std::isfinite(value))
	{
		return std::nullopt;
	}
	return value;
}

std::optional<std::int64_t> parseSigned(std::string_view text)
{
	if (!prepareNumber(text))
	{
		return std::nullopt;
	}
	std::int64_t value = 0;
	const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (ec != std::errc{} || ptr != text.data() + text.size())
	{
		return std::nullopt;
	}
	return value;
}

std::optional<std::uint64_t> parseUnsigned(std::string_view text)
{
	if (!prepareNumber(text))
	{
		return std::nullopt;
	}
	std::uint64_t value = 0;
	const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (ec != std::errc{} || ptr != text.data() + text.size())
	{
		return std::nullopt;
	}
	return value;
}

std::string formatFixed(double value, int decimals)
{
	if (!std::isfinite(value))
	{
		return std::isnan(value) ? "nan" : (value < 0.0 ? "-inf" : "inf");
	}
	// Fixed notation of DBL_MAX needs ~310 digits.
	std::array<char, 400> buffer{};
	const auto [ptr, ec] =
		std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::fixed, decimals);
	if (ec != std::errc{})
	{
		return "?";
	}
	std::string text(buffer.data(), ptr);
	if (text.find('.') != std::string::npos)
	{
		while (text.back() == '0')
		{
			text.pop_back();
		}
		if (text.back() == '.')
		{
			text.pop_back();
		}
	}
	if (text == "-0")
	{
		text = "0";
	}
	return text;
}

std::string formatShortestFloat(double value)
{
	std::array<char, 64> buffer{};
	// Narrowing an out-of-range double to float is undefined behaviour: print those as double.
	const bool fitsFloat = std::isfinite(value) && std::fabs(value) <= static_cast<double>(std::numeric_limits<float>::max());
	const auto [ptr, ec] = fitsFloat
							   ? std::to_chars(buffer.data(), buffer.data() + buffer.size(), static_cast<float>(value))
							   : std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
	if (ec != std::errc{})
	{
		return "?";
	}
	return std::string(buffer.data(), ptr);
}

std::string withUnit(std::string text, std::string_view unit)
{
	if (!unit.empty())
	{
		text += ' ';
		text += unit;
	}
	return text;
}

Unexpected invalid(DptId id, std::string_view what)
{
	return makeError(ErrorCode::InvalidArgument, "DPT " + id.toString() + ": " + std::string(what));
}

} // namespace kestrel::dpt::detail
