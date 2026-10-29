#include "kestrel/shell/MainWindow.hpp"

#include "kestrel/shell/DocumentArea.hpp"
#include "kestrel/shell/QmlDock.hpp"
#include "kestrel/shell/QmlHost.hpp"
#include "kestrel/shell/Style.hpp"
#include "kestrel/viewmodels/ActionRegistry.hpp"
#include "kestrel/viewmodels/BuildingsPanelVM.hpp"
#include "kestrel/viewmodels/BusConnectionVM.hpp"
#include "kestrel/viewmodels/CatalogPanelVM.hpp"
#include "kestrel/viewmodels/CommissioningVM.hpp"
#include "kestrel/viewmodels/GroupAddressPanelVM.hpp"
#include "kestrel/viewmodels/ProblemsPanelVM.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"
#include "kestrel/viewmodels/PropertiesPanelVM.hpp"
#include "kestrel/viewmodels/SelectionService.hpp"
#include "kestrel/viewmodels/TopologyPanelVM.hpp"
#include "kestrel/viewmodels/WelcomeVM.hpp"
#include "kestrel/viewmodels/Workspace.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLoggingCategory>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QQmlEngine>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcShell, "kestrel.shell")

namespace kestrel::shell
{

namespace
{
const QString kGeometryKey = u"mainWindow/geometry"_s;
const QString kStateKey = u"mainWindow/state"_s;
const QString kImportDirKey = u"catalog/importDir"_s;
} // namespace

MainWindow::MainWindow(MainWindowOptions options, QWidget* parent)
	: QMainWindow(parent)
	, m_options(options)
	, m_engine(std::make_unique<QQmlEngine>())
	, m_workspace(std::make_unique<viewmodels::Workspace>())
{
	setObjectName(u"mainWindow"_s);
	setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);
	setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
	connect(m_engine.get(), &QQmlEngine::warnings, this,
		[this](const QList<QQmlError>& warnings)
		{
			for (const QQmlError& warning : warnings)
				m_qmlWarnings.append(warning.toString());
		});

	m_documents = new DocumentArea(m_engine.get(), m_workspace.get(), this);
	setCentralWidget(m_documents);

	createActions();
	createDocks();
	createMenus();
	createStatusBar();

	viewmodels::ProjectSession* session = m_workspace->session();
	connect(session, &viewmodels::ProjectSession::projectNameChanged, this, &MainWindow::updateTitle);
	connect(session, &viewmodels::ProjectSession::dirtyChanged, this, &MainWindow::updateTitle);
	connect(session, &viewmodels::ProjectSession::dirtyChanged, this, &MainWindow::updateStatus);
	connect(session, &viewmodels::ProjectSession::pathChanged, this, &MainWindow::updateStatus);
	connect(session, &viewmodels::ProjectSession::errorReported, this,
		[this](const QString& message) { statusBar()->showMessage(message, 8000); });
	connect(m_workspace->problems(), &viewmodels::ProblemsPanelVM::countsChanged, this, &MainWindow::updateStatus);
	connect(m_workspace.get(), &viewmodels::Workspace::openDeviceRequested, this, &MainWindow::openDevice);
	connect(m_workspace.get(), &viewmodels::Workspace::openGroupMonitorRequested, this, &MainWindow::openGroupMonitor);
	connect(m_workspace->actions(), &viewmodels::ActionRegistry::contextMenuRequested, this, &MainWindow::showContextMenu);
	// Queued: the request comes from a click handler in the welcome tab, and opening a project changes that tab
	// (its recent list); QML items must not be destroyed while one of their handlers runs.
	connect(m_workspace->welcome(), &viewmodels::WelcomeVM::openRequested, this, &MainWindow::openProject,
		Qt::QueuedConnection);
	connect(m_workspace.get(), &viewmodels::Workspace::importCatalogRequested, this, &MainWindow::chooseAndImportProducts);
	connect(m_workspace.get(), &viewmodels::Workspace::showBusConnectionRequested, this,
		[this] { showDock(u"dock.busConnection"_s); });
	connect(m_workspace->commissioning(), &viewmodels::CommissioningVM::operationStarted, this,
		[this] { showDock(u"dock.commissioning"_s); });
	connect(m_workspace->commissioning(), &viewmodels::CommissioningVM::operationChanged, this,
		[this]
		{
			viewmodels::CommissioningVM* c = m_workspace->commissioning();
			if (!c->isOperationActive() && !c->lastResult().isEmpty())
				statusBar()->showMessage(c->lastResult(), 8000);
		});

