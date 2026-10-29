#include "SimTestSupport.hpp"
#include "kestrel/knx/IaProgrammer.hpp"
#include "kestrel/knx/ManagementSession.hpp"

#include <Printers.hpp>
#include <UnitTests.hpp>

using namespace kestrel;
using namespace kestrel::sim;
using namespace std::chrono_literals;
using knx::IndividualAddress;
using knx::ManagementSession;
using simtest::hex;
using simtest::kT0;
using simtest::only;

namespace
{

const knx::GroupAddress kSwitch(0x0901);      // 1/1/1
const knx::GroupAddress kTemperature(0x0902); // 1/1/2

/// A SimLink that records every frame it receives.
struct Probe
{
	SimLink link;
	std::vector<knx::cemi::LData> frames;
	int confirms = 0;

	explicit Probe(VirtualBus& bus) : link(bus)
	{
		link.onFrame = [this](const knx::cemi::LData& frame) { frames.push_back(frame); };
		link.onConfirm = [this](bool ok) { confirms += ok ? 1 : 0; };
		link.open();
	}

	std::vector<knx::Tpdu> tpdus() const
	{
		std::vector<knx::Tpdu> out;
		for (const auto& frame : frames)
		{
			if (auto tpdu = knx::decodeTpdu(frame))
			{
				out.push_back(*tpdu);
			}
		}
		return out;
	}
};

} // namespace

UNIT_TEST(FR_BUS_6__deviceGroupWriteAndRead)
{
	VirtualBus bus;
	VirtualDevice device(bus, simtest::switchConfig());
	Probe probe(bus);

	ASSERT(probe.link.send(knx::makeGroupFrame(kSwitch, knx::groupValueWriteShort(1))).has_value());
	ASSERT_EQ(probe.confirms, 1);
	ASSERT_EQ(*device.groupValue(kSwitch), hex("01"));
	ASSERT(probe.frames.empty());

	// 1/1/1 has no R flag: no answer. 1/1/2 has: long-form DPT 9 response from the device.
	ASSERT(probe.link.send(knx::makeGroupFrame(kSwitch, knx::groupValueRead())).has_value());
	ASSERT(probe.frames.empty());
	ASSERT(probe.link.send(knx::makeGroupFrame(kTemperature, knx::groupValueRead())).has_value());
	ASSERT_EQ(probe.frames.size(), std::size_t{1});
	ASSERT_EQ(probe.frames[0].sourceAddress(), IndividualAddress(0xFFFF));
	ASSERT_EQ(probe.tpdus()[0], knx::Tpdu::dataGroup(knx::groupValueResponse(hex("0c 33"))));

	// 1/1/2 has no W flag: a write is ignored.
	ASSERT(probe.link.send(knx::makeGroupFrame(kTemperature, knx::groupValueWrite(hex("00 00")))).has_value());
	ASSERT_EQ(*device.groupValue(kTemperature), hex("0c 33"));
	// U flag: responses update 1/1/1.
	ASSERT(probe.link.send(knx::makeGroupFrame(kSwitch, knx::groupValueResponseShort(0))).has_value());
	ASSERT_EQ(*device.groupValue(kSwitch), hex("00"));
	ASSERT_FALSE(device.groupValue(knx::GroupAddress(0x0903)).has_value());
}

UNIT_TEST(FR_BUS_6__deviceTransmitsApplicationChanges)
{
	VirtualBus bus;
	VirtualDevice device(bus, simtest::switchConfig());
	Probe probe(bus);
	ASSERT(device.setGroupValue(kSwitch, hex("01")).has_value());
	ASSERT_EQ(probe.tpdus(), (std::vector<knx::Tpdu>{knx::Tpdu::dataGroup(knx::groupValueWriteShort(1))}));
	ASSERT_FALSE(device.setGroupValue(knx::GroupAddress(0x0903), hex("01")).has_value());
}

