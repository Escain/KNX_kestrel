#pragma once

// A small, realistic project built with model operations (shared by the view-model and shell tests).

#include "kestrel/knx/Address.hpp"
#include "kestrel/model/Operations.hpp"
#include "kestrel/model/Project.hpp"

#include <QString>

#include <cassert>

namespace kestrel::testing
{

struct SampleIds
{
	model::Id<model::BuildingPart> house, groundFloor, firstFloor, livingRoom, kitchen, bedroom;
	model::Id<model::Area> area1;
	model::Id<model::Line> line11, line12;
	model::Id<model::Device> dimmer, actuator, button, weather;
	model::Id<model::ComObject> dimmerSwitch, dimmerDim, dimmerStatus, actuatorSwitch, buttonSwitch;
	model::Id<model::GroupRange> lighting, switching, dimming, heating;
	model::Id<model::GroupAddress> livingLight, kitchenLight, livingDim, temperature, unlinked;
};

template <class T>
T must(Result<T> result)
{
	assert(result.has_value());
	return std::move(result).value();
}

inline void must(const Status& status)
{
	assert(status.has_value());
	(void)status;
}

inline model::ComObject comObject(unsigned number, const QString& name, unsigned sizeInBit, const QString& flags, const QString& dpt)
{
	model::ComObject object;
	object.number = number;
	object.name = name;
	object.sizeInBit = sizeInBit;
	object.flags = *model::ComObjectFlags::parse(flags);
	object.dpt = dpt;
	return object;
}

/// Fills an empty project; the history is cleared and marked clean afterwards.
inline SampleIds buildSampleProject(model::Project& p)
{
	using namespace model;
	SampleIds ids;
	ProjectSettings settings = p.settings();
	settings.name = QStringLiteral("Sample house");
	must(updateSettings(p, settings));

	ids.house = must(addBuildingPart(p, std::nullopt, BuildingPartKind::Building, QStringLiteral("House")));
	ids.groundFloor = must(addBuildingPart(p, ids.house, BuildingPartKind::Floor, QStringLiteral("Ground floor")));
	ids.firstFloor = must(addBuildingPart(p, ids.house, BuildingPartKind::Floor, QStringLiteral("First floor")));
	ids.livingRoom = must(addBuildingPart(p, ids.groundFloor, BuildingPartKind::Room, QStringLiteral("Living room")));
	ids.kitchen = must(addBuildingPart(p, ids.groundFloor, BuildingPartKind::Room, QStringLiteral("Kitchen")));
	ids.bedroom = must(addBuildingPart(p, ids.firstFloor, BuildingPartKind::Room, QStringLiteral("Bedroom")));

	ids.area1 = must(addArea(p, 1, QStringLiteral("Main area")));
	ids.line11 = must(addLine(p, ids.area1, 1, QStringLiteral("Ground floor line")));
	ids.line12 = must(addLine(p, ids.area1, 2, QStringLiteral("First floor line")));
	ids.dimmer = must(addDevice(p, ids.line11, 1, QStringLiteral("Dimmer")));
	ids.actuator = must(addDevice(p, ids.line11, 2, QStringLiteral("Switch actuator")));
	ids.button = must(addDevice(p, ids.line11, 3, QStringLiteral("Push button")));
	ids.weather = must(addDevice(p, ids.line12, 1, QStringLiteral("Weather station")));
	must(placeDevice(p, ids.dimmer, ids.livingRoom));
	must(placeDevice(p, ids.actuator, ids.kitchen));
	must(placeDevice(p, ids.button, ids.livingRoom));

	ids.dimmerSwitch = must(addComObject(p, ids.dimmer, comObject(0, QStringLiteral("Switch"), 1, QStringLiteral("CW"), QStringLiteral("DPST-1-1"))));
	ids.dimmerDim = must(addComObject(p, ids.dimmer, comObject(1, QStringLiteral("Dimming"), 4, QStringLiteral("CW"), QStringLiteral("DPST-3-7"))));
	ids.dimmerStatus = must(addComObject(p, ids.dimmer, comObject(2, QStringLiteral("Status"), 1, QStringLiteral("CRT"), QStringLiteral("DPST-1-1"))));
	ids.actuatorSwitch = must(addComObject(p, ids.actuator, comObject(0, QStringLiteral("Channel A"), 1, QStringLiteral("CW"), QStringLiteral("DPST-1-1"))));
	ids.buttonSwitch = must(addComObject(p, ids.button, comObject(0, QStringLiteral("Button 1"), 1, QStringLiteral("CWT"), QStringLiteral("DPST-1-1"))));

	ids.lighting = must(addGroupRange(p, std::nullopt, QStringLiteral("Lighting"), 0x0000, 0x07FF));
	ids.switching = must(addGroupRange(p, ids.lighting, QStringLiteral("Switching"), 0x0000, 0x00FF));
	ids.dimming = must(addGroupRange(p, ids.lighting, QStringLiteral("Dimming"), 0x0100, 0x01FF));
	ids.heating = must(addGroupRange(p, std::nullopt, QStringLiteral("Heating"), 0x0800, 0x0FFF));
	ids.livingLight = must(addGroupAddress(p, knx::GroupAddress(0x0001), QStringLiteral("Living room light"), QStringLiteral("DPST-1-1"), ids.switching));
	ids.kitchenLight = must(addGroupAddress(p, knx::GroupAddress(0x0002), QStringLiteral("Kitchen light"), QStringLiteral("DPST-1-1"), ids.switching));
	ids.livingDim = must(addGroupAddress(p, knx::GroupAddress(0x0101), QStringLiteral("Living room dimming"), QStringLiteral("DPST-3-7"), ids.dimming));
	ids.temperature = must(addGroupAddress(p, knx::GroupAddress(0x0801), QStringLiteral("Outside temperature"), QStringLiteral("DPST-9-1"), ids.heating));
	ids.unlinked = must(addGroupAddress(p, knx::GroupAddress(0x0003), QStringLiteral("Spare"), QString(), ids.switching));

	must(link(p, ids.buttonSwitch, ids.livingLight));
	must(link(p, ids.dimmerSwitch, ids.livingLight));
	must(link(p, ids.dimmerDim, ids.livingDim));
	must(link(p, ids.dimmerStatus, ids.livingLight));
	must(link(p, ids.actuatorSwitch, ids.kitchenLight));

	p.history().clear();
	p.history().setClean();
	return ids;
}

} // namespace kestrel::testing
