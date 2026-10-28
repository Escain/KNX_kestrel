#include "kestrel/base/Bytes.hpp"

#include <Printers.hpp>
#include <UnitTests.hpp>

using namespace kestrel;

UNIT_TEST(Bytes_hexRoundTrip)
{
	const Bytes bytes{0x06, 0x10, 0x02, 0x05, 0xAB};
	ASSERT_EQ(toHex(bytes), std::string("06 10 02 05 ab"));
	ASSERT_EQ(*fromHex("06 10 02 05 ab"), bytes);
	ASSERT_EQ(*fromHex("061002:05AB"), bytes);
}

UNIT_TEST(Bytes_fromHexRejectsGarbage)
{
	ASSERT_FALSE(fromHex("0g").has_value());
	ASSERT_FALSE(fromHex("123").has_value());
}

UNIT_TEST(Bytes_readerBigEndian)
{
	const Bytes bytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
	ByteReader reader(bytes);
	ASSERT_EQ(int(reader.u8()), 0x01);
	ASSERT_EQ(int(reader.u16()), 0x0203);
	ASSERT_EQ(reader.u32(), 0x04050607u);
	ASSERT(reader.atEnd());
	ASSERT_FALSE(reader.failed());
}

UNIT_TEST(Bytes_readerOverrunSetsFailure)
{
	const Bytes bytes{0x01};
	ByteReader reader(bytes);
	ASSERT_EQ(int(reader.u16()), 0x0100);
	ASSERT(reader.failed());
	ASSERT_EQ(reader.remaining(), std::size_t{0});
	ASSERT(reader.take(1).empty());
}

UNIT_TEST(Bytes_writerPatch)
{
	Bytes out;
	ByteWriter writer(out);
	writer.u8(0x06);
	writer.u16(0);
	writer.u32(0xDEADBEEF);
	writer.patchU16(1, 0x1234);
	ASSERT_EQ(out, (Bytes{0x06, 0x12, 0x34, 0xDE, 0xAD, 0xBE, 0xEF}));
}