	resize(1280, 800);
	if (m_options.persistLayout)
		restoreLayout();
	else
		resetLayout();
	updateTitle();
	updateStatus();
}

MainWindow::~MainWindow()
{
	// Hosts before view models before the engine (ADR-006 spike).
	m_documents->closeAll();
	for (QmlDock* dock : std::as_const(m_docks))
		dock->releaseBody();
	m_workspace.reset();
	m_engine.reset();
}

QmlDock* MainWindow::dock(const QString& id) const
{
	for (QmlDock* candidate : m_docks)
	{
		if (candidate->objectName() == id)
			return candidate;
	}
	return nullptr;
}

// --- Construction ------------------------------------------------------------------------------------------

void MainWindow::createActions()
{
	viewmodels::ActionRegistry& a = *m_workspace->actions();
	connect(a.add(u"file.new"_s, tr("&New Project"), QKeySequence::New), &QAction::triggered, this, &MainWindow::newProject);
	connect(a.add(u"file.open"_s, tr("&Open Project…"), QKeySequence::Open), &QAction::triggered, this, &MainWindow::chooseAndOpen);
	connect(a.add(u"file.save"_s, tr("&Save"), QKeySequence::Save), &QAction::triggered, this, &MainWindow::saveProject);
	connect(a.add(u"file.saveAs"_s, tr("Save &As…"), QKeySequence::SaveAs), &QAction::triggered, this, &MainWindow::chooseAndSaveAs);
	QAction* quit = a.add(u"file.quit"_s, tr("&Quit"), QKeySequence::Quit);
	quit->setMenuRole(QAction::QuitRole);
	connect(quit, &QAction::triggered, this, &QWidget::close);
	connect(a.add(u"view.resetLayout"_s, tr("&Reset Layout")), &QAction::triggered, this, &MainWindow::resetLayout);
	QAction* about = a.add(u"help.about"_s, tr("&About Kestrel"));
	about->setMenuRole(QAction::AboutRole);
	connect(about, &QAction::triggered, this,
		[this]
		{
			QMessageBox::about(this, tr("About Kestrel"),
				tr("<b>Kestrel %1</b><p>Open configuration tool for KNX installations.</p>")
					.arg(QApplication::applicationVersion()));
		});

	// Tool bar labels.
	a.action(u"edit.undo"_s)->setIconText(tr("Undo"));
	a.action(u"edit.redo"_s)->setIconText(tr("Redo"));
	a.action(u"catalog.addToLine"_s)->setIconText(tr("Add Product"));
	a.action(u"device.programAddress"_s)->setIconText(tr("Program Address"));
	a.action(u"device.download"_s)->setIconText(tr("Download"));
	applyIcons();

	auto updateSave = [this] { m_workspace->actions()->setEnabled(u"file.save"_s, m_workspace->session()->isDirty() || m_workspace->session()->path().isEmpty()); };
	connect(m_workspace->session(), &viewmodels::ProjectSession::dirtyChanged, this, updateSave);
	connect(m_workspace->session(), &viewmodels::ProjectSession::pathChanged, this, updateSave);
	updateSave();
}

void MainWindow::applyIcons()
{
	viewmodels::ActionRegistry& a = *m_workspace->actions();
	const std::pair<const char16_t*, const char*> icons[] = {{u"file.new", "new"}, {u"file.open", "open"},
		{u"file.save", "save"}, {u"edit.undo", "undo"}, {u"edit.redo", "redo"}, {u"edit.delete", "delete"},
		{u"catalog.addToLine", "add-product"}, {u"device.programAddress", "program"}, {u"device.download", "download"},
		{u"bus.connect", "connect"}, {u"bus.groupMonitor", "monitor"}};
	for (const auto& [id, name] : icons)
		a.action(QString::fromUtf16(id))->setIcon(themedIcon(QString::fromLatin1(name)));
	setWindowIcon(appIcon());
	if (m_toolBar)
	{
		// The primary action draws on the accent colour.
		if (QWidget* button = m_toolBar->widgetForAction(a.action(u"device.download"_s)))
			qobject_cast<QToolButton*>(button)->setIcon(themedIcon(u"download"_s, theme().onAccent));
	}
}

