// Byte-exact encode/decode vectors. Expected bytes are derived by hand from the bit layouts in
// KNX 3/7/2 (comments show the arithmetic), not taken from another implementation.

#include "DptTestHelpers.hpp"
#include "kestrel/dpt/Dpt.hpp"

#include <Printers.hpp>
#include <UnitTests.hpp>

#include <cstdint>
#include <limits>

using namespace kestrel;
using namespace kestrel::dpt;
using namespace dpttest;

// ----- short form: 1, 2, 3 ---------------------------------------------------------------

UNIT_TEST(FR_BUS_6__dpt1EncodeIsOneLowBit)
{
	ASSERT_EQ(enc("1.001", DptValue{true}), std::string("01"));
	ASSERT_EQ(enc("1.001", DptValue{false}), std::string("00"));
	// lenient numeric alternatives
	ASSERT_EQ(enc("1.001", u64(1)), std::string("01"));
	ASSERT_EQ(enc("1.001", i64(0)), std::string("00"));
	ASSERT_EQ(enc("1.001", DptValue{1.0}), std::string("01"));
	ASSERT_EQ(encErr("1.001", i64(2)), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("1.001", DptValue{0.5}), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("1.001", str("on")), std::string("InvalidArgument"));
}

UNIT_TEST(FR_BUS_5__dpt1DecodeMasksApciBits)
{
	ASSERT_EQ(dec("1.001", "01"), DptValue{true});
	ASSERT_EQ(dec("1.001", "00"), DptValue{false});
	// Callers may pass the raw APCI low byte (0x80 | value for a GroupValueWrite)
	ASSERT_EQ(dec("1.001", "81"), DptValue{true});
	ASSERT_EQ(dec("1.001", "fe"), DptValue{false});
	ASSERT_EQ(decErr("1.001", ""), std::string("ParseError"));
	ASSERT_EQ(decErr("1.001", "00 01"), std::string("ParseError"));
}

UNIT_TEST(FR_BUS_6__dpt2ControlValue)
{
	// bit1 = c, bit0 = v
	ASSERT_EQ(enc("2.001", u64(3)), std::string("03"));
	ASSERT_EQ(enc("2.001", u64(2)), std::string("02"));
	ASSERT_EQ(encErr("2.001", u64(4)), std::string("InvalidArgument"));
	ASSERT_EQ(dec("2.001", "07"), u64(3));
	ASSERT_EQ(dec("2.001", "01"), u64(1));
	ASSERT_EQ(decErr("2.001", "01 02"), std::string("ParseError"));
}

UNIT_TEST(FR_BUS_6__dpt3StepCode)
{
	// c=1 (increase), step code 3 -> 1011b
	ASSERT_EQ(enc("3.007", u64(0x0B)), std::string("0b"));
	ASSERT_EQ(enc("3.008", u64(0x00)), std::string("00"));
	ASSERT_EQ(enc("3.007", u64(15)), std::string("0f"));
	ASSERT_EQ(encErr("3.007", u64(16)), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("3.007", i64(-1)), std::string("InvalidArgument"));
	ASSERT_EQ(dec("3.007", "fb"), u64(0x0B));
	ASSERT_EQ(dec("3.007", "09"), u64(9));
	ASSERT_EQ(decErr("3.007", ""), std::string("ParseError"));
}

// ----- 1-byte types ----------------------------------------------------------------------

UNIT_TEST(FR_BUS_6__dpt5ScalingPercent)
{
	// raw = round(v * 255 / 100): 50 % -> 127.5 -> 128
	ASSERT_EQ(enc("5.001", DptValue{50.0}), std::string("80"));
	ASSERT_EQ(enc("5.001", DptValue{100.0}), std::string("ff"));
	ASSERT_EQ(enc("5.001", DptValue{0.0}), std::string("00"));
	ASSERT_EQ(enc("5.001", i64(100)), std::string("ff"));
	// 1 % -> 2.55 -> 3
	ASSERT_EQ(enc("5.001", DptValue{1.0}), std::string("03"));
	ASSERT_EQ(encErr("5.001", DptValue{100.1}), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("5.001", DptValue{-0.1}), std::string("InvalidArgument"));
	ASSERT_EQ(dec("5.001", "ff"), DptValue{100.0});
	ASSERT_EQ(dec("5.001", "00"), DptValue{0.0});
	ASSERT_EQ(dec("5.001", "80"), DptValue{128.0 * 100.0 / 255.0});
	ASSERT_EQ(decErr("5.001", "80 00"), std::string("ParseError"));
}