UNIT_TEST(FR_COM_1__deviceAnswersIaServicesOnlyInProgMode)
{
	VirtualBus bus;
	VirtualDevice device(bus, simtest::switchConfig(false));
	Probe probe(bus);
	const auto read = knx::makeBroadcastFrame(knx::individualAddressRead());
	const auto write = knx::makeBroadcastFrame(knx::individualAddressWrite(IndividualAddress(0x1105)));

	ASSERT(probe.link.send(read).has_value());
	ASSERT(probe.link.send(write).has_value());
	ASSERT(probe.frames.empty());
	ASSERT_EQ(device.address(), IndividualAddress(0xFFFF));

	device.pressProgButton();
	ASSERT(device.progMode());
	ASSERT(probe.link.send(read).has_value());
	ASSERT_EQ(probe.tpdus(), (std::vector<knx::Tpdu>{knx::Tpdu::dataBroadcast(knx::individualAddressResponse())}));
	ASSERT(probe.link.send(write).has_value());
	ASSERT_EQ(device.address(), IndividualAddress(0x1105));
}

UNIT_TEST(FR_COM_4__deviceConnectionlessDescriptorRead)
{
	VirtualBus bus;
	VirtualDevice device(bus, simtest::switchConfig());
	Probe probe(bus);
	ASSERT(probe.link.send(knx::makeIndividualFrame(device.address(), knx::Tpdu::dataIndividual(knx::deviceDescriptorRead())))
			.has_value());
	ASSERT_EQ(probe.frames.size(), std::size_t{1});
	ASSERT_EQ(probe.frames[0].destination, probe.link.address().raw());
	ASSERT_EQ(knx::cemi::encode(probe.frames[0]), hex("29 00 b0 60 ff ff 11 fe 03 03 40 07 05"));
	// Unsupported descriptor type → 0x3F.
	probe.frames.clear();
	ASSERT(probe.link.send(knx::makeIndividualFrame(device.address(), knx::Tpdu::dataIndividual(knx::deviceDescriptorRead(2))))
			.has_value());
	ASSERT_EQ(probe.tpdus()[0].apdu.shortData, std::uint8_t{0x3F});
}

UNIT_TEST(FR_COM_4__managementSessionAgainstDevice)
{
	VirtualBus bus;
	VirtualDevice device(bus, simtest::switchConfig());
	SimLink link(bus);
	ManagementSession session{device.address()};
	session.setLocalAddress(link.address());
	link.onFrame = [&](const knx::cemi::LData& frame) { session.onFrame(frame, kT0); };
	link.open();
	std::vector<ManagementSession::Action> results;

	const auto descriptor = session.deviceDescriptorRead(kT0);
	Bytes image(30);
	for (std::size_t i = 0; i < image.size(); ++i)
	{
		image[i] = static_cast<std::uint8_t>(0xA0 + i);
	}
	const auto write = session.memoryWrite(0x4000, image, kT0);
	const auto read = session.memoryRead(0x4000, image.size(), kT0);
	const auto serial = session.propertyValueRead(0, pid::kSerialNumber, 1, 1, kT0);
	const auto manufacturer = session.propertyValueRead(0, pid::kManufacturerId, 1, 1, kT0);
	const auto missing = session.propertyValueRead(5, 99, 1, 1, kT0);
	simtest::drive(session, link, results);

	ASSERT_EQ(only<ManagementSession::Completed>(results),
		(std::vector<ManagementSession::Completed>{{descriptor, hex("07 05")}, {write, {}}, {read, image},
			{serial, hex("00 83 00 00 12 34")}, {manufacturer, hex("00 83")}}));
	auto failed = only<ManagementSession::Failed>(results);
	ASSERT_EQ(failed.size(), std::size_t{1});
	ASSERT_EQ(failed[0].id, missing);
	ASSERT_EQ(device.readMemory(0x4000, image.size()), image);
	ASSERT_EQ(device.connectionPartner(), std::optional<IndividualAddress>(link.address()));

	// Restart ends programming mode and the connection.
	device.setProgMode(true);
	const auto restart = session.restart(kT0);
	simtest::drive(session, link, results);
	ASSERT_EQ(only<ManagementSession::Completed>(results).back(), (ManagementSession::Completed{restart, {}}));
	ASSERT_FALSE(device.progMode());
	ASSERT_EQ(device.restartCount(), std::uint32_t{1});
	ASSERT_FALSE(device.connectionPartner().has_value());
}

