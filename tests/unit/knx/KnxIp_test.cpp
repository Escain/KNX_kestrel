#include "KnxTestSupport.hpp"
#include "kestrel/knx/KnxIp.hpp"

#include <Printers.hpp>
#include <UnitTests.hpp>

using namespace kestrel;
using namespace kestrel::knx;
using namespace kestrel::knx::knxip;
using knxtest::hex;

namespace
{

const IpEndpoint kServer{{192, 168, 1, 10}, 3671};

DeviceInfo sampleDeviceInfo()
{
	DeviceInfo info;
	info.programmingMode = true;
	info.address = *IndividualAddress::parse("1.1.255");
	info.serial = *SerialNumber::parse("0083:00001234");
	info.macAddress = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
	info.friendlyName = "Kestrel test interface";
	return info;
}

bool roundTrips(const Frame& frame)
{
	auto decoded = decode(encode(frame));
	return decoded.has_value() && *decoded == frame;
}

} // namespace

UNIT_TEST(FR_BUS_2__connectRequestNatByteExact)
{
	const Frame frame = ConnectRequest{Hpai::nat(), Hpai::nat(), Cri{}};
	ASSERT_EQ(encode(frame), hex("06 10 02 05 00 1a 08 01 00 00 00 00 00 00 08 01 00 00 00 00 00 00 04 04 02 00"));
	ASSERT(roundTrips(frame));
	ASSERT_EQ(serviceType(frame), ServiceType::ConnectRequest);
}

UNIT_TEST(FR_BUS_2__connectResponseByteExact)
{
	ConnectResponse response;
	response.channelId = 1;
	response.dataEndpoint = Hpai{HostProtocol::Udp, kServer};
	response.address = *IndividualAddress::parse("1.1.255");
	ASSERT_EQ(encode(response), hex("06 10 02 06 00 14 01 00 08 01 c0 a8 01 0a 0e 57 04 04 11 ff"));
	ASSERT(roundTrips(response));

	// Error responses carry only channel and status.
	const ConnectResponse rejected{0, status::kNoMoreConnections, {}, kTunnelConnection, {}};
	ASSERT_EQ(encode(rejected), hex("06 10 02 06 00 08 00 24"));
	ASSERT(roundTrips(rejected));
	// ... but some servers append HPAI + CRD anyway: accepted.
	auto padded = decode(hex("06 10 02 06 00 14 00 24 08 01 00 00 00 00 00 00 04 04 00 00"));
	ASSERT(padded.has_value());
	ASSERT_EQ(std::get<ConnectResponse>(*padded).status, status::kNoMoreConnections);
}

UNIT_TEST(FR_BUS_2__connectionStateAndDisconnectByteExact)
{
	ASSERT_EQ(encode(ConnectionStateRequest{1, Hpai::nat()}), hex("06 10 02 07 00 10 01 00 08 01 00 00 00 00 00 00"));
	ASSERT_EQ(encode(ConnectionStateResponse{1, status::kConnectionId}), hex("06 10 02 08 00 08 01 21"));
	ASSERT_EQ(encode(DisconnectRequest{7, Hpai{HostProtocol::Udp, kServer}}),
		hex("06 10 02 09 00 10 07 00 08 01 c0 a8 01 0a 0e 57"));
	ASSERT_EQ(encode(DisconnectResponse{7, status::kNoError}), hex("06 10 02 0a 00 08 07 00"));
	ASSERT(roundTrips(ConnectionStateRequest{1, Hpai::nat()}));
	ASSERT(roundTrips(ConnectionStateResponse{1, 0x21}));
	ASSERT(roundTrips(DisconnectRequest{7, Hpai{HostProtocol::Udp, kServer}}));
	ASSERT(roundTrips(DisconnectResponse{7, 0}));
}

UNIT_TEST(FR_BUS_2__tunnellingByteExact)
{
	const TunnellingRequest request{1, 0, hex("11 00 bc e0 00 00 08 01 01 00 81")};
	ASSERT_EQ(encode(request), hex("06 10 04 20 00 15 04 01 00 00 11 00 bc e0 00 00 08 01 01 00 81"));
	ASSERT(roundTrips(request));
	ASSERT_EQ(encode(TunnellingAck{1, 0xFF, 0}), hex("06 10 04 21 00 0a 04 01 ff 00"));
	ASSERT(roundTrips(TunnellingAck{1, 0xFF, 0x04}));
}

