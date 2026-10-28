#include "kestrel/base/Result.hpp"

#include <Printers.hpp>
#include <UnitTests.hpp>

#include <string>

using namespace kestrel;

namespace
{

Result<int> parsePositive(int value)
{
	if (value < 0)
	{
		return makeError(ErrorCode::InvalidArgument, "negative");
	}
	return value;
}

Result<int> doubled(int value)
{
	KESTREL_TRY(int parsed, parsePositive(value));
	return parsed * 2;
}

Status checkEven(int value)
{
	if (value % 2 != 0)
	{
		return makeError(ErrorCode::InvalidArgument, "odd");
	}
	return ok();
}

Status checkBoth(int value)
{
	KESTREL_CHECK(parsePositive(value));
	KESTREL_CHECK(checkEven(value));
	return ok();
}

} // namespace

UNIT_TEST(Result_holdsValue)
{
	auto r = parsePositive(3);
	ASSERT(r.has_value());
	ASSERT_EQ(*r, 3);
	ASSERT_EQ(r.value_or(7), 3);
}

UNIT_TEST(Result_holdsError)
{
	auto r = parsePositive(-1);
	ASSERT_FALSE(r.has_value());
	ASSERT_EQ(r.error().code, ErrorCode::InvalidArgument);
	ASSERT_EQ(r.error().message, std::string("negative"));
	ASSERT_EQ(r.value_or(7), 7);
}

UNIT_TEST(Result_tryPropagates)
{
	ASSERT_EQ(*doubled(4), 8);
	ASSERT_FALSE(doubled(-4).has_value());
}

UNIT_TEST(Result_statusAndCheck)
{
	ASSERT(checkBoth(4).has_value());
	ASSERT_EQ(checkBoth(3).error().message, std::string("odd"));
	ASSERT_EQ(checkBoth(-2).error().message, std::string("negative"));
}

UNIT_TEST(Result_stringValueIsNotConfusedWithError)
{
	Result<std::string> r = std::string("text");
	ASSERT(r.has_value());
	ASSERT_EQ(*r, std::string("text"));
}
