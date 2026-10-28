#include "kestrel/viewmodels/Format.hpp"

#include "kestrel/knx/Address.hpp"
#include "kestrel/model/Operations.hpp"

#include <QCoreApplication>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

namespace
{

QString tr(const char* text)
{
	return QCoreApplication::translate("kestrel::viewmodels::Format", text);
}

} // namespace

QString errorText(const Error& error)
{
	return QString::fromStdString(error.message);
}

QString individualAddressText(const model::Project& project, model::Id<model::Device> device)
{
	const model::Device* entity = project.devices().find(device);
	if (!entity)
	{
		return {};
	}
	if (const auto address = project.individualAddress(device))
	{
		return QString::fromStdString(address->toString());
	}
	return lineAddressText(project, entity->line) + u".–"_s;
}

QString lineAddressText(const model::Project& project, model::Id<model::Line> line)
{
	const model::Line* entity = project.lines().find(line);
	if (!entity)
	{
		return {};
	}
	const model::Area* area = project.areas().find(entity->area);
	return u"%1.%2"_s.arg(area ? area->number : 0).arg(entity->number);
}

QString groupAddressText(const model::Project& project, std::uint16_t raw)
{
	return QString::fromStdString(knx::GroupAddress(raw).toString(project.settings().gaStyle));
}

QString groupRangeText(const model::Project& project, const model::GroupRange& range)
{
	const knx::GroupAddress start(range.start);
	const knx::GroupAddress end(range.end);
	switch (project.settings().gaStyle)
	{
		case knx::GroupAddressStyle::ThreeLevel:
			if (range.start == (range.start & 0xF800) && range.end == (range.start | 0x07FF))
			{
				return QString::number(start.main());
			}
			if (range.start == (range.start & 0xFF00) && range.end == (range.start | 0x00FF))
			{
				return u"%1/%2"_s.arg(start.main()).arg(start.middle());
			}
			break;
		case knx::GroupAddressStyle::TwoLevel:
			if (range.start == (range.start & 0xF800) && range.end == (range.start | 0x07FF))
			{
				return QString::number(start.main());
			}
			break;
		case knx::GroupAddressStyle::Free:
			break;
	}
	return u"%1 – %2"_s.arg(groupAddressText(project, start.raw()), groupAddressText(project, end.raw()));
}

std::optional<std::uint16_t> parseGroupAddress(QStringView text)
{
	const auto parsed = knx::GroupAddress::parse(text.trimmed().toString().toStdString());
	if (!parsed)
	{
		return std::nullopt;
	}
	return parsed->raw();
}

QString kindName(model::EntityKind kind)
{
	switch (kind)
	{
		case model::EntityKind::Project: return tr("Project");
		case model::EntityKind::BuildingPart: return tr("Building part");
		case model::EntityKind::Area: return tr("Area");
		case model::EntityKind::Line: return tr("Line");
		case model::EntityKind::Device: return tr("Device");
		case model::EntityKind::ComObject: return tr("Group object");
		case model::EntityKind::GroupRange: return tr("Group range");
		case model::EntityKind::GroupAddress: return tr("Group address");
		case model::EntityKind::Link: return tr("Association");
	}
	return {};
}

QString buildingPartKindName(model::BuildingPartKind kind)
{
	switch (kind)
	{
		case model::BuildingPartKind::Building: return tr("Building");
		case model::BuildingPartKind::Floor: return tr("Floor");
		case model::BuildingPartKind::Room: return tr("Room");
		case model::BuildingPartKind::Corridor: return tr("Corridor");
		case model::BuildingPartKind::Stairway: return tr("Stairway");
		case model::BuildingPartKind::DistributionBoard: return tr("Distribution board");
	}
	return {};
}

QString mediumName(model::Medium medium)
{
	return model::toString(medium);
}

QString sizeText(unsigned sizeInBit)
{
	if (sizeInBit < 8 || sizeInBit % 8 != 0)
	{
		return sizeInBit == 1 ? tr("1 bit") : tr("%1 bits").arg(sizeInBit);
	}
	const unsigned bytes = sizeInBit / 8;
	return bytes == 1 ? tr("1 byte") : tr("%1 bytes").arg(bytes);
}

