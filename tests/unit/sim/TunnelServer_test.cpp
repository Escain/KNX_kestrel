#include "SimTestSupport.hpp"

#include <Printers.hpp>
#include <UnitTests.hpp>

using namespace kestrel;
using namespace kestrel::sim;
using namespace std::chrono_literals;
using knx::IndividualAddress;
using simtest::hex;
using simtest::kT0;
using simtest::knxipAs;
using simtest::only;
namespace knxip = kestrel::knx::knxip;

namespace
{

const knxip::IpEndpoint kClient{{127, 0, 0, 1}, 50000};
const knxip::IpEndpoint kClient2{{127, 0, 0, 1}, 50001};
const knxip::IpEndpoint kLocal{{127, 0, 0, 1}, 3671};
const Bytes kGroupWrite = hex("11 00 bc e0 00 00 09 01 01 00 81");

Bytes connectRequest(std::uint8_t layer = knxip::kTunnelLinkLayer, std::uint8_t type = knxip::kTunnelConnection)
{
	return knxip::encode(knxip::ConnectRequest{knxip::Hpai::nat(), knxip::Hpai::nat(), knxip::Cri{type, layer}});
}

/// Datagrams of the actions, decoded as T.
template <typename T>
std::vector<T> sentAs(const std::vector<TunnelServer::Action>& actions)
{
	std::vector<T> out;
	for (const auto& send : only<TunnelServer::SendDatagram>(actions))
	{
		if (auto frame = knxipAs<T>(send.bytes))
		{
			out.push_back(*frame);
		}
	}
	return out;
}

/// Server with one client connected on channel 1 (actions consumed).
struct Connected
{
	VirtualBus bus;
	TunnelServer server{bus};

	Connected()
	{
		server.onDatagram(kClient, connectRequest(), kT0);
		(void)server.takeActions();
	}
};

} // namespace

UNIT_TEST(FR_BUS_1__tunnelServerSearchAndDescription)
{
	VirtualBus bus;
	TunnelServer server(bus);
	server.setLocalEndpoint(kLocal);
	server.onDatagram(kClient, knxip::encode(knxip::SearchRequest{knxip::Hpai{knxip::HostProtocol::Udp, {{192, 168, 1, 2}, 3671}}}), kT0);
	auto actions = server.takeActions();
	auto sends = only<TunnelServer::SendDatagram>(actions);
	ASSERT_EQ(sends.size(), std::size_t{1});
	ASSERT_EQ(sends[0].to, (knxip::IpEndpoint{{192, 168, 1, 2}, 3671}));
	auto search = knxipAs<knxip::SearchResponse>(sends[0].bytes);
	ASSERT(search.has_value());
	ASSERT_EQ(search->controlEndpoint.endpoint, kLocal);
	ASSERT_EQ(search->deviceInfo.address, IndividualAddress(0x11FF));
	ASSERT_EQ(search->deviceInfo.friendlyName, std::string("Kestrel simulator"));
	ASSERT_EQ(search->serviceFamilies.size(), std::size_t{3});

	server.onDatagram(kClient, knxip::encode(knxip::DescriptionRequest{knxip::Hpai::nat()}), kT0);
	sends = only<TunnelServer::SendDatagram>(server.takeActions());
	ASSERT_EQ(sends[0].to, kClient); // NAT: answer the sender
	ASSERT(knxipAs<knxip::DescriptionResponse>(sends[0].bytes).has_value());
}

