#pragma once

// A small but complete project shared by the model, persist and validate tests. Use as a UNIT_TEST fixture:
//   UNIT_TEST(FR_X__name, SampleProject) { ... project ... }
//
//   Building > Floor > Room               area 1 "Main" > line 1.1 "Ground floor"
//   1.1.1 actuator (placed in Room)       co 0 "Switch"  1 bit  CW   -> 1/1/1 (sending)
//                                         co 1 "Status"  1 bit  CRT  -> 1/1/2 (sending)
//   1.1.2 push button (placed in Room)    co 0 "Switch"  1 bit  CWT  -> 1/1/1 (sending)
//   main group 1 "Lighting" (0x0800..0x0FFF) > middle group 1/1 "Ground floor" (0x0900..0x09FF)
//   1/1/1 "Light switch" DPST-1-1,  1/1/2 "Light status" DPST-1-1

#include "kestrel/model/Operations.hpp"

#include <QString>

using namespace Qt::StringLiterals;

struct SampleProject
{
	kestrel::model::Project project{kestrel::model::IdGenerator::sequential(1000)};

	kestrel::model::Id<kestrel::model::BuildingPart> building, floor, room;
	kestrel::model::Id<kestrel::model::Area> area;
	kestrel::model::Id<kestrel::model::Line> line;
	kestrel::model::Id<kestrel::model::Device> actuator, button;
	kestrel::model::Id<kestrel::model::ComObject> actuatorSwitch, actuatorStatus, buttonSwitch;
	kestrel::model::Id<kestrel::model::GroupRange> mainGroup, middleGroup;
	kestrel::model::Id<kestrel::model::GroupAddress> lightSwitch, lightStatus;
	kestrel::model::Id<kestrel::model::Link> actuatorSwitchLink, buttonSwitchLink, actuatorStatusLink;

	static kestrel::model::ComObject comObject(unsigned number, const QString& name, const QString& flags)
	{
		kestrel::model::ComObject object;
		object.number = number;
		object.name = name;
		object.function = u"Light"_s;
		object.sizeInBit = 1;
		object.flags = *kestrel::model::ComObjectFlags::parse(flags);
		object.dpt = u"DPST-1-1"_s;
		return object;
	}

	SampleProject()
	{
		using namespace kestrel::model;
		ProjectSettings settings;
		settings.name = u"Sample house"_s;
		updateSettings(project, settings).value();

		building = addBuildingPart(project, std::nullopt, BuildingPartKind::Building, u"House"_s).value();
		floor = addBuildingPart(project, building, BuildingPartKind::Floor, u"Ground floor"_s).value();
		room = addBuildingPart(project, floor, BuildingPartKind::Room, u"Living room"_s).value();

		area = addArea(project, 1, u"Main"_s).value();
		line = addLine(project, area, 1, u"Ground floor"_s).value();
		actuator = addDevice(project, line, 1u, u"Switch actuator"_s, u"M-FFFF_H-1"_s, u"M-FFFF_A-1"_s).value();
		button = addDevice(project, line, 2u, u"Push button"_s).value();
		placeDevice(project, actuator, room).value();
		placeDevice(project, button, room).value();

		actuatorSwitch = addComObject(project, actuator, comObject(0, u"Switch"_s, u"CW"_s)).value();
		actuatorStatus = addComObject(project, actuator, comObject(1, u"Status"_s, u"CRT"_s)).value();
		buttonSwitch = addComObject(project, button, comObject(0, u"Switch"_s, u"CWT"_s)).value();

		mainGroup = addGroupRange(project, std::nullopt, u"Lighting"_s, 0x0800, 0x0FFF).value();
		middleGroup = addGroupRange(project, mainGroup, u"Ground floor"_s, 0x0900, 0x09FF).value();
		lightSwitch = addGroupAddress(project, kestrel::knx::GroupAddress(0x0901), u"Light switch"_s, u"DPST-1-1"_s,
			middleGroup)
						  .value();
		lightStatus = addGroupAddress(project, kestrel::knx::GroupAddress(0x0902), u"Light status"_s, u"DPST-1-1"_s,
			middleGroup)
						  .value();

		actuatorSwitchLink = link(project, actuatorSwitch, lightSwitch).value();
		buttonSwitchLink = link(project, buttonSwitch, lightSwitch).value();
		actuatorStatusLink = link(project, actuatorStatus, lightStatus).value();

		project.history().clear();
		project.history().setClean();
	}
};
