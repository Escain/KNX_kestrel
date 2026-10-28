#pragma once

// Internal helpers shared by the DPT codec families (not installed / not public).

#include "kestrel/dpt/Dpt.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kestrel::dpt::detail
{

/// One main type's codec. format/parse may be nullptr: the dispatcher then falls back to the
/// generic "number + unit" handling driven by `kind`. format returns nullopt when the value does
/// not fit the DPT; the dispatcher then prints the value as-is.
struct Codec
{
	enum class Kind
	{
		Bool,
		Signed,
		Unsigned,
		Real,
		Text,
	};

	std::uint16_t main = 0;
	std::size_t bits = 0;
	Kind kind = Kind::Unsigned;
	Result<Bytes> (*encode)(DptId, const DptValue&) = nullptr;
	Result<DptValue> (*decode)(DptId, ByteView) = nullptr;
	std::optional<std::string> (*format)(DptId, const DptValue&) = nullptr;
	Result<DptValue> (*parse)(DptId, std::string_view) = nullptr;
};

// Family lookups (nullptr when the family does not own `main`).
const Codec* findBitCodec(std::uint16_t main);     // BitCodecs.cpp:     1, 2, 3, 17, 18
const Codec* findNumericCodec(std::uint16_t main); // NumericCodecs.cpp: 5, 6, 7, 8, 9, 12, 13, 14, 20
const Codec* findTextCodec(std::uint16_t main);    // TextCodecs.cpp:    10, 11, 16, 232

// ----- lenient value conversions (InvalidArgument on mismatch) -----
Result<bool> toBool(const DptValue& value);
Result<std::int64_t> toSigned(const DptValue& value, std::int64_t min, std::int64_t max);
Result<std::uint64_t> toUnsigned(const DptValue& value, std::uint64_t max);
Result<double> toReal(const DptValue& value);
Result<std::string> toText(const DptValue& value);

// ----- byte helpers -----
/// ParseError unless bytes.size() == expected.
Status checkSize(DptId id, ByteView bytes, std::size_t expected);
/// Big-endian unsigned of bytes.size() (<= 8) bytes.
std::uint64_t readBigEndian(ByteView bytes);
/// Appends the low `count` bytes of value, big-endian.
Bytes writeBigEndian(std::uint64_t value, std::size_t count);

// ----- text helpers -----
std::string_view trim(std::string_view text);
bool equalsNoCase(std::string_view a, std::string_view b);
bool startsWithNoCase(std::string_view text, std::string_view prefix);
/// Removes `unit` (and the whitespace before it) from the end of text, when present.
std::string_view stripUnit(std::string_view text, std::string_view unit);
/// Whole-string numeric parsing (after trim; leading '+' allowed). No exceptions, no locale.
std::optional<double> parseReal(std::string_view text);
std::optional<std::int64_t> parseSigned(std::string_view text);
std::optional<std::uint64_t> parseUnsigned(std::string_view text);
/// Fixed notation with at most `decimals` decimals, trailing zeros trimmed, "-0" -> "0".
std::string formatFixed(double value, int decimals);
/// Shortest text that round-trips through float (DPT 14); values beyond float range print as double.
std::string formatShortestFloat(double value);
/// "value unit" or "value" when unit is empty.
std::string withUnit(std::string text, std::string_view unit);

/// InvalidArgument "DPT x.yyy: <what>".
Unexpected invalid(DptId id, std::string_view what);

} // namespace kestrel::dpt::detail
