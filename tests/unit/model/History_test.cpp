#include "SampleProject.hpp"

#include "kestrel/model/Operations.hpp"

#include <Printers.hpp>
#include <QtPrinters.hpp>
#include <UnitTests.hpp>

#include <stdexcept>

using namespace kestrel;
using namespace kestrel::model;
using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace
{

/// Records every ChangeSet emitted by a project. Not movable (the callback captures `this`).
struct Recorder
{
	std::vector<ChangeSet> sets;
	Subscription subscription;

	explicit Recorder(Project& project)
		: subscription(project.onChanged([this](const ChangeSet& changeSet) { sets.push_back(changeSet); }))
	{
	}
	Recorder(const Recorder&) = delete;
	Recorder& operator=(const Recorder&) = delete;
};

struct ManualClock
{
	History::TimePoint now{};

	History::Clock clock()
	{
		return [this] { return now; };
	}
};

Area makeArea(unsigned number)
{
	Area area;
	area.number = number;
	area.name = u"Area %1"_s.arg(number);
	return area;
}

Change added(EntityRef ref)
{
	return {Change::Added, ref, false};
}

Change removed(EntityRef ref)
{
	return {Change::Removed, ref, false};
}

Change modified(EntityRef ref, bool structural = false)
{
	return {Change::Modified, ref, structural};
}

} // namespace

// --- Primitive ops -------------------------------------------------------------------------------------------

UNIT_TEST(FR_PRJ_2__undoRedoInsertPrimitive)
{
	Project project(IdGenerator::sequential());
	const Project empty = project;
	Id<Area> id;
	ASSERT(project
			   .edit(u"Insert area"_s,
				   [&](Tx& tx)
				   {
					   id = tx.insert(makeArea(3));
					   return ok();
				   })
			   .has_value());
	ASSERT_EQ(id, Id<Area>(1)); // assigned from the generator
	const Project after = project;
	ASSERT_EQ(project.history().undoText(), u"Insert area"_s);
	ASSERT(project.history().undo());
	ASSERT_EQ(project, empty);
	ASSERT_FALSE(project.history().undo());
	ASSERT(project.history().redo());
	ASSERT_EQ(project, after);
	ASSERT_FALSE(project.history().redo());
}

UNIT_TEST(FR_PRJ_2__undoRedoErasePrimitiveRestoresRowPosition)
{
	Project project(IdGenerator::sequential());
	std::vector<Id<Area>> ids;
	ASSERT(project
			   .edit(u"Insert"_s,
				   [&](Tx& tx)
				   {
					   for (unsigned n = 0; n < 4; ++n)
						   ids.push_back(tx.insert(makeArea(n)));
					   return ok();
				   })
			   .has_value());
	const Project before = project;
	ASSERT(project
			   .edit(u"Erase"_s,
				   [&](Tx& tx)
				   {
					   tx.erase(ids[1]);
					   return ok();
				   })
			   .has_value());
	ASSERT_EQ(project.areas().size(), std::size_t{3});
	ASSERT_EQ(project.areas().indexOf(ids[2]), std::optional<std::size_t>(1));
	const Project after = project;
	ASSERT(project.history().undo());
	ASSERT_EQ(project, before);
	ASSERT_EQ(project.areas().indexOf(ids[1]), std::optional<std::size_t>(1));
	ASSERT_EQ(project.areas()[1].id, ids[1]);
	ASSERT(project.history().redo());
	ASSERT_EQ(project, after);
}

UNIT_TEST(FR_PRJ_2__undoRedoSetFieldPrimitive, SampleProject)
{
	const Project before = project;
	ASSERT(project
			   .edit(u"Describe"_s,
				   [&](Tx& tx)
				   {
					   tx.set(actuator, &Device::description, u"Kitchen cabinet"_s);
					   tx.set(lightSwitch, &GroupAddress::raw, std::uint16_t{0x0905});
					   tx.set(actuatorSwitchLink, &Link::sending, false);
					   return ok();
				   })
			   .has_value());
	const Project after = project;
	ASSERT_EQ(project.devices().at(actuator).description, u"Kitchen cabinet"_s);
	ASSERT(project.history().undo());
	ASSERT_EQ(project, before);
	ASSERT(project.history().redo());
	ASSERT_EQ(project, after);
}

