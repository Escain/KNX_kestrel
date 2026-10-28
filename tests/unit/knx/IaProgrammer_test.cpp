#include "KnxTestSupport.hpp"
#include "kestrel/knx/Apdu.hpp"
#include "kestrel/knx/IaProgrammer.hpp"

#include <algorithm>

#include <Printers.hpp>
#include <UnitTests.hpp>

using namespace kestrel;
using namespace kestrel::knx;
using namespace std::chrono_literals;
using knxtest::hex;
using knxtest::kT0;
using knxtest::only;

namespace
{

const IndividualAddress kTarget(0x1105);  // 1.1.5
const IndividualAddress kFactory(0xFFFF); // 15.15.255

cemi::LData iaResponseFrom(IndividualAddress source)
{
	auto frame = makeBroadcastFrame(individualAddressResponse());
	frame.messageCode = cemi::MessageCode::LDataInd;
	frame.source = source.raw();
	return frame;
}

std::vector<Bytes> sentFrames(const std::vector<IaProgrammer::Action>& actions)
{
	std::vector<Bytes> out;
	for (const auto& send : only<IaProgrammer::SendFrame>(actions))
	{
		out.push_back(cemi::encode(send.frame));
	}
	return out;
}

const Bytes kIaRead = hex("11 00 b0 e0 00 00 00 00 01 01 00");
const Bytes kIaWrite = hex("11 00 b0 e0 00 00 00 00 03 00 c0 11 05");
const Bytes kRestart = hex("11 00 b0 60 00 00 11 05 01 03 80");
const Bytes kConnect = hex("11 00 b0 60 00 00 11 05 00 80");
const Bytes kDescriptorRead = hex("11 00 b0 60 00 00 11 05 01 43 00");
const Bytes kDisconnect = hex("11 00 b0 60 00 00 11 05 00 81");

/// Config without the address-in-use check, for tests about the other phases.
IaProgrammerConfig noCheck()
{
	IaProgrammerConfig config;
	config.allowOverwrite = true;
	return config;
}

cemi::LData fromTarget(const Tpdu& tpdu)
{
	auto frame = makeIndividualFrame(IndividualAddress(0x11FF), tpdu);
	frame.messageCode = cemi::MessageCode::LDataInd;
	frame.source = kTarget.raw();
	return frame;
}

} // namespace

UNIT_TEST(FR_COM_1__iaProgrammerHappyPath)
{
	IaProgrammer programmer;
	programmer.start(kTarget, kT0);
	auto actions = programmer.takeActions();
	ASSERT_EQ(only<IaProgrammer::StateChanged>(actions),
		(std::vector<IaProgrammer::StateChanged>{{IaProgrammerState::WaitProgMode}}));
	ASSERT_EQ(sentFrames(actions), (std::vector<Bytes>{kIaRead}));

	programmer.onFrame(iaResponseFrom(kFactory), kT0 + 100ms);
	programmer.tick(kT0 + 999ms);
	ASSERT(programmer.takeActions().empty()); // window still open

	programmer.tick(kT0 + 1s);
	actions = programmer.takeActions();
	// Is 1.1.5 free? T_Connect + A_DeviceDescriptor_Read to it.
	ASSERT_EQ(sentFrames(actions), (std::vector<Bytes>{kConnect, kDescriptorRead}));
	ASSERT_EQ(programmer.state(), IaProgrammerState::CheckAddress);
	programmer.tick(kT0 + 3999ms);
	ASSERT(programmer.takeActions().empty());

	programmer.tick(kT0 + 4s); // silence: the address is free
	actions = programmer.takeActions();
	ASSERT_EQ(sentFrames(actions), (std::vector<Bytes>{kDisconnect, kIaWrite, kIaRead}));
	ASSERT_EQ(programmer.state(), IaProgrammerState::Verify);

	programmer.onFrame(iaResponseFrom(kTarget), kT0 + 4100ms);
	programmer.tick(kT0 + 5s);
	actions = programmer.takeActions();
	ASSERT_EQ(sentFrames(actions), (std::vector<Bytes>{kRestart}));
	ASSERT_EQ(only<IaProgrammer::Finished>(actions),
		(std::vector<IaProgrammer::Finished>{{IaProgrammerFailure::None, kFactory}}));
	ASSERT_EQ(programmer.state(), IaProgrammerState::Done);
	ASSERT(programmer.finished());
}

