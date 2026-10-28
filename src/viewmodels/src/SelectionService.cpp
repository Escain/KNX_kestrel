#include "kestrel/viewmodels/SelectionService.hpp"

namespace kestrel::viewmodels
{

SelectionService::SelectionService(QObject* parent) : QObject(parent) {}

QString SelectionService::currentText() const
{
	return m_current ? m_current->toString() : QString();
}

QStringList SelectionService::selectionText() const
{
	QStringList result;
	result.reserve(m_selection.size());
	for (const model::EntityRef& ref : m_selection)
	{
		result.append(ref.toString());
	}
	return result;
}

void SelectionService::setSelection(
	const QList<model::EntityRef>& selection, std::optional<model::EntityRef> current, const QString& origin)
{
	if (selection == m_selection && current == m_current)
	{
		return;
	}
	m_selection = selection;
	m_current = current;
	m_origin = origin;
	Q_EMIT selectionChanged();
}

void SelectionService::select(model::EntityRef ref, const QString& origin)
{
	setSelection({ref}, ref, origin);
}

void SelectionService::activate(model::EntityRef ref, const QString& origin)
{
	select(ref, origin);
	Q_EMIT activated(ref, origin);
}

void SelectionService::prune(const model::ChangeSet& changeSet)
{
	QList<model::EntityRef> kept;
	bool removed = false;
	for (const model::EntityRef& ref : m_selection)
	{
		bool gone = false;
		for (const model::Change& change : changeSet.changes)
		{
			if (change.kind == model::Change::Removed && change.ref == ref)
			{
				gone = true;
				break;
			}
		}
		removed = removed || gone;
		if (!gone)
		{
			kept.append(ref);
		}
	}
	if (!removed)
	{
		return;
	}
	std::optional<model::EntityRef> current = m_current;
	if (current && !kept.contains(*current))
	{
		current = kept.isEmpty() ? std::nullopt : std::optional<model::EntityRef>(kept.front());
	}
	setSelection(kept, current, m_origin);
}

void SelectionService::selectRef(const QString& ref, const QString& origin)
{
	if (const auto parsed = model::EntityRef::parse(ref))
	{
		select(*parsed, origin);
	}
	else
	{
		setSelection({}, std::nullopt, origin);
	}
}

void SelectionService::activateRef(const QString& ref, const QString& origin)
{
	if (const auto parsed = model::EntityRef::parse(ref))
	{
		activate(*parsed, origin);
	}
}

void SelectionService::clear()
{
	setSelection({}, std::nullopt, {});
}

void SelectionService::requestRename()
{
	if (m_current)
	{
		Q_EMIT renameRequested(m_current->toString());
	}
}

} // namespace kestrel::viewmodels