UNIT_TEST(FR_PRJ_2__undoRedoReplaceSettingsAndParamPrimitives, SampleProject)
{
	const Project before = project;
	ASSERT(project
			   .edit(u"Several"_s,
				   [&](Tx& tx)
				   {
					   ComObject object = tx.project().comObjects().at(actuatorStatus);
					   object.flags.update = true;
					   tx.replace(object);
					   ProjectSettings settings = tx.project().settings();
					   settings.gaStyle = knx::GroupAddressStyle::Free;
					   tx.setSettings(settings);
					   tx.setParam(actuator, u"P-1"_s, u"42"_s);
					   tx.setParam(actuator, u"P-2"_s, u"7"_s);
					   tx.setParam(actuator, u"P-2"_s, std::nullopt);
					   return ok();
				   })
			   .has_value());
	const Project after = project;
	ASSERT(project.comObjects().at(actuatorStatus).flags.update);
	ASSERT(project.settings().gaStyle == knx::GroupAddressStyle::Free);
	ASSERT_EQ(project.devices().at(actuator).params.size(), std::size_t{1});
	ASSERT(project.history().undo());
	ASSERT_EQ(project, before);
	ASSERT(project.history().redo());
	ASSERT_EQ(project, after);
}

// --- Cascades ------------------------------------------------------------------------------------------------

UNIT_TEST(FR_PRJ_2__undoRestoresRemovedDeviceWithLinks, SampleProject)
{
	const Project before = project;
	ASSERT(removeDevice(project, actuator).has_value());
	const Project after = project;
	ASSERT(project.history().undo());
	ASSERT_EQ(project, before);
	ASSERT_EQ(project.linksOfGroupAddress(lightSwitch), (std::vector<Id<Link>>{actuatorSwitchLink, buttonSwitchLink}));
	ASSERT(project.history().redo());
	ASSERT_EQ(project, after);
}

UNIT_TEST(FR_PRJ_2__undoRestoresRemovedAreaCascade, SampleProject)
{
	const Project before = project;
	ASSERT(removeArea(project, area).has_value());
	ASSERT(project.links().empty());
	ASSERT(project.history().undo());
	ASSERT_EQ(project, before);
}

UNIT_TEST(FR_PRJ_2__undoRestoresRemovedGroupRangeCascade, SampleProject)
{
	const Project before = project;
	ASSERT(removeGroupRange(project, mainGroup).has_value());
	ASSERT(project.history().undo());
	ASSERT_EQ(project, before);
}

UNIT_TEST(FR_PRJ_2__undoRestoresSendingFlagsAfterPromotion, SampleProject)
{
	ASSERT(link(project, actuatorSwitch, lightStatus).has_value());
	const Project before = project;
	ASSERT(removeGroupAddress(project, lightSwitch).has_value());
	ASSERT(project.history().undo());
	ASSERT_EQ(project, before);
	ASSERT(unlink(project, actuatorSwitchLink).has_value());
	ASSERT(project.history().undo());
	ASSERT_EQ(project, before);
}

UNIT_TEST(FR_PRJ_2__undoRestoresRemovedBuildingPartAndPlacements, SampleProject)
{
	const Project before = project;
	ASSERT(removeBuildingPart(project, building).has_value());
	ASSERT(project.buildingParts().empty());
	ASSERT(project.history().undo());
	ASSERT_EQ(project, before);
	ASSERT_EQ(project.devicesIn(room), (std::vector<Id<Device>>{actuator, button}));
}