UNIT_TEST(FR_COM_1__iaProgrammerWaitsForProgModeThenTimesOut)
{
	IaProgrammerConfig config;
	config.progModeTimeout = 5s;
	IaProgrammer programmer(config);
	programmer.start(kTarget, kT0);
	(void)programmer.takeActions();
	std::size_t reads = 1;
	for (int i = 1; i <= 10 && !programmer.finished(); ++i)
	{
		programmer.tick(kT0 + std::chrono::seconds(i));
		reads += sentFrames(programmer.takeActions()).size();
	}
	ASSERT_EQ(programmer.state(), IaProgrammerState::Failed);
	ASSERT_EQ(reads, std::size_t{5}); // one read per second until the 5 s timeout
}

UNIT_TEST(FR_COM_1__iaProgrammerTimeoutReported)
{
	IaProgrammerConfig config;
	config.progModeTimeout = 2s;
	IaProgrammer programmer(config);
	programmer.start(kTarget, kT0);
	programmer.tick(kT0 + 1s);
	programmer.tick(kT0 + 2s);
	ASSERT_EQ(only<IaProgrammer::Finished>(programmer.takeActions()),
		(std::vector<IaProgrammer::Finished>{{IaProgrammerFailure::Timeout, IndividualAddress()}}));
}

UNIT_TEST(FR_COM_1__iaProgrammerLateDeviceIsFound)
{
	IaProgrammer programmer;
	programmer.start(kTarget, kT0);
	programmer.tick(kT0 + 1s);
	programmer.tick(kT0 + 2s);
	(void)programmer.takeActions();
	programmer.onFrame(iaResponseFrom(kFactory), kT0 + 2500ms);
	programmer.tick(kT0 + 3s);
	ASSERT_EQ(sentFrames(programmer.takeActions()), (std::vector<Bytes>{kConnect, kDescriptorRead}));
}

UNIT_TEST(FR_COM_1__iaProgrammerRefusesMultipleDevices)
{
	IaProgrammer programmer;
	programmer.start(kTarget, kT0);
	(void)programmer.takeActions();
	programmer.onFrame(iaResponseFrom(kFactory), kT0 + 100ms);
	programmer.onFrame(iaResponseFrom(IndividualAddress(0x1101)), kT0 + 200ms);
	programmer.onFrame(iaResponseFrom(kFactory), kT0 + 300ms); // same device twice counts once
	programmer.tick(kT0 + 1s);
	auto actions = programmer.takeActions();
	ASSERT(sentFrames(actions).empty()); // nothing written
	ASSERT_EQ(only<IaProgrammer::Finished>(actions),
		(std::vector<IaProgrammer::Finished>{{IaProgrammerFailure::MultipleDevicesInProgMode, IndividualAddress()}}));
	ASSERT_EQ(programmer.responders().size(), std::size_t{2});
}

UNIT_TEST(FR_COM_1__iaProgrammerRefusesTwoDevicesSharingAnAddress)
{
	// Two factory-new devices in programming mode both answer from 15.15.255: two responses, one
	// address. Programming them would give both the same individual address.
	IaProgrammer programmer;
	programmer.start(kTarget, kT0);
	(void)programmer.takeActions();
	programmer.onFrame(iaResponseFrom(kFactory), kT0 + 100ms);
	programmer.onFrame(iaResponseFrom(kFactory), kT0 + 110ms);
	programmer.tick(kT0 + 1s);
	auto actions = programmer.takeActions();
	ASSERT(sentFrames(actions).empty());
	ASSERT_EQ(only<IaProgrammer::Finished>(actions),
		(std::vector<IaProgrammer::Finished>{{IaProgrammerFailure::MultipleDevicesInProgMode, IndividualAddress()}}));
}

UNIT_TEST(FR_COM_1__iaProgrammerVerifyRejectsTwoDevicesAtTarget)
{
	IaProgrammer programmer{noCheck()};
	programmer.start(kTarget, kT0);
	programmer.onFrame(iaResponseFrom(kFactory), kT0);
	programmer.tick(kT0 + 1s);
	(void)programmer.takeActions();
	for (int window = 2; window <= 4; ++window)
	{
		programmer.onFrame(iaResponseFrom(kTarget), kT0 + std::chrono::seconds(window) - 500ms);
		programmer.onFrame(iaResponseFrom(kTarget), kT0 + std::chrono::seconds(window) - 400ms);
		programmer.tick(kT0 + std::chrono::seconds(window));
	}
	auto actions = programmer.takeActions();
	const auto frames = sentFrames(actions);
	ASSERT(std::find(frames.begin(), frames.end(), kRestart) == frames.end());
	ASSERT_EQ(only<IaProgrammer::Finished>(actions),
		(std::vector<IaProgrammer::Finished>{{IaProgrammerFailure::VerifyFailed, kFactory}}));
}

