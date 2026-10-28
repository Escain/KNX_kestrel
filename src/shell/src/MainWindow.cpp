#include "kestrel/shell/MainWindow.hpp"

#include "kestrel/shell/DocumentArea.hpp"
#include "kestrel/shell/QmlDock.hpp"
#include "kestrel/shell/QmlHost.hpp"
#include "kestrel/viewmodels/ActionRegistry.hpp"
#include "kestrel/viewmodels/BuildingsPanelVM.hpp"
#include "kestrel/viewmodels/BusConnectionVM.hpp"
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
} // namespace

MainWindow::MainWindow(MainWindowOptions options, QWidget* parent)
	: QMainWindow(parent)
	, m_options(options)
	, m_engine(std::make_unique<QQmlEngine>())
	, m_workspace(std::make_unique<viewmodels::Workspace>())
{
	setObjectName(u"mainWindow"_s);
	setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);
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
	connect(m_workspace->welcome(), &viewmodels::WelcomeVM::openRequested, this, &MainWindow::openProject);

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

	QIcon (*icon)(QIcon::ThemeIcon) = [](QIcon::ThemeIcon name) { return QIcon::fromTheme(name); };
	a.action(u"file.new"_s)->setIcon(icon(QIcon::ThemeIcon::DocumentNew));
	a.action(u"file.open"_s)->setIcon(icon(QIcon::ThemeIcon::DocumentOpen));
	a.action(u"file.save"_s)->setIcon(icon(QIcon::ThemeIcon::DocumentSave));
	a.action(u"edit.undo"_s)->setIcon(icon(QIcon::ThemeIcon::EditUndo));
	a.action(u"edit.undo"_s)->setIconText(tr("Undo"));
	a.action(u"edit.redo"_s)->setIcon(icon(QIcon::ThemeIcon::EditRedo));
	a.action(u"edit.redo"_s)->setIconText(tr("Redo"));
	a.action(u"edit.delete"_s)->setIcon(icon(QIcon::ThemeIcon::EditDelete));

	auto updateSave = [this] { m_workspace->actions()->setEnabled(u"file.save"_s, m_workspace->session()->isDirty() || m_workspace->session()->path().isEmpty()); };
	connect(m_workspace->session(), &viewmodels::ProjectSession::dirtyChanged, this, updateSave);
	connect(m_workspace->session(), &viewmodels::ProjectSession::pathChanged, this, updateSave);
	updateSave();
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
			u"topology.addLine"_s, u"topology.addDevice"_s, sep, u"ga.addMainGroup"_s, u"ga.addMiddleGroup"_s,
			u"ga.addAddress"_s, sep, u"device.open"_s});

	m_busMenu = menuBar()->addMenu(tr("&Bus"));
	m_busMenu->setObjectName(u"menu.bus"_s);
	QAction* showConnection = m_busMenu->addAction(tr("&Connect…"));
	showConnection->setObjectName(u"bus.connect"_s);
	connect(showConnection, &QAction::triggered, this,
		[this]
		{
			QmlDock* bus = dock(u"dock.busConnection"_s);
			bus->show();
			bus->raise();
		});
	add(m_busMenu,
		{u"bus.disconnect"_s, sep, u"bus.groupMonitor"_s, sep, u"device.programAddress"_s, u"device.readInfo"_s,
			u"device.cancelProgramming"_s});

	m_helpMenu = menuBar()->addMenu(tr("&Help"));
	m_helpMenu->setObjectName(u"menu.help"_s);
	add(m_helpMenu, {u"help.about"_s});

	QToolBar* toolBar = addToolBar(tr("Main"));
	toolBar->setObjectName(u"toolbar.main"_s);
	toolBar->setToolButtonStyle(Qt::ToolButtonFollowStyle);
	for (const QString& id : {u"file.new"_s, u"file.open"_s, u"file.save"_s, sep, u"edit.undo"_s, u"edit.redo"_s, sep,
			 u"edit.delete"_s})
	{
		if (id == sep)
			toolBar->addSeparator();
		else
			toolBar->addAction(a.action(id));
	}
}

QmlDock* MainWindow::addQmlDock(const QString& id, const QString& title, const QString& type, QObject* viewModel)
{
	auto* host = new QmlHost(m_engine.get(), u"Kestrel.Views"_s, type, {{u"vm"_s, QVariant::fromValue(viewModel)}});
	auto* dock = new QmlDock(id, title, host, this);
	m_docks.append(dock);
	return dock;
}

void MainWindow::createDocks()
{
	addQmlDock(u"dock.buildings"_s, tr("Buildings"), u"BuildingsPanel"_s, m_workspace->buildings());
	addQmlDock(u"dock.topology"_s, tr("Topology"), u"TopologyPanel"_s, m_workspace->topology());
	addQmlDock(u"dock.groupAddresses"_s, tr("Group Addresses"), u"GroupAddressPanel"_s, m_workspace->groupAddresses());
	addQmlDock(u"dock.properties"_s, tr("Properties"), u"PropertiesPanel"_s, m_workspace->properties());
	addQmlDock(u"dock.problems"_s, tr("Problems"), u"ProblemsPanel"_s, m_workspace->problems());

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
	statusBar()->addPermanentWidget(m_problemsButton);
	statusBar()->addPermanentWidget(m_dirtyLabel);
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

	addDockWidget(Qt::LeftDockWidgetArea, topology);
	tabifyDockWidget(topology, buildings);
	tabifyDockWidget(topology, groupAddresses);
	addDockWidget(Qt::RightDockWidgetArea, properties);
	addDockWidget(Qt::BottomDockWidgetArea, problems);
	tabifyDockWidget(problems, bus);
	for (QmlDock* d : std::as_const(m_docks))
		d->setVisible(true);
	topology->raise();
	problems->raise();
	resizeDocks({topology, properties}, {340, 300}, Qt::Horizontal);
	resizeDocks({problems}, {170}, Qt::Vertical);
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
