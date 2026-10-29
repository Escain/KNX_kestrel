#include "KnxTestSupport.hpp"
#include "kestrel/knx/Apdu.hpp"
#include "kestrel/knx/ManagementSession.hpp"

#include <Printers.hpp>
#include <UnitTests.hpp>

using namespace kestrel;
using namespace kestrel::knx;
using namespace std::chrono_literals;
using knxtest::hex;
using knxtest::indication;
using knxtest::kT0;
using knxtest::only;
using Session = ManagementSession;

namespace
{

constexpr std::uint16_t kDevice = 0x1105; // 1.1.5
constexpr std::uint16_t kUs = 0x11FF;     // 1.1.255

/// TPDUs sent by the session, in order (frames to other destinations are not expected).
std::vector<Tpdu> sent(const std::vector<Session::Action>& actions)
{
	std::vector<Tpdu> out;
	for (const auto& send : only<Session::SendFrame>(actions))
	{
		if (auto tpdu = knxtest::tpduOf(send.frame); tpdu && send.frame.destination == kDevice)
		{
			out.push_back(*tpdu);
		}
	}
	return out;
}

cemi::LData fromDevice(const Tpdu& tpdu)
{
	return indication(kDevice, kUs, tpdu);
}

Session newSession()
{
	Session session{IndividualAddress(kDevice)};
	session.setLocalAddress(IndividualAddress(kUs));
	return session;
}

} // namespace

UNIT_TEST(FR_COM_4__managementDeviceDescriptorRead)
{
	auto session = newSession();
	const auto id = session.deviceDescriptorRead(kT0);
	auto actions = session.takeActions();
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::disconnect(), Tpdu::connect(), Tpdu::dataConnected(0, deviceDescriptorRead())}));
	ASSERT_EQ(cemi::encode(std::get<Session::SendFrame>(actions[2]).frame), hex("11 00 b0 60 00 00 11 05 01 43 00"));
	ASSERT(session.connected());

	session.onFrame(fromDevice(Tpdu::ack(0)), kT0 + 50ms);
	ASSERT(session.takeActions().empty());
	session.onFrame(fromDevice(Tpdu::dataConnected(0, deviceDescriptorResponse(0, hex("07 05")))), kT0 + 60ms);
	actions = session.takeActions();
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::ack(0)}));
	ASSERT_EQ(only<Session::Completed>(actions), (std::vector<Session::Completed>{{id, hex("07 05")}}));
	ASSERT_FALSE(session.busy());
}

UNIT_TEST(FR_COM_4__managementClosesStaleConnectionBeforeConnecting)
{
	// A connection left open by an earlier run with our address keeps the device's sequence numbers; the
	// device would take our seq-0 request for a repetition (seen on a real System B actuator).
	auto session = newSession();
	(void)session.deviceDescriptorRead(kT0);
	auto actions = session.takeActions();
	const auto tpdus = sent(actions);
	ASSERT(tpdus.size() >= std::size_t{2});
	ASSERT_EQ(tpdus[0], Tpdu::disconnect());
	ASSERT_EQ(tpdus[1], Tpdu::connect());
}

UNIT_TEST(FR_COM_4__managementResponseBeforeAck)
{
	auto session = newSession();
	const auto id = session.deviceDescriptorRead(kT0);
	(void)session.takeActions();
	session.onFrame(fromDevice(Tpdu::dataConnected(0, deviceDescriptorResponse(0, hex("07 05")))), kT0);
	ASSERT(only<Session::Completed>(session.takeActions()).empty());
	session.onFrame(fromDevice(Tpdu::ack(0)), kT0);
	ASSERT_EQ(only<Session::Completed>(session.takeActions()), (std::vector<Session::Completed>{{id, hex("07 05")}}));
}

UNIT_TEST(FR_COM_4__managementIgnoresForeignFrames)
{
	auto session = newSession();
	(void)session.deviceDescriptorRead(kT0);
	(void)session.takeActions();
	session.onFrame(indication(0x1106, kUs, Tpdu::ack(0)), kT0);    // other device
	session.onFrame(indication(kDevice, 0x1101, Tpdu::ack(0)), kT0); // to someone else
	ASSERT(session.takeActions().empty());
	session.tick(kT0 + 2999ms);
	ASSERT(session.takeActions().empty());
}

