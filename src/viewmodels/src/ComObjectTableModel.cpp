#include "kestrel/viewmodels/ComObjectTableModel.hpp"

#include "kestrel/viewmodels/Format.hpp"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

using model::EntityKind;
using model::EntityRef;

ComObjectTableModel::ComObjectTableModel(ProjectSession* session, model::Id<model::Device> device, QObject* parent)
	: ShadowTreeModel(session,
		  {tr("#"), tr("Name"), tr("Function"), tr("Size"), tr("Flags"), tr("DPT"), tr("Group addresses")}, parent)
	, m_device(device)
{
	rebuild();
}

bool ComObjectTableModel::isShown(EntityKind kind) const
{
	return kind == EntityKind::ComObject;
}

std::vector<EntityRef> ComObjectTableModel::childrenOf(EntityRef parent) const
{
	if (parent != EntityRef(m_device))
	{
		return {};
	}
	const model::Project& p = project();
	auto objects = p.comObjectsOf(m_device);
	std::stable_sort(objects.begin(), objects.end(),
		[&](auto a, auto b) { return p.comObjects().at(a).number < p.comObjects().at(b).number; });
	return {objects.begin(), objects.end()};
}

std::optional<EntityRef> ComObjectTableModel::parentOf(EntityRef ref) const
{
	if (ref.kind == EntityKind::ComObject && project().comObjects().at(ref.as<model::ComObject>()).device == m_device)
	{
		return EntityRef(m_device);
	}
	return std::nullopt;
}

QString ComObjectTableModel::groupAddressesText(model::Id<model::ComObject> object) const
{
	const model::Project& p = project();
	auto links = p.linksOfComObject(object);
	std::stable_partition(links.begin(), links.end(), [&](auto id) { return p.links().at(id).sending; });
	QStringList texts;
	for (const auto id : links)
	{
		texts.append(groupAddressText(p, p.groupAddresses().at(p.links().at(id).groupAddress).raw));
	}
	return texts.join(u", "_s);
}

QVariant ComObjectTableModel::cellData(EntityRef ref, int column, int role) const
{
	const model::ComObject& object = project().comObjects().at(ref.as<model::ComObject>());
	switch (role)
	{
		case Qt::DisplayRole:
			switch (column)
			{
				case NumberColumn: return QString::number(object.number);
				case NameColumn: return object.name;
				case FunctionColumn: return object.function;
				case SizeColumn: return sizeText(object.sizeInBit);
				case FlagsColumn: return object.flags.toString();
				case DptColumn: return object.dpt;
				case GroupAddressesColumn: return groupAddressesText(object.id);
				default: return {};
			}
		case Qt::EditRole: return column == NameColumn ? object.name : QVariant();
		case Qt::ToolTipRole: return entityLabel(project(), ref);
		case DptRole: return object.dpt;
		case AddressRole: return groupAddressesText(object.id);
		case IconRole: return u"comobject"_s;
		default: return {};
	}
}

void ComObjectTableModel::collectRelated(const model::ChangeSet& changeSet, std::set<EntityRef>& refresh, bool& all) const
{
	(void)refresh;
	for (const model::Change& change : changeSet.changes)
	{
		// Few rows per device: refreshing them all is cheaper than tracking removed links.
		if (change.ref.kind == EntityKind::Link || change.ref.kind == EntityKind::Project
			|| (change.ref.kind == EntityKind::GroupAddress && change.kind == model::Change::Modified))
		{
			all = true;
			return;
		}
	}
}

bool ComObjectTableModel::isEditable(EntityRef, int column) const
{
	return column == NameColumn;
}

} // namespace kestrel::viewmodels
