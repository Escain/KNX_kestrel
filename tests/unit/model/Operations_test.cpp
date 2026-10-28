#include "SampleProject.hpp"

#include "kestrel/model/Operations.hpp"

#include <Printers.hpp>
#include <QtPrinters.hpp>
#include <UnitTests.hpp>

using namespace kestrel;
using namespace kestrel::model;
using namespace Qt::StringLiterals;

namespace
{

template <class T>
ErrorCode codeOf(const Result<T>& result)
{
	return result.error().code;
}

ComObject oneBit(unsigned number)
{
	ComObject object;
	object.number = number;
	object.name = u"Object"_s;
	object.sizeInBit = 1;
	return object;
}

} // namespace

// --- Building ------------------------------------------------------------------------------------------------

UNIT_TEST(FR_BLD_1__buildingTreeHierarchy)
{
	Project project(IdGenerator::sequential());
	const auto building = addBuildingPart(project, std::nullopt, BuildingPartKind::Building, u"House"_s);
	ASSERT(building.has_value());
	const auto floorPart = addBuildingPart(project, *building, BuildingPartKind::Floor, u"First"_s);
	const auto room = addBuildingPart(project, *floorPart, BuildingPartKind::Room, u"Kitchen"_s);
	const auto board = addBuildingPart(project, *floorPart, BuildingPartKind::DistributionBoard, u"Board"_s);
	ASSERT(room.has_value() && board.has_value());

	ASSERT_EQ(project.buildingParts().size(), std::size_t{4});
	ASSERT_EQ(project.buildingPartsIn(std::nullopt), std::vector<Id<BuildingPart>>{*building});
	ASSERT_EQ(project.buildingPartsIn(*building), std::vector<Id<BuildingPart>>{*floorPart});
	ASSERT_EQ(project.buildingPartsIn(*floorPart), (std::vector<Id<BuildingPart>>{*room, *board}));
	const BuildingPart& kitchen = project.buildingParts().at(*room);
	ASSERT_EQ(kitchen.kind, BuildingPartKind::Room);
	ASSERT_EQ(kitchen.name, u"Kitchen"_s);
	ASSERT_EQ(kitchen.parent, std::optional<Id<BuildingPart>>(*floorPart));
	ASSERT_EQ(project.history().count(), std::size_t{4});
	ASSERT_EQ(project.history().undoText(), u"Add building part"_s);
}

UNIT_TEST(FR_BLD_1__addBuildingPartRejectsUnknownParent)
{
	Project project(IdGenerator::sequential());
	const auto part = addBuildingPart(project, Id<BuildingPart>(999), BuildingPartKind::Room, u"Ghost"_s);
	ASSERT_FALSE(part.has_value());
	ASSERT_EQ(codeOf(part), ErrorCode::NotFound);
	ASSERT(project.buildingParts().empty());
	ASSERT_FALSE(project.history().canUndo());
}

UNIT_TEST(FR_BLD_1__removeBuildingPartCascadesAndUnplacesDevices, SampleProject)
{
	ASSERT(removeBuildingPart(project, floor).has_value());
	ASSERT_EQ(project.buildingParts().size(), std::size_t{1});
	ASSERT_FALSE(project.buildingParts().contains(room));
	ASSERT_EQ(project.devices().at(actuator).place, std::optional<Id<BuildingPart>>());
	ASSERT_EQ(project.devices().at(button).place, std::optional<Id<BuildingPart>>());
	ASSERT_EQ(project.history().count(), std::size_t{1});
	ASSERT_EQ(removeBuildingPart(project, floor).error().code, ErrorCode::NotFound);
}

UNIT_TEST(FR_BLD_2__placeAndUnplaceDevice, SampleProject)
{
	ASSERT_EQ(project.devicesIn(room), (std::vector<Id<Device>>{actuator, button}));
	ASSERT(placeDevice(project, actuator, floor).has_value());
	ASSERT_EQ(project.devicesIn(room), std::vector<Id<Device>>{button});
	ASSERT_EQ(project.devicesIn(floor), std::vector<Id<Device>>{actuator});
	ASSERT(placeDevice(project, actuator, std::nullopt).has_value());
	ASSERT_EQ(project.devices().at(actuator).place, std::optional<Id<BuildingPart>>());
	ASSERT(project.devicesIn(floor).empty());
}