UNIT_TEST(FR_COM_4__managementDescriptorTypeMustMatch)
{
	auto session = newSession();
	const auto id = session.deviceDescriptorRead(kT0, 2);
	(void)session.takeActions();
	session.onFrame(fromDevice(Tpdu::ack(0)), kT0);
	// A type-0 descriptor is not the answer to a type-2 read.
	session.onFrame(fromDevice(Tpdu::dataConnected(0, deviceDescriptorResponse(0, hex("07 05")))), kT0);
	auto actions = session.takeActions();
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::ack(0)}));
	ASSERT(only<Session::Completed>(actions).empty());
	// The "unsupported type" answer (0x3F) is.
	session.onFrame(fromDevice(Tpdu::dataConnected(1, deviceDescriptorResponse(0x3F, {}))), kT0);
	auto failed = only<Session::Failed>(session.takeActions());
	ASSERT_EQ(failed.size(), std::size_t{1});
	ASSERT_EQ(failed[0].id, id);
	ASSERT_EQ(failed[0].error.code, ErrorCode::Unsupported);
}

UNIT_TEST(FR_COM_4__managementLateResponseDoesNotCompleteNextRequest)
{
	auto session = newSession();
	const auto first = session.memoryRead(0x4000, 2, kT0);
	const auto second = session.memoryRead(0x5000, 2, kT0);
	(void)session.takeActions();
	session.onFrame(fromDevice(Tpdu::ack(0)), kT0);
	session.tick(kT0 + 3s); // no response: the first read times out, the second goes out
	auto actions = session.takeActions();
	ASSERT_EQ(only<Session::Failed>(actions).size(), std::size_t{1});
	ASSERT_EQ(only<Session::Failed>(actions)[0].id, first);
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::dataConnected(1, memoryRead(0x5000, 2))}));

	// The first read's answer arrives while the second waits for its T_ACK: acknowledged, dropped.
	session.onFrame(fromDevice(Tpdu::dataConnected(0, memoryResponse(0x4000, hex("aa aa")))), kT0 + 3100ms);
	session.onFrame(fromDevice(Tpdu::ack(1)), kT0 + 3200ms);
	actions = session.takeActions();
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::ack(0)}));
	ASSERT(only<Session::Completed>(actions).empty());

	session.onFrame(fromDevice(Tpdu::dataConnected(1, memoryResponse(0x5000, hex("bb bb")))), kT0 + 3300ms);
	ASSERT_EQ(only<Session::Completed>(session.takeActions()), (std::vector<Session::Completed>{{second, hex("bb bb")}}));
}

UNIT_TEST(FR_COM_4__managementWrongAckSequenceDisconnects)
{
	auto session = newSession();
	const auto id = session.deviceDescriptorRead(kT0);
	(void)session.takeActions();
	session.onFrame(fromDevice(Tpdu::ack(5)), kT0);
	auto actions = session.takeActions();
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::disconnect()}));
	auto failed = only<Session::Failed>(actions);
	ASSERT_EQ(failed.size(), std::size_t{1});
	ASSERT_EQ(failed[0].id, id);
	ASSERT_EQ(failed[0].error.code, ErrorCode::ProtocolError);
	ASSERT_EQ(only<Session::Disconnected>(actions),
		(std::vector<Session::Disconnected>{{ManagementDisconnectReason::ProtocolError}}));

	// A T_NAK nobody waits for (connection open, nothing outstanding) disconnects as well.
	auto idle = newSession();
	(void)idle.deviceDescriptorRead(kT0);
	idle.onFrame(fromDevice(Tpdu::ack(0)), kT0);
	idle.onFrame(fromDevice(Tpdu::dataConnected(0, deviceDescriptorResponse(0, hex("07 05")))), kT0);
	(void)idle.takeActions();
	idle.onFrame(fromDevice(Tpdu::nak(1)), kT0);
	ASSERT_EQ(only<Session::Disconnected>(idle.takeActions()),
		(std::vector<Session::Disconnected>{{ManagementDisconnectReason::ProtocolError}}));
}

