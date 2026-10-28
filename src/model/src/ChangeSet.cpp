#include "kestrel/model/ChangeSet.hpp"

#include <algorithm>
#include <map>

namespace kestrel::model
{

bool ChangeSet::contains(EntityRef ref) const
{
	return std::any_of(changes.begin(), changes.end(), [&](const Change& c) { return c.ref == ref; });
}

bool ChangeSet::touches(EntityKind kind) const
{
	return std::any_of(changes.begin(), changes.end(), [&](const Change& c) { return c.ref.kind == kind; });
}

ChangeSet ChangeSet::normalized(const std::vector<Change>& raw)
{
	// Coalesce per entity into slots; a slot keeps the position of the change that defines its final kind.
	struct Slot
	{
		Change change;
		bool dropped = false;
	};
	std::vector<Slot> slots;
	slots.reserve(raw.size());
	std::map<EntityRef, std::size_t> current; // entity -> live slot

	for (const Change& change : raw)
	{
		const auto it = current.find(change.ref);
		if (it == current.end())
		{
			current.emplace(change.ref, slots.size());
			slots.push_back({change});
			continue;
		}
		Slot& slot = slots[it->second];
		switch (change.kind)
		{
			case Change::Modified:
				// Added+Modified stays Added; Modified+Modified merges; Removed+Modified cannot happen.
				if (slot.change.kind == Change::Modified)
				{
					slot.change.structural = slot.change.structural || change.structural;
				}
				break;
			case Change::Removed:
				slot.dropped = true;
				if (slot.change.kind == Change::Added)
				{
					current.erase(it); // Added then Removed: nothing happened
				}
				else
				{
					it->second = slots.size();
					slots.push_back({change});
				}
				break;
			case Change::Added:
				if (slot.change.kind == Change::Removed)
				{
					// Removed then re-added (e.g. rolled back inside a nested edit): net effect is a modification.
					slot.dropped = true;
					it->second = slots.size();
					slots.push_back({Change{Change::Modified, change.ref, true}});
				}
				break;
		}
	}

	ChangeSet result;
	result.changes.reserve(slots.size());
	for (const Slot& slot : slots)
	{
		if (!slot.dropped)
		{
			result.changes.push_back(slot.change);
		}
	}
	return result;
}

std::ostream& operator<<(std::ostream& os, Change::Kind kind)
{
	switch (kind)
	{
		case Change::Added: return os << "Added";
		case Change::Removed: return os << "Removed";
		case Change::Modified: return os << "Modified";
	}
	return os << '?';
}

std::ostream& operator<<(std::ostream& os, const Change& change)
{
	return os << change.kind << ' ' << change.ref << (change.structural ? " (structural)" : "");
}

std::ostream& operator<<(std::ostream& os, const ChangeSet& changeSet)
{
	os << "ChangeSet{";
	if (changeSet.fromUndo)
		os << "fromUndo ";
	if (changeSet.stateOnly)
		os << "stateOnly ";
	os << '[';
	for (std::size_t i = 0; i < changeSet.changes.size(); ++i)
	{
		os << (i ? ", " : "") << changeSet.changes[i];
	}
	return os << "]}";
}

} // namespace kestrel::model
