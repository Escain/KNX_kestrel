// End-to-end, in-process: TunnelClient ⇄ TunnelServer ⇄ VirtualBus ⇄ VirtualDevice (NFR-5).

#include "SimTestSupport.hpp"
#include "kestrel/knx/IaProgrammer.hpp"
#include "kestrel/knx/ManagementSession.hpp"

#include <Printers.hpp>
#include <UnitTests.hpp>

using namespace kestrel;
using namespace kestrel::sim;
using namespace std::chrono_literals;
using knx::IndividualAddress;
using knx::TunnelClient;
using simtest::hex;
using simtest::only;

namespace
{

const knx::GroupAddress kSwitch(0x0901);      // 1/1/1
const knx::GroupAddress kTemperature(0x0902); // 1/1/2

} // namespace

UNIT_TEST(FR_BUS_2__tunnelConnectsToSimulator)
{
	simtest::TunnelRig rig;
	rig.connect();
	ASSERT_EQ(rig.client.state(), knx::TunnelState::Connected);
	ASSERT_EQ(rig.client.address(), IndividualAddress(0x11FF));
	ASSERT_EQ(only<TunnelClient::Connected>(rig.events),
		(std::vector<TunnelClient::Connected>{{1, IndividualAddress(0x11FF)}}));
	ASSERT_EQ(rig.server.connectionCount(), std::size_t{1});

	rig.client.disconnect(rig.now);
	rig.pump();
	ASSERT_EQ(rig.server.connectionCount(), std::size_t{0});
}

UNIT_TEST(FR_BUS_6__groupWriteAndReadThroughTunnel)
{
	simtest::TunnelRig rig;
	VirtualDevice device(rig.bus, simtest::switchConfig());
	rig.connect();

	rig.send(knx::makeGroupFrame(kSwitch, knx::groupValueWriteShort(1)));
	ASSERT_EQ(*device.groupValue(kSwitch), hex("01"));
	ASSERT_EQ(only<TunnelClient::SendConfirmed>(rig.events), (std::vector<TunnelClient::SendConfirmed>{{true}}));
	ASSERT_EQ(rig.bus.telegrams().back().frame.source, std::uint16_t{0x11FF});

	rig.send(knx::makeGroupFrame(kTemperature, knx::groupValueRead()));
	ASSERT_EQ(rig.received.size(), std::size_t{1});
	ASSERT_EQ(rig.received[0].sourceAddress(), IndividualAddress(0xFFFF));
	auto tpdu = knx::decodeTpdu(rig.received[0]);
	ASSERT(tpdu.has_value());
	ASSERT_EQ(tpdu->apdu, knx::groupValueResponse(hex("0c 33")));

	// A value changed by the device application reaches the client as L_Data.ind.
	ASSERT(device.setGroupValue(kSwitch, hex("00")).has_value());
	rig.pump();
	ASSERT_EQ(rig.received.size(), std::size_t{2});
	ASSERT_EQ(knx::decodeTpdu(rig.received[1])->apdu, knx::groupValueWriteShort(0));
}

UNIT_TEST(FR_COM_1__programIndividualAddressThroughTunnel)
{
	simtest::TunnelRig rig;
	VirtualDevice device(rig.bus, simtest::switchConfig(false));
	rig.connect();
	device.pressProgButton();
	ASSERT(device.progMode());

	knx::IaProgrammer programmer;
	std::vector<knx::IaProgrammer::Action> results;
	programmer.start(IndividualAddress(0x1105), rig.now);
	rig.run(programmer, results, [&] { return programmer.finished(); }, {&device});

	ASSERT_EQ(only<knx::IaProgrammer::Finished>(results),
		(std::vector<knx::IaProgrammer::Finished>{{knx::IaProgrammerFailure::None, IndividualAddress(0xFFFF)}}));
	ASSERT_EQ(device.address(), IndividualAddress(0x1105));
	ASSERT_FALSE(device.progMode());
	ASSERT_EQ(device.restartCount(), std::uint32_t{1});
	ASSERT_EQ(rig.client.state(), knx::TunnelState::Connected);
}

