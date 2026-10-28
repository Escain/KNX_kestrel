// format() / parse(): human-readable text with units, as shown and typed in the group monitor.

#include "DptTestHelpers.hpp"
#include "kestrel/dpt/Dpt.hpp"

#include <Printers.hpp>
#include <UnitTests.hpp>

using namespace kestrel;
using namespace kestrel::dpt;
using namespace dpttest;

UNIT_TEST(FR_BUS_5__formatBooleanLabels)
{
	ASSERT_EQ(fmt("1.001", DptValue{true}), std::string("On"));
	ASSERT_EQ(fmt("1.001", DptValue{false}), std::string("Off"));
	ASSERT_EQ(fmt("1", DptValue{true}), std::string("On"));
	ASSERT_EQ(fmt("1.002", DptValue{true}), std::string("True"));
	ASSERT_EQ(fmt("1.008", DptValue{true}), std::string("Down"));
	ASSERT_EQ(fmt("1.009", DptValue{false}), std::string("Open"));
	ASSERT_EQ(fmt("1.099", DptValue{true}), std::string("1"));
	ASSERT_EQ(fmt("1.001", u64(1)), std::string("On"));
	ASSERT_EQ(fmt("1.001", i64(7)), std::string("7")); // does not fit: printed as-is
}

UNIT_TEST(FR_BUS_5__formatNumbersWithUnits)
{
	ASSERT_EQ(fmt("9.001", DptValue{21.5}), std::string("21.5 °C"));
	ASSERT_EQ(fmt("9.001", DptValue{-30.0}), std::string("-30 °C"));
	ASSERT_EQ(fmt("9.001", DptValue{1000.32}), std::string("1000.32 °C"));
	ASSERT_EQ(fmt("9.001", DptValue{-0.0}), std::string("0 °C"));
	ASSERT_EQ(fmt("9.001", i64(21)), std::string("21 °C"));
	ASSERT_EQ(fmt("9.004", DptValue{670433.28}), std::string("670433.28 lx"));
	ASSERT_EQ(fmt("9", DptValue{1.5}), std::string("1.5"));
	ASSERT_EQ(fmt("5.001", DptValue{50.0}), std::string("50 %"));
	ASSERT_EQ(fmt("5.001", DptValue{128.0 * 100.0 / 255.0}), std::string("50.2 %"));
	ASSERT_EQ(fmt("5.003", DptValue{180.0}), std::string("180 °"));
	ASSERT_EQ(fmt("5.010", u64(200)), std::string("200"));
	ASSERT_EQ(fmt("5.004", u64(200)), std::string("200 %"));
	ASSERT_EQ(fmt("6.001", i64(-10)), std::string("-10 %"));
	ASSERT_EQ(fmt("7.001", u64(5)), std::string("5"));
	ASSERT_EQ(fmt("7.002", u64(250)), std::string("250 ms"));
	ASSERT_EQ(fmt("8.001", i64(-2)), std::string("-2"));
	ASSERT_EQ(fmt("12.001", u64(4294967295)), std::string("4294967295"));
	ASSERT_EQ(fmt("13.013", i64(-5)), std::string("-5 kWh"));
	ASSERT_EQ(fmt("14.056", DptValue{230.0}), std::string("230 W"));
	ASSERT_EQ(fmt("14.056", DptValue{static_cast<double>(0.1f)}), std::string("0.1 W"));
	ASSERT_EQ(fmt("17.001", u64(5)), std::string("5"));
	ASSERT_EQ(fmt("20.102", u64(2)), std::string("2"));
	// values that do not fit the DPT are printed as-is
	ASSERT_EQ(fmt("9.001", str("abc")), std::string("abc"));
	ASSERT_EQ(fmt("7.001", i64(-1)), std::string("-1"));
}

UNIT_TEST(FR_BUS_5__formatStructuredTypes)
{
	ASSERT_EQ(fmt("2.001", u64(3)), std::string("Control, On"));
	ASSERT_EQ(fmt("2.001", u64(0)), std::string("No control, Off"));
	ASSERT_EQ(fmt("2.008", u64(1)), std::string("No control, Down"));
	ASSERT_EQ(fmt("3.007", u64(0x0B)), std::string("Increase 3"));
	ASSERT_EQ(fmt("3.007", u64(0x03)), std::string("Decrease 3"));
	ASSERT_EQ(fmt("3.008", u64(0x09)), std::string("Down 1"));
	ASSERT_EQ(fmt("3.008", u64(0x01)), std::string("Up 1"));
	ASSERT_EQ(fmt("3.007", u64(0x08)), std::string("Stop"));
	ASSERT_EQ(fmt("18.001", u64(0x85)), std::string("Learn 5"));
	ASSERT_EQ(fmt("18.001", u64(0x05)), std::string("Activate 5"));
	ASSERT_EQ(fmt("232.600", u64(0xFF8000)), std::string("#FF8000"));
	ASSERT_EQ(fmt("232.600", u64(0x00000A)), std::string("#00000A"));
	ASSERT_EQ(fmt("10.001", str("Mon 14:30:00")), std::string("Mon 14:30:00"));
	ASSERT_EQ(fmt("11.001", str("2024-03-15")), std::string("2024-03-15"));
	ASSERT_EQ(fmt("16.000", str("hello")), std::string("hello"));
}

