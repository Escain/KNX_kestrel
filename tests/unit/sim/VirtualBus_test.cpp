#include "SimTestSupport.hpp"

#include <Printers.hpp>
#include <UnitTests.hpp>

using namespace kestrel;
using namespace kestrel::sim;
using simtest::hex;

namespace
{

knx::cemi::LData groupWrite(std::uint16_t ga, std::uint8_t value)
{
	auto frame = knx::makeGroupFrame(knx::GroupAddress(ga), knx::groupValueWriteShort(value));
	frame.source = 0x1101;
	return frame;
}

} // namespace

UNIT_TEST(NFR_5__virtualBusDeliversToAllButSender)
{
	VirtualBus bus;
	std::vector<int> seen;
	const auto a = bus.attach([&](const knx::cemi::LData&) { seen.push_back(1); });
	bus.attach([&](const knx::cemi::LData&) { seen.push_back(2); });
	bus.attach([&](const knx::cemi::LData& frame)
		{
			seen.push_back(3);
			// Delivered frames are indications.
			if (frame.messageCode != knx::cemi::MessageCode::LDataInd)
			{
				seen.push_back(-1);
			}
		});
	bus.transmit(a, groupWrite(0x0801, 1));
	ASSERT_EQ(seen, (std::vector<int>{2, 3}));
	ASSERT_EQ(bus.telegrams().size(), std::size_t{1});
	ASSERT_EQ(bus.telegrams()[0].sender, a);
	ASSERT_EQ(bus.telegrams()[0].frame.tpdu, hex("00 81"));
	bus.clearTelegrams();
	ASSERT(bus.telegrams().empty());
}

UNIT_TEST(NFR_5__virtualBusIsFifoWithoutRecursion)
{
	VirtualBus bus;
	std::vector<std::uint16_t> order;
	EndpointId echo = 0;
	echo = bus.attach(
		[&](const knx::cemi::LData& frame)
		{
			order.push_back(frame.destination);
			if (frame.destination == 0x0801)
			{
				bus.transmit(echo, groupWrite(0x0802, 0)); // answer queued behind 0x0803
			}
		});
	const auto observer = bus.attach([&](const knx::cemi::LData& frame) { order.push_back(static_cast<std::uint16_t>(frame.destination | 0x8000)); });
	bus.post(observer, groupWrite(0x0801, 1));
	bus.post(observer, groupWrite(0x0803, 1));
	bus.flush();
	ASSERT_EQ(order, (std::vector<std::uint16_t>{0x0801, 0x0803, 0x8802}));
	ASSERT_EQ(bus.telegrams().size(), std::size_t{3});
	ASSERT_EQ(bus.telegrams()[2].index, std::uint64_t{2});
}

UNIT_TEST(NFR_5__virtualBusFaultRules)
{
	VirtualBus bus;
	int count = 0;
	const auto sender = bus.attach(nullptr);
	bus.attach([&](const knx::cemi::LData&) { ++count; });
	bus.addFaultRule(FaultRule{FaultAction::Drop, [](const knx::cemi::LData& f) { return f.destination == 0x0801; }, 1});
	bus.addFaultRule(FaultRule{FaultAction::Duplicate, [](const knx::cemi::LData& f) { return f.destination == 0x0802; }, -1});

	bus.transmit(sender, groupWrite(0x0801, 1)); // dropped (rule used up)
	ASSERT_EQ(count, 0);
	bus.transmit(sender, groupWrite(0x0801, 1)); // delivered
	ASSERT_EQ(count, 1);
	bus.transmit(sender, groupWrite(0x0802, 1)); // duplicated
	bus.transmit(sender, groupWrite(0x0802, 1));
	ASSERT_EQ(count, 5);
	ASSERT_EQ(bus.telegrams().size(), std::size_t{5}); // dropped frames never reach the log

	bus.clearFaultRules();
	bus.transmit(sender, groupWrite(0x0802, 1));
	ASSERT_EQ(count, 6);
}

UNIT_TEST(NFR_5__virtualBusMonitorAndDetach)
{
	VirtualBus bus;
	std::vector<std::uint64_t> monitored;
	bus.onTelegram = [&](const Telegram& telegram) { monitored.push_back(telegram.index); };
	int count = 0;
	const auto sender = bus.attach(nullptr);
	EndpointId self = 0;
	self = bus.attach(
		[&](const knx::cemi::LData&)
		{
			++count;
			bus.detach(self); // detaching from inside a delivery is safe
		});
	bus.transmit(sender, groupWrite(0x0801, 1));
	bus.transmit(sender, groupWrite(0x0801, 1));
	ASSERT_EQ(count, 1);
	ASSERT_EQ(monitored, (std::vector<std::uint64_t>{0, 1}));
}