UNIT_TEST(FR_COM_1__iaProgrammerVerifyFailsOnWrongAddress)
{
	IaProgrammer programmer{noCheck()};
	programmer.start(kTarget, kT0);
	(void)programmer.takeActions();
	programmer.onFrame(iaResponseFrom(kFactory), kT0);
	programmer.tick(kT0 + 1s);
	ASSERT_EQ(sentFrames(programmer.takeActions()), (std::vector<Bytes>{kIaWrite, kIaRead}));
	// The write did not take effect: the old address keeps answering. Only the last attempt fails.
	for (int window = 2; window <= 3; ++window)
	{
		programmer.onFrame(iaResponseFrom(kFactory), kT0 + std::chrono::seconds(window) - 500ms);
		programmer.tick(kT0 + std::chrono::seconds(window));
		ASSERT_EQ(sentFrames(programmer.takeActions()), (std::vector<Bytes>{kIaRead}));
	}
	programmer.onFrame(iaResponseFrom(kFactory), kT0 + 3500ms);
	programmer.tick(kT0 + 4s);
	auto actions = programmer.takeActions();
	ASSERT(sentFrames(actions).empty()); // no restart
	ASSERT_EQ(only<IaProgrammer::Finished>(actions),
		(std::vector<IaProgrammer::Finished>{{IaProgrammerFailure::VerifyFailed, kFactory}}));
}

UNIT_TEST(FR_COM_1__iaProgrammerVerifyToleratesLateOldAddress)
{
	IaProgrammer programmer{noCheck()};
	programmer.start(kTarget, kT0);
	programmer.onFrame(iaResponseFrom(kFactory), kT0);
	programmer.tick(kT0 + 1s);
	(void)programmer.takeActions();
	// First verify window: a late answer from the old address next to the new one.
	programmer.onFrame(iaResponseFrom(kFactory), kT0 + 1100ms);
	programmer.onFrame(iaResponseFrom(kTarget), kT0 + 1200ms);
	programmer.tick(kT0 + 2s);
	ASSERT_EQ(sentFrames(programmer.takeActions()), (std::vector<Bytes>{kIaRead}));
	programmer.onFrame(iaResponseFrom(kTarget), kT0 + 2100ms);
	programmer.tick(kT0 + 3s);
	ASSERT_EQ(only<IaProgrammer::Finished>(programmer.takeActions()),
		(std::vector<IaProgrammer::Finished>{{IaProgrammerFailure::None, kFactory}}));
}

UNIT_TEST(FR_COM_1__iaProgrammerVerifyFailsOnPersistentThirdDevice)
{
	const IndividualAddress intruder(0x1101);
	IaProgrammer programmer{noCheck()};
	programmer.start(kTarget, kT0);
	programmer.onFrame(iaResponseFrom(kFactory), kT0);
	programmer.tick(kT0 + 1s);
	(void)programmer.takeActions();
	programmer.onFrame(iaResponseFrom(kTarget), kT0 + 1100ms);
	programmer.onFrame(iaResponseFrom(intruder), kT0 + 1200ms); // someone pressed another button
	programmer.tick(kT0 + 2s);
	ASSERT_EQ(sentFrames(programmer.takeActions()), (std::vector<Bytes>{kIaRead}));
	programmer.onFrame(iaResponseFrom(kTarget), kT0 + 2100ms);
	programmer.onFrame(iaResponseFrom(intruder), kT0 + 2200ms);
	programmer.tick(kT0 + 3s); // same intruder twice in a row: give up before attempt 3
	ASSERT_EQ(only<IaProgrammer::Finished>(programmer.takeActions()),
		(std::vector<IaProgrammer::Finished>{{IaProgrammerFailure::VerifyFailed, kFactory}}));
}

UNIT_TEST(FR_COM_1__iaProgrammerRefusesAddressInUse)
{
	IaProgrammer programmer;
	programmer.start(kTarget, kT0);
	programmer.onFrame(iaResponseFrom(kFactory), kT0);
	programmer.tick(kT0 + 1s);
	(void)programmer.takeActions();
	programmer.onFrame(fromTarget(Tpdu::ack(0)), kT0 + 1100ms);
	auto actions = programmer.takeActions();
	ASSERT_EQ(sentFrames(actions), (std::vector<Bytes>{kDisconnect})); // nothing written
	ASSERT_EQ(only<IaProgrammer::Finished>(actions),
		(std::vector<IaProgrammer::Finished>{{IaProgrammerFailure::AddressInUse, kFactory}}));
	programmer.tick(kT0 + 10s);
	ASSERT(programmer.takeActions().empty());
}

