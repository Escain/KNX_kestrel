#include "kestrel/model/Operations.hpp"

#include <algorithm>
#include <set>

using namespace Qt::StringLiterals;

namespace kestrel::model
{

namespace
{

Unexpected invalid(const QString& message)
{
	return makeError(ErrorCode::InvalidArgument, message.toStdString());
}

Unexpected conflict(const QString& message)
{
	return makeError(ErrorCode::Conflict, message.toStdString());
}

Unexpected notFound(EntityRef ref)
{
	return makeError(ErrorCode::NotFound, "unknown " + ref.toString().toStdString());
}

template <class T>
Status requireExists(const Project& project, Id<T> id)
{
	if (!project.table<T>().contains(id))
	{
		return notFound(id);
	}
	return ok();
}

template <class T>
Result<Id<T>> insertOne(Project& project, const QString& label, T entity)
{
	Id<T> id;
	KESTREL_CHECK(project.edit(label,
		[&](Tx& tx)
		{
			id = tx.insert(std::move(entity));
			return ok();
		}));
	return id;
}

Status checkDeviceNumberFree(const Project& project, Id<Line> line, std::optional<unsigned> number, Id<Device> self)
{
	if (!number)
	{
		return ok();
	}
	if (*number > kMaxDeviceNumber)
	{
		return invalid(u"device number %1 out of range 0..255"_s.arg(*number));
	}
	for (const Id<Device> other : project.devicesOf(line))
	{
		if (other != self && project.devices().at(other).number == number)
		{
			return conflict(u"device number %1 is already used on %2"_s.arg(*number).arg(line.toString()));
		}
	}
	return ok();
}

/// Everything a cascading removal erases, collected before the first mutation.
struct Removal
{
	std::vector<Id<Link>> links;
	std::vector<Id<ComObject>> comObjects;
	std::vector<Id<Device>> devices;
	std::vector<Id<Line>> lines;
	std::vector<Id<Area>> areas;
	std::vector<Id<GroupAddress>> groupAddresses;
	std::vector<Id<GroupRange>> groupRanges;