UNIT_TEST(FR_PRJ_2__everyOperationUndoesAndRedoesExactly)
{
	Project project(IdGenerator::sequential());
	ManualClock clock;
	project.history().setClock(clock.clock());
	std::vector<Project> snapshots{project};
	const auto snap = [&]
	{
		snapshots.push_back(project);
		clock.now += 2s; // never merge
	};

	const auto building = addBuildingPart(project, std::nullopt, BuildingPartKind::Building, u"B"_s).value();
	snap();
	const auto room = addBuildingPart(project, building, BuildingPartKind::Room, u"R"_s).value();
	snap();
	const auto area = addArea(project, 1, u"A"_s).value();
	snap();
	const auto line = addLine(project, area, 1, u"L"_s).value();
	snap();
	const auto line2 = addLine(project, area, 2, u"L2"_s).value();
	snap();
	const auto device = addDevice(project, line, std::nullopt, u"D"_s).value();
	snap();
	const auto device2 = addDevice(project, line, 2u, u"D2"_s).value();
	snap();
	ASSERT(setDeviceNumber(project, device, 1u).has_value());
	snap();
	ASSERT(placeDevice(project, device, room).has_value());
	snap();
	ASSERT(setDeviceParam(project, device, u"P-1"_s, u"1"_s).has_value());
	snap();
	ComObject object;
	object.number = 1;
	object.flags.transmit = true;
	const auto co = addComObject(project, device, object).value();
	snap();
	object.number = 2;
	const auto co2 = addComObject(project, device2, object).value();
	snap();
	const auto range = addGroupRange(project, std::nullopt, u"Main"_s, 0, 0x07FF).value();
	snap();
	const auto ga = addGroupAddress(project, knx::GroupAddress(1), u"G1"_s, u"DPST-1-1"_s, range).value();
	snap();
	const auto ga2 = addGroupAddress(project, knx::GroupAddress(2), u"G2"_s).value();
	snap();
	link(project, co, ga).value();
	snap();
	const auto lnk2 = link(project, co, ga2).value();
	snap();
	link(project, co2, ga).value();
	snap();
	ASSERT(setSendingLink(project, lnk2).has_value());
	snap();
	ASSERT(setGroupAddressDpt(project, ga2, u"DPT-9"_s).has_value());
	snap();
	ASSERT(rename(project, device, u"Renamed"_s).has_value());
	snap();
	const ProjectSettings settings{u"P"_s, knx::GroupAddressStyle::TwoLevel, u"de-DE"_s};
	ASSERT(updateSettings(project, settings).has_value());
	snap();
	ASSERT(moveDevice(project, device, line2).has_value());
	snap();
	ASSERT(unlink(project, lnk2).has_value());
	snap();
	ASSERT(removeComObject(project, co2).has_value());
	snap();
	ASSERT(removeGroupAddress(project, ga).has_value());
	snap();
	ASSERT(removeDevice(project, device2).has_value());
	snap();
	ASSERT(removeLine(project, line2).has_value());
	snap();
	ASSERT(removeBuildingPart(project, room).has_value());
	snap();
	ASSERT(removeGroupRange(project, range).has_value());
	snap();
	ASSERT(removeArea(project, area).has_value());
	snap();

	ASSERT_EQ(project.history().count(), snapshots.size() - 1);
	for (std::size_t i = snapshots.size() - 1; i > 0; --i)
	{
		ASSERT(project.history().undo());
		ASSERT(project == snapshots[i - 1]);
	}
	ASSERT_FALSE(project.history().canUndo());
	for (std::size_t i = 1; i < snapshots.size(); ++i)
	{
		ASSERT(project.history().redo());
		ASSERT(project == snapshots[i]);
	}
	ASSERT_FALSE(project.history().canRedo());
}

// --- History bookkeeping -------------------------------------------------------------------------------------

UNIT_TEST(FR_PRJ_2__undoTextAndRedoTail, SampleProject)
{
	ASSERT_FALSE(project.history().canUndo());
	ASSERT_EQ(project.history().undoText(), QString());
	ASSERT(addArea(project, 2, u"Two"_s).has_value());
	ASSERT(rename(project, actuator, u"X"_s).has_value());
	ASSERT_EQ(project.history().undoText(), u"Rename"_s);
	ASSERT(project.history().undo());
	ASSERT(project.history().canRedo());
	ASSERT_EQ(project.history().redoText(), u"Rename"_s);
	ASSERT_EQ(project.history().undoText(), u"Add area"_s);
	ASSERT_EQ(project.history().index(), std::size_t{1});
	// A new edit drops the redo tail.
	ASSERT(addArea(project, 3, u"Three"_s).has_value());
	ASSERT_FALSE(project.history().canRedo());
	ASSERT_EQ(project.history().count(), std::size_t{2});
	ASSERT_EQ(project.devices().at(actuator).name, u"Switch actuator"_s);
}

