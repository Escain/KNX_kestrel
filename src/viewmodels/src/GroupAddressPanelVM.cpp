#include "kestrel/viewmodels/GroupAddressPanelVM.hpp"

#include "kestrel/dpt/Dpt.hpp"
#include "kestrel/knx/Address.hpp"
#include "kestrel/model/Operations.hpp"
#include "kestrel/viewmodels/ActionRegistry.hpp"
#include "kestrel/viewmodels/Format.hpp"
#include "kestrel/viewmodels/GroupAddressTreeModel.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"

#include <set>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

std::optional<QString> normalizeDpt(const QString& text)
{
	const QString trimmed = text.trimmed();
	if (trimmed.isEmpty())
	{
		return QString();
	}
	const auto id = dpt::DptId::parse(trimmed.toStdString());
	if (!id)
	{
		return std::nullopt;
	}
	return QString::fromStdString(id->toEtsString());
}

GroupAddressPanelVM::GroupAddressPanelVM(const Services& services, QObject* parent)
	: TreePanelVM(services, u"groupAddresses"_s, parent)
{
	setModel(new GroupAddressTreeModel(services.session));
}

GroupAddressTreeModel* GroupAddressPanelVM::groupAddressModel() const
{
	return static_cast<GroupAddressTreeModel*>(treeModel());
}

std::optional<model::Id<model::GroupRange>> GroupAddressPanelVM::rangeFor(model::EntityRef ref) const
{
	if (!project().contains(ref))
		return std::nullopt;
	if (ref.is<model::GroupRange>())
		return ref.as<model::GroupRange>();
	if (ref.is<model::GroupAddress>())
		return project().groupAddresses().at(ref.as<model::GroupAddress>()).range;
	return std::nullopt;
}

std::optional<model::Id<model::GroupRange>> GroupAddressPanelVM::innermostRange(std::uint16_t raw) const
{
	return innermostGroupRange(project(), raw);
}

std::optional<model::Id<model::GroupRange>> innermostGroupRange(const model::Project& project, std::uint16_t raw)
{
	std::optional<model::Id<model::GroupRange>> found;
	std::optional<model::Id<model::GroupRange>> parent;
	for (bool descended = true; descended;)
	{
		descended = false;
		for (const auto id : project.groupRangesIn(parent))
		{
			if (project.groupRanges().at(id).contains(raw))
			{
				found = id;
				parent = id;
				descended = true;
				break;
			}
		}
	}
	return found;
}

QString GroupAddressPanelVM::addMainGroup(const QString& name)
{
	const bool free = project().settings().gaStyle == knx::GroupAddressStyle::Free;
	const unsigned span = free ? 0x100 : 0x800;
	const unsigned count = free ? 256 : 32;
	for (unsigned block = 0; block < count; ++block)
	{
		const auto start = static_cast<std::uint16_t>(block * span);
		const auto end = static_cast<std::uint16_t>(start + span - 1);
		bool overlaps = false;
		for (const auto id : project().groupRangesIn(std::nullopt))
		{
			const model::GroupRange& range = project().groupRanges().at(id);
			overlaps = overlaps || !(range.end < start || range.start > end);
		}
		if (overlaps)
			continue;
		const QString text = name.trimmed().isEmpty() ? tr("Main group %1").arg(block) : name.trimmed();
		const auto id = check(model::addGroupRange(project(), std::nullopt, text, start, end));
		return id ? id->toString() : QString();
	}
	setError(tr("All main groups are in use."));
	return {};
}

QString GroupAddressPanelVM::addMiddleGroup(const QString& parentRef, const QString& name)
{
	const auto parsed = parseRef(parentRef);
	if (!parsed)
		return {};
	const auto parentId = rangeFor(*parsed);
	if (project().settings().gaStyle != knx::GroupAddressStyle::ThreeLevel || !parentId
		|| project().groupRanges().at(*parentId).parent)
	{
		setError(tr("Middle groups exist only below main groups in the 3-level style."));
		return {};
	}
	const model::GroupRange& parent = project().groupRanges().at(*parentId);
	for (unsigned middle = 0; middle < 8; ++middle)
	{
		const auto start = static_cast<std::uint16_t>(parent.start + middle * 0x100);
		const auto end = static_cast<std::uint16_t>(start + 0xFF);
		if (end > parent.end)
			break;
		bool overlaps = false;
		for (const auto id : project().groupRangesIn(*parentId))
		{
			const model::GroupRange& range = project().groupRanges().at(id);
			overlaps = overlaps || !(range.end < start || range.start > end);
		}
		if (overlaps)
			continue;
		const QString text = name.trimmed().isEmpty() ? tr("Middle group %1").arg(middle) : name.trimmed();
		const auto id = check(model::addGroupRange(project(), *parentId, text, start, end));
		return id ? id->toString() : QString();
	}
	setError(tr("All middle groups of this main group are in use."));
	return {};
}