QString countText(qsizetype count, const char* singular, const char* plural)
{
	return tr(count == 1 ? singular : plural).arg(count);
}

QString entityName(const model::Project& project, model::EntityRef ref)
{
	using model::EntityKind;
	switch (ref.kind)
	{
		case EntityKind::Project: return project.settings().name;
		case EntityKind::BuildingPart:
			if (const auto* e = project.buildingParts().find(ref.as<model::BuildingPart>()))
				return e->name;
			break;
		case EntityKind::Area:
			if (const auto* e = project.areas().find(ref.as<model::Area>()))
				return e->name;
			break;
		case EntityKind::Line:
			if (const auto* e = project.lines().find(ref.as<model::Line>()))
				return e->name;
			break;
		case EntityKind::Device:
			if (const auto* e = project.devices().find(ref.as<model::Device>()))
				return e->name;
			break;
		case EntityKind::ComObject:
			if (const auto* e = project.comObjects().find(ref.as<model::ComObject>()))
				return e->name;
			break;
		case EntityKind::GroupRange:
			if (const auto* e = project.groupRanges().find(ref.as<model::GroupRange>()))
				return e->name;
			break;
		case EntityKind::GroupAddress:
			if (const auto* e = project.groupAddresses().find(ref.as<model::GroupAddress>()))
				return e->name;
			break;
		case EntityKind::Link: break;
	}
	return {};
}

QString entityLabel(const model::Project& project, model::EntityRef ref)
{
	using model::EntityKind;
	if (!project.contains(ref))
	{
		return ref.toString();
	}
	const QString name = entityName(project, ref);
	auto join = [](const QString& address, const QString& text)
	{ return text.isEmpty() ? address : address + u' ' + text; };
	switch (ref.kind)
	{
		case EntityKind::Area:
			return join(QString::number(project.areas().at(ref.as<model::Area>()).number), name);
		case EntityKind::Line: return join(lineAddressText(project, ref.as<model::Line>()), name);
		case EntityKind::Device: return join(individualAddressText(project, ref.as<model::Device>()), name);
		case EntityKind::GroupRange:
			return join(groupRangeText(project, project.groupRanges().at(ref.as<model::GroupRange>())), name);
		case EntityKind::GroupAddress:
			return join(groupAddressText(project, project.groupAddresses().at(ref.as<model::GroupAddress>()).raw), name);
		case EntityKind::ComObject:
		{
			const model::ComObject& object = project.comObjects().at(ref.as<model::ComObject>());
			return u"%1: %2 %3"_s.arg(entityLabel(project, object.device)).arg(object.number).arg(object.name).trimmed();
		}
		case EntityKind::Link:
		{
			const model::Link& link = project.links().at(ref.as<model::Link>());
			return u"%1 ↔ %2"_s.arg(entityLabel(project, link.comObject), entityLabel(project, link.groupAddress));
		}
		case EntityKind::Project:
		case EntityKind::BuildingPart: break;
	}
	return name.isEmpty() ? kindName(ref.kind) : name;
}

Status removeEntity(model::Project& project, model::EntityRef ref)
{
	using model::EntityKind;
	switch (ref.kind)
	{
		case EntityKind::BuildingPart: return model::removeBuildingPart(project, ref.as<model::BuildingPart>());
		case EntityKind::Area: return model::removeArea(project, ref.as<model::Area>());
		case EntityKind::Line: return model::removeLine(project, ref.as<model::Line>());
		case EntityKind::Device: return model::removeDevice(project, ref.as<model::Device>());
		case EntityKind::ComObject: return model::removeComObject(project, ref.as<model::ComObject>());
		case EntityKind::GroupRange: return model::removeGroupRange(project, ref.as<model::GroupRange>());
		case EntityKind::GroupAddress: return model::removeGroupAddress(project, ref.as<model::GroupAddress>());
		case EntityKind::Link: return model::unlink(project, ref.as<model::Link>());
		case EntityKind::Project: break;
	}
	return makeError(ErrorCode::InvalidArgument, "the project itself cannot be deleted");
}

} // namespace kestrel::viewmodels