	void addComObject(const Project& project, Id<ComObject> id)
	{
		for (const Id<Link> linkId : project.linksOfComObject(id))
			links.push_back(linkId);
		comObjects.push_back(id);
	}
	void addDevice(const Project& project, Id<Device> id)
	{
		for (const Id<ComObject> objectId : project.comObjectsOf(id))
			addComObject(project, objectId);
		devices.push_back(id);
	}
	void addLine(const Project& project, Id<Line> id)
	{
		for (const Id<Device> deviceId : project.devicesOf(id))
			addDevice(project, deviceId);
		lines.push_back(id);
	}
	void addArea(const Project& project, Id<Area> id)
	{
		for (const Id<Line> lineId : project.linesOf(id))
			addLine(project, lineId);
		areas.push_back(id);
	}
	void addGroupAddress(const Project& project, Id<GroupAddress> id)
	{
		for (const Id<Link> linkId : project.linksOfGroupAddress(id))
			links.push_back(linkId);
		groupAddresses.push_back(id);
	}
	void addGroupRange(const Project& project, Id<GroupRange> id)
	{
		for (const Id<GroupRange> child : project.groupRangesIn(id))
			addGroupRange(project, child);
		for (const Id<GroupAddress> addressId : project.groupAddressesIn(id))
			addGroupAddress(project, addressId);
		groupRanges.push_back(id);
	}
};

/// Erases children first (links, com objects, devices, lines, areas, group addresses, ranges). A removed
/// sending link of a surviving com object hands "sending" to that object's next link.
void eraseAll(Tx& tx, const Removal& removal)
{
	const Project& project = tx.project();
	const std::set<Id<ComObject>> removedObjects(removal.comObjects.begin(), removal.comObjects.end());
	std::vector<Id<ComObject>> orphanedSenders;
	std::set<Id<Link>> erased;
	for (const Id<Link> linkId : removal.links)
	{
		if (!erased.insert(linkId).second)
		{
			continue;
		}
		const Link& link = project.links().at(linkId);
		if (link.sending && !removedObjects.contains(link.comObject))
		{
			orphanedSenders.push_back(link.comObject);
		}
		tx.erase(linkId);
	}
	for (const Id<ComObject> objectId : orphanedSenders)
	{
		const auto remaining = project.linksOfComObject(objectId);
		if (!remaining.empty())
		{
			tx.set(remaining.front(), &Link::sending, true);
		}
	}
	for (const Id<ComObject> id : removal.comObjects)
		tx.erase(id);
	for (const Id<Device> id : removal.devices)
		tx.erase(id);
	for (const Id<Line> id : removal.lines)
		tx.erase(id);
	for (const Id<Area> id : removal.areas)
		tx.erase(id);
	for (const Id<GroupAddress> id : removal.groupAddresses)
		tx.erase(id);
	for (const Id<GroupRange> id : removal.groupRanges)
		tx.erase(id);
}

Status removeCollected(Project& project, const QString& label, const Removal& removal)
{
	return project.edit(label,
		[&](Tx& tx)
		{
			eraseAll(tx, removal);
			return ok();
		});
}

void collectParts(const Project& project, Id<BuildingPart> part, std::vector<Id<BuildingPart>>& out)
{
	for (const Id<BuildingPart> child : project.buildingPartsIn(part))
		collectParts(project, child, out);
	out.push_back(part);
}

} // namespace

// --- Building ----------------------------------------------------------------------------------------------

Result<Id<BuildingPart>> addBuildingPart(
	Project& project, std::optional<Id<BuildingPart>> parent, BuildingPartKind kind, QString name)
{
	if (parent)
	{
		KESTREL_CHECK(requireExists(project, *parent));
	}
	BuildingPart part;
	part.parent = parent;
	part.kind = kind;
	part.name = std::move(name);
	return insertOne(project, u"Add building part"_s, std::move(part));
}

Status removeBuildingPart(Project& project, Id<BuildingPart> part)
{
	KESTREL_CHECK(requireExists(project, part));
	std::vector<Id<BuildingPart>> parts;
	collectParts(project, part, parts);
	std::vector<Id<Device>> placed;
	for (const Id<BuildingPart> id : parts)
	{
		for (const Id<Device> device : project.devicesIn(id))
			placed.push_back(device);
	}
	return project.edit(u"Remove building part"_s,
		[&](Tx& tx)
		{
			for (const Id<Device> device : placed)
				tx.set(device, &Device::place, std::nullopt);
			for (const Id<BuildingPart> id : parts)
				tx.erase(id);
			return ok();
		});
}

Status placeDevice(Project& project, Id<Device> device, std::optional<Id<BuildingPart>> place)
{
	KESTREL_CHECK(requireExists(project, device));
	if (place)
	{
		KESTREL_CHECK(requireExists(project, *place));
	}
	return project.edit(u"Place device"_s,
		[&](Tx& tx)
		{
			tx.set(device, &Device::place, place);
			return ok();
		});
}

// --- Topology ----------------------------------------------------------------------------------------------

Result<Id<Area>> addArea(Project& project, unsigned number, QString name, Medium medium)
{
	if (number > kMaxAreaNumber)
	{
		return invalid(u"area number %1 out of range 0..15"_s.arg(number));
	}
	for (const Area& area : project.areas())
	{
		if (area.number == number)
		{
			return conflict(u"area %1 already exists"_s.arg(number));
		}
	}
	Area area;
	area.number = number;
	area.name = std::move(name);
	area.medium = medium;
	return insertOne(project, u"Add area"_s, std::move(area));
}

Status removeArea(Project& project, Id<Area> area)
{
	KESTREL_CHECK(requireExists(project, area));
	Removal removal;
	removal.addArea(project, area);
	return removeCollected(project, u"Remove area"_s, removal);
}

Result<Id<Line>> addLine(Project& project, Id<Area> area, unsigned number, QString name, Medium medium)
{
	KESTREL_CHECK(requireExists(project, area));
	if (number > kMaxLineNumber)
	{
		return invalid(u"line number %1 out of range 0..15"_s.arg(number));
	}
	for (const Id<Line> other : project.linesOf(area))
	{
		if (project.lines().at(other).number == number)
		{
			return conflict(u"line %1.%2 already exists"_s.arg(project.areas().at(area).number).arg(number));
		}
	}
	Line line;
	line.area = area;
	line.number = number;
	line.name = std::move(name);
	line.medium = medium;
	return insertOne(project, u"Add line"_s, std::move(line));
}

Status removeLine(Project& project, Id<Line> line)
{
	KESTREL_CHECK(requireExists(project, line));
	Removal removal;
	removal.addLine(project, line);
	return removeCollected(project, u"Remove line"_s, removal);
}

Result<Id<Device>> addDevice(Project& project, Id<Line> line, std::optional<unsigned> number, QString name,
	QString productRef, QString appProgramRef)
{
	KESTREL_CHECK(requireExists(project, line));
	KESTREL_CHECK(checkDeviceNumberFree(project, line, number, {}));
	Device device;
	device.line = line;
	device.number = number;
	device.name = std::move(name);
	device.productRef = std::move(productRef);
	device.appProgramRef = std::move(appProgramRef);
	return insertOne(project, u"Add device"_s, std::move(device));
}

Status setDeviceNumber(Project& project, Id<Device> device, std::optional<unsigned> number)
{
	KESTREL_CHECK(requireExists(project, device));
	KESTREL_CHECK(checkDeviceNumberFree(project, project.devices().at(device).line, number, device));
	return project.edit(u"Set device address"_s,
		[&](Tx& tx)
		{
			tx.set(device, &Device::number, number);
			return ok();
		});
}

Status moveDevice(Project& project, Id<Device> device, Id<Line> line)
{
	KESTREL_CHECK(requireExists(project, device));
	KESTREL_CHECK(requireExists(project, line));
	const Device& current = project.devices().at(device);
	if (current.line == line)
	{
		return ok();
	}
	KESTREL_CHECK(checkDeviceNumberFree(project, line, current.number, device));
	return project.edit(u"Move device"_s,
		[&](Tx& tx)
		{
			tx.set(device, &Device::line, line);
			return ok();
		});
}

Status removeDevice(Project& project, Id<Device> device)
{
	KESTREL_CHECK(requireExists(project, device));
	Removal removal;
	removal.addDevice(project, device);
	return removeCollected(project, u"Remove device"_s, removal);
}

Status setDeviceParam(Project& project, Id<Device> device, QString ref, std::optional<QString> value)
{
	KESTREL_CHECK(requireExists(project, device));
	if (ref.isEmpty())
	{
		return invalid(u"empty parameter reference"_s);
	}
	MergeKey key{u"param:"_s + device.toString() + u':' + ref};
	return project.edit(
		u"Change parameter"_s,
		[&](Tx& tx)
		{
			tx.setParam(device, ref, value);
			return ok();
		},
		std::move(key));
}

// --- Com objects -------------------------------------------------------------------------------------------

Result<Id<ComObject>> addComObject(Project& project, Id<Device> device, ComObject object)
{
	KESTREL_CHECK(requireExists(project, device));
	if (object.sizeInBit < 1 || object.sizeInBit > kMaxComObjectSizeInBit)
	{
		return invalid(u"com object size %1 bit out of range"_s.arg(object.sizeInBit));
	}
	if (!isValidDptRef(object.dpt))
	{
		return invalid(u"invalid DPT reference '%1'"_s.arg(object.dpt));
	}
	for (const Id<ComObject> other : project.comObjectsOf(device))
	{
		if (project.comObjects().at(other).number == object.number)
		{
			return conflict(u"com object %1 already exists on %2"_s.arg(object.number).arg(device.toString()));
		}
	}
	object.id = {};
	object.device = device;
	return insertOne(project, u"Add com object"_s, std::move(object));
}

Status removeComObject(Project& project, Id<ComObject> comObject)
{
	KESTREL_CHECK(requireExists(project, comObject));
	Removal removal;
	removal.addComObject(project, comObject);
	return removeCollected(project, u"Remove com object"_s, removal);
}

// --- Group addresses ---------------------------------------------------------------------------------------

Result<Id<GroupRange>> addGroupRange(Project& project, std::optional<Id<GroupRange>> parent, QString name,
	std::uint16_t start, std::uint16_t end)
{
	if (start > end)
	{
		return invalid(u"group range start %1 is after end %2"_s.arg(start).arg(end));
	}
	if (parent)
	{
		KESTREL_CHECK(requireExists(project, *parent));
		const GroupRange& outer = project.groupRanges().at(*parent);
		if (!outer.contains(start) || !outer.contains(end))
		{
			return invalid(u"group range %1..%2 is outside its parent %3..%4"_s.arg(start).arg(end).arg(outer.start).arg(
				outer.end));
		}
	}
	for (const Id<GroupRange> siblingId : project.groupRangesIn(parent))
	{
		const GroupRange& sibling = project.groupRanges().at(siblingId);
		if (start <= sibling.end && sibling.start <= end)
		{
			return conflict(u"group range %1..%2 overlaps '%3'"_s.arg(start).arg(end).arg(sibling.name));
		}
	}
	GroupRange range;
	range.parent = parent;
	range.name = std::move(name);
	range.start = start;
	range.end = end;
	return insertOne(project, u"Add group range"_s, std::move(range));
}

Status removeGroupRange(Project& project, Id<GroupRange> range)
{
	KESTREL_CHECK(requireExists(project, range));
	Removal removal;
	removal.addGroupRange(project, range);
	return removeCollected(project, u"Remove group range"_s, removal);
}

Result<Id<GroupAddress>> addGroupAddress(Project& project, knx::GroupAddress address, QString name, QString dpt,
	std::optional<Id<GroupRange>> range)
{
	if (address.raw() == 0)
	{
		return invalid(u"group address 0/0/0 is reserved"_s);
	}
	if (!isValidDptRef(dpt))
	{
		return invalid(u"invalid DPT reference '%1'"_s.arg(dpt));
	}
	if (range)
	{
		KESTREL_CHECK(requireExists(project, *range));
		if (!project.groupRanges().at(*range).contains(address.raw()))
		{
			return invalid(u"group address %1 is outside its range"_s.arg(QString::fromStdString(address.toString())));
		}
	}
	for (const GroupAddress& other : project.groupAddresses())
	{
		if (other.raw == address.raw())
		{
			return conflict(u"group address %1 already exists"_s.arg(QString::fromStdString(address.toString())));
		}
	}
	GroupAddress groupAddress;
	groupAddress.range = range;
	groupAddress.raw = address.raw();
	groupAddress.name = std::move(name);
	groupAddress.dpt = std::move(dpt);
	return insertOne(project, u"Add group address"_s, std::move(groupAddress));
}

Status setGroupAddressDpt(Project& project, Id<GroupAddress> groupAddress, QString dpt)
{
	KESTREL_CHECK(requireExists(project, groupAddress));
	if (!isValidDptRef(dpt))
	{
		return invalid(u"invalid DPT reference '%1'"_s.arg(dpt));
	}
	return project.edit(u"Set datapoint type"_s,
		[&](Tx& tx)
		{
			tx.set(groupAddress, &GroupAddress::dpt, dpt);
			return ok();
		});
}

Status removeGroupAddress(Project& project, Id<GroupAddress> groupAddress)
{
	KESTREL_CHECK(requireExists(project, groupAddress));
	Removal removal;
	removal.addGroupAddress(project, groupAddress);
	return removeCollected(project, u"Remove group address"_s, removal);
}

// --- Links -------------------------------------------------------------------------------------------------

Result<Id<Link>> link(Project& project, Id<ComObject> comObject, Id<GroupAddress> groupAddress)
{
	KESTREL_CHECK(requireExists(project, comObject));
	KESTREL_CHECK(requireExists(project, groupAddress));
	const auto existing = project.linksOfComObject(comObject);
	for (const Id<Link> other : existing)
	{
		if (project.links().at(other).groupAddress == groupAddress)
		{
			return conflict(u"%1 is already linked to %2"_s.arg(comObject.toString(), groupAddress.toString()));
		}
	}
	Link newLink;
	newLink.comObject = comObject;
	newLink.groupAddress = groupAddress;
	newLink.sending = existing.empty();
	return insertOne(project, u"Link group address"_s, std::move(newLink));
}

Status unlink(Project& project, Id<Link> linkId)
{
	KESTREL_CHECK(requireExists(project, linkId));
	Removal removal;
	removal.links.push_back(linkId);
	return removeCollected(project, u"Unlink group address"_s, removal);
}

Status setSendingLink(Project& project, Id<Link> linkId)
{
	KESTREL_CHECK(requireExists(project, linkId));
	return project.edit(u"Set sending group address"_s,
		[&](Tx& tx)
		{
			const Id<ComObject> comObject = tx.project().links().at(linkId).comObject;
			for (const Id<Link> other : tx.project().linksOfComObject(comObject))
			{
				if (other != linkId)
					tx.set(other, &Link::sending, false);
			}
			tx.set(linkId, &Link::sending, true);
			return ok();
		});
}

// --- Generic -----------------------------------------------------------------------------------------------

Status rename(Project& project, EntityRef ref, QString name)
{
	if (!project.contains(ref))
	{
		return notFound(ref);
	}
	MergeKey key{u"rename:"_s + ref.toString()};
	return project.edit(
		u"Rename"_s,
		[&](Tx& tx) -> Status
		{
			switch (ref.kind)
			{
				case EntityKind::Project:
				{
					ProjectSettings settings = tx.project().settings();
					settings.name = name;
					tx.setSettings(std::move(settings));
					return ok();
			}
			case EntityKind::BuildingPart: tx.set(ref.as<BuildingPart>(), &BuildingPart::name, name); return ok();
			case EntityKind::Area: tx.set(ref.as<Area>(), &Area::name, name); return ok();
			case EntityKind::Line: tx.set(ref.as<Line>(), &Line::name, name); return ok();
			case EntityKind::Device: tx.set(ref.as<Device>(), &Device::name, name); return ok();
			case EntityKind::ComObject: tx.set(ref.as<ComObject>(), &ComObject::name, name); return ok();
			case EntityKind::GroupRange: tx.set(ref.as<GroupRange>(), &GroupRange::name, name); return ok();
			case EntityKind::GroupAddress: tx.set(ref.as<GroupAddress>(), &GroupAddress::name, name); return ok();
			case EntityKind::Link: break;
			}
			return invalid(u"%1 has no name"_s.arg(ref.toString()));
		},
		std::move(key));
}

Status updateSettings(Project& project, ProjectSettings settings)
{
	if (settings.contentLanguage.isEmpty())
	{
		return invalid(u"empty content language"_s);
	}
	return project.edit(u"Change project settings"_s,
		[&](Tx& tx)
		{
			tx.setSettings(std::move(settings));
			return ok();
		});
}

} // namespace kestrel::model