QString GroupAddressPanelVM::addGroupAddress(const QString& ref, const QString& name)
{
	std::optional<model::Id<model::GroupRange>> range;
	if (!ref.isEmpty())
	{
		const auto parsed = parseRef(ref);
		if (!parsed)
			return {};
		range = rangeFor(*parsed);
	}
	std::set<std::uint16_t> used;
	for (const model::GroupAddress& address : project().groupAddresses())
		used.insert(address.raw);
	unsigned first = 1;
	unsigned last = 0xFFFF;
	if (range)
	{
		const model::GroupRange& r = project().groupRanges().at(*range);
		first = r.start == 0 ? 1u : r.start;
		last = r.end;
	}
	for (unsigned raw = first; raw <= last; ++raw)
	{
		const auto value = static_cast<std::uint16_t>(raw);
		// Outside any range, skip addresses that belong to a range so the tree stays tidy.
		if (used.contains(value) || (!range && innermostRange(value)))
			continue;
		// Inside a range, prefer addresses not claimed by a sub-range.
		if (range && innermostRange(value) != range)
			continue;
		const QString text = name.trimmed().isEmpty() ? tr("New group address") : name.trimmed();
		const auto id = check(model::addGroupAddress(project(), knx::GroupAddress(value), text, {}, range));
		return id ? id->toString() : QString();
	}
	setError(tr("No free group address in this range."));
	return {};
}

QString GroupAddressPanelVM::addGroupAddressAt(const QString& address, const QString& name, const QString& dpt)
{
	const auto raw = parseGroupAddress(address);
	if (!raw)
	{
		setError(tr("Not a group address: %1").arg(address));
		return {};
	}
	const auto normalized = normalizeDpt(dpt);
	if (!normalized)
	{
		setError(tr("Not a datapoint type: %1").arg(dpt));
		return {};
	}
	const auto id = check(model::addGroupAddress(project(), knx::GroupAddress(*raw), name.trimmed(), *normalized, innermostRange(*raw)));
	return id ? id->toString() : QString();
}

bool GroupAddressPanelVM::setDpt(const QString& groupAddressRef, const QString& dpt)
{
	const auto parsed = parseRef(groupAddressRef);
	if (!parsed)
		return false;
	if (!parsed->is<model::GroupAddress>())
	{
		setError(tr("Not a group address: %1").arg(groupAddressRef));
		return false;
	}
	const auto normalized = normalizeDpt(dpt);
	if (!normalized)
	{
		setError(tr("Not a datapoint type: %1").arg(dpt));
		return false;
	}
	return check(model::setGroupAddressDpt(project(), parsed->as<model::GroupAddress>(), *normalized));
}

QStringList GroupAddressPanelVM::contextActions(model::EntityRef ref) const
{
	const QString sep = ActionRegistry::kSeparator;
	switch (ref.kind)
	{
		case model::EntityKind::GroupRange:
			return {u"ga.addMiddleGroup"_s, u"ga.addAddress"_s, sep, u"edit.rename"_s, u"edit.delete"_s};
		case model::EntityKind::GroupAddress:
			return {u"ga.addAddress"_s, sep, u"edit.rename"_s, u"edit.delete"_s};
		default: return {u"ga.addMainGroup"_s, u"ga.addAddress"_s};
	}
}

bool GroupAddressPanelVM::canDrag(const QModelIndex& index) const
{
	return treeModel()->refAt(index).kind == model::EntityKind::GroupAddress;
}

} // namespace kestrel::viewmodels