UNIT_TEST(FR_COM_4__managementRepeatedAckHintsAtDuplicateAddress)
{
	// Captured on a real installation where an IP interface and a bus device both had the target address:
	// T_ACK, response, our T_ACK, then a second T_ACK 0 (from the other device). Style 1 disconnects.
	auto session = newSession();
	const auto first = session.deviceDescriptorRead(kT0);
	(void)session.takeActions();
	session.onFrame(fromDevice(Tpdu::ack(0)), kT0);
	session.onFrame(fromDevice(Tpdu::dataConnected(0, deviceDescriptorResponse(0, hex("00 12")))), kT0 + 10ms);
	ASSERT_EQ(only<Session::Completed>(session.takeActions()), (std::vector<Session::Completed>{{first, hex("00 12")}}));
	const auto second = session.memoryRead(0x4000, 2, kT0 + 20ms);
	(void)session.takeActions();
	session.onFrame(fromDevice(Tpdu::ack(0)), kT0 + 30ms);
	auto actions = session.takeActions();
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::disconnect()}));
	auto failed = only<Session::Failed>(actions);
	ASSERT_EQ(failed.size(), std::size_t{1});
	ASSERT_EQ(failed[0].id, second);
	ASSERT_EQ(failed[0].error.code, ErrorCode::ProtocolError);
	ASSERT(failed[0].error.message.find("more than one device") != std::string::npos);

	// Before any T_ACK on a connection nothing can be repeated: the wrap-around number 15 is a plain mismatch.
	auto fresh = newSession();
	(void)fresh.deviceDescriptorRead(kT0);
	(void)fresh.takeActions();
	fresh.onFrame(fromDevice(Tpdu::ack(15)), kT0);
	failed = only<Session::Failed>(fresh.takeActions());
	ASSERT_EQ(failed.size(), std::size_t{1});
	ASSERT(failed[0].error.message.find("unexpected T_ACK") != std::string::npos);
}

UNIT_TEST(FR_COM_4__managementAckTimeoutRepeatsThreeTimes)
{
	auto session = newSession();
	const auto id = session.deviceDescriptorRead(kT0);
	(void)session.takeActions();
	for (int i = 1; i <= 3; ++i)
	{
		session.tick(kT0 + std::chrono::seconds(3 * i));
		ASSERT_EQ(sent(session.takeActions()), (std::vector<Tpdu>{Tpdu::dataConnected(0, deviceDescriptorRead())}));
	}
	session.tick(kT0 + 12s);
	auto actions = session.takeActions();
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::disconnect()}));
	auto failed = only<Session::Failed>(actions);
	ASSERT_EQ(failed.size(), std::size_t{1});
	ASSERT_EQ(failed[0].id, id);
	ASSERT_EQ(failed[0].error.code, ErrorCode::Timeout);
	ASSERT_EQ(only<Session::Disconnected>(actions),
		(std::vector<Session::Disconnected>{{ManagementDisconnectReason::AckTimeout}}));
	ASSERT_FALSE(session.connected());
}

UNIT_TEST(FR_COM_4__managementNakTriggersRepetition)
{
	auto session = newSession();
	(void)session.deviceDescriptorRead(kT0);
	(void)session.takeActions();
	session.onFrame(fromDevice(Tpdu::nak(0)), kT0 + 10ms);
	ASSERT_EQ(sent(session.takeActions()), (std::vector<Tpdu>{Tpdu::dataConnected(0, deviceDescriptorRead())}));
}

UNIT_TEST(FR_COM_4__managementMemoryReadIsChunked)
{
	auto session = newSession();
	const auto id = session.memoryRead(0x4000, 20, kT0);
	auto actions = session.takeActions();
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::disconnect(), Tpdu::connect(), Tpdu::dataConnected(0, memoryRead(0x4000, 12))}));

	Bytes first(12);
	for (std::size_t i = 0; i < first.size(); ++i)
	{
		first[i] = static_cast<std::uint8_t>(i);
	}
	session.onFrame(fromDevice(Tpdu::ack(0)), kT0);
	session.onFrame(fromDevice(Tpdu::dataConnected(0, memoryResponse(0x4000, first))), kT0);
	actions = session.takeActions();
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::ack(0), Tpdu::dataConnected(1, memoryRead(0x400C, 8))}));

	const Bytes second = hex("0c 0d 0e 0f 10 11 12 13");
	session.onFrame(fromDevice(Tpdu::ack(1)), kT0);
	session.onFrame(fromDevice(Tpdu::dataConnected(1, memoryResponse(0x400C, second))), kT0);
	actions = session.takeActions();
	auto completed = only<Session::Completed>(actions);
	ASSERT_EQ(completed.size(), std::size_t{1});
	ASSERT_EQ(completed[0].id, id);
	Bytes expected = first;
	expected.insert(expected.end(), second.begin(), second.end());
	ASSERT_EQ(completed[0].data, expected);
}