void MainWindow::changeEvent(QEvent* event)
{
	if (event->type() == QEvent::PaletteChange)
	{
		applyIcons();
		if (m_busButton)
			updateBusStatus();
	}
	QMainWindow::changeEvent(event);
}

void MainWindow::createMenus()
{
	viewmodels::ActionRegistry& a = *m_workspace->actions();
	auto add = [&a](QMenu* menu, const QStringList& ids)
	{
		for (const QString& id : ids)
		{
			if (id == viewmodels::ActionRegistry::kSeparator)
				menu->addSeparator();
			else
				menu->addAction(a.action(id));
		}
	};
	const QString sep = viewmodels::ActionRegistry::kSeparator;

	m_fileMenu = menuBar()->addMenu(tr("&File"));
	m_fileMenu->setObjectName(u"menu.file"_s);
	add(m_fileMenu, {u"file.new"_s, u"file.open"_s, sep, u"file.save"_s, u"file.saveAs"_s, sep, u"file.quit"_s});

	m_editMenu = menuBar()->addMenu(tr("&Edit"));
	m_editMenu->setObjectName(u"menu.edit"_s);
	add(m_editMenu, {u"edit.undo"_s, u"edit.redo"_s, sep, u"edit.rename"_s, u"edit.delete"_s});

	m_viewMenu = menuBar()->addMenu(tr("&View"));
	m_viewMenu->setObjectName(u"menu.view"_s);
	for (QmlDock* dock : std::as_const(m_docks))
		m_viewMenu->addAction(dock->toggleViewAction());
	m_viewMenu->addSeparator();
	add(m_viewMenu, {u"view.resetLayout"_s});

	m_projectMenu = menuBar()->addMenu(tr("&Project"));
	m_projectMenu->setObjectName(u"menu.project"_s);
	add(m_projectMenu,
		{u"building.addBuilding"_s, u"building.addFloor"_s, u"building.addRoom"_s, sep, u"topology.addArea"_s,
			u"topology.addLine"_s, u"topology.addDevice"_s, sep, u"catalog.import"_s, u"catalog.addToLine"_s, sep,
			u"ga.addMainGroup"_s, u"ga.addMiddleGroup"_s, u"ga.addAddress"_s, sep, u"device.open"_s});

	m_busMenu = menuBar()->addMenu(tr("&Bus"));
	m_busMenu->setObjectName(u"menu.bus"_s);
	add(m_busMenu, {u"bus.connect"_s, u"bus.discover"_s, u"bus.disconnect"_s, sep, u"bus.groupMonitor"_s});

	m_commissioningMenu = menuBar()->addMenu(tr("&Commissioning"));
	m_commissioningMenu->setObjectName(u"menu.commissioning"_s);
	add(m_commissioningMenu,
		{u"device.programAddress"_s, u"device.download"_s, u"device.readConfiguration"_s, u"device.readInfo"_s, sep,
			u"bus.programmingMode"_s, sep, u"device.cancelOperation"_s});

	m_helpMenu = menuBar()->addMenu(tr("&Help"));
	m_helpMenu->setObjectName(u"menu.help"_s);
	add(m_helpMenu, {u"help.about"_s});

	QToolBar* toolBar = addToolBar(tr("Main"));
	m_toolBar = toolBar;
	toolBar->setObjectName(u"toolbar.main"_s);
	toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	toolBar->setIconSize(QSize(16, 16));
	toolBar->setMovable(false);
	for (const QString& id : {u"file.new"_s, u"file.open"_s, u"file.save"_s, sep, u"edit.undo"_s, u"edit.redo"_s, sep,
			 u"edit.delete"_s, sep, u"catalog.addToLine"_s})
	{
		if (id == sep)
			toolBar->addSeparator();
		else
			toolBar->addAction(a.action(id));
	}
	// Commissioning sits at the right end; Download is the primary action.
	auto* spacer = new QWidget(toolBar);
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	toolBar->addWidget(spacer);
	toolBar->addAction(a.action(u"device.programAddress"_s));
	toolBar->addAction(a.action(u"device.download"_s));
	toolBar->widgetForAction(a.action(u"device.download"_s))->setObjectName(u"primaryAction"_s);
	applyIcons();
}

