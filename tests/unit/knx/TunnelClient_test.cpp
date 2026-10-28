#include "KnxTestSupport.hpp"
#include "kestrel/knx/Apdu.hpp"
#include "kestrel/knx/TunnelClient.hpp"

#include <Printers.hpp>
#include <UnitTests.hpp>

using namespace kestrel;
using namespace kestrel::knx;
using namespace std::chrono_literals;
using knxtest::hex;
using knxtest::kT0;
using knxtest::knxipAs;
using knxtest::only;
using Action = TunnelClient::Action;

namespace
{

const Bytes kGroupWrite = hex("11 00 bc e0 00 00 08 01 01 00 81");

Bytes connectResponse(std::uint8_t channel, std::uint8_t status = knxip::status::kNoError)
{
	knxip::ConnectResponse response;
	response.channelId = channel;
	response.status = status;
	response.dataEndpoint = knxip::Hpai{knxip::HostProtocol::Udp, {{192, 168, 1, 10}, 3671}};
	response.address = IndividualAddress(0x11FF);
	return knxip::encode(response);
}

Bytes serverRequest(std::uint8_t seq, const cemi::LData& frame, std::uint8_t channel = 7)
{
	return knxip::encode(knxip::TunnellingRequest{channel, seq, cemi::encode(frame)});
}

cemi::LData groupWriteFrame()
{
	return makeGroupFrame(GroupAddress(0x0801), groupValueWriteShort(1));
}

cemi::LData confirmation(bool ok = true)
{
	auto frame = groupWriteFrame();
	frame.messageCode = cemi::MessageCode::LDataCon;
	frame.source = 0x11FF;
	frame.confirmError = !ok;
	return frame;
}

cemi::LData indication()
{
	auto frame = makeGroupFrame(GroupAddress(0x0802), groupValueWrite(hex("0c 33")));
	frame.messageCode = cemi::MessageCode::LDataInd;
	frame.source = 0x1105;
	return frame;
}

/// A client connected on channel 7 at kT0, with the actions consumed.
TunnelClient connectedClient(TunnelClientConfig config = {})
{
	TunnelClient client(config);
	client.connect(kT0);
	client.onDatagram(connectResponse(7), kT0);
	(void)client.takeActions();
	return client;
}

/// The SendData payloads decoded as TUNNELLING_REQUESTs.
std::vector<knxip::TunnellingRequest> tunnellingRequests(const std::vector<Action>& actions)
{
	std::vector<knxip::TunnellingRequest> out;
	for (const auto& data : only<TunnelClient::SendData>(actions))
	{
		if (auto request = knxipAs<knxip::TunnellingRequest>(data.bytes))
		{
			out.push_back(*request);
		}
	}
	return out;
}

std::vector<knxip::TunnellingAck> tunnellingAcks(const std::vector<Action>& actions)
{
	std::vector<knxip::TunnellingAck> out;
	for (const auto& data : only<TunnelClient::SendData>(actions))
	{
		if (auto ack = knxipAs<knxip::TunnellingAck>(data.bytes))
		{
			out.push_back(*ack);
		}
	}
	return out;
}

template <typename T>
std::vector<T> controlFrames(const std::vector<Action>& actions)
{
	std::vector<T> out;
	for (const auto& control : only<TunnelClient::SendControl>(actions))
	{
		if (auto frame = knxipAs<T>(control.bytes))
		{
			out.push_back(*frame);
		}
	}
	return out;
}

} // namespace

UNIT_TEST(FR_BUS_2__tunnelConnects)
{
	TunnelClient client;
	ASSERT_EQ(client.state(), TunnelState::Disconnected);
	client.connect(kT0);
	ASSERT_EQ(client.state(), TunnelState::Connecting);
	auto actions = client.takeActions();
	ASSERT_EQ(actions.size(), std::size_t{1});
	ASSERT_EQ(std::get<TunnelClient::SendControl>(actions[0]).bytes,
		hex("06 10 02 05 00 1a 08 01 00 00 00 00 00 00 08 01 00 00 00 00 00 00 04 04 02 00"));

	client.onDatagram(connectResponse(7), kT0 + 50ms);
	actions = client.takeActions();
	ASSERT_EQ(actions, (std::vector<Action>{TunnelClient::Connected{7, IndividualAddress(0x11FF)}}));
	ASSERT_EQ(client.state(), TunnelState::Connected);
	ASSERT_EQ(client.channel(), std::uint8_t{7});
	ASSERT_EQ(client.address(), IndividualAddress(0x11FF));
}

