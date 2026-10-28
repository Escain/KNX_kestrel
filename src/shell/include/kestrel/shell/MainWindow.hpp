#pragma once

// The Kestrel main window (specs/02-architecture.md §3.3, ADR-006): menus, tool bar, status bar, docks
// hosting QML views, and the central DocumentArea.
//
// Docks (stable objectNames, saved with saveState(kLayoutVersion)):
//   dock.buildings, dock.topology, dock.groupAddresses   left, tabbed
//   dock.properties                                      right
//   dock.problems                                        bottom
//   dock.catalog, dock.commissioning, dock.busConnection bottom, tabbed with problems
// Central tabs: Welcome, device editors, the group monitor.
// Bump kLayoutVersion whenever docks are added or renamed.
//
// Teardown: QML hosts are deleted before the view models (Workspace), which go before the QML engine.

#include "kestrel/model/Entities.hpp"
#include "kestrel/model/Id.hpp"

#include <QList>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QStringList>

#include <memory>

class QLabel;

class QQmlEngine;
class QToolButton;

namespace kestrel::viewmodels
{
class Workspace;
}

namespace kestrel::shell
{

class DocumentArea;
class QmlDock;

struct MainWindowOptions
{
	/// Restore the layout from QSettings at start and save it on close.
	bool persistLayout = true;
	/// Ask before discarding unsaved changes (tests turn this off).
	bool confirmDiscard = true;
};

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	static constexpr int kLayoutVersion = 3;

	explicit MainWindow(MainWindowOptions options = {}, QWidget* parent = nullptr);
	~MainWindow() override;

	viewmodels::Workspace* workspace() const { return m_workspace.get(); }
	QQmlEngine* engine() const { return m_engine.get(); }
	DocumentArea* documents() const { return m_documents; }
	/// Dock by objectName ("dock.topology"), nullptr if unknown.
	QmlDock* dock(const QString& id) const;
	QList<QmlDock*> docks() const { return m_docks; }
	/// QML warnings and errors reported by the engine since start.
	QStringList qmlWarnings() const { return m_qmlWarnings; }
	/// The most recent native context menu (tests).
	QMenu* lastContextMenu() const { return m_lastContextMenu; }

	/// File operations; each asks to save unsaved changes first (unless disabled by the options).
	bool newProject();
	bool openProject(const QString& dir);
	bool saveProject();
	bool saveProjectAs(const QString& dir);
	void openDevice(model::Id<model::Device> device);
	void openGroupMonitor();
	/// Puts every dock back to its default place.
	void resetLayout();
	/// Shows and raises a dock.
	void showDock(const QString& id);
	/// Asks for .knxprod files and imports them into the catalog.
	void chooseAndImportProducts();

protected:
	void closeEvent(QCloseEvent* event) override;

private:
	void createActions();
	void createMenus();
	void createDocks();
	void createStatusBar();
	QmlDock* addQmlDock(const QString& id, const QString& title, const QString& type, QObject* viewModel);
	void showContextMenu(const QStringList& ids);
	bool maybeSave();
	void chooseAndOpen();
	void chooseAndSaveAs();
	void updateTitle();
	void updateStatus();
	void restoreLayout();
	void saveLayout();

	MainWindowOptions m_options;
	std::unique_ptr<QQmlEngine> m_engine;
	std::unique_ptr<viewmodels::Workspace> m_workspace;
	DocumentArea* m_documents = nullptr;
	QList<QmlDock*> m_docks;
	QMenu* m_fileMenu = nullptr;
	QMenu* m_editMenu = nullptr;
	QMenu* m_viewMenu = nullptr;
	QMenu* m_projectMenu = nullptr;
	QMenu* m_busMenu = nullptr;
	QMenu* m_commissioningMenu = nullptr;
	QMenu* m_helpMenu = nullptr;
	QLabel* m_dirtyLabel = nullptr;
	QToolButton* m_problemsButton = nullptr;
	QPointer<QMenu> m_lastContextMenu;
	QStringList m_qmlWarnings;
};

} // namespace kestrel::shell