UNIT_TEST(FR_BUS_6__dpt5AngleAndRaw)
{
	// 180 ° -> 127.5 -> 128
	ASSERT_EQ(enc("5.003", DptValue{180.0}), std::string("80"));
	ASSERT_EQ(enc("5.003", DptValue{360.0}), std::string("ff"));
	ASSERT_EQ(encErr("5.003", DptValue{361.0}), std::string("InvalidArgument"));
	ASSERT_EQ(dec("5.003", "ff"), DptValue{360.0});
	// other 5.xxx stay raw
	ASSERT_EQ(enc("5.010", u64(200)), std::string("c8"));
	ASSERT_EQ(enc("5.004", u64(255)), std::string("ff"));
	ASSERT_EQ(enc("5", DptValue{7.0}), std::string("07"));
	ASSERT_EQ(encErr("5.010", u64(256)), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("5.010", DptValue{2.5}), std::string("InvalidArgument"));
	ASSERT_EQ(dec("5.010", "c8"), u64(200));
}

UNIT_TEST(FR_BUS_6__dpt6SignedByte)
{
	ASSERT_EQ(enc("6.010", i64(-128)), std::string("80"));
	ASSERT_EQ(enc("6.010", i64(-1)), std::string("ff"));
	ASSERT_EQ(enc("6.010", i64(127)), std::string("7f"));
	ASSERT_EQ(enc("6.001", u64(5)), std::string("05"));
	ASSERT_EQ(encErr("6.010", i64(128)), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("6.010", i64(-129)), std::string("InvalidArgument"));
	ASSERT_EQ(dec("6.010", "80"), i64(-128));
	ASSERT_EQ(dec("6.010", "ff"), i64(-1));
	ASSERT_EQ(dec("6.010", "7f"), i64(127));
}

UNIT_TEST(FR_BUS_6__dpt17SceneNumber)
{
	ASSERT_EQ(enc("17.001", u64(63)), std::string("3f"));
	ASSERT_EQ(enc("17.001", u64(0)), std::string("00"));
	ASSERT_EQ(encErr("17.001", u64(64)), std::string("InvalidArgument"));
	// reserved top bits ignored
	ASSERT_EQ(dec("17.001", "ff"), u64(63));
	ASSERT_EQ(dec("17.001", "05"), u64(5));
}

UNIT_TEST(FR_BUS_6__dpt18SceneControl)
{
	// learn (bit7) scene 5 -> 1000 0101b
	ASSERT_EQ(enc("18.001", u64(0x85)), std::string("85"));
	ASSERT_EQ(enc("18.001", u64(0x05)), std::string("05"));
	ASSERT_EQ(encErr("18.001", u64(0x40)), std::string("InvalidArgument")); // reserved bit 6
	ASSERT_EQ(encErr("18.001", u64(0x100)), std::string("InvalidArgument"));
	ASSERT_EQ(dec("18.001", "c5"), u64(0x85));
	ASSERT_EQ(dec("18.001", "3f"), u64(0x3F));
}

UNIT_TEST(FR_BUS_6__dpt20RawEnum)
{
	ASSERT_EQ(enc("20.102", u64(255)), std::string("ff"));
	ASSERT_EQ(enc("20.102", u64(3)), std::string("03"));
	ASSERT_EQ(encErr("20.102", u64(256)), std::string("InvalidArgument"));
	ASSERT_EQ(dec("20.102", "03"), u64(3));
}

// ----- 2/4-byte integers -----------------------------------------------------------------

UNIT_TEST(FR_BUS_6__dpt7Unsigned16)
{
	ASSERT_EQ(enc("7.001", u64(0x1234)), std::string("12 34"));
	ASSERT_EQ(enc("7.001", u64(65535)), std::string("ff ff"));
	ASSERT_EQ(enc("7.001", DptValue{3.0}), std::string("00 03"));
	ASSERT_EQ(encErr("7.001", u64(65536)), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("7.001", i64(-1)), std::string("InvalidArgument"));
	ASSERT_EQ(dec("7.001", "12 34"), u64(4660));
	ASSERT_EQ(decErr("7.001", "12"), std::string("ParseError"));
	ASSERT_EQ(decErr("7.001", "12 34 56"), std::string("ParseError"));
}