UNIT_TEST(FR_BLD_2__placeDeviceRejectsUnknownEntities, SampleProject)
{
	ASSERT_EQ(placeDevice(project, actuator, Id<BuildingPart>(1)).error().code, ErrorCode::NotFound);
	ASSERT_EQ(placeDevice(project, Id<Device>(1), room).error().code, ErrorCode::NotFound);
	ASSERT_FALSE(project.history().canUndo());
}

// --- Topology ------------------------------------------------------------------------------------------------

UNIT_TEST(FR_TOP_1__addAreaLineDeviceDerivesIndividualAddress)
{
	Project project(IdGenerator::sequential());
	const auto area = addArea(project, 1, u"Area 1"_s);
	ASSERT(area.has_value());
	const auto line = addLine(project, *area, 2, u"Line 1.2"_s, Medium::TP);
	ASSERT(line.has_value());
	const auto device = addDevice(project, *line, 3u, u"Dimmer"_s, u"M-FFFF_H-1"_s, u"M-FFFF_A-1"_s);
	ASSERT(device.has_value());

	ASSERT_EQ(project.individualAddress(*device), std::optional(*knx::IndividualAddress::parse("1.2.3")));
	ASSERT_EQ(project.linesOf(*area), std::vector<Id<Line>>{*line});
	ASSERT_EQ(project.devicesOf(*line), std::vector<Id<Device>>{*device});
	ASSERT_EQ(project.devices().at(*device).productRef, u"M-FFFF_H-1"_s);
	ASSERT_EQ(project.devices().at(*device).appProgramRef, u"M-FFFF_A-1"_s);

	// Limits are inclusive.
	const auto area15 = addArea(project, 15, u"Area 15"_s);
	ASSERT(area15.has_value());
	const auto line15 = addLine(project, *area15, 15, u"Line 15.15"_s);
	ASSERT(line15.has_value());
	const auto device255 = addDevice(project, *line15, 255u, u"Last"_s);
	ASSERT_EQ(project.individualAddress(*device255), std::optional(knx::IndividualAddress(0xFFFF)));
}

UNIT_TEST(FR_TOP_1__rejectsOutOfRangeNumbers)
{
	Project project(IdGenerator::sequential());
	ASSERT_EQ(codeOf(addArea(project, 16, u"x"_s)), ErrorCode::InvalidArgument);
	const auto area = addArea(project, 0, u"Backbone"_s);
	ASSERT_EQ(codeOf(addLine(project, *area, 16, u"x"_s)), ErrorCode::InvalidArgument);
	const auto line = addLine(project, *area, 0, u"Backbone line"_s);
	ASSERT_EQ(codeOf(addDevice(project, *line, 256u, u"x"_s)), ErrorCode::InvalidArgument);
	ASSERT_EQ(codeOf(addLine(project, Id<Area>(77), 1, u"x"_s)), ErrorCode::NotFound);
	ASSERT_EQ(codeOf(addDevice(project, Id<Line>(77), 1u, u"x"_s)), ErrorCode::NotFound);
	ASSERT_EQ(project.history().count(), std::size_t{2});
}

UNIT_TEST(FR_TOP_2__rejectsDuplicateNumbers, SampleProject)
{
	ASSERT_EQ(codeOf(addArea(project, 1, u"Again"_s)), ErrorCode::Conflict);
	ASSERT_EQ(codeOf(addLine(project, area, 1, u"Again"_s)), ErrorCode::Conflict);
	ASSERT_EQ(codeOf(addDevice(project, line, 2u, u"Again"_s)), ErrorCode::Conflict);
	// The same numbers are fine elsewhere.
	const auto area2 = addArea(project, 2, u"Second"_s);
	ASSERT(area2.has_value());
	const auto line21 = addLine(project, *area2, 1, u"2.1"_s);
	ASSERT(line21.has_value());
	ASSERT(addDevice(project, *line21, 2u, u"2.1.2"_s).has_value());
	// Unaddressed devices never conflict.
	ASSERT(addDevice(project, line, std::nullopt, u"New 1"_s).has_value());
	ASSERT(addDevice(project, line, std::nullopt, u"New 2"_s).has_value());
}

