#include "kestrel/viewmodels/ActionRegistry.hpp"

namespace kestrel::viewmodels
{

ActionRegistry::ActionRegistry(QObject* parent) : QObject(parent) {}

QAction* ActionRegistry::add(const QString& id, const QString& text, const QKeySequence& shortcut)
{
	if (QAction* existing = action(id))
	{
		return existing;
	}
	auto* created = new QAction(text, this);
	created->setObjectName(id);
	if (!shortcut.isEmpty())
	{
		created->setShortcut(shortcut);
	}
	m_actions.insert(id, created);
	connect(created, &QAction::changed, this,
		[this]
		{
			++m_revision;
			Q_EMIT actionsChanged();
		});
	++m_revision;
	Q_EMIT actionsChanged();
	return created;
}

QAction* ActionRegistry::action(const QString& id) const
{
	return m_actions.value(id, nullptr);
}

void ActionRegistry::setEnabled(const QString& id, bool enabled)
{
	if (QAction* found = action(id))
	{
		found->setEnabled(enabled);
	}
}

bool ActionRegistry::trigger(const QString& id)
{
	QAction* found = action(id);
	if (!found || !found->isEnabled())
	{
		return false;
	}
	found->trigger();
	return true;
}

bool ActionRegistry::isEnabled(const QString& id) const
{
	const QAction* found = action(id);
	return found && found->isEnabled();
}

QString ActionRegistry::text(const QString& id) const
{
	const QAction* found = action(id);
	// iconText(): no '&' mnemonic and no trailing ellipsis
	return found ? found->iconText() : QString();
}

void ActionRegistry::requestContextMenu(const QStringList& ids)
{
	Q_EMIT contextMenuRequested(ids);
}

} // namespace kestrel::viewmodels