UNIT_TEST(FR_PRJ_2__transactionRollsBackOnErrorStatus, SampleProject)
{
	Recorder recorder(project);
	const Project before = project;
	const Status status = project.edit(u"Failing"_s,
		[&](Tx& tx) -> Status
		{
			tx.insert(makeArea(9));
			tx.erase(button);
			tx.set(actuator, &Device::name, u"changed"_s);
			return makeError(ErrorCode::Conflict, "nope");
		});
	ASSERT_FALSE(status.has_value());
	ASSERT_EQ(status.error().code, ErrorCode::Conflict);
	ASSERT_EQ(project, before);
	ASSERT_FALSE(project.history().canUndo());
	ASSERT(recorder.sets.empty());
	ASSERT_FALSE(project.isDirty());
}

UNIT_TEST(FR_PRJ_2__transactionRollsBackOnException, SampleProject)
{
	Recorder recorder(project);
	const Project before = project;
	const Status status = project.edit(u"Throwing"_s,
		[&](Tx& tx) -> Status
		{
			tx.insert(makeArea(9));
			tx.erase(lightStatus);
			throw std::runtime_error("boom");
		});
	ASSERT_FALSE(status.has_value());
	ASSERT(status.error().message.find("boom") != std::string::npos);
	ASSERT_EQ(project, before);
	ASSERT(recorder.sets.empty());
	ASSERT_FALSE(project.history().canUndo());
}

UNIT_TEST(FR_PRJ_2__failedPrimitiveAbortsTransaction, SampleProject)
{
	const Project before = project;
	bool failedInside = false;
	Status status = project.edit(u"Bad erase"_s,
		[&](Tx& tx)
		{
			tx.insert(makeArea(9));
			tx.erase(Id<Device>(1)); // unknown
			failedInside = tx.failed();
			return ok();
		});
	ASSERT(failedInside);
	ASSERT_EQ(status.error().code, ErrorCode::NotFound);
	ASSERT_EQ(project, before);

	status = project.edit(u"Duplicate id"_s,
		[&](Tx& tx)
		{
			Area duplicate = makeArea(9);
			duplicate.id = area;
			tx.insert(duplicate);
			return ok();
		});
	ASSERT_EQ(status.error().code, ErrorCode::Conflict);
	status = project.edit(u"Bad set"_s,
		[&](Tx& tx)
		{
			tx.set(Id<GroupAddress>(3), &GroupAddress::name, u"x"_s);
			return ok();
		});
	ASSERT_EQ(status.error().code, ErrorCode::NotFound);
	ASSERT_EQ(project, before);
	ASSERT_FALSE(project.history().canUndo());
}

UNIT_TEST(FR_PRJ_2__nestedEditsFoldIntoOneStep, SampleProject)
{
	Recorder recorder(project);
	Id<Area> two;
	Id<Area> three;
	const Status status = project.edit(u"Batch"_s,
		[&](Tx&) -> Status
		{
			KESTREL_TRY(two, addArea(project, 2, u"Two"_s));
			KESTREL_TRY(three, addArea(project, 3, u"Three"_s));
			return ok();
		});
	ASSERT(status.has_value());
	ASSERT_EQ(project.history().count(), std::size_t{1});
	ASSERT_EQ(project.history().undoText(), u"Batch"_s);
	ASSERT_EQ(recorder.sets.size(), std::size_t{1});
	ASSERT_EQ(recorder.sets[0].changes, (std::vector<Change>{added(two), added(three)}));
	ASSERT(project.history().undo());
	ASSERT_EQ(project.areas().size(), std::size_t{1});
}