UNIT_TEST(FR_BUS_2__tunnelConnectTimeout)
{
	TunnelClient client;
	client.connect(kT0);
	(void)client.takeActions();
	client.tick(kT0 + 9999ms);
	ASSERT(client.takeActions().empty());
	client.tick(kT0 + 10s);
	ASSERT_EQ(client.takeActions(), (std::vector<Action>{TunnelClient::Disconnected{DisconnectReason::ConnectTimeout, 0}}));
	ASSERT_EQ(client.state(), TunnelState::Disconnected);
}

UNIT_TEST(FR_BUS_2__tunnelConnectRejected)
{
	TunnelClient client;
	client.connect(kT0);
	(void)client.takeActions();
	client.onDatagram(hex("06 10 02 06 00 08 00 24"), kT0 + 10ms);
	ASSERT_EQ(client.takeActions(),
		(std::vector<Action>{TunnelClient::Disconnected{DisconnectReason::ConnectRejected, 0x24}}));
}

UNIT_TEST(FR_BUS_2__tunnelSendWaitsForAckThenConfirmation)
{
	auto client = connectedClient();
	ASSERT(client.send(groupWriteFrame(), kT0).has_value());
	ASSERT(client.send(groupWriteFrame(), kT0).has_value());
	auto actions = client.takeActions();
	auto requests = tunnellingRequests(actions);
	ASSERT_EQ(requests.size(), std::size_t{1}); // one outstanding frame
	ASSERT_EQ(requests[0], (knxip::TunnellingRequest{7, 0, kGroupWrite}));
	ASSERT_EQ(client.pendingFrames(), std::size_t{2});

	client.onDatagram(knxip::encode(knxip::TunnellingAck{7, 0, 0}), kT0 + 10ms);
	ASSERT(client.takeActions().empty()); // still waiting for L_Data.con

	client.onDatagram(serverRequest(0, confirmation()), kT0 + 20ms);
	actions = client.takeActions();
	ASSERT_EQ(tunnellingAcks(actions), (std::vector<knxip::TunnellingAck>{{7, 0, 0}}));
	ASSERT_EQ(only<TunnelClient::SendConfirmed>(actions), (std::vector<TunnelClient::SendConfirmed>{{true}}));
	requests = tunnellingRequests(actions);
	ASSERT_EQ(requests.size(), std::size_t{1});
	ASSERT_EQ(requests[0].sequence, std::uint8_t{1});
}

UNIT_TEST(FR_BUS_2__tunnelNegativeConfirmation)
{
	auto client = connectedClient();
	ASSERT(client.send(groupWriteFrame(), kT0).has_value());
	(void)client.takeActions();
	client.onDatagram(knxip::encode(knxip::TunnellingAck{7, 0, 0}), kT0);
	client.onDatagram(serverRequest(0, confirmation(false)), kT0);
	ASSERT_EQ(only<TunnelClient::SendConfirmed>(client.takeActions()),
		(std::vector<TunnelClient::SendConfirmed>{{false}}));
}

UNIT_TEST(FR_BUS_2__tunnelAckTimeoutRetriesOnceThenDrops)
{
	auto client = connectedClient();
	ASSERT(client.send(groupWriteFrame(), kT0).has_value());
	(void)client.takeActions();

	client.tick(kT0 + 999ms);
	ASSERT(client.takeActions().empty());
	client.tick(kT0 + 1s);
	auto actions = client.takeActions();
	auto requests = tunnellingRequests(actions);
	ASSERT_EQ(requests.size(), std::size_t{1});
	ASSERT_EQ(requests[0].sequence, std::uint8_t{0}); // same sequence number on repetition

	client.tick(kT0 + 2s);
	actions = client.takeActions();
	ASSERT_EQ(controlFrames<knxip::DisconnectRequest>(actions).size(), std::size_t{1});
	ASSERT_EQ(only<TunnelClient::SendConfirmed>(actions), (std::vector<TunnelClient::SendConfirmed>{{false}}));
	ASSERT_EQ(only<TunnelClient::Disconnected>(actions),
		(std::vector<TunnelClient::Disconnected>{{DisconnectReason::AckTimeout, 0}}));
	ASSERT_EQ(client.state(), TunnelState::Disconnected);
}

