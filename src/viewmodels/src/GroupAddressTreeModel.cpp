#include "kestrel/viewmodels/GroupAddressTreeModel.hpp"

#include "kestrel/viewmodels/Format.hpp"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

using model::EntityKind;
using model::EntityRef;

GroupAddressTreeModel::GroupAddressTreeModel(ProjectSession* session, QObject* parent)
	: ShadowTreeModel(session, {tr("Name"), tr("Address"), tr("DPT")}, parent)
{
	rebuild();
}

bool GroupAddressTreeModel::isShown(EntityKind kind) const
{
	return kind == EntityKind::GroupRange || kind == EntityKind::GroupAddress;
}

std::vector<EntityRef> GroupAddressTreeModel::childrenOf(EntityRef parent) const
{
	std::optional<model::Id<model::GroupRange>> range;
	if (parent.kind == EntityKind::GroupRange)
	{
		range = parent.as<model::GroupRange>();
	}
	else if (parent.kind != EntityKind::Project)
	{
		return {};
	}
	const model::Project& p = project();
	auto ranges = p.groupRangesIn(range);
	std::stable_sort(ranges.begin(), ranges.end(),
		[&](auto a, auto b) { return p.groupRanges().at(a).start < p.groupRanges().at(b).start; });
	auto addresses = p.groupAddressesIn(range);
	std::stable_sort(addresses.begin(), addresses.end(),
		[&](auto a, auto b) { return p.groupAddresses().at(a).raw < p.groupAddresses().at(b).raw; });
	std::vector<EntityRef> result;
	result.reserve(ranges.size() + addresses.size());
	for (const auto id : ranges)
		result.emplace_back(id);
	for (const auto id : addresses)
		result.emplace_back(id);
	return result;
}

std::optional<EntityRef> GroupAddressTreeModel::parentOf(EntityRef ref) const
{
	if (ref.kind == EntityKind::GroupRange)
	{
		const auto& range = project().groupRanges().at(ref.as<model::GroupRange>());
		return range.parent ? EntityRef(*range.parent) : EntityRef::project();
	}
	if (ref.kind == EntityKind::GroupAddress)
	{
		const auto& address = project().groupAddresses().at(ref.as<model::GroupAddress>());
		return address.range ? EntityRef(*address.range) : EntityRef::project();
	}
	return std::nullopt;
}

QVariant GroupAddressTreeModel::cellData(EntityRef ref, int column, int role) const
{
	const model::Project& p = project();
	const bool isAddress = ref.kind == EntityKind::GroupAddress;
	auto address = [&]() -> QString
	{
		return isAddress ? groupAddressText(p, p.groupAddresses().at(ref.as<model::GroupAddress>()).raw)
						 : groupRangeText(p, p.groupRanges().at(ref.as<model::GroupRange>()));
	};
	auto dpt = [&]() -> QString { return isAddress ? p.groupAddresses().at(ref.as<model::GroupAddress>()).dpt : QString(); };
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
				case 2: return dpt();
				default: return {};
			}
		case Qt::EditRole: return column == 0 ? entityName(p, ref) : QVariant();
		case Qt::ToolTipRole: return entityLabel(p, ref);
		case AddressRole: return address();
		case DptRole: return dpt();
		case DescriptionRole:
			return isAddress ? p.groupAddresses().at(ref.as<model::GroupAddress>()).description : QString();
		case IconRole: return isAddress ? u"groupaddress"_s : u"grouprange"_s;
		default: return {};
	}
}

void GroupAddressTreeModel::collectRelated(const model::ChangeSet& changeSet, std::set<EntityRef>& refresh, bool& all) const
{
	(void)refresh;
	for (const model::Change& change : changeSet.changes)
	{
		if (change.ref.kind == EntityKind::Project && change.kind == model::Change::Modified)
		{
			all = true; // gaStyle changes every address text
		}
	}
}

} // namespace kestrel::viewmodels
