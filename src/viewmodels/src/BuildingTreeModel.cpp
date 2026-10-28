#include "kestrel/viewmodels/BuildingTreeModel.hpp"

#include "kestrel/viewmodels/Format.hpp"

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

using model::EntityKind;
using model::EntityRef;

BuildingTreeModel::BuildingTreeModel(ProjectSession* session, QObject* parent)
	: ShadowTreeModel(session, {tr("Name"), tr("Address")}, parent)
{
	rebuild();
}

bool BuildingTreeModel::isShown(EntityKind kind) const
{
	return kind == EntityKind::BuildingPart || kind == EntityKind::Device;
}

std::vector<EntityRef> BuildingTreeModel::childrenOf(EntityRef parent) const
{
	std::vector<EntityRef> result;
	if (parent.kind == EntityKind::Project)
	{
		for (const auto id : project().buildingPartsIn(std::nullopt))
			result.emplace_back(id);
	}
	else if (parent.kind == EntityKind::BuildingPart)
	{
		const auto part = parent.as<model::BuildingPart>();
		for (const auto id : project().buildingPartsIn(part))
			result.emplace_back(id);
		for (const auto id : project().devicesIn(part))
			result.emplace_back(id);
	}
	return result;
}

std::optional<EntityRef> BuildingTreeModel::parentOf(EntityRef ref) const
{
	if (ref.kind == EntityKind::BuildingPart)
	{
		const auto& part = project().buildingParts().at(ref.as<model::BuildingPart>());
		return part.parent ? EntityRef(*part.parent) : EntityRef::project();
	}
	if (ref.kind == EntityKind::Device)
	{
		const auto& device = project().devices().at(ref.as<model::Device>());
		if (device.place)
		{
			return EntityRef(*device.place);
		}
	}
	return std::nullopt;
}

QVariant BuildingTreeModel::cellData(EntityRef ref, int column, int role) const
{
	const bool isDevice = ref.kind == EntityKind::Device;
	switch (role)
	{
		case Qt::DisplayRole:
			if (column == 0)
			{
				const QString name = entityName(project(), ref);
				if (!name.isEmpty())
					return name;
				return isDevice ? tr("(unnamed device)") : buildingPartKindName(project().buildingParts().at(ref.as<model::BuildingPart>()).kind);
			}
			return isDevice ? individualAddressText(project(), ref.as<model::Device>()) : QString();
		case Qt::EditRole: return column == 0 ? entityName(project(), ref) : QVariant();
		case Qt::ToolTipRole: return entityLabel(project(), ref);
		case AddressRole: return isDevice ? individualAddressText(project(), ref.as<model::Device>()) : QString();
		case DescriptionRole:
			return isDevice ? project().devices().at(ref.as<model::Device>()).description
							: project().buildingParts().at(ref.as<model::BuildingPart>()).description;
		case IconRole:
			if (isDevice)
				return u"device"_s;
			return model::toString(project().buildingParts().at(ref.as<model::BuildingPart>()).kind).toLower();
		default: return {};
	}
}

void BuildingTreeModel::collectRelated(const model::ChangeSet& changeSet, std::set<EntityRef>& refresh, bool& all) const
{
	for (const model::Change& change : changeSet.changes)
	{
		// Renumbering an area or line changes the addresses of its devices.
		if (change.kind == model::Change::Modified && (change.ref.kind == EntityKind::Area || change.ref.kind == EntityKind::Line))
		{
			all = true;
		}
	}
	(void)refresh;
}

} // namespace kestrel::viewmodels