UNIT_TEST(FR_TOP_1__setDeviceNumber, SampleProject)
{
	const auto fresh = addDevice(project, line, std::nullopt, u"Fresh"_s);
	ASSERT(fresh.has_value());
	ASSERT_EQ(project.individualAddress(*fresh), std::optional<knx::IndividualAddress>());
	ASSERT(setDeviceNumber(project, *fresh, 7u).has_value());
	ASSERT_EQ(project.individualAddress(*fresh)->toString(), std::string("1.1.7"));
	ASSERT(setDeviceNumber(project, *fresh, 7u).has_value()); // its own number
	ASSERT_EQ(setDeviceNumber(project, *fresh, 1u).error().code, ErrorCode::Conflict);
	ASSERT_EQ(setDeviceNumber(project, *fresh, 300u).error().code, ErrorCode::InvalidArgument);
	ASSERT_EQ(setDeviceNumber(project, Id<Device>(5), 3u).error().code, ErrorCode::NotFound);
	ASSERT(setDeviceNumber(project, *fresh, std::nullopt).has_value());
	ASSERT_EQ(project.devices().at(*fresh).number, std::optional<unsigned>());
}

UNIT_TEST(FR_TOP_1__moveDevice, SampleProject)
{
	const auto line2 = addLine(project, area, 2, u"First floor"_s);
	ASSERT(line2.has_value());
	const auto occupant = addDevice(project, *line2, 2u, u"Occupant"_s);
	ASSERT(occupant.has_value());

	ASSERT(moveDevice(project, actuator, *line2).has_value());
	ASSERT_EQ(project.individualAddress(actuator)->toString(), std::string("1.2.1"));
	ASSERT_EQ(project.devicesOf(*line2), (std::vector<Id<Device>>{actuator, *occupant})); // table order
	ASSERT_EQ(project.devicesOf(line), std::vector<Id<Device>>{button});

	ASSERT_EQ(moveDevice(project, button, *line2).error().code, ErrorCode::Conflict); // 1.2.2 taken
	ASSERT_EQ(moveDevice(project, button, Id<Line>(3)).error().code, ErrorCode::NotFound);
	const std::size_t steps = project.history().count();
	ASSERT(moveDevice(project, button, line).has_value()); // same line: no-op
	ASSERT_EQ(project.history().count(), steps);
}

UNIT_TEST(FR_TOP_1__removeDeviceCascadesComObjectsAndLinks, SampleProject)
{
	ASSERT(removeDevice(project, actuator).has_value());
	ASSERT_FALSE(project.devices().contains(actuator));
	ASSERT_FALSE(project.comObjects().contains(actuatorSwitch));
	ASSERT_FALSE(project.comObjects().contains(actuatorStatus));
	ASSERT_EQ(project.links().size(), std::size_t{1});
	ASSERT(project.links().contains(buttonSwitchLink));
	ASSERT_EQ(project.groupAddresses().size(), std::size_t{2}); // group addresses stay
	ASSERT_EQ(project.history().count(), std::size_t{1});
	ASSERT_EQ(removeDevice(project, actuator).error().code, ErrorCode::NotFound);
}

UNIT_TEST(FR_TOP_1__removeLineAndAreaCascade, SampleProject)
{
	ASSERT(removeLine(project, line).has_value());
	ASSERT(project.devices().empty());
	ASSERT(project.comObjects().empty());
	ASSERT(project.links().empty());
	ASSERT_EQ(project.areas().size(), std::size_t{1});
	ASSERT(removeArea(project, area).has_value());
	ASSERT(project.areas().empty());
	ASSERT_EQ(removeArea(project, area).error().code, ErrorCode::NotFound);
	ASSERT_EQ(removeLine(project, line).error().code, ErrorCode::NotFound);
}

UNIT_TEST(FR_TOP_1__deviceParameters, SampleProject)
{
	ASSERT(setDeviceParam(project, actuator, u"P-1"_s, u"5"_s).has_value());
	ASSERT(setDeviceParam(project, actuator, u"P-2"_s, u"on"_s).has_value());
	ASSERT_EQ(project.devices().at(actuator).params.at(u"P-1"_s), u"5"_s);
	ASSERT(setDeviceParam(project, actuator, u"P-1"_s, std::nullopt).has_value());
	ASSERT_EQ(project.devices().at(actuator).params.size(), std::size_t{1});
	ASSERT_EQ(setDeviceParam(project, actuator, u""_s, u"1"_s).error().code, ErrorCode::InvalidArgument);
	ASSERT_EQ(setDeviceParam(project, Id<Device>(3), u"P-1"_s, u"1"_s).error().code, ErrorCode::NotFound);
}