QmlDock* MainWindow::addQmlDock(const QString& id, const QString& title, const QString& type, QObject* viewModel)
{
	auto* host = new QmlHost(m_engine.get(), u"Kestrel.Views"_s, type, {{u"vm"_s, QVariant::fromValue(viewModel)}});
	auto* dock = new QmlDock(id, title, host, this);
	m_docks.append(dock);
	connect(dock, &QDockWidget::dockLocationChanged, this, &MainWindow::updateDockTitles, Qt::QueuedConnection);
	connect(dock, &QDockWidget::topLevelChanged, this, &MainWindow::updateDockTitles, Qt::QueuedConnection);
	connect(dock, &QDockWidget::visibilityChanged, this, &MainWindow::updateDockTitles, Qt::QueuedConnection);
	return dock;
}

void MainWindow::createDocks()
{
	addQmlDock(u"dock.buildings"_s, tr("Buildings"), u"BuildingsPanel"_s, m_workspace->buildings());
	addQmlDock(u"dock.topology"_s, tr("Topology"), u"TopologyPanel"_s, m_workspace->topology());
	addQmlDock(u"dock.groupAddresses"_s, tr("Group Addresses"), u"GroupAddressPanel"_s, m_workspace->groupAddresses());
	addQmlDock(u"dock.properties"_s, tr("Properties"), u"PropertiesPanel"_s, m_workspace->properties());
	addQmlDock(u"dock.problems"_s, tr("Problems"), u"ProblemsPanel"_s, m_workspace->problems());

	addQmlDock(u"dock.catalog"_s, tr("Catalog"), u"CatalogPanel"_s, m_workspace->catalogPanel());
	addQmlDock(u"dock.commissioning"_s, tr("Commissioning"), u"CommissioningPanel"_s, m_workspace->commissioning());
	addQmlDock(u"dock.busConnection"_s, tr("Bus Connection"), u"BusConnectionPanel"_s, m_workspace->busConnection());
}

void MainWindow::createStatusBar()
{
	m_dirtyLabel = new QLabel(this);
	m_dirtyLabel->setObjectName(u"status.dirty"_s);
	m_problemsButton = new QToolButton(this);
	m_problemsButton->setObjectName(u"status.problems"_s);
	m_problemsButton->setAutoRaise(true);
	connect(m_problemsButton, &QToolButton::clicked, this,
		[this]
		{
			if (QmlDock* problems = dock(u"dock.problems"_s))
			{
				problems->show();
				problems->raise();
			}
		});
	// Bus connection state as a pill; a click shows the Bus Connection dock.
	m_busButton = new QToolButton(this);
	m_busButton->setObjectName(u"busStatus"_s);
	m_busButton->setAutoRaise(true);
	connect(m_busButton, &QToolButton::clicked, this, [this] { showDock(u"dock.busConnection"_s); });
	connect(m_workspace->busConnection(), &viewmodels::BusConnectionVM::stateChanged, this, &MainWindow::updateBusStatus);
	statusBar()->addPermanentWidget(m_busButton);
	statusBar()->addPermanentWidget(m_problemsButton);
	statusBar()->addPermanentWidget(m_dirtyLabel);
	updateBusStatus();
}

void MainWindow::updateBusStatus()
{
	const viewmodels::BusConnectionVM* bus = m_workspace->busConnection();
	const Theme& t = theme();
	const QColor color = bus->isConnected() ? t.success : bus->isConnecting() ? t.warning : t.textSecondary;
	m_busButton->setText(bus->isConnected() ? tr("\u25CF %1 \u00B7 %2").arg(bus->host(), bus->localAddress())
			: bus->isConnecting()             ? tr("\u25CF Connecting\u2026")
											  : tr("\u25CB Not connected"));
	m_busButton->setToolTip(bus->stateText());
	m_busButton->setStyleSheet(u"QToolButton#busStatus { color: %1; background: rgba(%2, %3, %4, 38); }"_s.arg(
		color.name(), QString::number(color.red()), QString::number(color.green()), QString::number(color.blue())));
}