UNIT_TEST(FR_PRJ_2__nestedFailureRollsBackOnlyItsOwnOps, SampleProject)
{
	Id<Area> kept;
	const Status status = project.edit(u"Outer"_s,
		[&](Tx& tx)
		{
			kept = tx.insert(makeArea(5));
			const Status inner = project.edit(u"Inner"_s,
				[&](Tx& innerTx) -> Status
				{
					innerTx.insert(makeArea(6));
					innerTx.erase(Id<Line>(1)); // fails the inner body
					return ok();
				});
			// The outer body decides to continue.
			return inner.has_value() ? makeError(ErrorCode::Conflict, "unexpected") : ok();
		});
	ASSERT(status.has_value());
	ASSERT_EQ(project.areas().size(), std::size_t{2});
	ASSERT(project.areas().contains(kept));
	ASSERT_EQ(project.history().count(), std::size_t{1});
}

UNIT_TEST(FR_PRJ_2__noOpEditCreatesNoStep, SampleProject)
{
	Recorder recorder(project);
	ASSERT(project.edit(u"Nothing"_s, [](Tx&) { return ok(); }).has_value());
	ASSERT(project
			   .edit(u"Same value"_s,
				   [&](Tx& tx)
				   {
					   tx.set(actuator, &Device::name, u"Switch actuator"_s);
					   tx.setSettings(tx.project().settings());
					   return ok();
				   })
			   .has_value());
	ASSERT_FALSE(project.history().canUndo());
	ASSERT(recorder.sets.empty());
	ASSERT_FALSE(project.isDirty());
}

// --- ChangeSet -----------------------------------------------------------------------------------------------

UNIT_TEST(FR_PRJ_2__changeSetOfCascadeIsChildrenFirst, SampleProject)
{
	Recorder recorder(project);
	ASSERT(removeDevice(project, actuator).has_value());
	ASSERT_EQ(recorder.sets.size(), std::size_t{1});
	const std::vector<Change> removal{removed(actuatorSwitchLink), removed(actuatorStatusLink),
		removed(actuatorSwitch), removed(actuatorStatus), removed(actuator)};
	ASSERT_EQ(recorder.sets[0].changes, removal);
	ASSERT_FALSE(recorder.sets[0].fromUndo);
	ASSERT_FALSE(recorder.sets[0].stateOnly);

	ASSERT(project.history().undo());
	ASSERT_EQ(recorder.sets.size(), std::size_t{2});
	const std::vector<Change> restore{added(actuator), added(actuatorStatus), added(actuatorSwitch),
		added(actuatorStatusLink), added(actuatorSwitchLink)};
	ASSERT_EQ(recorder.sets[1].changes, restore);
	ASSERT(recorder.sets[1].fromUndo);

	ASSERT(project.history().redo());
	ASSERT_EQ(recorder.sets[2].changes, removal);
	ASSERT(recorder.sets[2].fromUndo);
}

UNIT_TEST(FR_PRJ_2__changeSetMarksStructuralChanges, SampleProject)
{
	Recorder recorder(project);
	const auto line2 = addLine(project, area, 2, u"L2"_s);
	ASSERT(moveDevice(project, actuator, *line2).has_value());
	ASSERT(placeDevice(project, button, floor).has_value());
	ASSERT(rename(project, button, u"Button"_s).has_value());
	ASSERT(setSendingLink(project, actuatorSwitchLink).has_value()); // already sending: nothing
	ProjectSettings settings = project.settings();
	settings.name = u"Other"_s;
	ASSERT(updateSettings(project, settings).has_value());

	ASSERT_EQ(recorder.sets.size(), std::size_t{5});
	ASSERT_EQ(recorder.sets[0].changes, std::vector<Change>{added(*line2)});
	ASSERT_EQ(recorder.sets[1].changes, std::vector<Change>{modified(actuator, true)});
	ASSERT_EQ(recorder.sets[2].changes, std::vector<Change>{modified(button, true)});
	ASSERT_EQ(recorder.sets[3].changes, std::vector<Change>{modified(button, false)});
	ASSERT_EQ(recorder.sets[4].changes, std::vector<Change>{modified(EntityRef::project())});
	ASSERT(recorder.sets[4].touches(EntityKind::Project));
	ASSERT(recorder.sets[1].contains(actuator));
	ASSERT_FALSE(recorder.sets[1].contains(button));
}