UNIT_TEST(FR_TOP_1__addComObjectValidation, SampleProject)
{
	ComObject object = oneBit(5);
	object.id = Id<ComObject>(12345);   // ignored
	object.device = Id<Device>(54321); // overwritten
	const auto added = addComObject(project, actuator, object);
	ASSERT(added.has_value());
	ASSERT_EQ(project.comObjects().at(*added).device, actuator);
	ASSERT(*added != Id<ComObject>(12345));
	ASSERT_EQ(project.comObjectsOf(actuator), (std::vector<Id<ComObject>>{actuatorSwitch, actuatorStatus, *added}));

	ASSERT_EQ(codeOf(addComObject(project, actuator, oneBit(5))), ErrorCode::Conflict);
	ComObject bad = oneBit(6);
	bad.sizeInBit = 0;
	ASSERT_EQ(codeOf(addComObject(project, actuator, bad)), ErrorCode::InvalidArgument);
	bad.sizeInBit = kMaxComObjectSizeInBit + 1;
	ASSERT_EQ(codeOf(addComObject(project, actuator, bad)), ErrorCode::InvalidArgument);
	bad = oneBit(6);
	bad.dpt = u"9.001"_s;
	ASSERT_EQ(codeOf(addComObject(project, actuator, bad)), ErrorCode::InvalidArgument);
	ASSERT_EQ(codeOf(addComObject(project, Id<Device>(1), oneBit(6))), ErrorCode::NotFound);
	ASSERT(removeComObject(project, *added).has_value());
	ASSERT_EQ(removeComObject(project, *added).error().code, ErrorCode::NotFound);
}

// --- Group addresses -----------------------------------------------------------------------------------------

UNIT_TEST(FR_GA_1__groupRangesAndAddresses, SampleProject)
{
	ASSERT_EQ(project.groupRangesIn(std::nullopt), std::vector<Id<GroupRange>>{mainGroup});
	ASSERT_EQ(project.groupRangesIn(mainGroup), std::vector<Id<GroupRange>>{middleGroup});
	ASSERT_EQ(project.groupAddressesIn(middleGroup), (std::vector<Id<GroupAddress>>{lightSwitch, lightStatus}));
	const GroupAddress& address = project.groupAddresses().at(lightSwitch);
	ASSERT_EQ(address.address().toString(), std::string("1/1/1"));
	ASSERT_EQ(address.dpt, u"DPST-1-1"_s);

	// Addresses without range and free-style values.
	const auto freeAddress = addGroupAddress(project, knx::GroupAddress(0xF000), u"Free"_s);
	ASSERT(freeAddress.has_value());
	ASSERT_EQ(project.groupAddressesIn(std::nullopt), std::vector<Id<GroupAddress>>{*freeAddress});
	ASSERT_EQ(project.groupAddresses().at(*freeAddress).address().toString(knx::GroupAddressStyle::Free), std::string("61440"));
}

UNIT_TEST(FR_GA_2__groupRangeValidation, SampleProject)
{
	ASSERT_EQ(codeOf(addGroupRange(project, std::nullopt, u"x"_s, 10, 5)), ErrorCode::InvalidArgument);
	ASSERT_EQ(codeOf(addGroupRange(project, mainGroup, u"x"_s, 0x0700, 0x08FF)), ErrorCode::InvalidArgument);
	ASSERT_EQ(codeOf(addGroupRange(project, mainGroup, u"x"_s, 0x0950, 0x0A00)), ErrorCode::Conflict);
	ASSERT_EQ(codeOf(addGroupRange(project, std::nullopt, u"x"_s, 0x0FFF, 0x1000)), ErrorCode::Conflict);
	ASSERT_EQ(codeOf(addGroupRange(project, Id<GroupRange>(4), u"x"_s, 0, 1)), ErrorCode::NotFound);
	const auto middle2 = addGroupRange(project, mainGroup, u"First floor"_s, 0x0A00, 0x0AFF);
	ASSERT(middle2.has_value());
	ASSERT_EQ(project.groupRangesIn(mainGroup), (std::vector<Id<GroupRange>>{middleGroup, *middle2}));
}