UNIT_TEST(FR_BUS_3__routingIndicationByteExact)
{
	const RoutingIndication indication{hex("29 00 bc e0 11 05 08 01 01 00 81")};
	ASSERT_EQ(encode(indication), hex("06 10 05 30 00 11 29 00 bc e0 11 05 08 01 01 00 81"));
	ASSERT(roundTrips(indication));
}

UNIT_TEST(FR_BUS_1__searchAndDescription)
{
	ASSERT_EQ(encode(SearchRequest{Hpai{HostProtocol::Udp, {{192, 168, 1, 2}, 3671}}}),
		hex("06 10 02 01 00 0e 08 01 c0 a8 01 02 0e 57"));
	ASSERT_EQ(encode(DescriptionRequest{Hpai::nat()}), hex("06 10 02 03 00 0e 08 01 00 00 00 00 00 00"));

	const std::vector<ServiceFamily> families{{family::kCore, 1}, {family::kDeviceManagement, 1}, {family::kTunnelling, 1}};
	const SearchResponse search{Hpai{HostProtocol::Udp, kServer}, sampleDeviceInfo(), families};
	const auto bytes = encode(search);
	ASSERT_EQ(bytes.size(), std::size_t{6 + 8 + 54 + 8});
	// Header, HPAI, then the device information DIB.
	ASSERT_EQ(Bytes(bytes.begin(), bytes.begin() + 28),
		hex("06 10 02 02 00 4c 08 01 c0 a8 01 0a 0e 57 36 01 02 01 11 ff 00 00 00 83 00 00 12 34"));
	// Supported service families DIB at the end.
	ASSERT_EQ(Bytes(bytes.end() - 8, bytes.end()), hex("08 02 02 01 03 01 04 01"));
	ASSERT(roundTrips(search));
	ASSERT(roundTrips(DescriptionResponse{sampleDeviceInfo(), families}));
}

UNIT_TEST(FR_BUS_1__descriptionResponseSkipsUnknownDibs)
{
	DescriptionResponse response{sampleDeviceInfo(), {{family::kCore, 2}}};
	auto bytes = encode(response);
	// Append a manufacturer data DIB (type 0xFE) and fix the total length.
	for (auto b : hex("06 fe 00 83 01 02"))
	{
		bytes.push_back(b);
	}
	bytes[5] = static_cast<std::uint8_t>(bytes.size());
	auto decoded = decode(bytes);
	ASSERT(decoded.has_value());
	ASSERT_EQ(std::get<DescriptionResponse>(*decoded), response);
}

UNIT_TEST(FR_BUS_2__knxipRejectsMalformed)
{
	ASSERT_EQ(decode(hex("06 10 02 08")).error().code, ErrorCode::ParseError);              // truncated header
	ASSERT_EQ(decode(hex("06 13 02 08 00 08 01 00")).error().code, ErrorCode::Unsupported); // version 1.3
	ASSERT_EQ(decode(hex("05 10 02 08 00 08 01 00")).error().code, ErrorCode::ParseError);  // header length
	ASSERT_EQ(decode(hex("06 10 02 08 00 09 01 00")).error().code, ErrorCode::ParseError);  // total length
	ASSERT_EQ(decode(hex("06 10 03 10 00 08 01 00")).error().code, ErrorCode::Unsupported); // DEVICE_CONFIGURATION
	ASSERT_EQ(decode(hex("06 10 02 08 00 07 01")).error().code, ErrorCode::ParseError);     // truncated body
	ASSERT_EQ(decode(hex("06 10 02 08 00 09 01 00 00")).error().code, ErrorCode::ParseError); // trailing
	ASSERT_EQ(decode(hex("06 10 04 20 00 0a 04 01 00 00")).error().code, ErrorCode::ParseError); // no cEMI
	ASSERT_EQ(decode(hex("06 10 04 21 00 0a 05 01 00 00")).error().code, ErrorCode::ParseError); // header len
	ASSERT_EQ(decode(hex("06 10 02 01 00 0e 08 03 c0 a8 01 02 0e 57")).error().code, ErrorCode::ParseError); // protocol
}

UNIT_TEST(FR_BUS_2__serialNumberText)
{
	auto serial = SerialNumber::parse("0083:00001234");
	ASSERT(serial.has_value());
	ASSERT_EQ(serial->manufacturer(), std::uint16_t{0x0083});
	ASSERT_EQ(serial->toString(), std::string("0083:00001234"));
	ASSERT_EQ(*SerialNumber::parse("008300001234"), *serial);
	ASSERT_FALSE(SerialNumber::parse("0083:0000123").has_value());
	ASSERT_FALSE(SerialNumber::parse("0083:0000123x").has_value());
}