UNIT_TEST(FR_PRJ_2__changeSetCoalescesPerEntity, SampleProject)
{
	Recorder recorder(project);
	Id<Area> fresh;
	ASSERT(project
			   .edit(u"Add and edit"_s,
				   [&](Tx& tx)
				   {
					   fresh = tx.insert(makeArea(7));
					   tx.set(fresh, &Area::name, u"Seven"_s);
					   tx.set(actuator, &Device::name, u"A"_s);
					   tx.set(actuator, &Device::line, line); // unchanged: no op
					   tx.set(actuator, &Device::place, floor);
					   Area temporary = makeArea(8);
					   tx.erase(tx.insert(temporary));
					   return ok();
				   })
			   .has_value());
	ASSERT_EQ(recorder.sets.size(), std::size_t{1});
	ASSERT_EQ(recorder.sets[0].changes, (std::vector<Change>{added(fresh), modified(actuator, true)}));

	const std::vector<Change> raw{modified(Id<Area>(1)), added(Id<Area>(2)), modified(Id<Area>(2), true),
		removed(Id<Area>(1)), added(Id<Area>(3)), removed(Id<Area>(3)), modified(Id<Line>(1)),
		modified(Id<Line>(1), true)};
	ASSERT_EQ(ChangeSet::normalized(raw).changes,
		(std::vector<Change>{added(Id<Area>(2)), removed(Id<Area>(1)), modified(Id<Line>(1), true)}));
}

// --- Merging -------------------------------------------------------------------------------------------------

UNIT_TEST(FR_PRJ_2__consecutiveEditsWithSameKeyMergeWithinOneSecond, SampleProject)
{
	ManualClock clock;
	project.history().setClock(clock.clock());
	Recorder recorder(project);

	ASSERT(rename(project, actuator, u"A"_s).has_value());
	clock.now += 500ms;
	ASSERT(rename(project, actuator, u"AB"_s).has_value());
	clock.now += 900ms; // 1.4 s after the first edit, 0.9 s after the last: still merges
	ASSERT(rename(project, actuator, u"ABC"_s).has_value());
	ASSERT_EQ(project.history().count(), std::size_t{1});
	ASSERT_EQ(recorder.sets.size(), std::size_t{3}); // every edit still notifies

	clock.now += 1001ms;
	ASSERT(rename(project, actuator, u"ABCD"_s).has_value());
	ASSERT_EQ(project.history().count(), std::size_t{2});

	clock.now += 100ms;
	ASSERT(rename(project, button, u"B"_s).has_value()); // other key
	ASSERT_EQ(project.history().count(), std::size_t{3});

	ASSERT(project.history().undo());
	ASSERT(project.history().undo());
	ASSERT_EQ(project.devices().at(actuator).name, u"ABC"_s);
	ASSERT(project.history().undo());
	ASSERT_EQ(project.devices().at(actuator).name, u"Switch actuator"_s);
	ASSERT_FALSE(project.history().canUndo());
}

UNIT_TEST(FR_PRJ_2__parameterEditsMerge, SampleProject)
{
	ManualClock clock;
	project.history().setClock(clock.clock());
	for (int i = 1; i <= 5; ++i)
	{
		ASSERT(setDeviceParam(project, actuator, u"P-1"_s, QString::number(i)).has_value());
		clock.now += 200ms;
	}
	ASSERT_EQ(project.history().count(), std::size_t{1});
	ASSERT_EQ(project.devices().at(actuator).params.at(u"P-1"_s), u"5"_s);
	ASSERT(project.history().undo());
	ASSERT(project.devices().at(actuator).params.empty());
	ASSERT(project.history().redo());
	ASSERT_EQ(project.devices().at(actuator).params.at(u"P-1"_s), u"5"_s);
}

UNIT_TEST(FR_PRJ_2__mergeStopsAtCleanStateAndOtherEdits, SampleProject)
{
	ManualClock clock;
	project.history().setClock(clock.clock());
	ASSERT(rename(project, actuator, u"A"_s).has_value());
	project.history().setClean();
	clock.now += 100ms;
	ASSERT(rename(project, actuator, u"AB"_s).has_value());
	ASSERT_EQ(project.history().count(), std::size_t{2}); // the saved step is never modified
	clock.now += 100ms;
	ASSERT(addArea(project, 2, u"Two"_s).has_value());
	clock.now += 100ms;
	ASSERT(rename(project, actuator, u"ABC"_s).has_value());
	ASSERT_EQ(project.history().count(), std::size_t{4}); // not consecutive
}

