#include "kestrel/download/Executor.hpp"
#include "kestrel/download/Resolver.hpp"
#include "kestrel/sim/VirtualBus.hpp"
#include "kestrel/sim/VirtualDevice.hpp"

#include <Printers.hpp>
#include <UnitTests.hpp>

#include <deque>

using namespace kestrel;
using namespace kestrel::download;
using namespace std::chrono_literals;

namespace
{

constexpr std::uint16_t kDevice = 0x1105; // 1.1.5
constexpr std::uint16_t kClient = 0x11FF; // 1.1.255

const knx::TimePoint kT0 = knx::TimePoint{} + std::chrono::hours(1);

Bytes hex(std::string_view text)
{
	return fromHex(text).value();
}

LdCtrl ld(std::string name, std::map<std::string, std::string> attributes = {})
{
	return LdCtrl{std::move(name), std::move(attributes)};
}

/// The structure of the System B "ap1" load procedure (knx_master MV-07B0), hand-written.
std::vector<LdCtrl> systemBProcedure()
{
	return {
		ld("LdCtrlConnect"),
		ld("LdCtrlMerge", {{"MergeId", "1"}}),
		ld("LdCtrlMapError", {{"OriginalError", "3221498632"}, {"MappedError", "0"}}),
		ld("LdCtrlUnload", {{"LsmIdx", "5"}}),
		ld("LdCtrlMapError", {{"OriginalError", "3221498632"}, {"MappedError", "3221498632"}}),
		ld("LdCtrlUnload", {{"LsmIdx", "4"}}),
		ld("LdCtrlUnload", {{"LsmIdx", "3"}}),
		ld("LdCtrlUnload", {{"LsmIdx", "2"}}),
		ld("LdCtrlUnload", {{"LsmIdx", "1"}}),
		ld("LdCtrlLoad", {{"LsmIdx", "4"}}),
		ld("LdCtrlMerge", {{"MergeId", "2"}}),
		ld("LdCtrlLoad", {{"LsmIdx", "3"}}),
		ld("LdCtrlRelSegment", {{"LsmIdx", "3"}, {"Size", "2"}, {"Mode", "0"}, {"Fill", "0"}}),
		ld("LdCtrlLoad", {{"LsmIdx", "1"}}),
		ld("LdCtrlRelSegment", {{"LsmIdx", "1"}, {"Size", "2"}, {"Mode", "0"}, {"Fill", "0"}}),
		ld("LdCtrlLoad", {{"LsmIdx", "2"}}),
		ld("LdCtrlRelSegment", {{"LsmIdx", "2"}, {"Size", "2"}, {"Mode", "0"}, {"Fill", "0"}}),
		ld("LdCtrlMerge", {{"MergeId", "4"}}),
		ld("LdCtrlWriteRelMem", {{"ObjIdx", "3"}, {"Offset", "0"}, {"Size", "1048576"}, {"Verify", "true"}}),
		ld("LdCtrlWriteRelMem", {{"ObjIdx", "2"}, {"Offset", "0"}, {"Size", "1048576"}, {"Verify", "true"}}),
		ld("LdCtrlWriteRelMem", {{"ObjIdx", "1"}, {"Offset", "0"}, {"Size", "1048576"}, {"Verify", "true"}}),
		ld("LdCtrlWriteProp", {{"ObjIdx", "4"}, {"PropId", "13"}, {"Verify", "true"}, {"InlineData", "0000000000"}}),
		ld("LdCtrlLoadCompleted", {{"LsmIdx", "4"}}),
		ld("LdCtrlLoadCompleted", {{"LsmIdx", "3"}}),
		ld("LdCtrlLoadCompleted", {{"LsmIdx", "2"}}),
		ld("LdCtrlLoadCompleted", {{"LsmIdx", "1"}}),
		ld("LdCtrlMerge", {{"MergeId", "6"}}),
		ld("LdCtrlMerge", {{"MergeId", "7"}}),
		ld("LdCtrlRestart"),
	};
}

/// Application LoadProcedures shaped like a relative-segment application (M-FFFF test data).
std::map<int, std::vector<LdCtrl>> applicationMerges(std::size_t parameterSize)
{
	const std::string size = std::to_string(parameterSize);
	return {
		{2,
			{ld("LdCtrlRelSegment", {{"AppliesTo", "full"}, {"LsmIdx", "4"}, {"Size", size}, {"Mode", "1"}, {"Fill", "0"}}),
				ld("LdCtrlRelSegment",
					{{"AppliesTo", "par"}, {"LsmIdx", "4"}, {"Size", size}, {"Mode", "0"}, {"Fill", "0"}})}},
		{4,
			{ld("LdCtrlWriteRelMem",
				{{"AppliesTo", "full,par"}, {"ObjIdx", "4"}, {"Offset", "0"}, {"Size", size}, {"Verify", "true"}})}},
		{7,
			{ld("LdCtrlLoadImageProp", {{"ObjIdx", "1"}, {"PropId", "27"}}),
				ld("LdCtrlLoadImageProp", {{"ObjIdx", "2"}, {"PropId", "27"}}),
				ld("LdCtrlLoadImageProp", {{"ObjIdx", "3"}, {"PropId", "27"}}),
				ld("LdCtrlLoadImageProp", {{"ObjIdx", "4"}, {"PropId", "27"}})}},
	};
}

ResolveInput testInput()
{
	ResolveInput input;
	input.procedure = systemBProcedure();
	Bytes parameters(40);
	for (std::size_t i = 0; i < parameters.size(); ++i)
	{
		parameters[i] = static_cast<std::uint8_t>(i * 7);
	}
	input.merges = applicationMerges(parameters.size());
	input.images[1] = hex("00 02 08 06 10 06");             // 1/0/6, 2/0/6
	input.images[2] = hex("00 02 00 01 00 6b 00 02 00 6d"); // GA#1 → object 107, GA#2 → object 109
	input.images[3] = hex("00 03 00 00 17 00 4b 00");
	input.images[4] = parameters;
	input.programVersions[4] = hex("ff ff 00 01 21");
	return input;
}

/// Executor ⇄ VirtualBus ⇄ VirtualDevice, without sockets or threads.
struct Harness
{
	sim::VirtualBus bus;
	sim::VirtualDevice device{bus, sim::DeviceConfig{.name = "kaa", .address = knx::IndividualAddress(kDevice),
										 .serial = {}, .maskVersion = 0x07B0, .progMode = false, .groupObjects = {}}};
	std::deque<knx::cemi::LData> inbox;
	sim::EndpointId endpoint = bus.attach([this](const knx::cemi::LData& frame) { inbox.push_back(frame); });
	std::vector<Executor::Progress> progress;
	std::optional<Status> result;

