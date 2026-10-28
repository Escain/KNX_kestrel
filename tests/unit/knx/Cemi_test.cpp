#include "KnxTestSupport.hpp"
#include "kestrel/knx/Cemi.hpp"

#include <Printers.hpp>
#include <UnitTests.hpp>

using namespace kestrel;
using namespace kestrel::knx;
using knxtest::hex;

UNIT_TEST(FR_BUS_2__cemiGroupWriteEncodesByteExact)
{
	// L_Data.req, group write 1/0/1 = 1 (short form), standard frame, low priority, hop count 6.
	cemi::LData frame;
	frame.destination = GroupAddress::parse("1/0/1")->raw();
	frame.tpdu = hex("00 81");
	ASSERT_EQ(cemi::encode(frame), hex("11 00 bc e0 00 00 08 01 01 00 81"));
}

UNIT_TEST(FR_BUS_2__cemiDecodeRoundTrip)
{
	const auto bytes = hex("11 00 bc e0 00 00 08 01 01 00 81");
	auto frame = cemi::decode(bytes);
	ASSERT(frame.has_value());
	ASSERT_EQ(frame->messageCode, cemi::MessageCode::LDataReq);
	ASSERT(frame->frameType == cemi::FrameType::Standard);
	ASSERT_FALSE(frame->repeat);
	ASSERT(frame->broadcast);
	ASSERT_EQ(frame->priority, cemi::Priority::Low);
	ASSERT_FALSE(frame->ackRequest);
	ASSERT_FALSE(frame->confirmError);
	ASSERT(frame->groupDestination);
	ASSERT_EQ(frame->hopCount, std::uint8_t{6});
	ASSERT_EQ(frame->groupAddress(), *GroupAddress::parse("1/0/1"));
	ASSERT_EQ(frame->tpdu, hex("00 81"));
	ASSERT_EQ(cemi::encode(*frame), bytes);
}

UNIT_TEST(FR_BUS_5__cemiIndicationSkipsAdditionalInfo)
{
	// L_Data.ind from 1.1.5 with 4 bytes of additional info, DPT 9 write 0x0c33 to 1/0/2.
	auto frame = cemi::decode(hex("29 04 03 02 aa bb bc e0 11 05 08 02 03 00 80 0c 33"));
	ASSERT(frame.has_value());
	ASSERT_EQ(frame->messageCode, cemi::MessageCode::LDataInd);
	ASSERT_EQ(frame->sourceAddress(), *IndividualAddress::parse("1.1.5"));
	ASSERT_EQ(frame->tpdu, hex("00 80 0c 33"));
	// Re-encoding drops the additional info.
	ASSERT_EQ(cemi::encode(*frame), hex("29 00 bc e0 11 05 08 02 03 00 80 0c 33"));
}

UNIT_TEST(FR_BUS_2__cemiControlFieldBits)
{
	cemi::LData frame;
	frame.messageCode = cemi::MessageCode::LDataCon;
	frame.frameType = cemi::FrameType::Extended;
	frame.repeat = true;
	frame.broadcast = false;
	frame.priority = cemi::Priority::System;
	frame.ackRequest = true;
	frame.confirmError = true;
	frame.groupDestination = false;
	frame.hopCount = 7;
	frame.source = 0x1101;
	frame.destination = 0x1105;
	frame.tpdu = hex("80");
	const auto bytes = cemi::encode(frame);
	ASSERT_EQ(bytes, hex("2e 00 03 70 11 01 11 05 00 80"));
	ASSERT_EQ(*cemi::decode(bytes), frame);
}

UNIT_TEST(FR_BUS_2__cemiRejectsMalformed)
{
	ASSERT_FALSE(cemi::decode(hex("")).has_value());
	ASSERT_EQ(cemi::decode(hex("fc 00 00 01 01")).error().code, ErrorCode::Unsupported); // M_PropRead.req
	ASSERT_EQ(cemi::decode(hex("11 00 bc e0 00 00 08 01 02 00 81")).error().code, ErrorCode::ParseError);
	ASSERT_EQ(cemi::decode(hex("11 00 bc e0 00 00 08 01 01 00 81 ff")).error().code, ErrorCode::ParseError);
	ASSERT_EQ(cemi::decode(hex("11 05 bc e0")).error().code, ErrorCode::ParseError);
}
