#pragma once

// KNX Datapoint Type codecs (KNX Standard 3/7/2 "Datapoint Types").
// Hand-written, std-only. Master data (names, enum texts) arrives in M2 from knx_master.xml;
// this library only knows how to move values between bytes, C++ values and text.
//
// ---------------------------------------------------------------------------------------------
// Byte convention
//  - Short DPTs (sizeInBit <= 6: DPT 1, 2, 3) travel in the low 6 bits of the APCI byte.
//    encode() returns ONE byte holding the value in its low bits (DPT 1 true -> {0x01},
//    DPT 3 c=1 step=3 -> {0x0b}); decode() takes exactly one byte and masks the unused bits,
//    so callers may pass the APCI low byte as-is.
//  - Longer DPTs: the bytes are the payload that follows the APCI byte, big-endian
//    (DPT 5 -> 1 byte, DPT 9 -> 2 bytes, DPT 16 -> 14 bytes ...).
//
// Value convention (canonical DptValue alternative returned by decode() / parse())
//   1        bool
//   2        uint64  raw 0..3          bit1 = c (control), bit0 = v (value)
//   3        uint64  raw 0..15         bit3 = c (3.007: 1 = increase, 3.008: 1 = down),
//                                      bits2..0 = step code (0 = break/stop)
//   5        uint64  raw 0..255
//   5.001    double  0..100 %          raw = round(v * 255 / 100)
//   5.003    double  0..360 deg        raw = round(v * 255 / 360)
//   6        int64   -128..127
//   7        uint64  0..65535          (scaled subtypes 7.003/7.004 stay raw, no unit)
//   8        int64   -32768..32767     (scaled subtypes 8.003/8.004/8.010 stay raw, no unit)
//   9        double  KNX 2-byte float  0.01 * M * 2^E, range -671088.64 .. 670433.28
//                                      (0x7FFF is the "invalid data" marker: decode -> ProtocolError,
//                                      and encode never produces it)
//   10       string  "HH:MM:SS" or "Ddd HH:MM:SS" (Ddd = Mon..Sun when the weekday field is set)
//   11       string  "YYYY-MM-DD"      wire year 0..89 -> 2000+, 90..99 -> 1900+
//   12       uint64  0..4294967295
//   13       int64   -2147483648..2147483647 (scaled subtype 13.002, 0.0001 m³/h, stays raw, no unit)
//   14       double  IEEE 754 float32 (big-endian on the wire)
//   16       string  UTF-8 in C++; 14 bytes on the wire, NUL padded. 16.000 = ASCII,
//                    16.001 = ISO 8859-1 (converted to/from UTF-8). decode stops at the first NUL.
//   17       uint64  scene number 0..63
//   18       uint64  raw byte: bit7 = learn (1) / activate (0), bits5..0 = scene number
//   20       uint64  raw 0..255 (enumeration)
//   232      uint64  0xRRGGBB
//
// Leniency: encode() accepts any numeric alternative (bool, int64, uint64, double) when the
// conversion is exact and in range (e.g. int64 21 for DPT 9, double 3.0 for DPT 7, uint64 1 for DPT 1).
//
// Errors
//   ParseError       wrong byte count, or bytes with out-of-range fields (e.g. hour 25 in DPT 10)
//   InvalidArgument  value out of range / wrong alternative / malformed text
//   ProtocolError    DPT 9 "invalid data" marker 0x7FFF
//   Unsupported      unknown main type
// ---------------------------------------------------------------------------------------------

#include "kestrel/base/Bytes.hpp"
#include "kestrel/base/Result.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>

namespace kestrel::dpt
{

/// Datapoint type identifier. sub == 0 means "main type only, subtype unspecified"
/// (which coincides with the real subtypes 14.000 and 16.000 -- they are treated alike).
struct DptId
{
	std::uint16_t main = 0;
	std::uint16_t sub = 0;

	/// Accepts "9.001", "9.1", "9", "DPT-9", "DPT9.001", "DPST-9-1" (prefixes case-insensitive).
	/// main must be >= 1. Malformed text -> InvalidArgument.
	static Result<DptId> parse(std::string_view text);

	/// "9.001" -- always main.sub with sub zero-padded to 3 digits ("9.000" when sub == 0,
	/// "13.1200" for 4-digit subs). Round-trips through parse().
	std::string toString() const;
	/// ETS style: "DPST-9-1", or "DPT-9" when sub == 0.
	std::string toEtsString() const;

	friend bool operator==(const DptId&, const DptId&) = default;
	friend auto operator<=>(const DptId&, const DptId&) = default;
};

std::ostream& operator<<(std::ostream& os, const DptId& id);

using DptValue = std::variant<bool, std::int64_t, std::uint64_t, double, std::string>;

/// Debug printer with a type tag: "bool:true", "i64:-5", "u64:5", "f64:21.5", "str:\"abc\"".
/// (DptValue is a std:: type, so ADL does not find this: bring it in with
/// `using namespace kestrel::dpt;` or `using kestrel::dpt::operator<<;`.)
std::ostream& operator<<(std::ostream& os, const DptValue& value);

/// Encodes a value; see the conventions above.
Result<Bytes> encode(DptId id, const DptValue& value);

/// Decodes bytes; see the conventions above.
Result<DptValue> decode(DptId id, ByteView bytes);

/// Human-readable text with unit: "21.5 °C", "50 %", "On"/"Off" (1.001), "2024-03-15", "#FF8000".
/// Never fails: values that do not fit the DPT are printed as-is.
std::string format(DptId id, const DptValue& value);

/// Inverse of format(): text -> canonical value. The unit suffix is optional ("21.5", "21.5 °C",
/// "21.5°C"). DPT 1 also accepts 1/0/on/off/true/false and the subtype's labels (case-insensitive).
/// The result is range-checked exactly like encode(). Errors -> InvalidArgument / Unsupported.
Result<DptValue> parse(DptId id, std::string_view text);

/// Unit symbol of the subtype ("°C" for 9.001), empty when none or unknown.
std::string_view unitOf(DptId id);

/// Size of the value in bits (1 for DPT 1, 16 for DPT 9 ...); 0 if the main type is unknown.
std::size_t sizeInBit(DptId id);

/// True when the value travels in the low 6 bits of the APCI byte (0 < sizeInBit <= 6).
bool isShortForm(DptId id);

} // namespace kestrel::dpt