UNIT_TEST(FR_BUS_2__tunnelServerConnectAndSlots)
{
	VirtualBus bus;
	TunnelServerConfig config;
	config.tunnelAddresses = {IndividualAddress(0x11FF), IndividualAddress(0x11FE)};
	TunnelServer server(bus, config);

	server.onDatagram(kClient, connectRequest(), kT0);
	auto actions = server.takeActions();
	auto responses = sentAs<knxip::ConnectResponse>(actions);
	ASSERT_EQ(responses.size(), std::size_t{1});
	ASSERT_EQ(responses[0].status, knxip::status::kNoError);
	ASSERT_EQ(responses[0].channelId, std::uint8_t{1});
	ASSERT_EQ(responses[0].address, IndividualAddress(0x11FF));
	ASSERT_EQ(only<TunnelServer::ClientConnected>(actions),
		(std::vector<TunnelServer::ClientConnected>{{1, IndividualAddress(0x11FF)}}));

	server.onDatagram(kClient2, connectRequest(), kT0);
	responses = sentAs<knxip::ConnectResponse>(server.takeActions());
	ASSERT_EQ(responses[0].channelId, std::uint8_t{2});
	ASSERT_EQ(responses[0].address, IndividualAddress(0x11FE));

	server.onDatagram(kClient2, connectRequest(), kT0);
	responses = sentAs<knxip::ConnectResponse>(server.takeActions());
	ASSERT_EQ(responses[0].status, knxip::status::kNoMoreConnections);
	ASSERT_EQ(server.connectionCount(), std::size_t{2});

	server.onDatagram(kClient, connectRequest(0x80), kT0); // bus monitor layer
	ASSERT_EQ(sentAs<knxip::ConnectResponse>(server.takeActions())[0].status, knxip::status::kTunnellingLayer);
	server.onDatagram(kClient, connectRequest(0, knxip::kDeviceMgmtConnection), kT0);
	ASSERT_EQ(sentAs<knxip::ConnectResponse>(server.takeActions())[0].status, knxip::status::kConnectionType);
}

UNIT_TEST(FR_BUS_2__tunnelServerConnectionStateAndDisconnect)
{
	Connected rig;
	rig.server.onDatagram(kClient, knxip::encode(knxip::ConnectionStateRequest{1, knxip::Hpai::nat()}), kT0);
	ASSERT_EQ(sentAs<knxip::ConnectionStateResponse>(rig.server.takeActions()),
		(std::vector<knxip::ConnectionStateResponse>{{1, 0}}));
	rig.server.onDatagram(kClient, knxip::encode(knxip::ConnectionStateRequest{9, knxip::Hpai::nat()}), kT0);
	ASSERT_EQ(sentAs<knxip::ConnectionStateResponse>(rig.server.takeActions()),
		(std::vector<knxip::ConnectionStateResponse>{{9, knxip::status::kConnectionId}}));

	rig.server.onDatagram(kClient, knxip::encode(knxip::DisconnectRequest{1, knxip::Hpai::nat()}), kT0);
	auto actions = rig.server.takeActions();
	ASSERT_EQ(sentAs<knxip::DisconnectResponse>(actions), (std::vector<knxip::DisconnectResponse>{{1, 0}}));
	ASSERT_EQ(only<TunnelServer::ClientDisconnected>(actions), (std::vector<TunnelServer::ClientDisconnected>{{1}}));
	ASSERT_EQ(rig.server.connectionCount(), std::size_t{0});
}

UNIT_TEST(FR_BUS_2__tunnelServerForwardsRequestToBus)
{
	Connected rig;
	rig.server.onDatagram(kClient, knxip::encode(knxip::TunnellingRequest{1, 0, kGroupWrite}), kT0);
	auto actions = rig.server.takeActions();
	ASSERT_EQ(sentAs<knxip::TunnellingAck>(actions), (std::vector<knxip::TunnellingAck>{{1, 0, 0}}));
	auto requests = sentAs<knxip::TunnellingRequest>(actions);
	ASSERT_EQ(requests.size(), std::size_t{1});
	// L_Data.con with the tunnel's address as source.
	ASSERT_EQ(requests[0], (knxip::TunnellingRequest{1, 0, hex("2e 00 bc e0 11 ff 09 01 01 00 81")}));
	ASSERT_EQ(rig.bus.telegrams().size(), std::size_t{1});
	ASSERT_EQ(rig.bus.telegrams()[0].frame.source, std::uint16_t{0x11FF});

	// Repetition: ACK again, not forwarded twice. Out of sequence: ignored.
	rig.server.onDatagram(kClient, knxip::encode(knxip::TunnellingRequest{1, 0, kGroupWrite}), kT0);
	ASSERT_EQ(sentAs<knxip::TunnellingAck>(rig.server.takeActions()), (std::vector<knxip::TunnellingAck>{{1, 0, 0}}));
	rig.server.onDatagram(kClient, knxip::encode(knxip::TunnellingRequest{1, 7, kGroupWrite}), kT0);
	ASSERT(rig.server.takeActions().empty());
	ASSERT_EQ(rig.bus.telegrams().size(), std::size_t{1});
}

