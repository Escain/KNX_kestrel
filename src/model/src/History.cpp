#include "kestrel/model/History.hpp"

#include "kestrel/model/Project.hpp"

namespace kestrel::model
{

std::ostream& operator<<(std::ostream& os, const MergeKey& key)
{
	return os << "MergeKey{" << key.key.toStdString() << '}';
}

QString History::undoText() const
{
	return canUndo() ? m_steps[m_index - 1].label : QString();
}

QString History::redoText() const
{
	return canRedo() ? m_steps[m_index].label : QString();
}

bool History::undo()
{
	if (!canUndo() || m_project->m_tx)
	{
		return false;
	}
	--m_index;
	m_project->replay(m_steps[m_index].ops, false);
	return true;
}

bool History::redo()
{
	if (!canRedo() || m_project->m_tx)
	{
		return false;
	}
	// Advance before replaying: replay() notifies observers, which must see the post-redo
	// canUndo()/isClean() state (undo() decrements first for the same reason).
	++m_index;
	m_project->replay(m_steps[m_index - 1].ops, true);
	return true;
}

void History::setClean()
{
	m_cleanIndex = m_index;
	m_stateDirty = false;
}

bool History::isClean() const
{
	return m_cleanIndex == m_index && !m_stateDirty;
}

void History::clear()
{
	const bool clean = m_cleanIndex == m_index;
	m_steps.clear();
	m_index = 0;
	m_cleanIndex = clean ? std::optional<std::size_t>(0) : std::nullopt;
}

void History::setClock(Clock clock)
{
	m_clock = std::move(clock);
}

void History::push(QString label, std::vector<detail::Op> ops, std::optional<MergeKey> merge)
{
	const TimePoint now = m_clock ? m_clock() : std::chrono::steady_clock::now();

	// A new step discards the redo tail.
	if (m_index < m_steps.size())
	{
		m_steps.resize(m_index);
		if (m_cleanIndex && *m_cleanIndex > m_index)
		{
			m_cleanIndex.reset();
		}
	}

	if (merge && m_index > 0 && m_cleanIndex != m_index)
	{
		Step& top = m_steps[m_index - 1];
		if (top.merge == merge && now >= top.time && now - top.time <= kMergeWindow)
		{
			top.ops.insert(top.ops.end(), std::make_move_iterator(ops.begin()), std::make_move_iterator(ops.end()));
			top.time = now;
			return;
		}
	}

	m_steps.push_back(Step{std::move(label), std::move(ops), std::move(merge), now});
	++m_index;
}

} // namespace kestrel::model