// --- Clean state / recordState -------------------------------------------------------------------------------

UNIT_TEST(FR_PRJ_2__cleanStateFollowsHistoryPosition, SampleProject)
{
	ASSERT_FALSE(project.isDirty());
	ASSERT(addArea(project, 2, u"Two"_s).has_value());
	ASSERT(project.isDirty());
	ASSERT(project.history().undo());
	ASSERT_FALSE(project.isDirty());
	ASSERT(project.history().redo());
	ASSERT(project.isDirty());
	project.history().setClean();
	ASSERT_FALSE(project.isDirty());
	ASSERT(project.history().undo());
	ASSERT(project.isDirty());
	ASSERT(addArea(project, 3, u"Three"_s).has_value()); // the saved state is now unreachable
	ASSERT(project.history().undo());
	ASSERT(project.isDirty());
	ASSERT(project.history().redo());
	ASSERT(project.isDirty());
}

UNIT_TEST(FR_PRJ_2__recordStateIsNotUndoableButMarksDirty, SampleProject)
{
	Recorder recorder(project);
	const DeviceState state{true, u"00FA:12345678"_s, u"sha256:abc"_s};
	project.recordState(actuator, state);
	ASSERT_EQ(project.deviceState(actuator), state);
	ASSERT_EQ(project.deviceState(button), DeviceState{});
	ASSERT(project.isDirty());
	ASSERT_FALSE(project.history().canUndo());
	ASSERT_EQ(recorder.sets.size(), std::size_t{1});
	ASSERT(recorder.sets[0].stateOnly);
	ASSERT_FALSE(recorder.sets[0].fromUndo);
	ASSERT_EQ(recorder.sets[0].changes, std::vector<Change>{modified(actuator)});

	// Undoing unrelated edits never touches the state.
	ASSERT(rename(project, actuator, u"X"_s).has_value());
	ASSERT(project.history().undo());
	ASSERT_EQ(project.deviceState(actuator), state);
	ASSERT(project.isDirty()); // back at the clean index, but the state changed since
	project.history().setClean();
	ASSERT_FALSE(project.isDirty());

	project.recordState(Id<Device>(1), state); // unknown device: ignored
	ASSERT_FALSE(project.isDirty());
	ASSERT_EQ(recorder.sets.size(), std::size_t{3});
}

// --- Subscriptions and copies --------------------------------------------------------------------------------

UNIT_TEST(FR_PRJ_2__subscriptionsAreRaii, SampleProject)
{
	int first = 0;
	int second = 0;
	Subscription a = project.onChanged([&](const ChangeSet&) { ++first; });
	{
		Subscription b = project.onChanged([&](const ChangeSet&) { ++second; });
		ASSERT(b.isActive());
		ASSERT(addArea(project, 2, u"Two"_s).has_value());
	}
	ASSERT(addArea(project, 3, u"Three"_s).has_value());
	ASSERT_EQ(first, 2);
	ASSERT_EQ(second, 1);

	Subscription moved = std::move(a);
	ASSERT_FALSE(a.isActive());
	ASSERT(project.history().undo());
	ASSERT_EQ(first, 3);
	moved.reset();
	ASSERT_FALSE(moved.isActive());
	ASSERT(project.history().undo());
	ASSERT_EQ(first, 3);

	Subscription outlives;
	{
		Project temporary(IdGenerator::sequential());
		outlives = temporary.onChanged([](const ChangeSet&) {});
		ASSERT(outlives.isActive());
	}
	ASSERT_FALSE(outlives.isActive());
	outlives.reset(); // safe after the project is gone
}