UNIT_TEST(FR_COM_4__deviceRefusesSecondConnection)
{
	VirtualBus bus;
	VirtualDevice device(bus, simtest::switchConfig());
	Probe first(bus);
	ASSERT(first.link.send(knx::makeIndividualFrame(device.address(), knx::Tpdu::connect())).has_value());

	SimLink second(bus, IndividualAddress(0x11FD));
	ManagementSession session{device.address()};
	second.onFrame = [&](const knx::cemi::LData& frame) { session.onFrame(frame, kT0); };
	second.open();
	std::vector<ManagementSession::Action> results;
	const auto id = session.deviceDescriptorRead(kT0);
	simtest::drive(session, second, results);
	// The refusal comes right after T_Connect: it takes effect when the disconnect echo window ends.
	session.tick(kT0 + std::chrono::seconds(1));
	simtest::drive(session, second, results);
	ASSERT_EQ(only<ManagementSession::Failed>(results).size(), std::size_t{1});
	ASSERT_EQ(only<ManagementSession::Failed>(results)[0].id, id);
	ASSERT_EQ(only<ManagementSession::Disconnected>(results),
		(std::vector<ManagementSession::Disconnected>{{knx::ManagementDisconnectReason::RemoteDisconnect}}));
	ASSERT_EQ(device.connectionPartner(), std::optional<IndividualAddress>(first.link.address()));
}

UNIT_TEST(FR_COM_4__deviceDropsIdleConnection)
{
	VirtualBus bus;
	VirtualDevice device(bus, simtest::switchConfig());
	Probe probe(bus);
	ASSERT(probe.link.send(knx::makeIndividualFrame(device.address(), knx::Tpdu::connect())).has_value());
	device.tick(kT0);
	device.tick(kT0 + 5900ms);
	ASSERT(probe.frames.empty());
	device.tick(kT0 + 6s);
	ASSERT_EQ(probe.tpdus(), (std::vector<knx::Tpdu>{knx::Tpdu::disconnect()}));
	ASSERT_FALSE(device.connectionPartner().has_value());
}

UNIT_TEST(FR_COM_4__deviceTransportSequencing)
{
	VirtualBus bus;
	VirtualDevice device(bus, simtest::switchConfig());
	Probe probe(bus);
	auto to = [&](const knx::Tpdu& tpdu) { return probe.link.send(knx::makeIndividualFrame(device.address(), tpdu)).has_value(); };

	// Data without a connection → T_Disconnect.
	ASSERT(to(knx::Tpdu::dataConnected(0, knx::deviceDescriptorRead())));
	ASSERT_EQ(probe.tpdus(), (std::vector<knx::Tpdu>{knx::Tpdu::disconnect()}));
	probe.frames.clear();

	ASSERT(to(knx::Tpdu::connect()));
	ASSERT(to(knx::Tpdu::dataConnected(0, knx::deviceDescriptorRead())));
	ASSERT_EQ(probe.tpdus(), (std::vector<knx::Tpdu>{knx::Tpdu::ack(0),
		knx::Tpdu::dataConnected(0, knx::deviceDescriptorResponse(0, hex("07 05")))}));
	probe.frames.clear();
	// Repetition of seq 0 → ACK only; wrong sequence → NAK.
	ASSERT(to(knx::Tpdu::dataConnected(0, knx::deviceDescriptorRead())));
	ASSERT(to(knx::Tpdu::dataConnected(5, knx::deviceDescriptorRead())));
	ASSERT_EQ(probe.tpdus(), (std::vector<knx::Tpdu>{knx::Tpdu::ack(0), knx::Tpdu::nak(5)}));
	probe.frames.clear();
	// After our ACK the device's next response uses seq 1.
	ASSERT(to(knx::Tpdu::ack(0)));
	ASSERT(to(knx::Tpdu::dataConnected(1, knx::memoryRead(0x0000, 2))));
	ASSERT_EQ(probe.tpdus(), (std::vector<knx::Tpdu>{knx::Tpdu::ack(1),
		knx::Tpdu::dataConnected(1, knx::memoryResponse(0x0000, hex("00 00")))}));
	// More than 12 bytes is refused with count 0.
	probe.frames.clear();
	ASSERT(to(knx::Tpdu::ack(1)));
	ASSERT(to(knx::Tpdu::dataConnected(2, knx::memoryRead(0x0000, 13))));
	ASSERT_EQ(probe.tpdus().back(), knx::Tpdu::dataConnected(2, knx::memoryResponse(0x0000, {})));
}