UNIT_TEST(FR_BUS_6__dpt8Signed16)
{
	ASSERT_EQ(enc("8.001", i64(-32768)), std::string("80 00"));
	ASSERT_EQ(enc("8.001", i64(-2)), std::string("ff fe"));
	ASSERT_EQ(enc("8.001", i64(1000)), std::string("03 e8"));
	ASSERT_EQ(enc("8.001", i64(32767)), std::string("7f ff"));
	ASSERT_EQ(encErr("8.001", i64(32768)), std::string("InvalidArgument"));
	ASSERT_EQ(dec("8.001", "ff fe"), i64(-2));
	ASSERT_EQ(dec("8.001", "80 00"), i64(-32768));
	ASSERT_EQ(dec("8.001", "03 e8"), i64(1000));
}

UNIT_TEST(FR_BUS_6__dpt12Unsigned32)
{
	ASSERT_EQ(enc("12.001", u64(0xDEADBEEF)), std::string("de ad be ef"));
	ASSERT_EQ(enc("12.001", u64(0)), std::string("00 00 00 00"));
	ASSERT_EQ(encErr("12.001", u64(4294967296)), std::string("InvalidArgument"));
	ASSERT_EQ(dec("12.001", "de ad be ef"), u64(3735928559));
	ASSERT_EQ(decErr("12.001", "de ad be"), std::string("ParseError"));
}

UNIT_TEST(FR_BUS_6__dpt13Signed32)
{
	ASSERT_EQ(enc("13.013", i64(-1)), std::string("ff ff ff ff"));
	ASSERT_EQ(enc("13.013", i64(-2147483648)), std::string("80 00 00 00"));
	ASSERT_EQ(enc("13.013", i64(2147483647)), std::string("7f ff ff ff"));
	ASSERT_EQ(enc("13.013", i64(100000)), std::string("00 01 86 a0"));
	ASSERT_EQ(encErr("13.013", i64(2147483648)), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("13.013", u64(0xFFFFFFFFFFFFFFFF)), std::string("InvalidArgument"));
	ASSERT_EQ(dec("13.013", "80 00 00 00"), i64(-2147483648));
	ASSERT_EQ(dec("13.013", "00 01 86 a0"), i64(100000));
}

// ----- DPT 9: KNX 2-byte float -----------------------------------------------------------

UNIT_TEST(FR_BUS_6__dpt9EncodeVectors)
{
	ASSERT_EQ(enc("9.001", DptValue{0.0}), std::string("00 00"));
	ASSERT_EQ(enc("9.001", DptValue{0.01}), std::string("00 01"));
	// -1 hundredth: M = -1 (12-bit 0xFFF), E = 0 -> sign | 0x7FF
	ASSERT_EQ(enc("9.001", DptValue{-0.01}), std::string("87 ff"));
	// 2047 fits with E = 0
	ASSERT_EQ(enc("9.001", DptValue{20.47}), std::string("07 ff"));
	// 2150 > 2047 -> E = 1, M = 1075 = 0x433 -> 0x0800 | 0x433
	ASSERT_EQ(enc("9.001", DptValue{21.5}), std::string("0c 33"));
	// 2100 -> E = 1, M = 1050 = 0x41A (int64 input accepted)
	ASSERT_EQ(enc("9.001", i64(21)), std::string("0c 1a"));
	// -3000 -> E = 1, M = -1500 = 0xA24 (12 bit) -> 0x8000 | 0x0800 | 0x224
	ASSERT_EQ(enc("9.001", DptValue{-30.0}), std::string("8a 24"));
	// -27300 -> E = 4, M = -1706.25 -> -1706 = 0x956 -> 0x8000 | 0x2000 | 0x156
	ASSERT_EQ(enc("9.001", DptValue{-273.0}), std::string("a1 56"));
	// rounding: 100010 / 64 = 1562.66 -> 1563 = 0x61B, E = 6 -> 0x3000 | 0x61B
	ASSERT_EQ(enc("9.001", DptValue{1000.1}), std::string("36 1b"));
	// extremes: -2048 * 2^15 and 2046 * 2^15 (2047 * 2^15 would be 0x7FFF = invalid)
	ASSERT_EQ(enc("9.001", DptValue{-671088.64}), std::string("f8 00"));
	ASSERT_EQ(enc("9.001", DptValue{670433.28}), std::string("7f fe"));
}