UNIT_TEST(FR_BUS_5__tunnelServerForwardsBusFramesOneAtATime)
{
	Connected rig;
	const auto device = rig.bus.attach(nullptr);
	auto frame = knx::cemi::decode(kGroupWrite).value();
	frame.source = 0x1105;
	rig.bus.transmit(device, frame);
	rig.bus.transmit(device, frame);
	auto requests = sentAs<knxip::TunnellingRequest>(rig.server.takeActions());
	ASSERT_EQ(requests, (std::vector<knxip::TunnellingRequest>{{1, 0, hex("29 00 bc e0 11 05 09 01 01 00 81")}}));
	rig.server.onDatagram(kClient, knxip::encode(knxip::TunnellingAck{1, 0, 0}), kT0);
	requests = sentAs<knxip::TunnellingRequest>(rig.server.takeActions());
	ASSERT_EQ(requests.size(), std::size_t{1});
	ASSERT_EQ(requests[0].sequence, std::uint8_t{1});
}

UNIT_TEST(FR_BUS_2__tunnelServerDropsClientWithoutAcks)
{
	Connected rig;
	const auto device = rig.bus.attach(nullptr);
	rig.bus.transmit(device, knx::cemi::decode(kGroupWrite).value());
	(void)rig.server.takeActions();
	rig.server.tick(kT0 + 999ms);
	ASSERT(rig.server.takeActions().empty());
	rig.server.tick(kT0 + 1s);
	auto requests = sentAs<knxip::TunnellingRequest>(rig.server.takeActions());
	ASSERT_EQ(requests.size(), std::size_t{1});
	ASSERT_EQ(requests[0].sequence, std::uint8_t{0}); // repetition
	rig.server.tick(kT0 + 2s);
	auto actions = rig.server.takeActions();
	ASSERT_EQ(sentAs<knxip::DisconnectRequest>(actions).size(), std::size_t{1});
	ASSERT_EQ(only<TunnelServer::ClientDisconnected>(actions), (std::vector<TunnelServer::ClientDisconnected>{{1}}));
}

UNIT_TEST(FR_BUS_2__tunnelServerHeartbeatTimeout)
{
	Connected rig;
	rig.server.onDatagram(kClient, knxip::encode(knxip::ConnectionStateRequest{1, knxip::Hpai::nat()}), kT0 + 60s);
	rig.server.tick(kT0 + 179s);
	(void)rig.server.takeActions();
	ASSERT_EQ(rig.server.connectionCount(), std::size_t{1});
	rig.server.tick(kT0 + 180s);
	ASSERT_EQ(only<TunnelServer::ClientDisconnected>(rig.server.takeActions()),
		(std::vector<TunnelServer::ClientDisconnected>{{1}}));
	ASSERT_EQ(rig.server.connectionCount(), std::size_t{0});
}

UNIT_TEST(FR_BUS_5__tunnelServerSharesTrafficBetweenClients)
{
	VirtualBus bus;
	TunnelServerConfig config;
	config.tunnelAddresses = {IndividualAddress(0x11FF), IndividualAddress(0x11FE)};
	TunnelServer server(bus, config);
	server.onDatagram(kClient, connectRequest(), kT0);
	server.onDatagram(kClient2, connectRequest(), kT0);
	(void)server.takeActions();
	server.onDatagram(kClient, knxip::encode(knxip::TunnellingRequest{1, 0, kGroupWrite}), kT0);
	auto sends = only<TunnelServer::SendDatagram>(server.takeActions());
	// Client 1: ACK + L_Data.con; client 2: the frame as L_Data.ind.
	std::vector<knxip::TunnellingRequest> toClient2;
	for (const auto& send : sends)
	{
		if (send.to == kClient2)
		{
			toClient2.push_back(*knxipAs<knxip::TunnellingRequest>(send.bytes));
		}
	}
	ASSERT_EQ(toClient2, (std::vector<knxip::TunnellingRequest>{{2, 0, hex("29 00 bc e0 11 ff 09 01 01 00 81")}}));
}