UNIT_TEST(NFR_5__managementSurvivesLostAck)
{
	VirtualBus bus;
	VirtualDevice device(bus, simtest::switchConfig());
	// Lose the device's first T_ACK: the session repeats, the device recognises the repetition.
	bus.addFaultRule(FaultRule{FaultAction::Drop,
		[](const knx::cemi::LData& frame) { return frame.source == 0xFFFF && frame.tpdu == Bytes{0xC2}; }, 1});
	SimLink link(bus);
	ManagementSession session{device.address()};
	knx::TimePoint now = kT0;
	link.onFrame = [&](const knx::cemi::LData& frame) { session.onFrame(frame, now); };
	link.open();
	std::vector<ManagementSession::Action> results;
	const auto id = session.deviceDescriptorRead(now);
	simtest::drive(session, link, results);
	ASSERT(results.empty());
	now += 3s;
	session.tick(now);
	simtest::drive(session, link, results);
	ASSERT_EQ(only<ManagementSession::Completed>(results), (std::vector<ManagementSession::Completed>{{id, hex("07 05")}}));
	// The device answered the descriptor read exactly once.
	std::size_t responses = 0;
	for (const auto& telegram : bus.telegrams())
	{
		auto tpdu = knx::decodeTpdu(telegram.frame);
		responses += tpdu && tpdu->apdu.apci == knx::Apci::DeviceDescriptorResponse ? 1u : 0u;
	}
	ASSERT_EQ(responses, std::size_t{1});
}

UNIT_TEST(FR_COM_1__iaProgrammerOverSimLink)
{
	VirtualBus bus;
	VirtualDevice device(bus, simtest::switchConfig(true));
	VirtualDevice other(bus, [] { auto c = simtest::switchConfig(false); c.address = IndividualAddress(0x1101); return c; }());
	SimLink link(bus);
	knx::IaProgrammer programmer;
	knx::TimePoint now = kT0;
	link.onFrame = [&](const knx::cemi::LData& frame) { programmer.onFrame(frame, now); };
	link.open();
	std::vector<knx::IaProgrammer::Action> results;
	programmer.start(IndividualAddress(0x1105), now);
	for (int i = 0; i < 100 && !programmer.finished(); ++i)
	{
		simtest::drive(programmer, link, results);
		now += 100ms;
		programmer.tick(now);
	}
	simtest::drive(programmer, link, results);
	ASSERT_EQ(only<knx::IaProgrammer::Finished>(results),
		(std::vector<knx::IaProgrammer::Finished>{{knx::IaProgrammerFailure::None, IndividualAddress(0xFFFF)}}));
	ASSERT_EQ(device.address(), IndividualAddress(0x1105));
	ASSERT_FALSE(device.progMode());
	ASSERT_EQ(other.address(), IndividualAddress(0x1101));
}

