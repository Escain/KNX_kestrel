#include "KnxTestSupport.hpp"
#include "kestrel/knx/Apdu.hpp"

#include <Printers.hpp>
#include <UnitTests.hpp>

using namespace kestrel;
using namespace kestrel::knx;
using knxtest::hex;

namespace
{

Bytes group(const Apdu& apdu)
{
	return encode(Tpdu::dataGroup(apdu));
}

Bytes individual(const Apdu& apdu)
{
	return encode(Tpdu::dataIndividual(apdu));
}

} // namespace

UNIT_TEST(FR_BUS_6__groupValueServicesEncode)
{
	ASSERT_EQ(group(groupValueRead()), hex("00 00"));
	ASSERT_EQ(group(groupValueWriteShort(1)), hex("00 81"));
	ASSERT_EQ(group(groupValueWriteShort(0)), hex("00 80"));
	ASSERT_EQ(group(groupValueResponseShort(0x3F)), hex("00 7f"));
	ASSERT_EQ(group(groupValueWrite(hex("0c 33"))), hex("00 80 0c 33"));
	ASSERT_EQ(group(groupValueResponse(hex("12 34"))), hex("00 40 12 34"));
}

UNIT_TEST(FR_BUS_6__groupValueDecodeShortAndLong)
{
	auto shortWrite = decodeTpdu(hex("00 81"), true, 0x0801);
	ASSERT(shortWrite.has_value());
	ASSERT_EQ(shortWrite->type, TpduType::DataGroup);
	ASSERT_EQ(shortWrite->apdu.apci, Apci::GroupValueWrite);
	ASSERT_EQ(shortWrite->apdu.shortData, std::uint8_t{1});
	ASSERT_EQ(shortWrite->apdu.groupValue(), hex("01"));

	auto longResponse = decodeTpdu(hex("00 40 0c 33"), true, 0x0802);
	ASSERT(longResponse.has_value());
	ASSERT_EQ(longResponse->apdu.apci, Apci::GroupValueResponse);
	ASSERT_EQ(longResponse->apdu.shortData, std::uint8_t{0});
	ASSERT_EQ(longResponse->apdu.groupValue(), hex("0c 33"));

	auto read = decodeTpdu(hex("00 00"), true, 0x0801);
	ASSERT_EQ(read->apdu, groupValueRead());
}

UNIT_TEST(FR_COM_1__individualAddressServicesEncode)
{
	ASSERT_EQ(encode(Tpdu::dataBroadcast(individualAddressRead())), hex("01 00"));
	ASSERT_EQ(encode(Tpdu::dataBroadcast(individualAddressResponse())), hex("01 40"));
	const auto write = individualAddressWrite(*IndividualAddress::parse("1.1.5"));
	ASSERT_EQ(encode(Tpdu::dataBroadcast(write)), hex("00 c0 11 05"));

	auto decoded = decodeTpdu(hex("00 c0 11 05"), true, 0);
	ASSERT(decoded.has_value());
	ASSERT_EQ(decoded->type, TpduType::DataBroadcast);
	ASSERT_EQ(*decodeIndividualAddressWrite(decoded->apdu), *IndividualAddress::parse("1.1.5"));
	ASSERT_FALSE(decodeIndividualAddressWrite(groupValueRead()).has_value());
}

UNIT_TEST(FR_COM_4__deviceDescriptorAndMemoryServicesEncode)
{
	ASSERT_EQ(encode(Tpdu::dataConnected(0, deviceDescriptorRead())), hex("43 00"));
	ASSERT_EQ(encode(Tpdu::dataConnected(0, deviceDescriptorResponse(0, hex("07 05")))), hex("43 40 07 05"));
	ASSERT_EQ(encode(Tpdu::dataConnected(1, memoryRead(0x0060, 1))), hex("46 01 00 60"));
	ASSERT_EQ(encode(Tpdu::dataConnected(2, memoryWrite(0x4000, hex("01 02 03")))), hex("4a 83 40 00 01 02 03"));
	ASSERT_EQ(encode(Tpdu::dataConnected(15, memoryResponse(0x4000, hex("aa")))), hex("7e 41 40 00 aa"));

	auto response = decodeTpdu(hex("4a 83 40 00 01 02 03"), false, 0x1105);
	ASSERT(response.has_value());
	ASSERT_EQ(response->type, TpduType::DataConnected);
	ASSERT_EQ(response->seq, std::uint8_t{2});
	auto access = decodeMemoryAccess(response->apdu);
	ASSERT(access.has_value());
	ASSERT_EQ(*access, (MemoryAccess{0x4000, 3, hex("01 02 03")}));

	// Count / data mismatch is rejected.
	ASSERT_FALSE(decodeMemoryAccess(Apdu{Apci::MemoryResponse, 2, hex("40 00 01")}).has_value());
	ASSERT_FALSE(decodeMemoryAccess(Apdu{Apci::MemoryRead, 2, hex("40")}).has_value());
}

