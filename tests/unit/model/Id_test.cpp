#include "kestrel/model/Entities.hpp"
#include "kestrel/model/Id.hpp"
#include "kestrel/model/Table.hpp"

#include <Printers.hpp>
#include <QtPrinters.hpp>
#include <UnitTests.hpp>

#include <set>

using namespace kestrel::model;
using namespace Qt::StringLiterals;

UNIT_TEST(FR_PRJ_1__idTextForm)
{
	ASSERT_EQ(Id<Device>(1).toString(), u"dev-0000000000001"_s);
	ASSERT_EQ(Id<Device>(32 * 9 + 3).toString(), u"dev-0000000000093"_s);
	ASSERT_EQ(Id<GroupAddress>(~std::uint64_t{0}).toString(), u"ga-fzzzzzzzzzzzz"_s);
	ASSERT_EQ(Id<BuildingPart>(7).toString().left(3), u"bp-"_s);
	ASSERT_EQ(Id<Area>(7).toString().left(5), u"area-"_s);
	ASSERT_EQ(Id<Line>(7).toString().left(5), u"line-"_s);
	ASSERT_EQ(Id<ComObject>(7).toString().left(3), u"co-"_s);
	ASSERT_EQ(Id<GroupRange>(7).toString().left(3), u"gr-"_s);
	ASSERT_EQ(Id<Link>(7).toString().left(4), u"lnk-"_s);
	// 13 base32 characters after the prefix
	ASSERT_EQ(Id<Device>(0x0123456789abcdefULL).toString().size(), qsizetype{17});
}

UNIT_TEST(FR_PRJ_1__idParseRoundTrip)
{
	for (const std::uint64_t value : {std::uint64_t{1}, std::uint64_t{0x0123456789abcdefULL}, ~std::uint64_t{0},
			 std::uint64_t{0x8000000000000000ULL}})
	{
		const auto parsed = Id<Device>::parse(Id<Device>(value).toString());
		ASSERT(parsed.has_value());
		ASSERT_EQ(parsed->value(), value);
	}
}

UNIT_TEST(FR_PRJ_1__idParseRejectsMalformedText)
{
	ASSERT_FALSE(Id<Device>::parse(u"ga-0000000000001"_s).has_value());   // wrong prefix
	ASSERT_FALSE(Id<Device>::parse(u"dev-000000000001"_s).has_value());   // 12 chars
	ASSERT_FALSE(Id<Device>::parse(u"dev-00000000000001"_s).has_value()); // 14 chars
	ASSERT_FALSE(Id<Device>::parse(u"dev_0000000000001"_s).has_value());  // separator
	ASSERT_FALSE(Id<Device>::parse(u"dev-000000000000i"_s).has_value());  // not in alphabet
	ASSERT_FALSE(Id<Device>::parse(u"dev-000000000000U"_s).has_value());  // upper case
	ASSERT_FALSE(Id<Device>::parse(u"dev-g000000000000"_s).has_value());  // > 64 bits
	ASSERT_FALSE(Id<Device>::parse(u"dev-0000000000000"_s).has_value());  // null id
	ASSERT_FALSE(Id<Device>::parse(u""_s).has_value());
}

UNIT_TEST(FR_PRJ_1__idGeneratorSequential)
{
	IdGenerator gen = IdGenerator::sequential(5);
	ASSERT_EQ(gen.next(), std::uint64_t{5});
	ASSERT_EQ(gen.next(), std::uint64_t{6});
	IdGenerator zero = IdGenerator::sequential(0);
	ASSERT_EQ(zero.next(), std::uint64_t{1}); // 0 is never produced
	ASSERT_EQ(zero.next(), std::uint64_t{2});
}

UNIT_TEST(FR_PRJ_1__idGeneratorRandomIsNonNullAndDistinct)
{
	IdGenerator gen = IdGenerator::random();
	std::set<std::uint64_t> seen;
	for (int i = 0; i < 1000; ++i)
	{
		const std::uint64_t value = gen.next();
		ASSERT(value != 0);
		seen.insert(value);
	}
	ASSERT_EQ(seen.size(), std::size_t{1000});
}

UNIT_TEST(FR_PRJ_1__entityRefTextAndConversion)
{
	const EntityRef ref = Id<GroupAddress>(42);
	ASSERT_EQ(ref.kind, EntityKind::GroupAddress);
	ASSERT(ref.is<GroupAddress>());
	ASSERT_FALSE(ref.is<Device>());
	ASSERT_EQ(ref.as<GroupAddress>(), Id<GroupAddress>(42));
	ASSERT_EQ(EntityRef::parse(ref.toString()), std::optional<EntityRef>(ref));
	ASSERT_EQ(EntityRef::project().toString(), u"project"_s);
	ASSERT_EQ(EntityRef::parse(u"project"_s), std::optional<EntityRef>(EntityRef::project()));
	ASSERT_FALSE(EntityRef::parse(u"xyz-0000000000001"_s).has_value());
	ASSERT(EntityRef(Id<Device>(1)) < EntityRef(Id<Device>(2)));
	ASSERT(EntityRef(Id<Device>(1)) != EntityRef(Id<Link>(1)));
}

UNIT_TEST(FR_PRJ_1__comObjectFlagsText)
{
	ComObjectFlags flags;
	flags.write = true;
	flags.transmit = true;
	ASSERT_EQ(flags.toString(), u"CWT"_s);
	ASSERT_EQ(ComObjectFlags::parse(u"TWC"_s), std::optional<ComObjectFlags>(flags));
	ASSERT_EQ(ComObjectFlags::parse(u""_s)->toString(), u""_s);
	ASSERT_EQ(ComObjectFlags::parse(u"CRWTUI"_s)->toString(), u"CRWTUI"_s);
	ASSERT_FALSE(ComObjectFlags::parse(u"CC"_s).has_value());
	ASSERT_FALSE(ComObjectFlags::parse(u"CX"_s).has_value());
}

UNIT_TEST(FR_GA_4__dptReferenceSyntax)
{
	ASSERT(isValidDptRef(u""_s));
	ASSERT(isValidDptRef(u"DPT-9"_s));
	ASSERT(isValidDptRef(u"DPST-1-1"_s));
	ASSERT(isValidDptRef(u"DPST-232-600"_s));
	ASSERT_FALSE(isValidDptRef(u"9.001"_s));
	ASSERT_FALSE(isValidDptRef(u"DPST-1"_s));
	ASSERT_FALSE(isValidDptRef(u"DPT-0"_s));
	ASSERT_FALSE(isValidDptRef(u"DPST-1-1 "_s));
}

UNIT_TEST(FR_PRJ_1__enumTextRoundTrip)
{
	ASSERT_EQ(parseBuildingPartKind(toString(BuildingPartKind::DistributionBoard)),
		std::optional<BuildingPartKind>(BuildingPartKind::DistributionBoard));
	ASSERT_EQ(parseMedium(u"RF"_s), std::optional<Medium>(Medium::RF));
	ASSERT_FALSE(parseMedium(u"rf"_s).has_value());
	ASSERT(parseGroupAddressStyle(u"TwoLevel"_s) == kestrel::knx::GroupAddressStyle::TwoLevel);
	ASSERT_FALSE(parseGroupAddressStyle(u"Four"_s).has_value());
}