UNIT_TEST(FR_COM_1__iaProgrammerBusyTargetIsInUse)
{
	IaProgrammer programmer;
	programmer.start(kTarget, kT0);
	programmer.onFrame(iaResponseFrom(kFactory), kT0);
	programmer.tick(kT0 + 1s);
	(void)programmer.takeActions();
	// A device connected elsewhere refuses with T_Disconnect: it exists, and there is nothing to close.
	programmer.onFrame(fromTarget(Tpdu::disconnect()), kT0 + 1100ms);
	auto actions = programmer.takeActions();
	ASSERT(sentFrames(actions).empty());
	ASSERT_EQ(only<IaProgrammer::Finished>(actions),
		(std::vector<IaProgrammer::Finished>{{IaProgrammerFailure::AddressInUse, kFactory}}));
}

UNIT_TEST(FR_COM_1__iaProgrammerSkipsCheckWhenDeviceHasTarget)
{
	IaProgrammer programmer;
	programmer.start(kTarget, kT0);
	(void)programmer.takeActions();
	programmer.onFrame(iaResponseFrom(kTarget), kT0); // reprogramming the same address
	programmer.tick(kT0 + 1s);
	ASSERT_EQ(sentFrames(programmer.takeActions()), (std::vector<Bytes>{kIaWrite, kIaRead}));

	IaProgrammer overwrite{noCheck()};
	overwrite.start(kTarget, kT0);
	(void)overwrite.takeActions();
	overwrite.onFrame(iaResponseFrom(kFactory), kT0);
	overwrite.tick(kT0 + 1s);
	ASSERT_EQ(sentFrames(overwrite.takeActions()), (std::vector<Bytes>{kIaWrite, kIaRead}));
}

UNIT_TEST(FR_COM_1__iaProgrammerCancelDuringAddressCheck)
{
	IaProgrammer programmer;
	programmer.start(kTarget, kT0);
	programmer.onFrame(iaResponseFrom(kFactory), kT0);
	programmer.tick(kT0 + 1s);
	(void)programmer.takeActions();
	programmer.cancel(kT0 + 2s);
	auto actions = programmer.takeActions();
	ASSERT_EQ(sentFrames(actions), (std::vector<Bytes>{kDisconnect}));
	ASSERT_EQ(only<IaProgrammer::Finished>(actions),
		(std::vector<IaProgrammer::Finished>{{IaProgrammerFailure::Cancelled, kFactory}}));
}

UNIT_TEST(FR_COM_1__iaProgrammerVerifyRetriesSilence)
{
	IaProgrammer programmer{noCheck()};
	programmer.start(kTarget, kT0);
	programmer.onFrame(iaResponseFrom(kFactory), kT0);
	programmer.tick(kT0 + 1s);
	(void)programmer.takeActions();
	programmer.tick(kT0 + 2s); // attempt 1: silence → read again
	ASSERT_EQ(sentFrames(programmer.takeActions()), (std::vector<Bytes>{kIaRead}));
	programmer.tick(kT0 + 3s); // attempt 2
	ASSERT_EQ(sentFrames(programmer.takeActions()), (std::vector<Bytes>{kIaRead}));
	programmer.tick(kT0 + 4s); // attempt 3 exhausted
	ASSERT_EQ(only<IaProgrammer::Finished>(programmer.takeActions()),
		(std::vector<IaProgrammer::Finished>{{IaProgrammerFailure::VerifyFailed, kFactory}}));
}

UNIT_TEST(FR_COM_1__iaProgrammerCancelAndIgnoresOtherFrames)
{
	IaProgrammer programmer;
	programmer.start(kTarget, kT0);
	(void)programmer.takeActions();
	// Group traffic and individual frames are not IA responses.
	auto group = makeGroupFrame(GroupAddress(0x0801), groupValueResponseShort(1));
	group.source = 0x1101;
	programmer.onFrame(group, kT0);
	programmer.tick(kT0 + 1s);
	ASSERT(only<IaProgrammer::Finished>(programmer.takeActions()).empty());
	ASSERT(programmer.responders().empty());

	programmer.cancel(kT0 + 1500ms);
	ASSERT_EQ(only<IaProgrammer::Finished>(programmer.takeActions()),
		(std::vector<IaProgrammer::Finished>{{IaProgrammerFailure::Cancelled, IndividualAddress()}}));
	programmer.tick(kT0 + 10s);
	ASSERT(programmer.takeActions().empty());
}