UNIT_TEST(FR_COM_2__managementMemoryWriteIsChunked)
{
	auto session = newSession();
	Bytes data(15, 0xAB);
	const auto id = session.memoryWrite(0x0100, data, kT0);
	auto actions = session.takeActions();
	ASSERT_EQ(sent(actions),
		(std::vector<Tpdu>{Tpdu::disconnect(), Tpdu::connect(), Tpdu::dataConnected(0, memoryWrite(0x0100, Bytes(12, 0xAB)))}));
	session.onFrame(fromDevice(Tpdu::ack(0)), kT0);
	actions = session.takeActions();
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::dataConnected(1, memoryWrite(0x010C, Bytes(3, 0xAB)))}));
	ASSERT(only<Session::Completed>(actions).empty());
	session.onFrame(fromDevice(Tpdu::ack(1)), kT0);
	ASSERT_EQ(only<Session::Completed>(session.takeActions()), (std::vector<Session::Completed>{{id, {}}}));
}

UNIT_TEST(FR_COM_4__managementRejectsBadMemoryRanges)
{
	auto session = newSession();
	const auto empty = session.memoryRead(0x4000, 0, kT0);
	const auto overflow = session.memoryWrite(0xFFF0, Bytes(32, 0), kT0);
	auto failed = only<Session::Failed>(session.takeActions());
	ASSERT_EQ(failed.size(), std::size_t{2});
	ASSERT_EQ(failed[0].id, empty);
	ASSERT_EQ(failed[1].id, overflow);
	ASSERT_EQ(failed[1].error.code, ErrorCode::InvalidArgument);
	ASSERT_FALSE(session.connected()); // nothing was sent
}

UNIT_TEST(FR_COM_4__managementMemoryReadRefused)
{
	auto session = newSession();
	const auto id = session.memoryRead(0x4000, 4, kT0);
	const auto next = session.deviceDescriptorRead(kT0);
	(void)session.takeActions();
	session.onFrame(fromDevice(Tpdu::ack(0)), kT0);
	session.onFrame(fromDevice(Tpdu::dataConnected(0, memoryResponse(0x4000, {}))), kT0);
	auto actions = session.takeActions();
	auto failed = only<Session::Failed>(actions);
	ASSERT_EQ(failed.size(), std::size_t{1});
	ASSERT_EQ(failed[0].id, id);
	ASSERT_EQ(failed[0].error.code, ErrorCode::ProtocolError);
	// The queue continues with the next request on the same connection.
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::ack(0), Tpdu::dataConnected(1, deviceDescriptorRead())}));
	ASSERT(next > id);
}

UNIT_TEST(FR_COM_4__managementDuplicateResponseNotDeliveredTwice)
{
	auto session = newSession();
	(void)session.deviceDescriptorRead(kT0);
	const auto second = session.deviceDescriptorRead(kT0);
	(void)session.takeActions();
	const auto response = fromDevice(Tpdu::dataConnected(0, deviceDescriptorResponse(0, hex("07 05"))));
	session.onFrame(fromDevice(Tpdu::ack(0)), kT0);
	session.onFrame(response, kT0);
	ASSERT_EQ(only<Session::Completed>(session.takeActions()).size(), std::size_t{1});
	// Our ACK got lost; the device repeats: re-ACK, but no second completion.
	session.onFrame(response, kT0);
	auto actions = session.takeActions();
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::ack(0)}));
	ASSERT(only<Session::Completed>(actions).empty());
	// A wrong sequence number gets a NAK.
	session.onFrame(fromDevice(Tpdu::dataConnected(9, deviceDescriptorResponse(0, hex("07 05")))), kT0);
	ASSERT_EQ(sent(session.takeActions()), (std::vector<Tpdu>{Tpdu::nak(9)}));
	(void)second;
}

