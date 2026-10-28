#pragma once

// String id -> QAction (QAction lives in QtGui in Qt 6). Menus, tool bars, native context menus and QML
// buttons all use the same actions, so their text and enablement (computed in C++) can never disagree.
// The QAction objectName is its id ("edit.undo"). QML uses trigger()/isEnabled()/text() and re-evaluates its
// bindings on `revision`.

#include <QAction>
#include <QKeySequence>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace kestrel::viewmodels
{

class ActionRegistry : public QObject
{
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("ActionRegistry is owned by the application")
	Q_PROPERTY(int revision READ revision NOTIFY actionsChanged)

public:
	/// Separator marker in context-menu id lists.
	static inline const QString kSeparator = QStringLiteral("-");

	explicit ActionRegistry(QObject* parent = nullptr);

	/// Creates (or returns the existing) action `id`. The registry owns it.
	QAction* add(const QString& id, const QString& text, const QKeySequence& shortcut = {});
	/// nullptr if unknown.
	QAction* action(const QString& id) const;
	QStringList ids() const { return m_actions.keys(); }
	void setEnabled(const QString& id, bool enabled);
	int revision() const { return m_revision; }

	/// Triggers the action if it exists and is enabled. Returns whether it ran.
	Q_INVOKABLE bool trigger(const QString& id);
	Q_INVOKABLE bool isEnabled(const QString& id) const;
	Q_INVOKABLE QString text(const QString& id) const;
	/// Asks the shell to show a native context menu with these actions at the cursor ("-" = separator).
	Q_INVOKABLE void requestContextMenu(const QStringList& ids);

Q_SIGNALS:
	void actionsChanged();
	void contextMenuRequested(const QStringList& ids);

private:
	QMap<QString, QAction*> m_actions;
	int m_revision = 0;
};

} // namespace kestrel::viewmodels