UNIT_TEST(FR_PRJ_2__copiesAreIndependentSnapshots, SampleProject)
{
	int notified = 0;
	Subscription subscription = project.onChanged([&](const ChangeSet&) { ++notified; });
	ASSERT(addArea(project, 2, u"Two"_s).has_value());
	ASSERT_EQ(notified, 1);

	Project copy = project;
	ASSERT_EQ(copy, project);
	ASSERT(rename(copy, actuator, u"Copy"_s).has_value());
	ASSERT_EQ(notified, 1); // subscriptions are not copied
	ASSERT_EQ(project.devices().at(actuator).name, u"Switch actuator"_s);
	ASSERT(copy.history().undo());
	ASSERT(copy.history().undo()); // the copied history acts on the copy
	ASSERT_EQ(copy.areas().size(), std::size_t{1});
	ASSERT_EQ(project.areas().size(), std::size_t{2});
	ASSERT_EQ(notified, 1);

	Project moved = std::move(project);
	ASSERT(moved.history().undo()); // subscriptions move with the project
	ASSERT_EQ(notified, 2);
	ASSERT_EQ(moved.areas().size(), std::size_t{1});
}

UNIT_TEST(FR_PRJ_1__tableIsOrderedAndCopyOnWrite, SampleProject)
{
	const Table<Device> snapshot = project.devices();
	ASSERT_EQ(snapshot.size(), std::size_t{2});
	ASSERT_EQ(snapshot[0].id, actuator);
	ASSERT_EQ(snapshot.indexOf(button), std::optional<std::size_t>(1));
	ASSERT(snapshot.find(Id<Device>(1)) == nullptr);
	ASSERT(removeDevice(project, actuator).has_value());
	ASSERT_EQ(snapshot.size(), std::size_t{2}); // detached
	ASSERT_EQ(project.devices().size(), std::size_t{1});
	ASSERT_EQ(project.devices().indexOf(button), std::optional<std::size_t>(0));
}

UNIT_TEST(FR_PRJ_2__undoIsRefusedInsideATransaction, SampleProject)
{
	ASSERT(addArea(project, 2, u"Two"_s).has_value());
	bool undone = true;
	ASSERT(project
			   .edit(u"Body"_s,
				   [&](Tx& tx)
				   {
					   tx.set(actuator, &Device::name, u"X"_s);
					   undone = project.history().undo() || project.history().redo();
					   return ok();
				   })
			   .has_value());
	ASSERT_FALSE(undone);
	ASSERT_EQ(project.areas().size(), std::size_t{2});
	ASSERT_EQ(project.history().count(), std::size_t{2});
}

UNIT_TEST(FR_PRJ_2__cancellingTransactionRecordsNothing, SampleProject)
{
	Recorder recorder(project);
	const Project before = project;
	ASSERT(project
			   .edit(u"Insert and erase"_s,
				   [&](Tx& tx)
				   {
					   Area temporary;
					   temporary.number = 9;
					   const Id<Area> id = tx.insert(temporary);
					   tx.set(id, &Area::name, u"Temporary"_s);
					   tx.erase(id);
					   return ok();
				   })
			   .has_value());
	ASSERT_EQ(project, before);
	ASSERT_FALSE(project.history().canUndo());
	ASSERT_FALSE(project.isDirty());
	ASSERT(recorder.sets.empty());
}

UNIT_TEST(FR_PRJ_2__observersSeePostUndoRedoHistoryState)
{
	// Observers (e.g. the GUI's dirty flag and Undo/Redo enablement) read History inside the
	// change callback; they must see the state after the undo/redo, not before it.
	Project project(IdGenerator::sequential(1));
	project.history().setClean();
	ASSERT(addArea(project, 1, u"Area"_s).has_value());

	bool canUndoSeen = false;
	bool canRedoSeen = false;
	bool cleanSeen = false;
	Subscription subscription = project.onChanged(
		[&](const ChangeSet&)
		{
			canUndoSeen = project.history().canUndo();
			canRedoSeen = project.history().canRedo();
			cleanSeen = project.history().isClean();
		});

	ASSERT(project.history().undo());
	ASSERT_FALSE(canUndoSeen);
	ASSERT(canRedoSeen);
	ASSERT(cleanSeen);

	ASSERT(project.history().redo());
	ASSERT(canUndoSeen);
	ASSERT_FALSE(canRedoSeen);
	ASSERT_FALSE(cleanSeen);
}