UNIT_TEST(FR_BUS_6__dpt9EncodeOutOfRange)
{
	ASSERT_EQ(encErr("9.001", DptValue{670760.96}), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("9.001", DptValue{700000.0}), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("9.001", DptValue{-680000.0}), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("9.001", str("21.5")), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("9.001", DptValue{std::numeric_limits<double>::quiet_NaN()}), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("9.001", DptValue{std::numeric_limits<double>::infinity()}), std::string("InvalidArgument"));
}

UNIT_TEST(FR_BUS_5__dpt9DecodeVectors)
{
	ASSERT_EQ(dec("9.001", "00 00"), DptValue{0.0});
	ASSERT_EQ(dec("9.001", "0c 33"), DptValue{21.5});
	ASSERT_EQ(dec("9.001", "8a 24"), DptValue{-30.0});
	ASSERT_EQ(dec("9.001", "87 ff"), DptValue{-0.01});
	ASSERT_EQ(dec("9.001", "a1 56"), DptValue{-272.96});
	ASSERT_EQ(dec("9.001", "36 1b"), DptValue{1000.32});
	ASSERT_EQ(dec("9.001", "f8 00"), DptValue{-671088.64});
	ASSERT_EQ(dec("9.001", "7f fe"), DptValue{670433.28});
	// not normalised (E = 1, M = 1) still decodes: 0.01 * 1 * 2
	ASSERT_EQ(dec("9.001", "08 01"), DptValue{0.02});
	// E = 0, M = -2048
	ASSERT_EQ(dec("9.001", "80 00"), DptValue{-20.48});
}

UNIT_TEST(FR_BUS_5__dpt9InvalidDataAndSize)
{
	ASSERT_EQ(decErr("9.001", "7f ff"), std::string("ProtocolError"));
	ASSERT_EQ(decErr("9.001", "0c"), std::string("ParseError"));
	ASSERT_EQ(decErr("9.001", "0c 33 00"), std::string("ParseError"));
}

UNIT_TEST(FR_BUS_5__dpt9RoundTripAllCodes)
{
	// Every wire code decodes, re-encodes to the minimal exponent and decodes to the same value.
	for (std::uint32_t raw = 0; raw <= 0xFFFF; ++raw)
	{
		if (raw == 0x7FFF)
		{
			continue;
		}
		const Bytes bytes{static_cast<std::uint8_t>(raw >> 8), static_cast<std::uint8_t>(raw & 0xFF)};
		auto value = decode(DptId{9, 1}, bytes);
		ASSERT(value.has_value());
		auto again = encode(DptId{9, 1}, *value);
		ASSERT(again.has_value());
		ASSERT_EQ(*decode(DptId{9, 1}, *again), *value);
		// the text form survives too (DPT 9 values have at most 2 decimals)
		auto parsed = parse(DptId{9, 1}, format(DptId{9, 1}, *value));
		ASSERT(parsed.has_value());
		ASSERT_EQ(*parsed, *value);
	}
}

// ----- DPT 14: IEEE 754 float32 ----------------------------------------------------------

UNIT_TEST(FR_BUS_6__dpt14Float32)
{
	ASSERT_EQ(enc("14.056", DptValue{1.0}), std::string("3f 80 00 00"));
	ASSERT_EQ(enc("14.056", DptValue{-2.5}), std::string("c0 20 00 00"));
	// 230 = 1.796875 * 2^7: exponent 134 = 0x86, fraction 0x660000
	ASSERT_EQ(enc("14.056", DptValue{230.0}), std::string("43 66 00 00"));
	ASSERT_EQ(enc("14.056", DptValue{0.1}), std::string("3d cc cc cd"));
	ASSERT_EQ(enc("14.056", i64(0)), std::string("00 00 00 00"));
	ASSERT_EQ(encErr("14.056", DptValue{1e39}), std::string("InvalidArgument"));
	ASSERT_EQ(dec("14.056", "43 66 00 00"), DptValue{230.0});
	ASSERT_EQ(dec("14.056", "c0 20 00 00"), DptValue{-2.5});
	ASSERT_EQ(dec("14.056", "3d cc cc cd"), DptValue{static_cast<double>(0.1f)});
	ASSERT_EQ(decErr("14.056", "3d cc cc"), std::string("ParseError"));
}

// ----- DPT 10, 11: time and date ---------------------------------------------------------