UNIT_TEST(FR_COM_1__programmingRefusesAddressInUse)
{
	simtest::TunnelRig rig;
	VirtualDevice existing(rig.bus, [] { auto c = simtest::switchConfig(); c.address = IndividualAddress(0x1105); return c; }());
	VirtualDevice fresh(rig.bus, simtest::switchConfig(true));
	rig.connect();

	knx::IaProgrammer programmer;
	std::vector<knx::IaProgrammer::Action> results;
	programmer.start(IndividualAddress(0x1105), rig.now);
	rig.run(programmer, results, [&] { return programmer.finished(); }, {&existing, &fresh});

	ASSERT_EQ(only<knx::IaProgrammer::Finished>(results),
		(std::vector<knx::IaProgrammer::Finished>{{knx::IaProgrammerFailure::AddressInUse, IndividualAddress(0xFFFF)}}));
	ASSERT_EQ(fresh.address(), IndividualAddress(0xFFFF));
	ASSERT(fresh.progMode());
	ASSERT_FALSE(existing.connectionPartner().has_value()); // our check connection was closed
}

UNIT_TEST(FR_COM_4__deviceInfoAndMemoryThroughTunnel)
{
	simtest::TunnelRig rig;
	VirtualDevice device(rig.bus, [] { auto c = simtest::switchConfig(); c.address = IndividualAddress(0x1105); return c; }());
	rig.connect();

	knx::ManagementSession session{device.address()};
	session.setLocalAddress(rig.client.address());
	std::vector<knx::ManagementSession::Action> results;
	const Bytes image = hex("01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10 11 12 13 14");
	const auto descriptor = session.deviceDescriptorRead(rig.now);
	const auto write = session.memoryWrite(0x4000, image, rig.now);
	const auto read = session.memoryRead(0x4000, image.size(), rig.now);
	const auto serial = session.propertyValueRead(0, pid::kSerialNumber, 1, 1, rig.now);
	rig.run(session, results, [&] { return !session.busy(); }, {&device});

	ASSERT_EQ(only<knx::ManagementSession::Completed>(results),
		(std::vector<knx::ManagementSession::Completed>{{descriptor, hex("07 05")}, {write, {}}, {read, image},
			{serial, hex("00 83 00 00 12 34")}}));
	ASSERT(only<knx::ManagementSession::Failed>(results).empty());
	ASSERT_EQ(device.readMemory(0x4000, image.size()), image);

	// Left alone, the session closes the transport connection after 6 s.
	rig.run(session, results, [&] { return !session.connected(); }, {&device});
	ASSERT_EQ(only<knx::ManagementSession::Disconnected>(results),
		(std::vector<knx::ManagementSession::Disconnected>{{knx::ManagementDisconnectReason::IdleTimeout}}));
	ASSERT_FALSE(device.connectionPartner().has_value());
}