UNIT_TEST(FR_BUS_2__tunnelLateAckAfterRetryIsAccepted)
{
	auto client = connectedClient();
	ASSERT(client.send(groupWriteFrame(), kT0).has_value());
	client.tick(kT0 + 1s);
	(void)client.takeActions();
	client.onDatagram(knxip::encode(knxip::TunnellingAck{7, 0, 0}), kT0 + 1500ms);
	client.tick(kT0 + 2500ms);
	ASSERT(client.takeActions().empty());
	ASSERT_EQ(client.state(), TunnelState::Connected);
}

UNIT_TEST(FR_BUS_2__tunnelConfirmationTimeout)
{
	auto client = connectedClient();
	ASSERT(client.send(groupWriteFrame(), kT0).has_value());
	ASSERT(client.send(groupWriteFrame(), kT0).has_value());
	(void)client.takeActions();
	client.onDatagram(knxip::encode(knxip::TunnellingAck{7, 0, 0}), kT0 + 100ms);
	client.tick(kT0 + 3099ms);
	ASSERT(client.takeActions().empty());
	client.tick(kT0 + 3100ms);
	auto actions = client.takeActions();
	ASSERT_EQ(only<TunnelClient::SendConfirmed>(actions), (std::vector<TunnelClient::SendConfirmed>{{false}}));
	auto requests = tunnellingRequests(actions);
	ASSERT_EQ(requests.size(), std::size_t{1});
	ASSERT_EQ(requests[0].sequence, std::uint8_t{1});
}

UNIT_TEST(FR_BUS_5__tunnelInboundSequenceAndDuplicates)
{
	auto client = connectedClient();
	client.onDatagram(serverRequest(0, indication()), kT0);
	auto actions = client.takeActions();
	ASSERT_EQ(tunnellingAcks(actions), (std::vector<knxip::TunnellingAck>{{7, 0, 0}}));
	ASSERT_EQ(only<TunnelClient::FrameReceived>(actions), (std::vector<TunnelClient::FrameReceived>{{indication()}}));

	// Duplicate (our ACK was lost): ACK again, do not deliver.
	client.onDatagram(serverRequest(0, indication()), kT0);
	actions = client.takeActions();
	ASSERT_EQ(tunnellingAcks(actions), (std::vector<knxip::TunnellingAck>{{7, 0, 0}}));
	ASSERT(only<TunnelClient::FrameReceived>(actions).empty());

	// Out of sequence: dropped silently.
	client.onDatagram(serverRequest(5, indication()), kT0);
	ASSERT(client.takeActions().empty());
	// Wrong channel: dropped.
	client.onDatagram(serverRequest(1, indication(), 9), kT0);
	ASSERT(client.takeActions().empty());

	client.onDatagram(serverRequest(1, indication()), kT0);
	ASSERT_EQ(only<TunnelClient::FrameReceived>(client.takeActions()).size(), std::size_t{1});
}

UNIT_TEST(FR_BUS_2__tunnelInboundSequenceWraps)
{
	auto client = connectedClient();
	for (unsigned i = 0; i < 300; ++i)
	{
		client.onDatagram(serverRequest(static_cast<std::uint8_t>(i & 0xFF), indication()), kT0);
	}
	ASSERT_EQ(only<TunnelClient::FrameReceived>(client.takeActions()).size(), std::size_t{300});
}

UNIT_TEST(FR_BUS_2__tunnelHeartbeat)
{
	auto client = connectedClient();
	client.tick(kT0 + 59s);
	ASSERT(client.takeActions().empty());
	client.tick(kT0 + 60s);
	auto requests = controlFrames<knxip::ConnectionStateRequest>(client.takeActions());
	ASSERT_EQ(requests, (std::vector<knxip::ConnectionStateRequest>{{7, knxip::Hpai::nat()}}));

	client.onDatagram(knxip::encode(knxip::ConnectionStateResponse{7, 0}), kT0 + 60100ms);
	client.tick(kT0 + 119s);
	ASSERT(client.takeActions().empty());
	client.tick(kT0 + 120100ms);
	ASSERT_EQ(controlFrames<knxip::ConnectionStateRequest>(client.takeActions()).size(), std::size_t{1});
}