UNIT_TEST(FR_BUS_6__dpt10TimeOfDay)
{
	// day 1 (Mon) << 5 | 14 = 0x2E, 30 = 0x1E
	ASSERT_EQ(enc("10.001", str("Mon 14:30:00")), std::string("2e 1e 00"));
	ASSERT_EQ(enc("10.001", str("23:59:59")), std::string("17 3b 3b"));
	// day 7 (Sun) << 5 = 0xE0
	ASSERT_EQ(enc("10.001", str("Sun 00:00:01")), std::string("e0 00 01"));
	ASSERT_EQ(enc("10.001", str("8:05")), std::string("08 05 00"));
	ASSERT_EQ(encErr("10.001", str("24:00:00")), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("10.001", str("12:60:00")), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("10.001", str("12:00:00:00")), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("10.001", str("Xyz 12:00:00")), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("10.001", u64(5)), std::string("InvalidArgument"));
	ASSERT_EQ(dec("10.001", "2e 1e 00"), str("Mon 14:30:00"));
	ASSERT_EQ(dec("10.001", "e0 00 01"), str("Sun 00:00:01"));
	ASSERT_EQ(dec("10.001", "08 05 00"), str("08:05:00"));
	// reserved bits of minutes/seconds ignored
	ASSERT_EQ(dec("10.001", "17 fb fb"), str("23:59:59"));
	ASSERT_EQ(decErr("10.001", "18 00 00"), std::string("ParseError")); // hour 24
	ASSERT_EQ(decErr("10.001", "17 3c 00"), std::string("ParseError")); // minute 60
	ASSERT_EQ(decErr("10.001", "17 00"), std::string("ParseError"));
}

UNIT_TEST(FR_BUS_6__dpt11Date)
{
	// 15, 3, 24
	ASSERT_EQ(enc("11.001", str("2024-03-15")), std::string("0f 03 18"));
	ASSERT_EQ(enc("11.001", str("1999-12-31")), std::string("1f 0c 63"));
	ASSERT_EQ(enc("11.001", str("1990-01-01")), std::string("01 01 5a"));
	ASSERT_EQ(enc("11.001", str("2089-12-31")), std::string("1f 0c 59"));
	ASSERT_EQ(enc("11.001", str("2024-02-29")), std::string("1d 02 18"));
	ASSERT_EQ(encErr("11.001", str("2090-01-01")), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("11.001", str("1989-12-31")), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("11.001", str("2023-02-29")), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("11.001", str("2023-04-31")), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("11.001", str("2023/04/01")), std::string("InvalidArgument"));
	// year rule: 0..89 -> 20xx, 90..99 -> 19xx
	ASSERT_EQ(dec("11.001", "01 01 5a"), str("1990-01-01"));
	ASSERT_EQ(dec("11.001", "1f 0c 59"), str("2089-12-31"));
	ASSERT_EQ(dec("11.001", "0f 03 18"), str("2024-03-15"));
	ASSERT_EQ(dec("11.001", "01 01 00"), str("2000-01-01"));
	ASSERT_EQ(decErr("11.001", "00 01 18"), std::string("ParseError")); // day 0
	ASSERT_EQ(decErr("11.001", "01 0d 18"), std::string("ParseError")); // month 13
	ASSERT_EQ(decErr("11.001", "01 01 64"), std::string("ParseError")); // year 100
	ASSERT_EQ(decErr("11.001", "1e 02 18"), std::string("ParseError")); // 2024-02-30
}

// ----- DPT 16: strings -------------------------------------------------------------------

