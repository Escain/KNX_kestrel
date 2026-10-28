#pragma once

// Cross-dock selection (specs/brainstorm/round1-ui.md §2). Docks have different item models, so selection is
// shared as entity refs: a list, the current ref and the origin (name of the view that set it). Views react
// only to selections whose origin is not themselves, which avoids feedback loops.
// QML sees refs as their text form ("dev-7f3k2m9qv1abc", "project").

#include "kestrel/model/ChangeSet.hpp"
#include "kestrel/model/Id.hpp"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace kestrel::viewmodels
{

class SelectionService : public QObject
{
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("SelectionService is owned by the application")
	Q_PROPERTY(QString current READ currentText NOTIFY selectionChanged)
	Q_PROPERTY(QStringList selection READ selectionText NOTIFY selectionChanged)
	Q_PROPERTY(QString origin READ origin NOTIFY selectionChanged)

public:
	explicit SelectionService(QObject* parent = nullptr);

	const QList<model::EntityRef>& selection() const { return m_selection; }
	std::optional<model::EntityRef> current() const { return m_current; }
	QString origin() const { return m_origin; }
	QString currentText() const;
	QStringList selectionText() const;

	/// Replaces the selection. No signal if nothing changes (the origin alone does not count).
	void setSelection(const QList<model::EntityRef>& selection, std::optional<model::EntityRef> current,
		const QString& origin);
	void select(model::EntityRef ref, const QString& origin);
	/// Double-click / Enter on an entity (opens editors).
	void activate(model::EntityRef ref, const QString& origin);
	/// Drops refs whose entity was removed.
	void prune(const model::ChangeSet& changeSet);

	Q_INVOKABLE void selectRef(const QString& ref, const QString& origin);
	Q_INVOKABLE void activateRef(const QString& ref, const QString& origin);
	Q_INVOKABLE void clear();
	/// Asks the view showing the current entity to start inline renaming.
	Q_INVOKABLE void requestRename();

Q_SIGNALS:
	void selectionChanged();
	void activated(kestrel::model::EntityRef ref, const QString& origin);
	void renameRequested(const QString& ref);

private:
	QList<model::EntityRef> m_selection;
	std::optional<model::EntityRef> m_current;
	QString m_origin;
};

} // namespace kestrel::viewmodels