UNIT_TEST(FR_BUS_6__parseBoolean)
{
	for (std::string_view text : {"1", "on", "On", "ON", "true", "TRUE", " on "})
	{
		ASSERT_EQ(prs("1.001", text), DptValue{true});
	}
	for (std::string_view text : {"0", "off", "Off", "false", "False"})
	{
		ASSERT_EQ(prs("1.001", text), DptValue{false});
	}
	ASSERT_EQ(prs("1.008", "down"), DptValue{true});
	ASSERT_EQ(prs("1.008", "Up"), DptValue{false});
	ASSERT_EQ(prs("1.009", "Close"), DptValue{true});
	ASSERT_EQ(prs("1.019", "open"), DptValue{true});
	ASSERT_EQ(prs("1.017", "Trigger"), DptValue{true});
	ASSERT_EQ(prsErr("1.001", "maybe"), std::string("InvalidArgument"));
	ASSERT_EQ(prsErr("1.001", ""), std::string("InvalidArgument"));
	ASSERT_EQ(prsErr("1.001", "2"), std::string("InvalidArgument"));
}

UNIT_TEST(FR_BUS_6__parseRealWithOptionalUnit)
{
	ASSERT_EQ(prs("9.001", "21.5"), DptValue{21.5});
	ASSERT_EQ(prs("9.001", "21.5 °C"), DptValue{21.5});
	ASSERT_EQ(prs("9.001", "21.5°C"), DptValue{21.5});
	ASSERT_EQ(prs("9.001", " 21,5 "), DptValue{21.5});
	ASSERT_EQ(prs("9.001", "+21.5"), DptValue{21.5});
	ASSERT_EQ(prs("9.001", "-30"), DptValue{-30.0});
	ASSERT_EQ(prs("9.001", "1e2"), DptValue{100.0});
	ASSERT_EQ(prsErr("9.001", "21.5 K"), std::string("InvalidArgument"));
	ASSERT_EQ(prsErr("9.001", "abc"), std::string("InvalidArgument"));
	ASSERT_EQ(prsErr("9.001", ""), std::string("InvalidArgument"));
	ASSERT_EQ(prsErr("9.001", "nan"), std::string("InvalidArgument"));
	ASSERT_EQ(prsErr("9.001", "inf"), std::string("InvalidArgument"));
	ASSERT_EQ(prsErr("9.001", "+-1"), std::string("InvalidArgument"));
	ASSERT_EQ(prsErr("9.001", "1,000.5"), std::string("InvalidArgument"));
	ASSERT_EQ(prsErr("9.001", "1e9"), std::string("InvalidArgument")); // out of DPT 9 range
	ASSERT_EQ(prs("14.056", "230 W"), DptValue{230.0});
	ASSERT_EQ(prsErr("14.056", "1e39"), std::string("InvalidArgument"));
	ASSERT_EQ(prs("5.001", "50 %"), DptValue{50.0});
	ASSERT_EQ(prs("5.001", "50%"), DptValue{50.0});
	ASSERT_EQ(prs("5.001", "12.5"), DptValue{12.5});
	ASSERT_EQ(prsErr("5.001", "101 %"), std::string("InvalidArgument"));
	ASSERT_EQ(prs("5.003", "90 °"), DptValue{90.0});
}

UNIT_TEST(FR_BUS_6__parseIntegers)
{
	ASSERT_EQ(prs("5.010", "200"), u64(200));
	ASSERT_EQ(prs("5.004", "200 %"), u64(200));
	ASSERT_EQ(prsErr("5.010", "256"), std::string("InvalidArgument"));
	ASSERT_EQ(prsErr("5.010", "-1"), std::string("InvalidArgument"));
	ASSERT_EQ(prsErr("5.010", "2.5"), std::string("InvalidArgument"));
	ASSERT_EQ(prs("6.010", "-128"), i64(-128));
	ASSERT_EQ(prsErr("6.010", "-129"), std::string("InvalidArgument"));
	ASSERT_EQ(prs("7.002", "250 ms"), u64(250));
	ASSERT_EQ(prs("7.002", "250"), u64(250));
	ASSERT_EQ(prs("8.001", "-32768"), i64(-32768));
	ASSERT_EQ(prs("12.001", "4294967295"), u64(4294967295));
	ASSERT_EQ(prsErr("12.001", "4294967296"), std::string("InvalidArgument"));
	ASSERT_EQ(prsErr("12.001", "99999999999999999999999"), std::string("InvalidArgument"));
	ASSERT_EQ(prs("13.013", "-5 kWh"), i64(-5));
	ASSERT_EQ(prs("17.001", "63"), u64(63));
	ASSERT_EQ(prsErr("17.001", "64"), std::string("InvalidArgument"));
	ASSERT_EQ(prs("20.102", "3"), u64(3));
}