UNIT_TEST(FR_BUS_5__otherClientSeesTunnelledFrames)
{
	// A group monitor on a second tunnel must see what the first client writes.
	VirtualBus bus;
	TunnelServerConfig config;
	config.tunnelAddresses = {IndividualAddress(0x11FF), IndividualAddress(0x11FE)};
	TunnelServer server(bus, config);
	VirtualDevice device(bus, simtest::switchConfig());
	TunnelClient writer;
	TunnelClient monitor;
	const knx::knxip::IpEndpoint writerEndpoint{{127, 0, 0, 1}, 50001};
	const knx::knxip::IpEndpoint monitorEndpoint{{127, 0, 0, 1}, 50002};
	const auto now = simtest::kT0;
	std::vector<knx::cemi::LData> writerFrames;
	std::vector<knx::cemi::LData> monitorFrames;

	auto pump = [&]
	{
		for (bool moved = true; moved;)
		{
			moved = false;
			for (auto* client : {&writer, &monitor})
			{
				const auto& from = client == &writer ? writerEndpoint : monitorEndpoint;
				auto& frames = client == &writer ? writerFrames : monitorFrames;
				for (auto& action : client->takeActions())
				{
					if (auto* control = std::get_if<TunnelClient::SendControl>(&action))
					{
						server.onDatagram(from, control->bytes, now);
						moved = true;
					}
					else if (auto* data = std::get_if<TunnelClient::SendData>(&action))
					{
						server.onDatagram(from, data->bytes, now);
						moved = true;
					}
					else if (auto* frame = std::get_if<TunnelClient::FrameReceived>(&action))
					{
						frames.push_back(frame->frame);
					}
				}
			}
			for (auto& action : server.takeActions())
			{
				if (auto* send = std::get_if<TunnelServer::SendDatagram>(&action))
				{
					(send->to == writerEndpoint ? writer : monitor).onDatagram(send->bytes, now);
					moved = true;
				}
			}
		}
	};

	writer.connect(now);
	monitor.connect(now);
	pump();
	ASSERT_EQ(writer.state(), knx::TunnelState::Connected);
	ASSERT_EQ(monitor.state(), knx::TunnelState::Connected);

	ASSERT(writer.send(knx::makeGroupFrame(kSwitch, knx::groupValueWriteShort(1)), now).has_value());
	pump();
	ASSERT_EQ(*device.groupValue(kSwitch), hex("01"));
	ASSERT_EQ(monitorFrames.size(), std::size_t{1});
	ASSERT_EQ(monitorFrames[0].sourceAddress(), IndividualAddress(0x11FF));
	ASSERT_EQ(knx::decodeTpdu(monitorFrames[0])->apdu, knx::groupValueWriteShort(1));
	ASSERT(writerFrames.empty()); // the sender gets L_Data.con, not its own frame back

	ASSERT(monitor.send(knx::makeGroupFrame(kTemperature, knx::groupValueRead()), now).has_value());
	pump();
	// The writer sees the monitor's read and the device's response.
	ASSERT_EQ(writerFrames.size(), std::size_t{2});
	ASSERT_EQ(writerFrames[0].sourceAddress(), IndividualAddress(0x11FE));
	ASSERT_EQ(knx::decodeTpdu(writerFrames[1])->apdu, knx::groupValueResponse(hex("0c 33")));
}

UNIT_TEST(FR_COM_1__twoFactoryDevicesThroughTunnelAreRefused)
{
	simtest::TunnelRig rig;
	VirtualDevice a(rig.bus, simtest::switchConfig(true));
	VirtualDevice b(rig.bus, simtest::switchConfig(true));
	rig.connect();
	knx::IaProgrammer programmer;
	std::vector<knx::IaProgrammer::Action> results;
	programmer.start(IndividualAddress(0x1105), rig.now);
	rig.run(programmer, results, [&] { return programmer.finished(); }, {&a, &b});
	ASSERT_EQ(only<knx::IaProgrammer::Finished>(results),
		(std::vector<knx::IaProgrammer::Finished>{{knx::IaProgrammerFailure::MultipleDevicesInProgMode, IndividualAddress()}}));
	ASSERT_EQ(a.address(), IndividualAddress(0xFFFF));
	ASSERT_EQ(b.address(), IndividualAddress(0xFFFF));
}

UNIT_TEST(NFR_5__heartbeatKeepsTunnelAlive)
{
	simtest::TunnelRig rig;
	rig.connect();
	rig.advance(5min, {}, 1s);
	ASSERT_EQ(rig.client.state(), knx::TunnelState::Connected);
	ASSERT_EQ(rig.server.connectionCount(), std::size_t{1});
	ASSERT(only<TunnelClient::Disconnected>(rig.events).empty());
}

UNIT_TEST(NFR_5__tunnelRecoversFromLostDatagramsViaBusFaults)
{
	// A frame dropped on the bus never produces an answer, but the tunnel stays healthy and the
	// next request works.
	simtest::TunnelRig rig;
	VirtualDevice device(rig.bus, simtest::switchConfig());
	rig.connect();
	rig.bus.addFaultRule(FaultRule{FaultAction::Drop, [](const knx::cemi::LData&) { return true; }, 1});
	rig.send(knx::makeGroupFrame(kSwitch, knx::groupValueWriteShort(1)));
	ASSERT_EQ(*device.groupValue(kSwitch), hex("00"));
	rig.send(knx::makeGroupFrame(kSwitch, knx::groupValueWriteShort(1)));
	ASSERT_EQ(*device.groupValue(kSwitch), hex("01"));
	ASSERT_EQ(only<TunnelClient::SendConfirmed>(rig.events), (std::vector<TunnelClient::SendConfirmed>{{true}, {true}}));
}
