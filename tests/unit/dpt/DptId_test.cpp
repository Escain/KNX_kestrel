#include "DptTestHelpers.hpp"
#include "kestrel/dpt/Dpt.hpp"

#include <Printers.hpp>
#include <UnitTests.hpp>

#include <sstream>

using namespace kestrel;
using namespace kestrel::dpt;
using namespace dpttest;

UNIT_TEST(FR_GA_4__dptIdParsesDottedForms)
{
	auto full = DptId::parse("9.001");
	ASSERT(full.has_value());
	ASSERT_EQ(full->main, std::uint16_t{9});
	ASSERT_EQ(full->sub, std::uint16_t{1});
	ASSERT_EQ(*DptId::parse("9.1"), (DptId{9, 1}));
	ASSERT_EQ(*DptId::parse("9"), (DptId{9, 0}));
	ASSERT_EQ(*DptId::parse("13.1200"), (DptId{13, 1200}));
	ASSERT_EQ(*DptId::parse("  232.600 "), (DptId{232, 600}));
	ASSERT_EQ(*DptId::parse("16.000"), (DptId{16, 0}));
}

UNIT_TEST(FR_GA_4__dptIdParsesEtsForms)
{
	ASSERT_EQ(*DptId::parse("DPT-9"), (DptId{9, 0}));
	ASSERT_EQ(*DptId::parse("dpt-1"), (DptId{1, 0}));
	ASSERT_EQ(*DptId::parse("DPST-9-1"), (DptId{9, 1}));
	ASSERT_EQ(*DptId::parse("dpst-232-600"), (DptId{232, 600}));
	ASSERT_EQ(*DptId::parse("DPT9.001"), (DptId{9, 1}));
	ASSERT_EQ(*DptId::parse("DPT 5.001"), (DptId{5, 1}));
}

UNIT_TEST(FR_GA_4__dptIdRejectsMalformed)
{
	for (std::string_view text : {"", "DPT", "DPT-", "DPST-9", "0", "0.001", "9.", ".1", "9.1.2", "9..1", "a.b", "-9",
								  "9.-1", "65536.1", "9.65536", "9.001x", "DPX-9", "9 .001"})
	{
		auto id = DptId::parse(text);
		ASSERT_FALSE(id.has_value());
		ASSERT_EQ(id.error().code, ErrorCode::InvalidArgument);
	}
}

UNIT_TEST(FR_GA_4__dptIdToString)
{
	ASSERT_EQ((DptId{9, 1}).toString(), std::string("9.001"));
	ASSERT_EQ((DptId{9, 0}).toString(), std::string("9.000"));
	ASSERT_EQ((DptId{13, 1200}).toString(), std::string("13.1200"));
	ASSERT_EQ((DptId{232, 600}).toString(), std::string("232.600"));
	ASSERT_EQ((DptId{9, 1}).toEtsString(), std::string("DPST-9-1"));
	ASSERT_EQ((DptId{9, 0}).toEtsString(), std::string("DPT-9"));
	// Round trips
	for (DptId id : {DptId{1, 1}, DptId{9, 0}, DptId{13, 1200}, DptId{65535, 65535}})
	{
		ASSERT_EQ(*DptId::parse(id.toString()), id);
		ASSERT_EQ(*DptId::parse(id.toEtsString()), id);
	}
	std::ostringstream os;
	os << DptId{5, 1};
	ASSERT_EQ(os.str(), std::string("5.001"));
}

UNIT_TEST(DptId_ordering)
{
	ASSERT((DptId{1, 1}) < (DptId{1, 2}));
	ASSERT((DptId{1, 99}) < (DptId{2, 0}));
	ASSERT((DptId{9, 1}) == (DptId{9, 1}));
	ASSERT((DptId{9, 1}) != (DptId{9, 2}));
}

UNIT_TEST(DptValue_printer)
{
	std::ostringstream os;
	os << DptValue{true} << ' ' << i64(-5) << ' ' << u64(5) << ' ' << DptValue{21.5} << ' ' << str("ab");
	ASSERT_EQ(os.str(), std::string("bool:true i64:-5 u64:5 f64:21.5 str:\"ab\""));
}

UNIT_TEST(FR_GA_4__sizeInBitPerMainType)
{
	struct Case
	{
		std::string_view id;
		std::size_t bits;
	};
	for (const Case& c : {Case{"1.001", 1}, Case{"2.001", 2}, Case{"3.007", 4}, Case{"5.001", 8}, Case{"6.010", 8},
						  Case{"7.001", 16}, Case{"8.001", 16}, Case{"9.001", 16}, Case{"10.001", 24},
						  Case{"11.001", 24}, Case{"12.001", 32}, Case{"13.001", 32}, Case{"14.056", 32},
						  Case{"16.000", 112}, Case{"16.001", 112}, Case{"17.001", 8}, Case{"18.001", 8},
						  Case{"20.102", 8}, Case{"232.600", 24}, Case{"1", 1}, Case{"99.001", 0}, Case{"4.001", 0}})
	{
		ASSERT_EQ(sizeInBit(dptId(c.id)), c.bits);
	}
	ASSERT_EQ(sizeInBit(DptId{}), std::size_t{0});
}

UNIT_TEST(FR_GA_4__isShortFormForSixBitsOrLess)
{
	ASSERT(isShortForm(dptId("1.001")));
	ASSERT(isShortForm(dptId("2.001")));
	ASSERT(isShortForm(dptId("3.007")));
	ASSERT_FALSE(isShortForm(dptId("5.001")));
	ASSERT_FALSE(isShortForm(dptId("17.001")));
	ASSERT_FALSE(isShortForm(dptId("9.001")));
	ASSERT_FALSE(isShortForm(dptId("99.001")));
	ASSERT_FALSE(isShortForm(DptId{}));
}

UNIT_TEST(FR_BUS_5__unitOfKnownSubtypes)
{
	ASSERT_EQ(std::string(unitOf(dptId("9.001"))), std::string("°C"));
	ASSERT_EQ(std::string(unitOf(dptId("5.001"))), std::string("%"));
	ASSERT_EQ(std::string(unitOf(dptId("13.013"))), std::string("kWh"));
	ASSERT_EQ(std::string(unitOf(dptId("14.056"))), std::string("W"));
	ASSERT_EQ(std::string(unitOf(dptId("7.001"))), std::string(""));
	ASSERT_EQ(std::string(unitOf(dptId("9"))), std::string(""));
}

UNIT_TEST(FR_BUS_5__scaledFlowRateHasNoUnit)
{
	// 13.002 travels in units of 0.0001 m³/h; the value stays raw, so no unit may be shown.
	ASSERT_EQ(std::string(unitOf(dptId("13.002"))), std::string(""));
	ASSERT_EQ(fmt("13.002", i64(12345)), std::string("12345"));
	ASSERT_EQ(std::string(unitOf(dptId("13.010"))), std::string("Wh"));
}

UNIT_TEST(FR_BUS_5__unknownMainTypeIsUnsupported)
{
	ASSERT_EQ(encErr("99.001", i64(1)), std::string("Unsupported"));
	ASSERT_EQ(decErr("99.001", "01"), std::string("Unsupported"));
	ASSERT_EQ(prsErr("99.001", "1"), std::string("Unsupported"));
	ASSERT_EQ(encErr("4.001", str("a")), std::string("Unsupported"));
	// format never fails: unknown types print the raw value
	ASSERT_EQ(fmt("99.001", i64(-3)), std::string("-3"));
	ASSERT_EQ(fmt("99.001", str("x")), std::string("x"));
	ASSERT_EQ(fmt("99.001", DptValue{true}), std::string("true"));
}