void MainWindow::resetLayout()
{
	for (QmlDock* d : std::as_const(m_docks))
	{
		d->setFloating(false);
		removeDockWidget(d);
	}
	QmlDock* buildings = dock(u"dock.buildings"_s);
	QmlDock* topology = dock(u"dock.topology"_s);
	QmlDock* groupAddresses = dock(u"dock.groupAddresses"_s);
	QmlDock* properties = dock(u"dock.properties"_s);
	QmlDock* problems = dock(u"dock.problems"_s);
	QmlDock* bus = dock(u"dock.busConnection"_s);
	QmlDock* catalog = dock(u"dock.catalog"_s);
	QmlDock* commissioning = dock(u"dock.commissioning"_s);

	addDockWidget(Qt::LeftDockWidgetArea, topology);
	tabifyDockWidget(topology, buildings);
	tabifyDockWidget(topology, groupAddresses);
	addDockWidget(Qt::RightDockWidgetArea, properties);
	addDockWidget(Qt::BottomDockWidgetArea, problems);
	tabifyDockWidget(problems, catalog);
	tabifyDockWidget(problems, commissioning);
	tabifyDockWidget(problems, bus);
	for (QmlDock* d : std::as_const(m_docks))
		d->setVisible(true);
	topology->raise();
	problems->raise();
	updateDockTitles();
	resizeDocks({topology, properties}, {380, 300}, Qt::Horizontal);
	resizeDocks({problems}, {220}, Qt::Vertical);
}

void MainWindow::restoreLayout()
{
	const QSettings settings;
	const QByteArray geometry = settings.value(kGeometryKey).toByteArray();
	const QByteArray state = settings.value(kStateKey).toByteArray();
	resetLayout();
	if (!geometry.isEmpty())
		restoreGeometry(geometry);
	if (!state.isEmpty() && !restoreState(state, kLayoutVersion))
	{
		qCInfo(lcShell) << "Saved layout is from another version: using the default layout";
		resetLayout();
	}
	updateDockTitles();
}

void MainWindow::updateDockTitles()
{
	// A tabbed dock is named by its tab: its title bar would repeat the name. A dock on its own keeps it (and the
	// handle to move it).
	for (QmlDock* d : std::as_const(m_docks))
	{
		// A hidden dock is left alone (changing its title bar could show it again); it is updated when shown.
		if (d->isHidden())
			continue;
		const bool tabbed = !d->isFloating() && !tabifiedDockWidgets(d).isEmpty();
		QWidget* title = d->titleBarWidget();
		if (tabbed && !title)
		{
			title = new QWidget(d);
			(new QHBoxLayout(title))->setContentsMargins(0, 0, 0, 0); // a (0, 0) size hint
			d->setTitleBarWidget(title);
		}
		else if (!tabbed && title)
		{
			d->setTitleBarWidget(nullptr);
			delete title;
		}
	}
}

void MainWindow::saveLayout()
{
	QSettings settings;
	settings.setValue(kGeometryKey, saveGeometry());
	settings.setValue(kStateKey, saveState(kLayoutVersion));
}

// --- Behaviour ---------------------------------------------------------------------------------------------

void MainWindow::showContextMenu(const QStringList& ids)
{
	viewmodels::ActionRegistry& a = *m_workspace->actions();
	auto* menu = new QMenu(this);
	menu->setObjectName(u"contextMenu"_s);
	menu->setAttribute(Qt::WA_DeleteOnClose);
	for (const QString& id : ids)
	{
		if (id == viewmodels::ActionRegistry::kSeparator)
			menu->addSeparator();
		else if (QAction* action = a.action(id))
			menu->addAction(action);
	}
	if (m_lastContextMenu)
		m_lastContextMenu->close();
	m_lastContextMenu = menu;
	if (menu->isEmpty())
	{
		menu->deleteLater();
		return;
	}
	menu->popup(QCursor::pos());
}