UNIT_TEST(FR_BUS_2__tunnelHeartbeatLostAfterFourRequests)
{
	// 3/8/2 §5.4: one CONNECTIONSTATE_REQUEST plus three repetitions, 10 s each.
	auto client = connectedClient();
	for (int request = 0; request < 4; ++request)
	{
		client.tick(kT0 + 60s + std::chrono::seconds(10 * request));
		auto actions = client.takeActions();
		ASSERT_EQ(controlFrames<knxip::ConnectionStateRequest>(actions).size(), std::size_t{1});
		ASSERT(only<TunnelClient::Disconnected>(actions).empty());
	}
	client.tick(kT0 + 99s);
	ASSERT(client.takeActions().empty());
	client.tick(kT0 + 100s); // fourth timeout → lost
	auto actions = client.takeActions();
	ASSERT_EQ(controlFrames<knxip::DisconnectRequest>(actions).size(), std::size_t{1});
	ASSERT_EQ(only<TunnelClient::Disconnected>(actions),
		(std::vector<TunnelClient::Disconnected>{{DisconnectReason::HeartbeatLost, 0}}));
}

UNIT_TEST(FR_BUS_2__tunnelHeartbeatErrorStatusDrops)
{
	auto client = connectedClient();
	client.tick(kT0 + 60s);
	(void)client.takeActions();
	client.onDatagram(knxip::encode(knxip::ConnectionStateResponse{7, knxip::status::kConnectionId}), kT0 + 60s);
	auto actions = client.takeActions();
	ASSERT_EQ(only<TunnelClient::Disconnected>(actions),
		(std::vector<TunnelClient::Disconnected>{{DisconnectReason::HeartbeatLost, knxip::status::kConnectionId}}));
	ASSERT(controlFrames<knxip::DisconnectRequest>(actions).empty()); // the server forgot the channel already
}

UNIT_TEST(FR_BUS_2__tunnelHeartbeatKnxErrorReleasesChannel)
{
	for (const auto code : {knxip::status::kKnxConnection, knxip::status::kDataConnection})
	{
		auto client = connectedClient();
		client.tick(kT0 + 60s);
		(void)client.takeActions();
		client.onDatagram(knxip::encode(knxip::ConnectionStateResponse{7, code}), kT0 + 60s);
		auto actions = client.takeActions();
		ASSERT_EQ(controlFrames<knxip::DisconnectRequest>(actions),
			(std::vector<knxip::DisconnectRequest>{{7, knxip::Hpai::nat()}}));
		ASSERT_EQ(only<TunnelClient::Disconnected>(actions),
			(std::vector<TunnelClient::Disconnected>{{DisconnectReason::HeartbeatLost, code}}));
	}
}

UNIT_TEST(FR_BUS_2__tunnelLateConfirmationDoesNotShiftFrames)
{
	auto client = connectedClient();
	auto frameA = groupWriteFrame();
	auto frameB = makeGroupFrame(GroupAddress(0x0802), groupValueWriteShort(0));
	ASSERT(client.send(frameA, kT0).has_value());
	ASSERT(client.send(frameB, kT0).has_value());
	(void)client.takeActions();
	client.onDatagram(knxip::encode(knxip::TunnellingAck{7, 0, 0}), kT0);
	client.tick(kT0 + 3s); // A's con is late: A fails, B goes out
	ASSERT_EQ(only<TunnelClient::SendConfirmed>(client.takeActions()), (std::vector<TunnelClient::SendConfirmed>{{false}}));
	client.onDatagram(knxip::encode(knxip::TunnellingAck{7, 1, 0}), kT0 + 3100ms);

	client.onDatagram(serverRequest(0, confirmation()), kT0 + 3200ms); // A's con, finally
	auto actions = client.takeActions();
	ASSERT_EQ(tunnellingAcks(actions), (std::vector<knxip::TunnellingAck>{{7, 0, 0}}));
	ASSERT(only<TunnelClient::SendConfirmed>(actions).empty()); // not taken for B's

	auto conB = frameB;
	conB.messageCode = cemi::MessageCode::LDataCon;
	conB.source = 0x11FF;
	client.onDatagram(serverRequest(1, conB), kT0 + 3300ms);
	ASSERT_EQ(only<TunnelClient::SendConfirmed>(client.takeActions()), (std::vector<TunnelClient::SendConfirmed>{{true}}));
}