	Status run(Executor& executor)
	{
		executor.setLocalAddress(knx::IndividualAddress(kClient));
		knx::TimePoint now = kT0;
		executor.start(now);
		for (int guard = 0; guard < 200000 && !result; ++guard)
		{
			for (auto& action : executor.takeActions())
			{
				if (auto* send = std::get_if<Executor::SendFrame>(&action))
				{
					send->frame.source = kClient;
					bus.transmit(endpoint, send->frame);
				}
				else if (auto* step = std::get_if<Executor::Progress>(&action))
				{
					progress.push_back(*step);
				}
				else
				{
					result = std::get<Executor::Finished>(action).status;
				}
			}
			if (!inbox.empty())
			{
				const auto frame = inbox.front();
				inbox.pop_front();
				executor.onFrame(frame, now);
			}
			else if (!result)
			{
				now += 100ms;
				executor.tick(now);
			}
		}
		return result.value_or(Status(makeError(ErrorCode::Timeout, "harness gave up")));
	}
};

} // namespace

UNIT_TEST(FR_COM_2__memoryControlBlockMatchesRealDevice)
{
	// Address table and PID_MCB_TABLE record read from a MEAN WELL KAA-8R (written by ETS6).
	const Bytes table = hex("00 11 00 01 08 01 08 02 08 03 08 04 08 05 08 06 08 07 08 08 10 01 10 02 10 03 10 04 10 05"
							" 10 06 10 07 10 08");
	ASSERT_EQ(crc16AugCcitt(table), std::uint16_t{0x6760});
	ASSERT_EQ(memoryControlBlock(table), hex("00 00 00 24 00 ff 67 60"));
	ASSERT_EQ(allocRelSegmentPayload(AllocRelSegment{4, 1798, 1, 0}), hex("03 0b 00 00 07 06 01 00 00 00"));
}