UNIT_TEST(FR_COM_1__iaProgrammerRefusesTwoDevicesInProgMode)
{
	VirtualBus bus;
	VirtualDevice a(bus, simtest::switchConfig(true));
	VirtualDevice b(bus, [] { auto c = simtest::switchConfig(true); c.address = IndividualAddress(0x1101); return c; }());
	SimLink link(bus);
	knx::IaProgrammer programmer;
	knx::TimePoint now = kT0;
	link.onFrame = [&](const knx::cemi::LData& frame) { programmer.onFrame(frame, now); };
	link.open();
	std::vector<knx::IaProgrammer::Action> results;
	programmer.start(IndividualAddress(0x1105), now);
	simtest::drive(programmer, link, results);
	now += 1s;
	programmer.tick(now);
	simtest::drive(programmer, link, results);
	ASSERT_EQ(only<knx::IaProgrammer::Finished>(results),
		(std::vector<knx::IaProgrammer::Finished>{{knx::IaProgrammerFailure::MultipleDevicesInProgMode, IndividualAddress()}}));
	ASSERT_EQ(a.address(), IndividualAddress(0xFFFF));
	ASSERT_EQ(b.address(), IndividualAddress(0x1101));
}

UNIT_TEST(FR_COM_1__iaProgrammerRefusesTwoFactoryDevicesInProgMode)
{
	VirtualBus bus;
	VirtualDevice a(bus, simtest::switchConfig(true));
	VirtualDevice b(bus, simtest::switchConfig(true)); // same 15.15.255
	SimLink link(bus);
	knx::IaProgrammer programmer;
	knx::TimePoint now = kT0;
	link.onFrame = [&](const knx::cemi::LData& frame) { programmer.onFrame(frame, now); };
	link.open();
	std::vector<knx::IaProgrammer::Action> results;
	programmer.start(IndividualAddress(0x1105), now);
	simtest::drive(programmer, link, results);
	now += 1s;
	programmer.tick(now);
	simtest::drive(programmer, link, results);
	ASSERT_EQ(only<knx::IaProgrammer::Finished>(results),
		(std::vector<knx::IaProgrammer::Finished>{{knx::IaProgrammerFailure::MultipleDevicesInProgMode, IndividualAddress()}}));
	ASSERT_EQ(a.address(), IndividualAddress(0xFFFF));
	ASSERT_EQ(b.address(), IndividualAddress(0xFFFF));
	// Both answers really were on the bus.
	std::size_t responses = 0;
	for (const auto& telegram : bus.telegrams())
	{
		auto tpdu = knx::decodeTpdu(telegram.frame);
		responses += tpdu && tpdu->apdu.apci == knx::Apci::IndividualAddressResponse ? 1u : 0u;
	}
	ASSERT_EQ(responses, std::size_t{2});
}

UNIT_TEST(NFR_5__simLinkStateAndErrors)
{
	VirtualBus bus;
	SimLink link(bus);
	std::vector<knx::LinkState> states;
	link.onState = [&](knx::LinkState state) { states.push_back(state); };
	ASSERT_FALSE(link.send(knx::makeGroupFrame(kSwitch, knx::groupValueRead())).has_value());
	link.open();
	ASSERT(link.isOpen());
	ASSERT_FALSE(link.send(knx::cemi::LData{}).has_value()); // no TPDU
	ASSERT(link.send(knx::makeGroupFrame(kSwitch, knx::groupValueRead())).has_value());
	ASSERT_EQ(bus.telegrams().back().frame.source, std::uint16_t{0x11FE}); // source substituted
	link.close();
	ASSERT_EQ(states, (std::vector<knx::LinkState>{knx::LinkState::Connected, knx::LinkState::Disconnected}));
}

UNIT_TEST(FR_BUS_6__groupObjectFlagsAndConfigText)
{
	auto flags = GroupObjectFlags::parse("cwtu");
	ASSERT(flags.has_value());
	ASSERT_EQ(flags->toString(), std::string("CWTU"));
	ASSERT_FALSE(flags->read);
	ASSERT_EQ(GroupObjectFlags::parse("ICRWTU")->toString(), std::string("CRWTUI"));
	ASSERT_FALSE(GroupObjectFlags::parse("CX").has_value());
	ASSERT_EQ(simtest::switchConfig(), simtest::switchConfig());
	ASSERT_NEQ(simtest::switchConfig(true), simtest::switchConfig(false));
}