UNIT_TEST(FR_COM_4__propertyAndRestartServicesEncode)
{
	ASSERT_EQ(individual(propertyValueRead(0, 11, 1, 1)), hex("03 d5 00 0b 10 01"));
	ASSERT_EQ(individual(propertyValueResponse(0, 11, 1, 1, hex("00 83 00 00 12 34"))),
		hex("03 d6 00 0b 10 01 00 83 00 00 12 34"));
	ASSERT_EQ(individual(propertyValueWrite(3, 0x35, 2, 0x123, hex("ab cd"))), hex("03 d7 03 35 21 23 ab cd"));
	ASSERT_EQ(individual(restart()), hex("03 80"));

	auto decoded = decodeTpdu(hex("03 d6 00 0b 10 01 00 83 00 00 12 34"), false, 0x11FF);
	ASSERT(decoded.has_value());
	ASSERT_EQ(decoded->type, TpduType::DataIndividual);
	ASSERT_EQ(decoded->apdu.apci, Apci::PropertyValueResponse);
	auto access = decodePropertyAccess(decoded->apdu);
	ASSERT_EQ(*access, (PropertyAccess{0, 11, 1, 1, hex("00 83 00 00 12 34")}));

	auto restartTpdu = decodeTpdu(hex("03 80"), false, 0x1105);
	ASSERT_EQ(restartTpdu->apdu, restart());
	auto masterReset = decodeTpdu(hex("03 81 01 00"), false, 0x1105);
	ASSERT_EQ(masterReset->apdu.apci, Apci::Restart);
	ASSERT_EQ(masterReset->apdu.shortData, std::uint8_t{1});
}

UNIT_TEST(FR_COM_4__transportControlEncode)
{
	ASSERT_EQ(encode(Tpdu::connect()), hex("80"));
	ASSERT_EQ(encode(Tpdu::disconnect()), hex("81"));
	ASSERT_EQ(encode(Tpdu::ack(0)), hex("c2"));
	ASSERT_EQ(encode(Tpdu::ack(3)), hex("ce"));
	ASSERT_EQ(encode(Tpdu::nak(1)), hex("c7"));
	ASSERT_EQ(encode(Tpdu::ack(15)), hex("fe"));

	ASSERT_EQ(*decodeTpdu(hex("80"), false, 0x1105), Tpdu::connect());
	ASSERT_EQ(*decodeTpdu(hex("81"), false, 0x1105), Tpdu::disconnect());
	ASSERT_EQ(*decodeTpdu(hex("ce"), false, 0x1105), Tpdu::ack(3));
	ASSERT_EQ(*decodeTpdu(hex("c7"), false, 0x1105), Tpdu::nak(1));
}

UNIT_TEST(FR_BUS_5__tpduRejectsMalformed)
{
	ASSERT_FALSE(decodeTpdu(hex(""), true, 1).has_value());
	ASSERT_FALSE(decodeTpdu(hex("00"), true, 1).has_value());         // data without APCI
	ASSERT_FALSE(decodeTpdu(hex("80"), true, 1).has_value());         // control to a group
	ASSERT_FALSE(decodeTpdu(hex("80 00"), false, 1).has_value());     // control with payload
	ASSERT_FALSE(decodeTpdu(hex("04 80"), true, 1).has_value());      // tag group: unsupported
	ASSERT_FALSE(decodeTpdu(hex("04 80"), false, 0x1105).has_value()); // seq on unnumbered data
}

UNIT_TEST(FR_BUS_5__unknownApciIsKept)
{
	// A_UserMemory_Read (0x2C0) is a 10-bit code: kept verbatim, no short data.
	auto tpdu = decodeTpdu(hex("02 c0 01 02 03"), false, 0x1105);
	ASSERT(tpdu.has_value());
	ASSERT_EQ(static_cast<unsigned>(tpdu->apdu.apci), 0x2C0u);
	ASSERT_EQ(tpdu->apdu.shortData, std::uint8_t{0});
	ASSERT_EQ(encode(*tpdu), hex("02 c0 01 02 03"));
	ASSERT(hasShortData(Apci::MemoryRead));
	ASSERT_FALSE(hasShortData(Apci::PropertyValueRead));
}

UNIT_TEST(FR_COM_1__frameBuilders)
{
	const auto broadcast = makeBroadcastFrame(individualAddressWrite(*IndividualAddress::parse("1.1.5")));
	ASSERT_EQ(cemi::encode(broadcast), hex("11 00 b0 e0 00 00 00 00 03 00 c0 11 05"));

	const auto connect = makeIndividualFrame(*IndividualAddress::parse("1.1.5"), Tpdu::connect());
	ASSERT_EQ(cemi::encode(connect), hex("11 00 b0 60 00 00 11 05 00 80"));

	const auto groupWrite = makeGroupFrame(*GroupAddress::parse("1/0/1"), groupValueWriteShort(1));
	ASSERT_EQ(cemi::encode(groupWrite), hex("11 00 bc e0 00 00 08 01 01 00 81"));
	ASSERT_EQ(*decodeTpdu(groupWrite), Tpdu::dataGroup(groupValueWriteShort(1)));
}