UNIT_TEST(FR_COM_2__resolverMergesAndSizesSystemBProcedure)
{
	auto plan = resolve(testInput());
	ASSERT(plan.has_value());
	const auto& steps = plan->steps;
	ASSERT_EQ(steps.size(), std::size_t{27});
	ASSERT_EQ(steps[0], Step(Unload{5, true}));
	ASSERT_EQ(steps[1], Step(Unload{4, false}));
	ASSERT_EQ(steps[5], Step(Load{4}));
	// Merge 2: only the "full" allocation applies.
	ASSERT_EQ(steps[6], Step(AllocRelSegment{4, 40, 1, 0}));
	// Table allocations use the image size, not the procedure's placeholder.
	ASSERT_EQ(steps[8], Step(AllocRelSegment{3, 8, 0, 0}));
	ASSERT_EQ(steps[10], Step(AllocRelSegment{1, 6, 0, 0}));
	ASSERT_EQ(steps[12], Step(AllocRelSegment{2, 10, 0, 0}));
	ASSERT_EQ(std::get<WriteRelMem>(steps[13]).object, std::uint8_t{4});
	ASSERT_EQ(std::get<WriteRelMem>(steps[13]).data.size(), std::size_t{40});
	ASSERT_EQ(steps[17], Step(WriteProp{4, 13, hex("ff ff 00 01 21"), true}));
	ASSERT_EQ(steps[22], Step(LoadImageProp{1, 27}));
	ASSERT_EQ(steps[26], Step(Restart{}));
}

UNIT_TEST(FR_COM_2__resolverRejectsUnknownElementsAndMissingImages)
{
	auto input = testInput();
	input.procedure.insert(input.procedure.begin(), ld("LdCtrlTaskSegment", {{"LsmIdx", "4"}, {"Address", "16384"}}));
	auto plan = resolve(input);
	ASSERT(!plan.has_value());
	ASSERT_EQ(plan.error().code, ErrorCode::Unsupported);

	input = testInput();
	input.images.erase(2);
	plan = resolve(input);
	ASSERT(!plan.has_value());
	ASSERT_EQ(plan.error().code, ErrorCode::InvalidArgument);
}

UNIT_TEST(FR_COM_2__downloadsSystemBApplication)
{
	Harness h;
	const auto input = testInput();
	Executor executor(knx::IndividualAddress(kDevice), resolve(input).value());
	const Status status = h.run(executor);
	ASSERT(status.has_value());
	ASSERT_EQ(h.progress.size(), std::size_t{27});

	const std::uint16_t segments[] = {0x1002, 0x1804, 0x2806, 0x2A18};
	for (std::uint8_t object = 1; object <= 4; ++object)
	{
		const Bytes& image = input.images.at(object);
		ASSERT_EQ(h.device.readMemory(segments[object - 1], image.size()), image);
		ASSERT_EQ(*h.device.loadState(object), std::uint8_t{1});
		ASSERT_EQ(*h.device.property(object, kPidMcbTable), memoryControlBlock(image));
	}
	ASSERT_EQ(*h.device.loadState(5), std::uint8_t{0}); // "ap1" leaves the interface program unloaded
	ASSERT_EQ(*h.device.property(4, kPidProgramVersion), hex("ff ff 00 01 21"));
	ASSERT_EQ(h.device.restartCount(), std::uint32_t{1});
}

UNIT_TEST(FR_COM_2__downloadStopsOnUnexpectedLoadState)
{
	// LoadCompleted without Load: the device goes to "error"; nothing after that step may run.
	Harness h;
	Plan plan{{Unload{4, false}, LoadCompleted{4}, WriteProp{4, 13, hex("01 02 03 04 05"), true}, Restart{}}};
	Executor executor(knx::IndividualAddress(kDevice), plan);
	const Status status = h.run(executor);
	ASSERT(!status.has_value());
	ASSERT_EQ(status.error().code, ErrorCode::ProtocolError);
	ASSERT(status.error().message.find("error") != std::string::npos);
	ASSERT_EQ(*h.device.property(4, kPidProgramVersion), Bytes(5, 0x00));
	ASSERT_EQ(h.device.restartCount(), std::uint32_t{0});
	ASSERT_EQ(h.progress.size(), std::size_t{2});
}

UNIT_TEST(FR_COM_2__toleratedUnloadFailureContinues)
{
	// Object 6 does not exist: the refused unload is mapped to success, as LdCtrlMapError asks.
	Harness h;
	Plan plan{{Unload{6, true}, Unload{4, false}}};
	Executor executor(knx::IndividualAddress(kDevice), plan);
	ASSERT(h.run(executor).has_value());
	Plan strict{{Unload{6, false}}};
	Harness h2;
	Executor failing(knx::IndividualAddress(kDevice), strict);
	ASSERT(!h2.run(failing).has_value());
}
