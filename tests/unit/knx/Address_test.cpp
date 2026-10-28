#include "kestrel/knx/Address.hpp"

#include <Printers.hpp>
#include <UnitTests.hpp>

using namespace kestrel;
using namespace kestrel::knx;

UNIT_TEST(FR_TOP_1__individualAddressParts)
{
	auto ia = IndividualAddress::parse("1.2.3");
	ASSERT(ia.has_value());
	ASSERT_EQ(ia->raw(), std::uint16_t{0x1203});
	ASSERT_EQ(ia->area(), 1u);
	ASSERT_EQ(ia->line(), 2u);
	ASSERT_EQ(ia->device(), 3u);
	ASSERT_EQ(ia->toString(), std::string("1.2.3"));
	ASSERT_EQ(IndividualAddress(0xFFFF).toString(), std::string("15.15.255"));
}

UNIT_TEST(FR_TOP_1__individualAddressRejectsOutOfRange)
{
	ASSERT_FALSE(IndividualAddress::parse("16.0.1").has_value());
	ASSERT_FALSE(IndividualAddress::parse("1.1.256").has_value());
	ASSERT_FALSE(IndividualAddress::parse("1.1").has_value());
	ASSERT_FALSE(IndividualAddress::parse("1..1").has_value());
	ASSERT_FALSE(IndividualAddress::parse("a.b.c").has_value());
}

UNIT_TEST(FR_GA_1__groupAddressThreeLevel)
{
	auto ga = GroupAddress::parse("1/2/3");
	ASSERT(ga.has_value());
	ASSERT_EQ(ga->raw(), std::uint16_t{0x0A03});
	ASSERT_EQ(ga->toString(), std::string("1/2/3"));
	ASSERT_EQ(ga->toString(GroupAddressStyle::TwoLevel), std::string("1/515"));
	ASSERT_EQ(ga->toString(GroupAddressStyle::Free), std::string("2563"));
	ASSERT_EQ(GroupAddress(0xFFFF).toString(), std::string("31/7/255"));
}

UNIT_TEST(FR_GA_1__groupAddressTwoLevelAndFree)
{
	ASSERT_EQ(GroupAddress::parse("1/515")->raw(), std::uint16_t{0x0A03});
	ASSERT_EQ(GroupAddress::parse("2563")->raw(), std::uint16_t{0x0A03});
	ASSERT_FALSE(GroupAddress::parse("32/0/0").has_value());
	ASSERT_FALSE(GroupAddress::parse("1/8/0").has_value());
	ASSERT_FALSE(GroupAddress::parse("1/2048").has_value());
	ASSERT_FALSE(GroupAddress::parse("65536").has_value());
	ASSERT_FALSE(GroupAddress::parse("1/2/3/4").has_value());
}

UNIT_TEST(GroupAddress_ordering)
{
	ASSERT(GroupAddress(1) < GroupAddress(2));
	ASSERT(IndividualAddress(0x1101) == *IndividualAddress::parse("1.1.1"));
}
