#include "kestrel/viewmodels/TopologyTreeModel.hpp"

#include "kestrel/viewmodels/Format.hpp"

#include <algorithm>
#include <limits>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

using model::EntityKind;
using model::EntityRef;

TopologyTreeModel::TopologyTreeModel(ProjectSession* session, QObject* parent)
	: ShadowTreeModel(session, {tr("Name"), tr("Address"), tr("Medium")}, parent)
{
	rebuild();
}

bool TopologyTreeModel::isShown(EntityKind kind) const
{
	return kind == EntityKind::Area || kind == EntityKind::Line || kind == EntityKind::Device;
}

std::vector<EntityRef> TopologyTreeModel::childrenOf(EntityRef parent) const
{
	std::vector<EntityRef> result;
	const model::Project& p = project();
	if (parent.kind == EntityKind::Project)
	{
		std::vector<const model::Area*> areas;
		for (const model::Area& area : p.areas())
			areas.push_back(&area);
		std::stable_sort(areas.begin(), areas.end(), [](const auto* a, const auto* b) { return a->number < b->number; });
		for (const auto* area : areas)
			result.emplace_back(area->id);
	}
	else if (parent.kind == EntityKind::Area)
	{
		auto lines = p.linesOf(parent.as<model::Area>());
		std::stable_sort(lines.begin(), lines.end(),
			[&](auto a, auto b) { return p.lines().at(a).number < p.lines().at(b).number; });
		for (const auto id : lines)
			result.emplace_back(id);
	}
	else if (parent.kind == EntityKind::Line)
	{
		auto devices = p.devicesOf(parent.as<model::Line>());
		const auto key = [&](model::Id<model::Device> id)
		{ return p.devices().at(id).number.value_or(std::numeric_limits<unsigned>::max()); };
		std::stable_sort(devices.begin(), devices.end(), [&](auto a, auto b) { return key(a) < key(b); });
		for (const auto id : devices)
			result.emplace_back(id);
	}
	return result;
}

std::optional<EntityRef> TopologyTreeModel::parentOf(EntityRef ref) const
{
	switch (ref.kind)
	{
		case EntityKind::Area: return EntityRef::project();
		case EntityKind::Line: return EntityRef(project().lines().at(ref.as<model::Line>()).area);
		case EntityKind::Device: return EntityRef(project().devices().at(ref.as<model::Device>()).line);
		default: return std::nullopt;
	}
}

QVariant TopologyTreeModel::cellData(EntityRef ref, int column, int role) const
{
	const model::Project& p = project();
	auto address = [&]() -> QString
	{
		switch (ref.kind)
		{
			case EntityKind::Area: return QString::number(p.areas().at(ref.as<model::Area>()).number);
			case EntityKind::Line: return lineAddressText(p, ref.as<model::Line>());
			case EntityKind::Device: return individualAddressText(p, ref.as<model::Device>());
			default: return {};
		}
	};
	auto medium = [&]() -> QString
	{
		switch (ref.kind)
		{
			case EntityKind::Area: return mediumName(p.areas().at(ref.as<model::Area>()).medium);
			case EntityKind::Line: return mediumName(p.lines().at(ref.as<model::Line>()).medium);
			default: return {};
		}
	};
	switch (role)
	{
		case Qt::DisplayRole:
			switch (column)
			{
				case 0:
				{
					const QString name = entityName(p, ref);
					return name.isEmpty() ? kindName(ref.kind) : name;
				}
				case 1: return address();
				case 2: return medium();
				default: return {};
			}
		case Qt::EditRole: return column == 0 ? entityName(p, ref) : QVariant();
		case Qt::ToolTipRole: return entityLabel(p, ref);
		case AddressRole: return address();
		case DescriptionRole:
			return ref.kind == EntityKind::Device ? p.devices().at(ref.as<model::Device>()).description : QString();
		case IconRole: return QString::fromLatin1(model::toString(ref.kind)).toLower();
		default: return {};
	}
}

void TopologyTreeModel::collectRelated(const model::ChangeSet& changeSet, std::set<EntityRef>& refresh, bool& all) const
{
	(void)refresh;
	for (const model::Change& change : changeSet.changes)
	{
		// A renumbered area or line changes the addresses of everything below it.
		if (change.kind == model::Change::Modified && (change.ref.kind == EntityKind::Area || change.ref.kind == EntityKind::Line))
		{
			all = true;
		}
	}
}

} // namespace kestrel::viewmodels