UNIT_TEST(FR_BUS_6__parseStructuredTypes)
{
	ASSERT_EQ(prs("2.001", "Control, On"), u64(3));
	ASSERT_EQ(prs("2.001", "control: off"), u64(2));
	ASSERT_EQ(prs("2.001", "No control, Off"), u64(0));
	ASSERT_EQ(prs("2.001", "2"), u64(2));
	ASSERT_EQ(prsErr("2.001", "4"), std::string("InvalidArgument"));
	ASSERT_EQ(prsErr("2.001", "Control, maybe"), std::string("InvalidArgument"));
	ASSERT_EQ(prs("3.007", "Increase 3"), u64(0x0B));
	ASSERT_EQ(prs("3.007", "decrease 7"), u64(0x07));
	ASSERT_EQ(prs("3.007", "Stop"), u64(0));
	ASSERT_EQ(prs("3.007", "11"), u64(11));
	ASSERT_EQ(prs("3.008", "Down 1"), u64(0x09));
	ASSERT_EQ(prs("3.008", "up 2"), u64(0x02));
	ASSERT_EQ(prsErr("3.007", "Increase 8"), std::string("InvalidArgument"));
	ASSERT_EQ(prsErr("3.007", "Sideways 1"), std::string("InvalidArgument"));
	ASSERT_EQ(prs("18.001", "learn 5"), u64(0x85));
	ASSERT_EQ(prs("18.001", "Activate 63"), u64(0x3F));
	ASSERT_EQ(prs("18.001", "133"), u64(0x85));
	ASSERT_EQ(prsErr("18.001", "Activate 64"), std::string("InvalidArgument"));
	ASSERT_EQ(prs("232.600", "#ff8000"), u64(0xFF8000));
	ASSERT_EQ(prs("232.600", "0x0000FF"), u64(0xFF));
	ASSERT_EQ(prs("232.600", "255"), u64(255));
	ASSERT_EQ(prsErr("232.600", "#12345"), std::string("InvalidArgument"));
	ASSERT_EQ(prsErr("232.600", "#GGGGGG"), std::string("InvalidArgument"));
	ASSERT_EQ(prsErr("232.600", "16777216"), std::string("InvalidArgument"));
}

UNIT_TEST(FR_BUS_6__parseTimeDateAndText)
{
	ASSERT_EQ(prs("10.001", "8:05"), str("08:05:00"));
	ASSERT_EQ(prs("10.001", "mon 8:05"), str("Mon 08:05:00"));
	ASSERT_EQ(prs("10.001", "3 10:00:00"), str("Wed 10:00:00"));
	ASSERT_EQ(prs("10.001", "0 10:00:00"), str("10:00:00"));
	ASSERT_EQ(prsErr("10.001", "10"), std::string("InvalidArgument"));
	ASSERT_EQ(prsErr("10.001", "8 10:00"), std::string("InvalidArgument"));
	ASSERT_EQ(prs("11.001", "2024-3-5"), str("2024-03-05"));
	ASSERT_EQ(prsErr("11.001", "2024/03/05"), std::string("InvalidArgument"));
	ASSERT_EQ(prsErr("11.001", "24-03-05"), std::string("InvalidArgument"));
	ASSERT_EQ(prs("16.000", "  hi "), str("  hi "));
	ASSERT_EQ(prsErr("16.000", "way too long string"), std::string("InvalidArgument"));
}

UNIT_TEST(FR_BUS_6__formatThenParseRoundTrip)
{
	struct Case
	{
		std::string_view id;
		DptValue value;
	};
	const Case cases[] = {
		{"1.001", DptValue{true}},   {"1.009", DptValue{false}}, {"2.001", u64(1)},
		{"3.007", u64(0x0B)},        {"3.008", u64(0x01)},       {"5.001", DptValue{50.0}},
		{"5.010", u64(7)},           {"6.001", i64(-10)},        {"7.002", u64(250)},
		{"9.001", DptValue{21.5}},   {"9.007", DptValue{-0.5}},  {"12.100", u64(3600)},
		{"13.013", i64(-123456)},    {"14.056", DptValue{230.0}}, {"10.001", str("Tue 07:00:00")},
		{"11.001", str("1995-06-30")}, {"16.000", str("Scene 1")}, {"17.001", u64(12)},
		{"18.001", u64(0x81)},       {"232.600", u64(0xA0B0C0)},
	};
	for (const Case& c : cases)
	{
		ASSERT_EQ(prs(c.id, fmt(c.id, c.value)), c.value);
	}
}