void MainWindow::showDock(const QString& id)
{
	if (QmlDock* d = dock(id))
	{
		d->show();
		d->raise();
	}
}

void MainWindow::chooseAndImportProducts()
{
	QSettings settings;
	const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import Product Files"),
		settings.value(kImportDirKey, QDir::homePath()).toString(), tr("KNX product files (*.knxprod);;All files (*)"));
	if (files.isEmpty())
		return;
	settings.setValue(kImportDirKey, QFileInfo(files.constFirst()).absolutePath());
	showDock(u"dock.catalog"_s);
	viewmodels::CatalogPanelVM* catalog = m_workspace->catalogPanel();
	if (catalog->importFiles(files))
		statusBar()->showMessage(tr("Importing %n product file(s)…", nullptr, static_cast<int>(files.size())), 4000);
	else
		statusBar()->showMessage(catalog->lastError(), 8000);
}

bool MainWindow::maybeSave()
{
	viewmodels::ProjectSession* session = m_workspace->session();
	if (!session->isDirty() || !m_options.confirmDiscard)
		return true;
	const auto answer = QMessageBox::warning(this, tr("Unsaved Changes"),
		tr("The project \"%1\" has unsaved changes.").arg(session->title()),
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
	if (answer == QMessageBox::Save)
		return saveProject();
	return answer == QMessageBox::Discard;
}

bool MainWindow::newProject()
{
	if (!maybeSave())
		return false;
	m_workspace->session()->newProject();
	return true;
}

bool MainWindow::openProject(const QString& dir)
{
	if (!maybeSave())
		return false;
	if (!m_workspace->session()->open(dir))
	{
		statusBar()->showMessage(m_workspace->session()->lastError(), 8000);
		return false;
	}
	statusBar()->showMessage(tr("Opened %1").arg(QDir::toNativeSeparators(dir)), 4000);
	return true;
}

bool MainWindow::saveProject()
{
	if (m_workspace->session()->path().isEmpty())
	{
		chooseAndSaveAs();
		return !m_workspace->session()->isDirty();
	}
	const bool ok = m_workspace->session()->save();
	statusBar()->showMessage(ok ? tr("Saved") : m_workspace->session()->lastError(), 4000);
	return ok;
}

bool MainWindow::saveProjectAs(const QString& dir)
{
	const bool ok = m_workspace->session()->saveAs(dir);
	statusBar()->showMessage(ok ? tr("Saved to %1").arg(QDir::toNativeSeparators(dir)) : m_workspace->session()->lastError(), 4000);
	return ok;
}

void MainWindow::chooseAndOpen()
{
	if (!maybeSave())
		return;
	const QString dir = QFileDialog::getExistingDirectory(this, tr("Open Project Folder"), m_workspace->session()->path());
	if (!dir.isEmpty())
	{
		if (!m_workspace->session()->open(dir))
			QMessageBox::warning(this, tr("Open Project"), m_workspace->session()->lastError());
	}
}

void MainWindow::chooseAndSaveAs()
{
	const QString dir = QFileDialog::getExistingDirectory(this, tr("Save Project Into Folder"), m_workspace->session()->path());
	if (!dir.isEmpty())
		saveProjectAs(dir);
}

void MainWindow::openDevice(model::Id<model::Device> device)
{
	m_documents->openDevice(device);
}

void MainWindow::openGroupMonitor()
{
	m_documents->openGroupMonitor();
}

void MainWindow::updateTitle()
{
	setWindowTitle(tr("%1[*] — Kestrel").arg(m_workspace->session()->title()));
	setWindowModified(m_workspace->session()->isDirty());
}

void MainWindow::updateStatus()
{
	viewmodels::ProjectSession* session = m_workspace->session();
	m_dirtyLabel->setText(session->isDirty() ? tr("Modified") : (session->path().isEmpty() ? tr("Not saved yet") : tr("Saved")));
	m_dirtyLabel->setToolTip(QDir::toNativeSeparators(session->path()));
	m_problemsButton->setText(m_workspace->problems()->summary());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
	if (!maybeSave())
	{
		event->ignore();
		return;
	}
	if (m_options.persistLayout)
		saveLayout();
	event->accept();
}

} // namespace kestrel::shell