UNIT_TEST(FR_COM_4__managementResponseTimeout)
{
	auto session = newSession();
	const auto id = session.deviceDescriptorRead(kT0);
	(void)session.takeActions();
	session.onFrame(fromDevice(Tpdu::ack(0)), kT0 + 100ms);
	session.tick(kT0 + 3099ms);
	ASSERT(session.takeActions().empty());
	session.tick(kT0 + 3100ms);
	auto failed = only<Session::Failed>(session.takeActions());
	ASSERT_EQ(failed.size(), std::size_t{1});
	ASSERT_EQ(failed[0].id, id);
	ASSERT_EQ(failed[0].error.code, ErrorCode::Timeout);
}

UNIT_TEST(FR_COM_4__managementPropertyValueRead)
{
	auto session = newSession();
	const auto id = session.propertyValueRead(0, 11, 1, 1, kT0);
	auto actions = session.takeActions();
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::disconnect(), Tpdu::connect(), Tpdu::dataConnected(0, propertyValueRead(0, 11, 1, 1))}));
	session.onFrame(fromDevice(Tpdu::ack(0)), kT0);
	session.onFrame(
		fromDevice(Tpdu::dataConnected(0, propertyValueResponse(0, 11, 1, 1, hex("00 83 00 00 12 34")))), kT0);
	ASSERT_EQ(only<Session::Completed>(session.takeActions()),
		(std::vector<Session::Completed>{{id, hex("00 83 00 00 12 34")}}));

	const auto refused = session.propertyValueRead(0, 99, 1, 1, kT0);
	(void)session.takeActions();
	session.onFrame(fromDevice(Tpdu::ack(1)), kT0);
	session.onFrame(fromDevice(Tpdu::dataConnected(1, propertyValueResponse(0, 99, 0, 1, {}))), kT0);
	auto failed = only<Session::Failed>(session.takeActions());
	ASSERT_EQ(failed.size(), std::size_t{1});
	ASSERT_EQ(failed[0].id, refused);
}

UNIT_TEST(FR_COM_5__managementRestartDisconnects)
{
	auto session = newSession();
	const auto id = session.restart(kT0);
	auto actions = session.takeActions();
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::disconnect(), Tpdu::connect(), Tpdu::dataConnected(0, restart())}));
	session.onFrame(fromDevice(Tpdu::ack(0)), kT0);
	actions = session.takeActions();
	ASSERT_EQ(only<Session::Completed>(actions), (std::vector<Session::Completed>{{id, {}}}));
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::disconnect()}));
	ASSERT_EQ(only<Session::Disconnected>(actions),
		(std::vector<Session::Disconnected>{{ManagementDisconnectReason::DeviceRestart}}));
	ASSERT_FALSE(session.connected());

	// A later request opens a fresh connection with sequence numbers reset.
	(void)session.deviceDescriptorRead(kT0 + 1s);
	ASSERT_EQ(sent(session.takeActions()), (std::vector<Tpdu>{Tpdu::disconnect(), Tpdu::connect(), Tpdu::dataConnected(0, deviceDescriptorRead())}));
}

UNIT_TEST(FR_COM_5__managementRestartWithoutAckCompletes)
{
	// A real device restarted on A_Restart without sending T_ACK; the request must not be repeated.
	auto session = newSession();
	const auto id = session.restart(kT0);
	(void)session.takeActions();
	session.tick(kT0 + 3s);
	const auto actions = session.takeActions();
	ASSERT_EQ(only<Session::Completed>(actions), (std::vector<Session::Completed>{{id, {}}}));
	ASSERT(only<Session::Failed>(actions).empty());
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::disconnect()}));
	ASSERT(!session.connected());
}

