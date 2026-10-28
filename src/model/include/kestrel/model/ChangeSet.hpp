#pragma once

// One ordered ChangeSet per commit / undo / redo / recordState (specs/03-domain-model.md §3).

#include "kestrel/model/Id.hpp"

#include <ostream>
#include <vector>

namespace kestrel::model
{

struct Change
{
	enum Kind
	{
		Added,
		Removed,
		Modified,
	};

	Kind kind = Modified;
	EntityRef ref;
	bool structural = false; ///< parent/owner changed (a move)

	friend bool operator==(const Change&, const Change&) = default;
};

/// Changes in application order. Within one ChangeSet each entity appears at most once:
/// Added+Modified -> Added, Modified+Removed -> Removed, Added+Removed -> nothing.
/// Removals of a cascade are ordered children-first; additions of an undone cascade parents-first.
struct ChangeSet
{
	std::vector<Change> changes;
	bool fromUndo = false;  ///< produced by History::undo() or History::redo()
	bool stateOnly = false; ///< produced by Project::recordState() (not undoable)

	bool empty() const { return changes.empty(); }
	bool contains(EntityRef ref) const;
	/// True if any change concerns an entity of this kind.
	bool touches(EntityKind kind) const;

	/// Builds a ChangeSet from raw changes, coalescing repeated entries per entity (see above).
	static ChangeSet normalized(const std::vector<Change>& raw);

	friend bool operator==(const ChangeSet&, const ChangeSet&) = default;
};

std::ostream& operator<<(std::ostream& os, Change::Kind kind);
std::ostream& operator<<(std::ostream& os, const Change& change);
std::ostream& operator<<(std::ostream& os, const ChangeSet& changeSet);

} // namespace kestrel::model