UNIT_TEST(FR_BUS_6__dpt16String)
{
	ASSERT_EQ(enc("16.000", str("KNX is OK")), std::string("4b 4e 58 20 69 73 20 4f 4b 00 00 00 00 00"));
	ASSERT_EQ(enc("16.000", str("ABCDEFGHIJKLMN")), std::string("41 42 43 44 45 46 47 48 49 4a 4b 4c 4d 4e"));
	ASSERT_EQ(enc("16.000", str("")), std::string("00 00 00 00 00 00 00 00 00 00 00 00 00 00"));
	ASSERT_EQ(encErr("16.000", str("ABCDEFGHIJKLMNO")), std::string("InvalidArgument"));
	// "é" is U+00E9: not ASCII, but ISO 8859-1 0xE9
	ASSERT_EQ(encErr("16.000", str("\xc3\xa9")), std::string("InvalidArgument"));
	ASSERT_EQ(enc("16.001", str("\xc3\xa9t\xc3\xa9")), std::string("e9 74 e9 00 00 00 00 00 00 00 00 00 00 00"));
	// 14 Latin-1 characters = 28 UTF-8 bytes still fit
	ASSERT_EQ(encErr("16.001", str("\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"
								   "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9")),
			  std::string("ok"));
	// "€" (U+20AC) is outside ISO 8859-1; a lone lead byte is malformed
	ASSERT_EQ(encErr("16.001", str("\xe2\x82\xac")), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("16.001", str("\xc3")), std::string("InvalidArgument"));
	ASSERT_EQ(encErr("16.000", i64(1)), std::string("InvalidArgument"));
	ASSERT_EQ(dec("16.000", "4b 4e 58 20 69 73 20 4f 4b 00 00 00 00 00"), str("KNX is OK"));
	ASSERT_EQ(dec("16.000", "41 42 43 44 45 46 47 48 49 4a 4b 4c 4d 4e"), str("ABCDEFGHIJKLMN"));
	// stops at the first NUL
	ASSERT_EQ(dec("16.000", "41 00 42 00 00 00 00 00 00 00 00 00 00 00"), str("A"));
	ASSERT_EQ(dec("16.001", "e9 74 e9 00 00 00 00 00 00 00 00 00 00 00"), str("\xc3\xa9t\xc3\xa9"));
	ASSERT_EQ(decErr("16.000", "41 42"), std::string("ParseError"));
}

// ----- DPT 232: RGB ----------------------------------------------------------------------

UNIT_TEST(FR_BUS_6__dpt232Rgb)
{
	ASSERT_EQ(enc("232.600", u64(0xFF8000)), std::string("ff 80 00"));
	ASSERT_EQ(enc("232.600", u64(0x0000FF)), std::string("00 00 ff"));
	ASSERT_EQ(encErr("232.600", u64(0x1000000)), std::string("InvalidArgument"));
	ASSERT_EQ(dec("232.600", "ff 80 00"), u64(0xFF8000));
	ASSERT_EQ(decErr("232.600", "ff 80"), std::string("ParseError"));
}

// ----- generic round trips ---------------------------------------------------------------

UNIT_TEST(FR_BUS_5__roundTripRepresentativeValues)
{
	struct Case
	{
		std::string_view id;
		DptValue value;
	};
	const Case cases[] = {
		{"1.001", DptValue{true}},
		{"1.001", DptValue{false}},
		{"2.001", u64(2)},
		{"3.007", u64(0x0B)},
		{"5.001", DptValue{100.0}},
		{"5.003", DptValue{360.0}},
		{"5.010", u64(42)},
		{"6.010", i64(-42)},
		{"7.001", u64(65535)},
		{"8.001", i64(-32768)},
		{"9.001", DptValue{21.5}},
		{"9.001", DptValue{-30.0}},
		{"10.001", str("Fri 23:59:59")},
		{"11.001", str("2024-03-15")},
		{"12.001", u64(4294967295)},
		{"13.013", i64(-2147483648)},
		{"14.056", DptValue{-2.5}},
		{"16.000", str("Hello")},
		{"16.001", str("Gr\xc3\xbc\xc3\x9f" "e")},
		{"17.001", u64(63)},
		{"18.001", u64(0xBF)},
		{"20.102", u64(4)},
		{"232.600", u64(0x123456)},
	};
	for (const Case& c : cases)
	{
		auto bytes = encode(dptId(c.id), c.value);
		ASSERT(bytes.has_value());
		ASSERT_EQ(bytes->size(), sizeInBit(dptId(c.id)) <= 8 ? std::size_t{1} : sizeInBit(dptId(c.id)) / 8);
		ASSERT_EQ(*decode(dptId(c.id), *bytes), c.value);
	}
}

UNIT_TEST(FR_BUS_5__dpt5ScaledRoundTripAllCodes)
{
	for (unsigned raw = 0; raw <= 0xFF; ++raw)
	{
		const Bytes bytes{static_cast<std::uint8_t>(raw)};
		for (std::string_view id : {"5.001", "5.003"})
		{
			auto value = decode(dptId(id), bytes);
			ASSERT(value.has_value());
			ASSERT_EQ(*encode(dptId(id), *value), bytes);
		}
	}
}
