#pragma once

// High-level operations (specs/03-domain-model.md §3). Each opens exactly one transaction (one undo step),
// validates its input and returns an error without touching the project on failure:
//   InvalidArgument  value out of range / malformed (numbers, DPT refs, wrong entity kind)
//   NotFound         referenced entity does not exist
//   Conflict         uniqueness violation (area/line/device numbers, group addresses, links)
// Removals cascade explicitly, children first.

#include "kestrel/base/Result.hpp"
#include "kestrel/knx/Address.hpp"
#include "kestrel/model/Entities.hpp"
#include "kestrel/model/Id.hpp"
#include "kestrel/model/Project.hpp"

#include <QString>

#include <cstdint>
#include <optional>

namespace kestrel::model
{

// --- Building ----------------------------------------------------------------------------------------------
Result<Id<BuildingPart>> addBuildingPart(
	Project& project, std::optional<Id<BuildingPart>> parent, BuildingPartKind kind, QString name);
/// Cascades sub-parts; devices placed in removed parts become unplaced.
Status removeBuildingPart(Project& project, Id<BuildingPart> part);
/// place = nullopt unplaces the device.
Status placeDevice(Project& project, Id<Device> device, std::optional<Id<BuildingPart>> place);

// --- Topology ----------------------------------------------------------------------------------------------
/// number 0..15, unique among areas.
Result<Id<Area>> addArea(Project& project, unsigned number, QString name, Medium medium = Medium::TP);
/// Cascades lines, devices, com objects and links.
Status removeArea(Project& project, Id<Area> area);
/// number 0..15, unique within the area.
Result<Id<Line>> addLine(Project& project, Id<Area> area, unsigned number, QString name, Medium medium = Medium::TP);
/// Cascades devices, com objects and links.
Status removeLine(Project& project, Id<Line> line);
/// number 0..255 (or nullopt = not addressed yet), unique within the line.
Result<Id<Device>> addDevice(Project& project, Id<Line> line, std::optional<unsigned> number, QString name,
	QString productRef = {}, QString appProgramRef = {});
Status setDeviceNumber(Project& project, Id<Device> device, std::optional<unsigned> number);
/// Keeps the device number; Conflict if it is taken on the target line.
Status moveDevice(Project& project, Id<Device> device, Id<Line> line);
/// Cascades com objects and links.
Status removeDevice(Project& project, Id<Device> device);
/// value = nullopt resets the parameter to its default. Consecutive edits of one parameter merge.
Status setDeviceParam(Project& project, Id<Device> device, QString ref, std::optional<QString> value);

// --- Com objects -------------------------------------------------------------------------------------------
/// Manually defined com object (M1). object.id and object.device are ignored/overwritten.
/// number unique within the device, sizeInBit 1..kMaxComObjectSizeInBit, dpt valid or empty.
Result<Id<ComObject>> addComObject(Project& project, Id<Device> device, ComObject object);
/// Cascades links.
Status removeComObject(Project& project, Id<ComObject> comObject);

// --- Group addresses ---------------------------------------------------------------------------------------
/// start <= end, inside the parent range, not overlapping a sibling range.
Result<Id<GroupRange>> addGroupRange(Project& project, std::optional<Id<GroupRange>> parent, QString name,
	std::uint16_t start, std::uint16_t end);
/// Cascades sub-ranges, their group addresses and links.
Status removeGroupRange(Project& project, Id<GroupRange> range);
/// address != 0/0/0, unique, inside `range` if given; dpt valid or empty.
Result<Id<GroupAddress>> addGroupAddress(Project& project, knx::GroupAddress address, QString name,
	QString dpt = {}, std::optional<Id<GroupRange>> range = {});
/// dpt valid or empty.
Status setGroupAddressDpt(Project& project, Id<GroupAddress> groupAddress, QString dpt);
/// Cascades links (a removed sending link hands "sending" to the com object's next link).
Status removeGroupAddress(Project& project, Id<GroupAddress> groupAddress);

// --- Links -------------------------------------------------------------------------------------------------
/// The first link of a com object becomes its sending link. Conflict if already linked.
Result<Id<Link>> link(Project& project, Id<ComObject> comObject, Id<GroupAddress> groupAddress);
/// If the sending link is removed, the com object's next link becomes the sending one.
Status unlink(Project& project, Id<Link> link);
/// Makes `link` the sending link of its com object (clears the previous one).
Status setSendingLink(Project& project, Id<Link> link);

// --- Generic -----------------------------------------------------------------------------------------------
/// Renames any named entity (EntityRef::project() renames the project). Consecutive renames of one entity merge.
Status rename(Project& project, EntityRef ref, QString name);
Status updateSettings(Project& project, ProjectSettings settings);

} // namespace kestrel::model
