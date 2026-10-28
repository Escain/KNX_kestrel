#pragma once

// Base classes of the panel view models. A panel VM gives QML its services (session, selection, actions),
// exposes Q_INVOKABLE intents and reports failures through `lastError` (the shell also shows them in the
// status bar via ProjectSession::errorReported).

#include "kestrel/base/Result.hpp"
#include "kestrel/model/Id.hpp"
#include "kestrel/model/Project.hpp"
#include "kestrel/viewmodels/ActionRegistry.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"
#include "kestrel/viewmodels/SelectionService.hpp"

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace kestrel::viewmodels
{

class ShadowTreeModel;

/// The application services every view model works with.
struct Services
{
	ProjectSession* session = nullptr;
	SelectionService* selection = nullptr;
	ActionRegistry* actions = nullptr;
};

class PanelVM : public QObject
{
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("View models are created by the application")
	Q_PROPERTY(kestrel::viewmodels::ProjectSession* session READ session CONSTANT)
	Q_PROPERTY(kestrel::viewmodels::SelectionService* selection READ selection CONSTANT)
	Q_PROPERTY(kestrel::viewmodels::ActionRegistry* actions READ actions CONSTANT)
	Q_PROPERTY(QString origin READ origin CONSTANT)
	Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
	PanelVM(const Services& services, const QString& origin, QObject* parent = nullptr);

	ProjectSession* session() const { return m_services.session; }
	SelectionService* selection() const { return m_services.selection; }
	ActionRegistry* actions() const { return m_services.actions; }
	/// Selection origin used by this view ("buildings", "topology", …).
	QString origin() const { return m_origin; }
	QString lastError() const { return m_lastError; }

	Q_INVOKABLE void clearError();
	/// Generic intents shared by every panel.
	Q_INVOKABLE bool rename(const QString& ref, const QString& name);
	Q_INVOKABLE bool remove(const QString& ref);
	Q_INVOKABLE void select(const QString& ref);

Q_SIGNALS:
	void lastErrorChanged();

protected:
	model::Project& project();
	const model::Project& project() const;
	/// Sets lastError from a failed status. Returns status.has_value().
	bool check(const Status& status);
	template <class T>
	std::optional<T> check(Result<T> result)
	{
		if (!result)
		{
			setError(result.error());
			return std::nullopt;
		}
		clearError();
		return std::move(result).value();
	}
	void setError(const Error& error);
	void setError(const QString& message);
	std::optional<model::EntityRef> parseRef(const QString& ref);

private:
	Services m_services;
	QString m_origin;
	QString m_lastError;
};

/// A panel showing one ShadowTreeModel in a TreeView, synchronised with the SelectionService.
class TreePanelVM : public PanelVM
{
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("View models are created by the application")
	Q_PROPERTY(QAbstractItemModel* model READ model CONSTANT)

public:
	TreePanelVM(const Services& services, const QString& origin, QObject* parent = nullptr);

	QAbstractItemModel* model() const;
	ShadowTreeModel* treeModel() const { return m_model; }

	/// The view's current row changed: publishes it to the SelectionService.
	Q_INVOKABLE void selectIndex(const QModelIndex& index);
	/// Double-click / Enter.
	Q_INVOKABLE void activateIndex(const QModelIndex& index);
	/// Right-click: selects the row (or clears the selection on empty space) and requests a native menu.
	Q_INVOKABLE void contextMenuAt(const QModelIndex& index);
	Q_INVOKABLE QModelIndex indexOfRef(const QString& ref) const;
	Q_INVOKABLE QString refAt(const QModelIndex& index) const;
	/// Context-menu action ids for an entity (the root ref for empty space).
	virtual QStringList contextActions(model::EntityRef ref) const = 0;

Q_SIGNALS:
	/// Another view selected an entity shown here: the view should expand to, scroll to and select it.
	void revealRequested(const QModelIndex& index);
	/// The view should start inline editing of this row.
	void editRequested(const QModelIndex& index);

protected:
	/// Takes ownership. Call once from the subclass constructor.
	void setModel(ShadowTreeModel* model);

private:
	void onSelectionChanged();

	ShadowTreeModel* m_model = nullptr;
};

} // namespace kestrel::viewmodels