UNIT_TEST(FR_BUS_2__tunnelAbandonedConnectReleasesServerSlot)
{
	TunnelClient client;
	client.connect(kT0);
	(void)client.takeActions();
	client.disconnect(kT0 + 1s);
	auto actions = client.takeActions();
	ASSERT(only<TunnelClient::SendControl>(actions).empty()); // no channel yet
	ASSERT_EQ(only<TunnelClient::Disconnected>(actions),
		(std::vector<TunnelClient::Disconnected>{{DisconnectReason::UserRequest, 0}}));

	client.onDatagram(connectResponse(7), kT0 + 2s); // the server allocated a slot anyway
	actions = client.takeActions();
	ASSERT_EQ(controlFrames<knxip::DisconnectRequest>(actions), (std::vector<knxip::DisconnectRequest>{{7, knxip::Hpai::nat()}}));
	ASSERT(only<TunnelClient::Connected>(actions).empty());
	ASSERT_EQ(client.state(), TunnelState::Disconnected);
	client.onDatagram(connectResponse(7), kT0 + 3s); // handled once
	ASSERT(client.takeActions().empty());

	// Same after a connect timeout.
	client.connect(kT0 + 10s);
	client.tick(kT0 + 20s);
	(void)client.takeActions();
	client.onDatagram(connectResponse(9), kT0 + 21s);
	ASSERT_EQ(controlFrames<knxip::DisconnectRequest>(client.takeActions()),
		(std::vector<knxip::DisconnectRequest>{{9, knxip::Hpai::nat()}}));
}

UNIT_TEST(FR_BUS_2__tunnelServerDisconnect)
{
	auto client = connectedClient();
	ASSERT(client.send(groupWriteFrame(), kT0).has_value());
	(void)client.takeActions();
	client.onDatagram(knxip::encode(knxip::DisconnectRequest{7, knxip::Hpai::nat()}), kT0);
	auto actions = client.takeActions();
	ASSERT_EQ(controlFrames<knxip::DisconnectResponse>(actions), (std::vector<knxip::DisconnectResponse>{{7, 0}}));
	ASSERT_EQ(only<TunnelClient::SendConfirmed>(actions), (std::vector<TunnelClient::SendConfirmed>{{false}}));
	ASSERT_EQ(only<TunnelClient::Disconnected>(actions),
		(std::vector<TunnelClient::Disconnected>{{DisconnectReason::ServerDisconnect, 0}}));
}

UNIT_TEST(FR_BUS_2__tunnelUserDisconnectAndSendWhileDisconnected)
{
	TunnelClient idle;
	ASSERT_FALSE(idle.send(groupWriteFrame(), kT0).has_value());

	auto client = connectedClient();
	client.disconnect(kT0);
	auto actions = client.takeActions();
	ASSERT_EQ(controlFrames<knxip::DisconnectRequest>(actions), (std::vector<knxip::DisconnectRequest>{{7, knxip::Hpai::nat()}}));
	ASSERT_EQ(only<TunnelClient::Disconnected>(actions),
		(std::vector<TunnelClient::Disconnected>{{DisconnectReason::UserRequest, 0}}));
	ASSERT_FALSE(client.send(groupWriteFrame(), kT0).has_value());

	// Reconnecting starts from scratch.
	client.connect(kT0 + 1s);
	ASSERT_EQ(client.state(), TunnelState::Connecting);
}

UNIT_TEST(FR_BUS_2__tunnelIgnoresGarbageAndForeignFrames)
{
	auto client = connectedClient();
	client.onDatagram(hex("00 01 02"), kT0);
	client.onDatagram(knxip::encode(knxip::DisconnectRequest{3, knxip::Hpai::nat()}), kT0);
	client.onDatagram(knxip::encode(knxip::TunnellingAck{7, 4, 0}), kT0);
	client.onDatagram(connectResponse(8), kT0);
	ASSERT(client.takeActions().empty());
	ASSERT_EQ(client.state(), TunnelState::Connected);
}