UNIT_TEST(FR_GA_1__addGroupAddressValidation, SampleProject)
{
	ASSERT_EQ(codeOf(addGroupAddress(project, knx::GroupAddress(0), u"x"_s)), ErrorCode::InvalidArgument);
	ASSERT_EQ(codeOf(addGroupAddress(project, knx::GroupAddress(0x0901), u"dup"_s)), ErrorCode::Conflict);
	ASSERT_EQ(codeOf(addGroupAddress(project, knx::GroupAddress(0x0A01), u"x"_s, {}, middleGroup)),
		ErrorCode::InvalidArgument);
	ASSERT_EQ(codeOf(addGroupAddress(project, knx::GroupAddress(0x0903), u"x"_s, u"DPT9"_s)),
		ErrorCode::InvalidArgument);
	ASSERT_EQ(codeOf(addGroupAddress(project, knx::GroupAddress(0x0903), u"x"_s, {}, Id<GroupRange>(9))),
		ErrorCode::NotFound);
	ASSERT_EQ(project.groupAddresses().size(), std::size_t{2});
	ASSERT_FALSE(project.history().canUndo());
}

UNIT_TEST(FR_GA_4__setGroupAddressDpt, SampleProject)
{
	ASSERT(setGroupAddressDpt(project, lightStatus, u"DPST-5-1"_s).has_value());
	ASSERT_EQ(project.groupAddresses().at(lightStatus).dpt, u"DPST-5-1"_s);
	ASSERT(setGroupAddressDpt(project, lightStatus, u""_s).has_value());
	ASSERT_EQ(setGroupAddressDpt(project, lightStatus, u"5.001"_s).error().code, ErrorCode::InvalidArgument);
	ASSERT_EQ(setGroupAddressDpt(project, Id<GroupAddress>(1), u""_s).error().code, ErrorCode::NotFound);
}

UNIT_TEST(FR_GA_2__removeGroupRangeCascades, SampleProject)
{
	ASSERT(removeGroupRange(project, mainGroup).has_value());
	ASSERT(project.groupRanges().empty());
	ASSERT(project.groupAddresses().empty());
	ASSERT(project.links().empty());
	ASSERT_EQ(project.comObjects().size(), std::size_t{3});
	ASSERT_EQ(removeGroupRange(project, mainGroup).error().code, ErrorCode::NotFound);
}

// --- Links ---------------------------------------------------------------------------------------------------

UNIT_TEST(FR_GA_3__firstLinkIsTheSendingOne, SampleProject)
{
	ASSERT(project.links().at(actuatorSwitchLink).sending);
	ASSERT(project.links().at(buttonSwitchLink).sending); // first link of *its* com object
	const auto second = link(project, actuatorSwitch, lightStatus);
	ASSERT(second.has_value());
	ASSERT_FALSE(project.links().at(*second).sending);
	ASSERT_EQ(project.linksOfComObject(actuatorSwitch), (std::vector<Id<Link>>{actuatorSwitchLink, *second}));
	ASSERT_EQ(project.linksOfGroupAddress(lightStatus), (std::vector<Id<Link>>{actuatorStatusLink, *second}));
}

UNIT_TEST(FR_GA_3__linkRejections, SampleProject)
{
	ASSERT_EQ(codeOf(link(project, actuatorSwitch, lightSwitch)), ErrorCode::Conflict);
	ASSERT_EQ(codeOf(link(project, Id<ComObject>(1), lightSwitch)), ErrorCode::NotFound);
	ASSERT_EQ(codeOf(link(project, actuatorSwitch, Id<GroupAddress>(1))), ErrorCode::NotFound);
	ASSERT_EQ(unlink(project, Id<Link>(1)).error().code, ErrorCode::NotFound);
	ASSERT_EQ(setSendingLink(project, Id<Link>(1)).error().code, ErrorCode::NotFound);
	ASSERT_FALSE(project.history().canUndo());
}

UNIT_TEST(FR_GA_3__unlinkSendingPromotesNextLink, SampleProject)
{
	const auto second = link(project, actuatorSwitch, lightStatus);
	ASSERT(second.has_value());
	ASSERT(unlink(project, actuatorSwitchLink).has_value());
	ASSERT_FALSE(project.links().contains(actuatorSwitchLink));
	ASSERT(project.links().at(*second).sending);
	ASSERT(unlink(project, *second).has_value());
	ASSERT(project.linksOfComObject(actuatorSwitch).empty());
}

