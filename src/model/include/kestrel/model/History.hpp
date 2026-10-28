#pragma once

// The single undo/redo stack of a Project (ADR-003). Owned by Project; reach it via Project::history().

#include "kestrel/model/detail/Ops.hpp"

#include <QString>

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <ostream>
#include <vector>

namespace kestrel::model
{

class Project;

/// Consecutive edits with equal MergeKey within History::kMergeWindow fold into one undo step.
struct MergeKey
{
	QString key;

	friend bool operator==(const MergeKey&, const MergeKey&) = default;
};

std::ostream& operator<<(std::ostream& os, const MergeKey& key);

class History
{
public:
	using TimePoint = std::chrono::steady_clock::time_point;
	using Clock = std::function<TimePoint()>;

	static constexpr std::chrono::milliseconds kMergeWindow{1000};

	bool canUndo() const { return m_index > 0; }
	bool canRedo() const { return m_index < m_steps.size(); }
	/// Label of the step undo() would revert; empty if none.
	QString undoText() const;
	/// Label of the step redo() would re-apply; empty if none.
	QString redoText() const;

	/// Reverts the last step and emits a ChangeSet (fromUndo). False if there is nothing to undo or if called
	/// from inside a transaction.
	bool undo();
	/// Re-applies the next step and emits a ChangeSet (fromUndo). False if there is nothing to redo.
	bool redo();

	std::size_t count() const { return m_steps.size(); }
	/// Number of applied steps (0..count()).
	std::size_t index() const { return m_index; }

	/// Marks the current state as saved.
	void setClean();
	/// True if the state equals the saved one: same history position and no recordState() since.
	bool isClean() const;
	/// Drops all steps (e.g. after loading). Keeps the clean/dirty state.
	void clear();

	/// Injects the clock used for merging (tests). Default: std::chrono::steady_clock::now.
	void setClock(Clock clock);

private:
	friend class Project;

	struct Step
	{
		QString label;
		std::vector<detail::Op> ops;
		std::optional<MergeKey> merge;
		TimePoint time;
	};

	void push(QString label, std::vector<detail::Op> ops, std::optional<MergeKey> merge);
	void markStateDirty() { m_stateDirty = true; }

	Project* m_project = nullptr;
	std::vector<Step> m_steps;
	std::size_t m_index = 0;
	std::optional<std::size_t> m_cleanIndex = 0; ///< nullopt: the saved state is unreachable
	bool m_stateDirty = false;
	Clock m_clock;
};

} // namespace kestrel::model