UNIT_TEST(FR_COM_4__managementIdleDisconnect)
{
	auto session = newSession();
	(void)session.deviceDescriptorRead(kT0);
	(void)session.takeActions();
	session.onFrame(fromDevice(Tpdu::ack(0)), kT0);
	session.onFrame(fromDevice(Tpdu::dataConnected(0, deviceDescriptorResponse(0, hex("07 05")))), kT0 + 1s);
	(void)session.takeActions();
	session.tick(kT0 + 6999ms);
	ASSERT(session.takeActions().empty());
	session.tick(kT0 + 7s);
	auto actions = session.takeActions();
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::disconnect()}));
	ASSERT_EQ(only<Session::Disconnected>(actions),
		(std::vector<Session::Disconnected>{{ManagementDisconnectReason::IdleTimeout}}));
}

UNIT_TEST(FR_COM_4__managementRemoteDisconnectFailsQueue)
{
	auto session = newSession();
	const auto a = session.deviceDescriptorRead(kT0);
	const auto b = session.memoryRead(0, 1, kT0);
	(void)session.takeActions();
	session.onFrame(fromDevice(Tpdu::disconnect()), kT0 + 600ms); // after the echo window
	auto actions = session.takeActions();
	auto failed = only<Session::Failed>(actions);
	ASSERT_EQ(failed.size(), std::size_t{2});
	ASSERT_EQ(failed[0].id, a);
	ASSERT_EQ(failed[1].id, b);
	ASSERT_EQ(only<Session::Disconnected>(actions),
		(std::vector<Session::Disconnected>{{ManagementDisconnectReason::RemoteDisconnect}}));
	ASSERT(sent(actions).empty()); // no T_Disconnect back
}

UNIT_TEST(FR_COM_4__managementCloseCancels)
{
	auto session = newSession();
	const auto id = session.deviceDescriptorRead(kT0);
	(void)session.takeActions();
	session.close(kT0);
	auto actions = session.takeActions();
	auto failed = only<Session::Failed>(actions);
	ASSERT_EQ(failed.size(), std::size_t{1});
	ASSERT_EQ(failed[0].id, id);
	ASSERT_EQ(failed[0].error.code, ErrorCode::Cancelled);
	ASSERT_EQ(sent(actions), (std::vector<Tpdu>{Tpdu::disconnect()}));
	ASSERT_EQ(only<Session::Disconnected>(actions),
		(std::vector<Session::Disconnected>{{ManagementDisconnectReason::UserClose}}));
}

UNIT_TEST(FR_COM_4__managementIgnoresTheEchoOfItsOwnDisconnect)
{
	// A Merten push button answers the T_Disconnect sent before T_Connect with its own; the bus repeated it after
	// our T_Connect, and the device then acknowledged the request.
	auto session = newSession();
	const auto id = session.deviceDescriptorRead(kT0);
	(void)session.takeActions();
	session.onFrame(fromDevice(Tpdu::disconnect()), kT0 + 20ms);
	session.onFrame(fromDevice(Tpdu::disconnect()), kT0 + 40ms);
	session.tick(kT0 + 100ms);
	ASSERT(session.takeActions().empty());
	session.onFrame(fromDevice(Tpdu::ack(0)), kT0 + 120ms);
	session.onFrame(fromDevice(Tpdu::dataConnected(0, deviceDescriptorResponse(0, hex("07 05")))), kT0 + 130ms);
	session.tick(kT0 + 700ms);
	const auto actions = session.takeActions();
	ASSERT_EQ(only<Session::Completed>(actions), (std::vector<Session::Completed>{{id, hex("07 05")}}));
	ASSERT(only<Session::Disconnected>(actions).empty());
}

UNIT_TEST(FR_COM_4__managementEarlyDisconnectWithoutAckClosesAfterTheEchoWindow)
{
	// A device busy with another partner refuses the connection the same way, but never acknowledges.
	auto session = newSession();
	const auto id = session.deviceDescriptorRead(kT0);
	(void)session.takeActions();
	session.onFrame(fromDevice(Tpdu::disconnect()), kT0 + 20ms);
	session.tick(kT0 + 400ms);
	ASSERT(session.takeActions().empty());
	session.tick(kT0 + 500ms);
	const auto actions = session.takeActions();
	ASSERT_EQ(only<Session::Failed>(actions).size(), std::size_t{1});
	ASSERT_EQ(only<Session::Failed>(actions)[0].id, id);
	ASSERT_EQ(only<Session::Disconnected>(actions),
		(std::vector<Session::Disconnected>{{ManagementDisconnectReason::RemoteDisconnect}}));
}