UNIT_TEST(FR_GA_3__setSendingLinkKeepsOneSender, SampleProject)
{
	const auto second = link(project, actuatorSwitch, lightStatus);
	ASSERT(setSendingLink(project, *second).has_value());
	ASSERT(project.links().at(*second).sending);
	ASSERT_FALSE(project.links().at(actuatorSwitchLink).sending);
	ASSERT_EQ(project.history().undoText(), u"Set sending group address"_s);
	const std::size_t steps = project.history().count();
	ASSERT(setSendingLink(project, *second).has_value()); // already sending: no new step
	ASSERT_EQ(project.history().count(), steps);
}

UNIT_TEST(FR_GA_3__removeGroupAddressCascadesLinksAndPromotes, SampleProject)
{
	const auto second = link(project, actuatorSwitch, lightStatus);
	ASSERT(removeGroupAddress(project, lightSwitch).has_value());
	ASSERT_FALSE(project.groupAddresses().contains(lightSwitch));
	ASSERT_FALSE(project.links().contains(actuatorSwitchLink));
	ASSERT_FALSE(project.links().contains(buttonSwitchLink));
	ASSERT(project.links().at(*second).sending); // promoted
	ASSERT_EQ(project.links().size(), std::size_t{2});
	ASSERT_EQ(removeGroupAddress(project, lightSwitch).error().code, ErrorCode::NotFound);
}

// --- Generic -------------------------------------------------------------------------------------------------

UNIT_TEST(FR_PRJ_1__renameAnyNamedEntity, SampleProject)
{
	ASSERT(rename(project, actuator, u"Actuator A1"_s).has_value());
	ASSERT_EQ(project.devices().at(actuator).name, u"Actuator A1"_s);
	ASSERT(rename(project, lightSwitch, u"Kitchen light"_s).has_value());
	ASSERT_EQ(project.groupAddresses().at(lightSwitch).name, u"Kitchen light"_s);
	ASSERT(rename(project, room, u"Kitchen"_s).has_value());
	ASSERT(rename(project, area, u"A"_s).has_value());
	ASSERT(rename(project, line, u"L"_s).has_value());
	ASSERT(rename(project, actuatorStatus, u"State"_s).has_value());
	ASSERT(rename(project, middleGroup, u"GF"_s).has_value());
	ASSERT(rename(project, EntityRef::project(), u"My house"_s).has_value());
	ASSERT_EQ(project.settings().name, u"My house"_s);
	ASSERT_EQ(project.comObjects().at(actuatorStatus).name, u"State"_s);

	ASSERT_EQ(rename(project, actuatorSwitchLink, u"x"_s).error().code, ErrorCode::InvalidArgument);
	ASSERT_EQ(rename(project, Id<Device>(1), u"x"_s).error().code, ErrorCode::NotFound);
}

UNIT_TEST(FR_GA_1__updateSettingsGroupAddressStyle, SampleProject)
{
	ProjectSettings settings = project.settings();
	settings.gaStyle = knx::GroupAddressStyle::TwoLevel;
	ASSERT(updateSettings(project, settings).has_value());
	ASSERT(project.settings().gaStyle == knx::GroupAddressStyle::TwoLevel);
	settings.contentLanguage.clear();
	ASSERT_EQ(updateSettings(project, settings).error().code, ErrorCode::InvalidArgument);
}

UNIT_TEST(FR_PRJ_1__rejectedOperationsLeaveProjectUntouched, SampleProject)
{
	const Project before = project;
	ASSERT_FALSE(addArea(project, 1, u"dup"_s).has_value());
	ASSERT_FALSE(addDevice(project, line, 1u, u"dup"_s).has_value());
	ASSERT_FALSE(link(project, actuatorSwitch, lightSwitch).has_value());
	ASSERT_FALSE(addGroupAddress(project, knx::GroupAddress(0x0902), u"dup"_s).has_value());
	ASSERT_EQ(project, before);
	ASSERT_FALSE(project.history().canUndo());
	ASSERT_FALSE(project.isDirty());
}
