// Project commands: stateless edits of a folder-format project. Each invocation loads the project,
// applies one model operation (one transaction), saves it and prints the created id.

#include "Cli.hpp"

#include "kestrel/appconfig/DeviceImages.hpp"
#include "kestrel/catalog/Knxprod.hpp"
#include "kestrel/dpt/Dpt.hpp"
#include "kestrel/model/Entities.hpp"
#include "kestrel/model/Operations.hpp"
#include "kestrel/model/Project.hpp"
#include "kestrel/persist/Persist.hpp"
#include "kestrel/validate/Validate.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <optional>

namespace kestrel::cli
{

using namespace Qt::StringLiterals;

namespace
{

QString qs(const std::string& text)
{
	return QString::fromStdString(text);
}

/// Model rejections (bad numbers, duplicates, unknown ids) are findings; anything else is I/O.
int modelExitCode(const Error& error)
{
	switch (error.code)
	{
		case ErrorCode::InvalidArgument:
		case ErrorCode::Conflict:
		case ErrorCode::NotFound: return kExitFindings;
		default: return kExitIo;
	}
}

std::optional<model::Project> load(Invocation& inv, const QString& dir, int& exitCode)
{
	auto project = persist::load(dir);
	if (!project)
	{
		exitCode = inv.out.failure(kExitIo, project.error());
		return std::nullopt;
	}
	return std::move(project).value();
}

int save(Invocation& inv, model::Project& project, const QString& dir)
{
	const Status saved = persist::save(project, dir);
	if (!saved)
	{
		return inv.out.failure(kExitIo, saved.error());
	}
	project.history().setClean();
	return kExitOk;
}

template <class T>
std::optional<model::Id<T>> parseId(Invocation& inv, const QString& text, int& exitCode)
{
	auto id = model::Id<T>::parse(text);
	if (!id)
	{
		const QString prefix = model::idPrefix(model::EntityTraits<T>::kKind);
		exitCode = inv.out.failure(kExitUsage, u"invalid id '%1' (expected %2-…)"_s.arg(text, prefix), u"Usage"_s);
		return std::nullopt;
	}
	return *id;
}

std::optional<unsigned> parseNumber(Invocation& inv, const QString& text, const QString& what, int& exitCode)
{
	bool ok = false;
	const uint value = text.toUInt(&ok);
	if (!ok)
	{
		exitCode = inv.out.failure(kExitUsage, u"invalid %1 '%2'"_s.arg(what, text), u"Usage"_s);
		return std::nullopt;
	}
	return value;
}

/// "1.001", "DPT-1", "DPST-1-1" → the model's ETS form ("DPST-1-1"). Empty stays empty.
std::optional<QString> parseDpt(Invocation& inv, const QString& text, int& exitCode)
{
	if (text.isEmpty())
	{
		return QString();
	}
	auto id = dpt::DptId::parse(text.toStdString());
	if (!id)
	{
		exitCode = inv.out.failure(kExitUsage, u"invalid datapoint type '%1'"_s.arg(text), u"Usage"_s);
		return std::nullopt;
	}
	return qs(id->toEtsString());
}

/// Runs a model operation returning Result<Id<T>> on the loaded project, saves, prints the id.
template <class T>
int createEntity(Invocation& inv, const QString& dir, const std::function<Result<model::Id<T>>(model::Project&)>& op,
	const QString& kind)
{
	int exitCode = kExitOk;
	auto project = load(inv, dir, exitCode);
	if (!project)
	{
		return exitCode;
	}
	auto id = op(*project);
	if (!id)
	{
		return inv.out.failure(modelExitCode(id.error()), id.error());
	}
	if (const int saved = save(inv, *project, dir); saved != kExitOk)
	{
		return saved;
	}
	return inv.out.success({{u"id"_s, id->toString()}, {u"kind"_s, kind}}, id->toString());
}

// --- describing entities -----------------------------------------------------------------------------------

QString deviceAddressText(const model::Project& project, const model::Device& device)
{
	if (const auto ia = project.individualAddress(device.id))
	{
		return qs(ia->toString());
	}
	const model::Line* line = project.lines().find(device.line);
	const model::Area* area = line ? project.areas().find(line->area) : nullptr;
	return area ? u"%1.%2.-"_s.arg(area->number).arg(line->number) : u"-.-.-"_s;
}

QString gaText(const model::Project& project, const model::GroupAddress& ga)
{
	return qs(ga.address().toString(project.settings().gaStyle));
}

QString describe(const model::Project& project, const model::EntityRef& ref)
{
	using model::EntityKind;
	switch (ref.kind)
	{
		case EntityKind::Project: return u"project \"%1\""_s.arg(project.settings().name);
		case EntityKind::BuildingPart:
			if (const auto* part = project.buildingParts().find(ref.as<model::BuildingPart>()))
			{
				return u"%1 \"%2\""_s.arg(model::toString(part->kind), part->name);
			}
			break;
		case EntityKind::Area:
			if (const auto* area = project.areas().find(ref.as<model::Area>()))
			{
				return u"area %1 \"%2\""_s.arg(area->number).arg(area->name);
			}
			break;
		case EntityKind::Line:
			if (const auto* line = project.lines().find(ref.as<model::Line>()))
			{
				const auto* area = project.areas().find(line->area);
				return u"line %1.%2 \"%3\""_s.arg(area ? area->number : 0).arg(line->number).arg(line->name);
			}
			break;
		case EntityKind::Device:
			if (const auto* device = project.devices().find(ref.as<model::Device>()))
			{
				return u"device %1 \"%2\""_s.arg(deviceAddressText(project, *device), device->name);
			}
			break;
		case EntityKind::ComObject:
			if (const auto* object = project.comObjects().find(ref.as<model::ComObject>()))
			{
				const auto* device = project.devices().find(object->device);
				return u"object #%1 \"%2\" of %3"_s.arg(object->number)
					.arg(object->name, device ? describe(project, device->id) : u"?"_s);
			}
			break;
		case EntityKind::GroupRange:
			if (const auto* range = project.groupRanges().find(ref.as<model::GroupRange>()))
			{
				return u"range \"%1\""_s.arg(range->name);
			}
			break;
		case EntityKind::GroupAddress:
			if (const auto* ga = project.groupAddresses().find(ref.as<model::GroupAddress>()))
			{
				return u"group address %1 \"%2\""_s.arg(gaText(project, *ga), ga->name);
			}
			break;
		case EntityKind::Link:
			if (const auto* link = project.links().find(ref.as<model::Link>()))
			{
				return u"link %1 -> %2"_s.arg(describe(project, link->comObject), describe(project, link->groupAddress));
			}
			break;
	}
	return ref.toString();
}

QJsonObject subjectJson(const model::Project& project, const model::EntityRef& ref)
{
	return {{u"id"_s, ref.toString()}, {u"text"_s, describe(project, ref)}};
}

// --- project new / info ------------------------------------------------------------------------------------

int projectNew(Invocation& inv)
{
	const QString dir = inv.args[0];
	if (QFileInfo::exists(QDir(dir).filePath(u"kestrel.json"_s)))
	{
		return inv.out.failure(kExitIo, u"%1 already contains a project"_s.arg(dir), u"Conflict"_s);
	}
	model::ProjectSettings settings;
	settings.name = inv.value(u"name"_s);
	if (settings.name.isEmpty())
	{
		return inv.out.failure(kExitUsage, u"--name is required"_s, u"Usage"_s);
	}
	const QString style = inv.value(u"ga-style"_s);
	if (style == u"3"_s || style.compare(u"ThreeLevel"_s, Qt::CaseInsensitive) == 0)
	{
		settings.gaStyle = knx::GroupAddressStyle::ThreeLevel;
	}
	else if (style == u"2"_s || style.compare(u"TwoLevel"_s, Qt::CaseInsensitive) == 0)
	{
		settings.gaStyle = knx::GroupAddressStyle::TwoLevel;
	}
	else if (style.compare(u"Free"_s, Qt::CaseInsensitive) == 0)
	{
		settings.gaStyle = knx::GroupAddressStyle::Free;
	}
	else
	{
		return inv.out.failure(kExitUsage, u"invalid --ga-style (ThreeLevel, TwoLevel or Free)"_s, u"Usage"_s);
	}

	model::Project project;
	if (const Status updated = model::updateSettings(project, settings); !updated)
	{
		return inv.out.failure(modelExitCode(updated.error()), updated.error());
	}
	if (const int saved = save(inv, project, dir); saved != kExitOk)
	{
		return saved;
	}
	return inv.out.success({{u"dir"_s, dir}, {u"name"_s, settings.name}},
		u"created project \"%1\" in %2"_s.arg(settings.name, dir));
}

int projectInfo(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto project = load(inv, inv.args[0], exitCode);
	if (!project)
	{
		return exitCode;
	}
	const model::Project& p = *project;

	QJsonArray buildings;
	for (const auto& part : p.buildingParts())
	{
		buildings.append(QJsonObject{{u"id"_s, part.id.toString()}, {u"kind"_s, model::toString(part.kind)},
			{u"name"_s, part.name}, {u"parent"_s, part.parent ? part.parent->toString() : QString()}});
	}
	QJsonArray areas;
	for (const auto& area : p.areas())
	{
		areas.append(QJsonObject{{u"id"_s, area.id.toString()}, {u"number"_s, static_cast<int>(area.number)},
			{u"name"_s, area.name}, {u"medium"_s, model::toString(area.medium)}});
	}
	QJsonArray lines;
	for (const auto& line : p.lines())
	{
		lines.append(QJsonObject{{u"id"_s, line.id.toString()}, {u"area"_s, line.area.toString()},
			{u"number"_s, static_cast<int>(line.number)}, {u"name"_s, line.name}});
	}
	QJsonArray devices;
	for (const auto& device : p.devices())
	{
		const auto ia = p.individualAddress(device.id);
		devices.append(QJsonObject{{u"id"_s, device.id.toString()}, {u"line"_s, device.line.toString()},
			{u"name"_s, device.name}, {u"address"_s, ia ? qs(ia->toString()) : QString()},
			{u"place"_s, device.place ? device.place->toString() : QString()}});
	}
	QJsonArray comObjects;
	for (const auto& object : p.comObjects())
	{
		comObjects.append(QJsonObject{{u"id"_s, object.id.toString()}, {u"device"_s, object.device.toString()},
			{u"number"_s, static_cast<int>(object.number)}, {u"name"_s, object.name},
			{u"sizeInBit"_s, static_cast<int>(object.sizeInBit)}, {u"flags"_s, object.flags.toString()},
			{u"dpt"_s, object.dpt}});
	}
	QJsonArray groupAddresses;
	for (const auto& ga : p.groupAddresses())
	{
		groupAddresses.append(QJsonObject{{u"id"_s, ga.id.toString()}, {u"address"_s, gaText(p, ga)},
			{u"name"_s, ga.name}, {u"dpt"_s, ga.dpt}});
	}
	QJsonArray links;
	for (const auto& link : p.links())
	{
		links.append(QJsonObject{{u"id"_s, link.id.toString()}, {u"comObject"_s, link.comObject.toString()},
			{u"groupAddress"_s, link.groupAddress.toString()}, {u"sending"_s, link.sending}});
	}

	const QString text = u"project       %1\nga style      %2\nbuildings     %3\nareas         %4\nlines         %5\n"
						 u"devices       %6\ncom objects   %7\ngroup addr.   %8\nlinks         %9"_s.arg(p.settings().name,
							 model::toString(p.settings().gaStyle))
							 .arg(p.buildingParts().size())
							 .arg(p.areas().size())
							 .arg(p.lines().size())
							 .arg(p.devices().size())
							 .arg(p.comObjects().size())
							 .arg(p.groupAddresses().size())
							 .arg(p.links().size());
	return inv.out.success(
		{
			{u"name"_s, p.settings().name},
			{u"gaStyle"_s, model::toString(p.settings().gaStyle)},
			{u"buildingParts"_s, buildings},
			{u"areas"_s, areas},
			{u"lines"_s, lines},
			{u"devices"_s, devices},
			{u"comObjects"_s, comObjects},
			{u"groupAddresses"_s, groupAddresses},
			{u"links"_s, links},
		},
		text);
}

// --- edits -------------------------------------------------------------------------------------------------

int buildingAdd(Invocation& inv)
{
	std::optional<model::BuildingPartKind> kind;
	for (const auto candidate : {model::BuildingPartKind::Building, model::BuildingPartKind::Floor,
			 model::BuildingPartKind::Room, model::BuildingPartKind::Corridor, model::BuildingPartKind::Stairway,
			 model::BuildingPartKind::DistributionBoard})
	{
		if (model::toString(candidate).compare(inv.args[1], Qt::CaseInsensitive) == 0)
		{
			kind = candidate;
		}
	}
	if (!kind)
	{
		return inv.out.failure(kExitUsage,
			u"invalid kind '%1' (Building, Floor, Room, Corridor, Stairway, DistributionBoard)"_s.arg(inv.args[1]),
			u"Usage"_s);
	}
	int exitCode = kExitOk;
	std::optional<model::Id<model::BuildingPart>> parent;
	if (inv.isSet(u"parent"_s))
	{
		parent = parseId<model::BuildingPart>(inv, inv.value(u"parent"_s), exitCode);
		if (!parent)
		{
			return exitCode;
		}
	}
	const QString name = inv.args[2];
	return createEntity<model::BuildingPart>(inv, inv.args[0],
		[&](model::Project& p) { return model::addBuildingPart(p, parent, *kind, name); }, u"BuildingPart"_s);
}

std::optional<model::Medium> parseMediumOption(Invocation& inv, int& exitCode)
{
	for (const auto medium : {model::Medium::TP, model::Medium::IP, model::Medium::RF, model::Medium::PL})
	{
		if (model::toString(medium).compare(inv.value(u"medium"_s), Qt::CaseInsensitive) == 0)
		{
			return medium;
		}
	}
	exitCode = inv.out.failure(kExitUsage, u"invalid --medium (TP, IP, RF or PL)"_s, u"Usage"_s);
	return std::nullopt;
}

int topologyAddArea(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto number = parseNumber(inv, inv.args[1], u"area number"_s, exitCode);
	const auto medium = number ? parseMediumOption(inv, exitCode) : std::nullopt;
	if (!number || !medium)
	{
		return exitCode;
	}
	const QString name = inv.args[2];
	return createEntity<model::Area>(inv, inv.args[0],
		[&](model::Project& p) { return model::addArea(p, *number, name, *medium); }, u"Area"_s);
}

int topologyAddLine(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto area = parseId<model::Area>(inv, inv.args[1], exitCode);
	const auto number = area ? parseNumber(inv, inv.args[2], u"line number"_s, exitCode) : std::nullopt;
	const auto medium = number ? parseMediumOption(inv, exitCode) : std::nullopt;
	if (!medium)
	{
		return exitCode;
	}
	const QString name = inv.args[3];
	return createEntity<model::Line>(inv, inv.args[0],
		[&](model::Project& p) { return model::addLine(p, *area, *number, name, *medium); }, u"Line"_s);
}

int topologyAddDevice(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto line = parseId<model::Line>(inv, inv.args[1], exitCode);
	if (!line)
	{
		return exitCode;
	}
	std::optional<unsigned> number;
	if (inv.isSet(u"number"_s))
	{
		number = parseNumber(inv, inv.value(u"number"_s), u"device number"_s, exitCode);
		if (!number)
		{
			return exitCode;
		}
	}
	std::optional<model::Id<model::BuildingPart>> place;
	if (inv.isSet(u"place"_s))
	{
		place = parseId<model::BuildingPart>(inv, inv.value(u"place"_s), exitCode);
		if (!place)
		{
			return exitCode;
		}
	}
	const QString name = inv.args[2];
	return createEntity<model::Device>(inv, inv.args[0],
		[&](model::Project& p) -> Result<model::Id<model::Device>>
		{
			// Two operations: the device and its placement. A failed placement rolls back the device.
			std::optional<Error> failure;
			model::Id<model::Device> created;
			const Status status = p.edit(u"Add device"_s,
				[&](model::Tx&) -> Status
				{
					auto id = model::addDevice(p, *line, number, name, inv.value(u"product"_s), inv.value(u"application"_s));
					if (!id)
					{
						return Unexpected{id.error()};
					}
					created = *id;
					if (place)
					{
						KESTREL_CHECK(model::placeDevice(p, created, place));
					}
					return ok();
				});
			if (!status)
			{
				return Unexpected{status.error()};
			}
			return created;
		},
		u"Device"_s);
}

int coAdd(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto device = parseId<model::Device>(inv, inv.args[1], exitCode);
	const auto number = device ? parseNumber(inv, inv.args[2], u"object number"_s, exitCode) : std::nullopt;
	const auto dptRef = number ? parseDpt(inv, inv.value(u"dpt"_s), exitCode) : std::nullopt;
	if (!dptRef)
	{
		return exitCode;
	}
	model::ComObject object;
	object.number = *number;
	object.name = inv.args[3];
	object.function = inv.value(u"function"_s);
	object.dpt = *dptRef;
	if (inv.isSet(u"size"_s))
	{
		const auto size = parseNumber(inv, inv.value(u"size"_s), u"size"_s, exitCode);
		if (!size)
		{
			return exitCode;
		}
		object.sizeInBit = *size;
	}
	else if (!object.dpt.isEmpty())
	{
		const auto size = dpt::sizeInBit(*dpt::DptId::parse(inv.value(u"dpt"_s).toStdString()));
		object.sizeInBit = size > 0 ? static_cast<unsigned>(size) : 1;
	}
	if (inv.isSet(u"flags"_s))
	{
		const auto flags = model::ComObjectFlags::parse(inv.value(u"flags"_s).toUpper());
		if (!flags)
		{
			return inv.out.failure(kExitUsage, u"invalid --flags '%1' (letters of CRWTUI)"_s.arg(inv.value(u"flags"_s)),
				u"Usage"_s);
		}
		object.flags = *flags;
	}
	return createEntity<model::ComObject>(inv, inv.args[0],
		[&](model::Project& p) { return model::addComObject(p, *device, object); }, u"ComObject"_s);
}

int gaAdd(Invocation& inv)
{
	auto address = knx::GroupAddress::parse(inv.args[1].toStdString());
	if (!address)
	{
		return inv.out.failure(kExitUsage, u"invalid group address '%1'"_s.arg(inv.args[1]), u"Usage"_s);
	}
	int exitCode = kExitOk;
	const auto dptRef = parseDpt(inv, inv.value(u"dpt"_s), exitCode);
	if (!dptRef)
	{
		return exitCode;
	}
	const QString name = inv.args[2];
	return createEntity<model::GroupAddress>(inv, inv.args[0],
		[&](model::Project& p) { return model::addGroupAddress(p, *address, name, *dptRef); }, u"GroupAddress"_s);
}

int linkAdd(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto co = parseId<model::ComObject>(inv, inv.args[1], exitCode);
	const auto ga = co ? parseId<model::GroupAddress>(inv, inv.args[2], exitCode) : std::nullopt;
	if (!ga)
	{
		return exitCode;
	}
	return createEntity<model::Link>(inv, inv.args[0], [&](model::Project& p) { return model::link(p, *co, *ga); },
		u"Link"_s);
}

// --- device configuration ----------------------------------------------------------------------------------

int deviceParam(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto device = parseId<model::Device>(inv, inv.args[1], exitCode);
	if (!device)
	{
		return exitCode;
	}
	auto project = load(inv, inv.args[0], exitCode);
	if (!project)
	{
		return exitCode;
	}
	const bool reset = inv.isSet(u"reset"_s);
	const Status status = model::setDeviceParam(*project, *device, inv.args[2],
		reset ? std::nullopt : std::optional<QString>(inv.args[3]));
	if (!status)
	{
		return inv.out.failure(modelExitCode(status.error()), status.error());
	}
	if (const int saved = save(inv, *project, inv.args[0]); saved != kExitOk)
	{
		return saved;
	}
	return inv.out.success({{u"parameter"_s, inv.args[2]}, {u"reset"_s, reset}},
		reset ? u"%1 reset to its default"_s.arg(inv.args[2]) : u"%1 = %2"_s.arg(inv.args[2], inv.args[3]));
}

/// Links a catalog com object (ComObjectRef id, or an object number resolved with the device's current
/// parameters) to a group address; creates the com object and the group address when missing.
int deviceLink(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto deviceId = parseId<model::Device>(inv, inv.args[1], exitCode);
	if (!deviceId)
	{
		return exitCode;
	}
	const auto address = knx::GroupAddress::parse(inv.args[3].toStdString());
	if (!address)
	{
		return inv.out.failure(kExitUsage, u"invalid group address %1"_s.arg(inv.args[3]), u"Usage"_s);
	}
	if (!inv.isSet(u"knxprod"_s))
	{
		return inv.out.failure(kExitUsage, u"--knxprod is required (the device's product file)"_s, u"Usage"_s);
	}
	auto project = load(inv, inv.args[0], exitCode);
	if (!project)
	{
		return exitCode;
	}
	const model::Device* device = project->devices().find(*deviceId);
	if (!device)
	{
		return inv.out.failure(kExitFindings, u"no device %1"_s.arg(inv.args[1]), u"NotFound"_s);
	}
	auto loaded = catalog::loadKnxprodFile(inv.value(u"knxprod"_s));
	if (!loaded)
	{
		return inv.out.failure(kExitFindings, loaded.error());
	}
	const catalog::ApplicationProgram* app = loaded->database.findApplicationProgram(device->appProgramRef);
	if (!app)
	{
		return inv.out.failure(kExitFindings,
			u"the product file does not contain the device's application \"%1\""_s.arg(device->appProgramRef),
			u"NotFound"_s);
	}
	auto configuration = appconfig::deviceConfiguration(*project, *deviceId, *app);
	auto evaluation = configuration ? appconfig::evaluate(*app, configuration->values)
									: Result<appconfig::Evaluation>(Unexpected{configuration.error()});
	if (!evaluation)
	{
		return inv.out.failure(kExitFindings, evaluation.error());
	}
	const QString wanted = inv.args[2];
	bool isNumber = false;
	const uint number = wanted.toUInt(&isNumber);
	const catalog::EffectiveComObject* definition = nullptr;
	const auto effective = app->effectiveComObjects();
	for (const catalog::EffectiveComObject& candidate : effective)
	{
		const bool matches = isNumber ? candidate.number == number : candidate.refId == wanted;
		if (matches && evaluation->isComObjectRefActive(candidate.refId))
		{
			definition = &candidate;
			break;
		}
	}
	if (!definition)
	{
		return inv.out.failure(kExitFindings,
			u"no active com object %1 with the device's current parameters"_s.arg(wanted), u"NotFound"_s);
	}

	std::optional<model::Id<model::Link>> created;
	const Status status = project->edit(u"Link %1 to %2"_s.arg(definition->number).arg(inv.args[3]),
		[&](model::Tx&) -> Status
		{
			std::optional<model::Id<model::ComObject>> objectId;
			for (const model::ComObject& object : project->comObjects().rows())
			{
				if (object.device == *deviceId && object.number == definition->number)
				{
					objectId = object.id;
				}
			}
			if (!objectId)
			{
				model::ComObject object;
				object.number = definition->number;
				object.name = definition->text.isEmpty() ? definition->name : definition->text;
				object.function = definition->functionText;
				object.sizeInBit = definition->sizeInBit == 0 ? 1 : definition->sizeInBit;
				object.flags = model::ComObjectFlags{definition->flags.communication, definition->flags.read,
					definition->flags.write, definition->flags.transmit, definition->flags.update,
					definition->flags.readOnInit};
				const QString dpt = definition->datapointType.section(u' ', 0, 0);
				object.dpt = model::isValidDptRef(dpt) ? dpt : QString();
				object.catalogRef = definition->refId;
				KESTREL_TRY(const auto id, model::addComObject(*project, *deviceId, object));
				objectId = id;
			}
			std::optional<model::Id<model::GroupAddress>> groupAddress;
			for (const model::GroupAddress& candidate : project->groupAddresses().rows())
			{
				if (candidate.raw == address->raw())
				{
					groupAddress = candidate.id;
				}
			}
			if (!groupAddress)
			{
				const QString name = inv.isSet(u"ga-name"_s) ? inv.value(u"ga-name"_s) : qs(address->toString());
				KESTREL_TRY(const auto id, model::addGroupAddress(*project, *address, name));
				groupAddress = id;
			}
			KESTREL_TRY(const auto link, model::link(*project, *objectId, *groupAddress));
			created = link;
			return ok();
		});
	if (!status)
	{
		return inv.out.failure(modelExitCode(status.error()), status.error());
	}
	if (const int saved = save(inv, *project, inv.args[0]); saved != kExitOk)
	{
		return saved;
	}
	return inv.out.success({{u"id"_s, created->toString()}, {u"object"_s, static_cast<int>(definition->number)},
							   {u"comObjectRef"_s, definition->refId}},
		u"linked object %1 (%2) to %3"_s.arg(definition->number).arg(definition->functionText, inv.args[3]));
}

// --- validate ----------------------------------------------------------------------------------------------

int validateProject(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto project = load(inv, inv.args[0], exitCode);
	if (!project)
	{
		return exitCode;
	}
	const std::vector<validate::Diagnostic> diagnostics = validate::run(*project);
	int errors = 0;
	int warnings = 0;
	int infos = 0;
	QJsonArray list;
	QStringList lines;
	for (const validate::Diagnostic& diagnostic : diagnostics)
	{
		switch (diagnostic.severity)
		{
			case validate::Severity::Error: ++errors; break;
			case validate::Severity::Warning: ++warnings; break;
			case validate::Severity::Info: ++infos; break;
		}
		QJsonArray subjects;
		QStringList subjectTexts;
		for (const model::EntityRef& ref : diagnostic.subjects)
		{
			subjects.append(subjectJson(*project, ref));
			subjectTexts.append(describe(*project, ref));
		}
		const QString severity = QString::fromLatin1(validate::toString(diagnostic.severity));
		list.append(QJsonObject{{u"code"_s, diagnostic.code}, {u"severity"_s, severity},
			{u"message"_s, diagnostic.message}, {u"subjects"_s, subjects}});
		lines.append(u"%1 %2 %3: %4"_s.arg(severity.leftJustified(7), diagnostic.code.leftJustified(7), diagnostic.message,
			subjectTexts.join(u", "_s)));
	}
	lines.append(u"%1 error(s), %2 warning(s), %3 info(s)"_s.arg(errors).arg(warnings).arg(infos));
	const QJsonObject object{{u"diagnostics"_s, list}, {u"errors"_s, errors}, {u"warnings"_s, warnings},
		{u"infos"_s, infos}};
	if (errors == 0)
	{
		return inv.out.success(object, lines.join(u'\n'));
	}
	// Findings are a normal outcome, not a failure of the command: print the full report, exit 1.
	if (inv.out.json())
	{
		QJsonObject withStatus = object;
		withStatus.insert(u"ok"_s, false);
		withStatus.insert(u"exitCode"_s, kExitFindings);
		Output::writeStdout(QJsonDocument(withStatus).toJson(QJsonDocument::Compact));
	}
	else
	{
		Output::writeStdout(lines.join(u'\n').toUtf8());
	}
	return kExitFindings;
}

} // namespace

std::vector<Command> projectCommands()
{
	const QCommandLineOption name(u"name"_s, u"Project name."_s, u"name"_s);
	const QCommandLineOption gaStyle(u"ga-style"_s, u"ThreeLevel (default), TwoLevel or Free."_s, u"style"_s,
		u"ThreeLevel"_s);
	const QCommandLineOption parent(u"parent"_s, u"Parent building part id."_s, u"id"_s);
	const QCommandLineOption medium(u"medium"_s, u"TP (default), IP, RF or PL."_s, u"medium"_s, u"TP"_s);
	const QCommandLineOption number(u"number"_s, u"Device number 0..255 (omit: not addressed yet)."_s, u"n"_s);
	const QCommandLineOption place(u"place"_s, u"Building part id to place the device in."_s, u"id"_s);
	const QCommandLineOption size(u"size"_s, u"Size in bits (default: from --dpt, else 1)."_s, u"bits"_s);
	const QCommandLineOption flags(u"flags"_s, u"Communication flags, letters of CRWTUI (default C)."_s, u"flags"_s);
	const QCommandLineOption dptOption(u"dpt"_s, u"Datapoint type: 1.001, DPT-9 or DPST-9-1."_s, u"dpt"_s);
	const QCommandLineOption function(u"function"_s, u"Function text of the object."_s, u"text"_s);
	const QCommandLineOption product(u"product"_s, u"Catalog product id (from catalog inspect)."_s, u"id"_s);
	const QCommandLineOption application(u"application"_s, u"Application program id (from catalog inspect)."_s, u"id"_s);
	const QCommandLineOption reset(u"reset"_s, u"Reset the parameter to its default instead of setting a value."_s);
	const QCommandLineOption knxprodOption(u"knxprod"_s, u"Product file (.knxprod) of the device."_s, u"file"_s);
	const QCommandLineOption gaName(u"ga-name"_s, u"Name of the group address when it is created."_s, u"name"_s);

	return {
		{{u"project"_s, u"new"_s}, {u"dir"_s}, u"Create a project folder (FR-PRJ-1)."_s, {name, gaStyle}, projectNew},
		{{u"project"_s, u"info"_s}, {u"dir"_s}, u"Summarise a project and list its entities with ids."_s, {}, projectInfo},
		{{u"building"_s, u"add"_s}, {u"dir"_s, u"kind"_s, u"name"_s}, u"Add a building part; prints its id."_s, {parent},
			buildingAdd},
		{{u"topology"_s, u"add-area"_s}, {u"dir"_s, u"number"_s, u"name"_s}, u"Add an area (FR-TOP-1); prints its id."_s,
			{medium}, topologyAddArea},
		{{u"topology"_s, u"add-line"_s}, {u"dir"_s, u"areaId"_s, u"number"_s, u"name"_s},
			u"Add a line to an area (FR-TOP-1); prints its id."_s, {medium}, topologyAddLine},
		{{u"topology"_s, u"add-device"_s}, {u"dir"_s, u"lineId"_s, u"name"_s},
			u"Add a device to a line (FR-TOP-2); prints its id."_s, {number, place, product, application}, topologyAddDevice},
		{{u"co"_s, u"add"_s}, {u"dir"_s, u"deviceId"_s, u"number"_s, u"name"_s},
			u"Add a manually defined com object; prints its id."_s, {size, flags, dptOption, function}, coAdd},
		{{u"ga"_s, u"add"_s}, {u"dir"_s, u"address"_s, u"name"_s}, u"Add a group address (FR-GA-1); prints its id."_s,
			{dptOption}, gaAdd},
		{{u"link"_s}, {u"dir"_s, u"comObjectId"_s, u"groupAddressId"_s},
			u"Link a com object to a group address (FR-GA-3); prints the link id."_s, {}, linkAdd},
		{{u"device"_s, u"param"_s}, {u"dir"_s, u"deviceId"_s, u"parameterRef"_s, u"value"_s},
			u"Set a device parameter (ParameterRef id); with --reset the value is ignored (FR-DEV-1)."_s, {reset},
			deviceParam},
		{{u"device"_s, u"link"_s}, {u"dir"_s, u"deviceId"_s, u"comObject"_s, u"groupAddress"_s},
			u"Link a catalog com object (ComObjectRef id or number) to a group address, creating both if needed (FR-GA-3)."_s,
			{knxprodOption, gaName}, deviceLink},
		{{u"validate"_s}, {u"dir"_s}, u"Run the project checks (FR-GA-4); exit 1 if any error."_s, {}, validateProject},
	};
}

} // namespace kestrel::cli
