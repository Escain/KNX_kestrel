// Shell smoke tests (NFR-2): the real MainWindow, every dock hosting its QML view in a QQuickWidget on the
// shared engine, offscreen. A message handler fails the test on any QML warning ("qrc:/…", "….qml").
// Set KESTREL_SCREENSHOT_DIR to also write PNG screenshots of the window.

#include "SampleProject.hpp"

#include "kestrel/model/Operations.hpp"
#include "kestrel/persist/Persist.hpp"
#include "kestrel/shell/DocumentArea.hpp"
#include "kestrel/shell/MainWindow.hpp"
#include "kestrel/shell/QmlDock.hpp"
#include "kestrel/shell/QmlHost.hpp"
#include "kestrel/shell/Style.hpp"
#include "kestrel/simnet/ScenarioLoader.hpp"
#include "kestrel/simnet/Simulation.hpp"
#include "kestrel/viewmodels/BusConnectionVM.hpp"
#include "kestrel/viewmodels/CatalogPanelVM.hpp"
#include "kestrel/viewmodels/CatalogService.hpp"
#include "kestrel/viewmodels/CommissioningVM.hpp"
#include "kestrel/viewmodels/GroupMonitorVM.hpp"
#include "kestrel/viewmodels/ActionRegistry.hpp"
#include "kestrel/viewmodels/DeviceEditorVM.hpp"
#include "kestrel/viewmodels/DeviceStatusService.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"
#include "kestrel/viewmodels/SelectionService.hpp"
#include "kestrel/viewmodels/TopologyPanelVM.hpp"
#include "kestrel/viewmodels/Workspace.hpp"

#include <QAccessible>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QQuickItem>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <memory>

// Qt 6.8's QTRY_* macros narrow a std::chrono count to int inside the expansion.
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wconversion"
#endif

using namespace Qt::StringLiterals;
using namespace kestrel;
using kestrel::shell::MainWindow;

namespace
{

QStringList g_qmlMessages;
QtMessageHandler g_previousHandler = nullptr;

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
	if (type != QtDebugMsg && (message.contains(u"qrc:/"_s) || message.contains(u".qml"_s)))
	{
		g_qmlMessages.append(message);
	}
	if (g_previousHandler)
	{
		g_previousHandler(type, context, message);
	}
}

QObject* findInHost(shell::QmlHost* host, const QString& objectName)
{
	if (!host || !host->rootObject())
		return nullptr;
	if (host->rootObject()->objectName() == objectName)
		return host->rootObject();
	return host->rootObject()->findChild<QObject*>(objectName);
}

/// Every button and text input in the scene has an objectName and an accessible name.
QStringList unnamedInteractiveItems(QQuickItem* root)
{
	QStringList missing;
	const auto items = root->findChildren<QQuickItem*>();
	for (QQuickItem* item : items)
	{
		const QMetaObject* meta = item->metaObject();
		const bool button = meta->inherits(&QQuickItem::staticMetaObject)
			&& QByteArray(meta->className()).contains("Button") && !QByteArray(meta->className()).contains("Header");
		const bool input = item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit");
		if (!(button || input) || !item->isVisible() || item->parentItem() == nullptr)
			continue;
		// Internal parts of controls (e.g. the text input of a SpinBox) are covered by their control.
		if (item->parentItem()->inherits("QQuickControl") && !item->parentItem()->inherits("QQuickPane")
			&& !item->parentItem()->inherits("QQuickToolBar"))
			continue;
		QString accessibleName;
		if (QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(item))
			accessibleName = iface->text(QAccessible::Name);
		if (item->objectName().isEmpty() || accessibleName.isEmpty())
			missing.append(QString::fromLatin1(meta->className()) + u" '"_s + item->objectName() + u"'"_s);
	}
	return missing;
}

} // namespace

class MainWindowTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_temp;
	QString m_projectDir;
	QString m_catalogDir;
	testing::SampleIds m_ids;

	std::unique_ptr<MainWindow> makeWindow()
	{
		shell::MainWindowOptions options;
		options.persistLayout = false;
		options.confirmDiscard = false;
		auto window = std::make_unique<MainWindow>(options);
		window->show();
		return window;
	}

	static void showDock(MainWindow& window, const QString& id)
	{
		shell::QmlDock* dock = window.dock(id);
		QVERIFY(dock);
		dock->show();
		dock->raise();
		QTRY_VERIFY(!dock->host() || dock->host()->isLoaded());
	}

	static void screenshot(MainWindow& window, const QString& name)
	{
		const QString dir = qEnvironmentVariable("KESTREL_SCREENSHOT_DIR");
		if (dir.isEmpty())
			return;
		QTest::qWait(200);
		window.grab().save(QDir(dir).filePath(name + u".png"_s));
	}

private Q_SLOTS:
	void initTestCase()
	{
		QStandardPaths::setTestModeEnabled(true);
		QCoreApplication::setOrganizationName(u"KestrelTests"_s);
		QCoreApplication::setApplicationName(u"tst_MainWindow"_s);
		QSettings().clear();
		shell::initializeStyle();
		QAccessible::setActive(true);
		g_previousHandler = qInstallMessageHandler(messageHandler);

		QVERIFY(m_temp.isValid());
		// The product catalog of every window: a private folder with the synthetic product file (ADR-009).
		m_catalogDir = m_temp.filePath(u"catalog"_s);
		QVERIFY(QDir().mkpath(m_catalogDir));
		QVERIFY(QFile::copy(QStringLiteral(KESTREL_TEST_DATA_DIR "/catalog/M-FFFF.knxprod"), m_catalogDir + u"/M-FFFF.knxprod"_s));
		qputenv("KESTREL_CATALOG_DIR", m_catalogDir.toUtf8());
		m_projectDir = m_temp.filePath(u"sample"_s);
		model::Project project(model::IdGenerator::sequential());
		m_ids = testing::buildSampleProject(project);
		QVERIFY(persist::save(project, m_projectDir).has_value());
	}

	void init() { g_qmlMessages.clear(); }

	void cleanup() { QVERIFY2(g_qmlMessages.isEmpty(), qPrintable(g_qmlMessages.join(u'\n'))); }

	void cleanupTestCase() { qInstallMessageHandler(g_previousHandler); }

	void nfr_2__everyDockHostsItsQmlView()
	{
		auto window = makeWindow();
		QVERIFY(QTest::qWaitForWindowExposed(window.get()));
		const QStringList expected{u"dock.buildings"_s, u"dock.topology"_s, u"dock.groupAddresses"_s, u"dock.properties"_s,
			u"dock.problems"_s, u"dock.busConnection"_s, u"dock.catalog"_s, u"dock.commissioning"_s};
		for (const QString& id : expected)
		{
			QVERIFY2(window->dock(id), qPrintable(id));
		}
		const QStringList roots{u"buildingsPanel"_s, u"topologyPanel"_s, u"groupAddressPanel"_s, u"propertiesPanel"_s,
			u"problemsPanel"_s, u"busConnectionPanel"_s, u"catalogPanel"_s, u"commissioningPanel"_s};
		for (qsizetype i = 0; i < roots.size(); ++i)
		{
			showDock(*window, expected[i]);
			shell::QmlHost* host = window->dock(expected[i])->host();
			QVERIFY2(host->loadErrors().isEmpty(), qPrintable(host->loadErrors().join(u'\n')));
			QCOMPARE(host->rootObject()->objectName(), roots[i]);
			// One shared engine for every host.
			QCOMPARE(host->engine(), window->engine());
			const QStringList unnamed = unnamedInteractiveItems(host->rootObject());
			QVERIFY2(unnamed.isEmpty(), qPrintable(roots[i] + u": "_s + unnamed.join(u", "_s)));
		}
		QVERIFY(findInHost(window->dock(u"dock.topology"_s)->host(), u"topology.tree"_s));
		QVERIFY(findInHost(window->dock(u"dock.buildings"_s)->host(), u"buildings.tree"_s));
		QVERIFY(findInHost(window->dock(u"dock.groupAddresses"_s)->host(), u"groupAddresses.tree"_s));
		QVERIFY(findInHost(window->dock(u"dock.problems"_s)->host(), u"problems.table"_s));
		// The welcome tab is the central document when no editor is open.
		QCOMPARE(window->documents()->documentIds(), QStringList{shell::DocumentArea::kWelcomeId});
		QTRY_VERIFY(window->documents()->host(shell::DocumentArea::kWelcomeId)->isLoaded());
		QVERIFY(window->qmlWarnings().isEmpty());
	}

	void fr_prj_1__openProjectShowsItInEveryDock()
	{
		auto window = makeWindow();
		QVERIFY(window->openProject(m_projectDir));
		QCOMPARE(window->workspace()->session()->path(), QDir::cleanPath(m_projectDir));
		QVERIFY(window->windowTitle().startsWith(u"Sample house"_s));
		for (const QString& id : {u"dock.buildings"_s, u"dock.groupAddresses"_s, u"dock.topology"_s})
			showDock(*window, id);

		// Trees expand two levels initially: area + its 2 lines; house + its 2 floors; ranges + middle groups.
		QObject* topology = findInHost(window->dock(u"dock.topology"_s)->host(), u"topology.tree"_s);
		QTRY_COMPARE(topology->property("rows").toInt(), 1 + 2 + 4);
		QObject* buildings = findInHost(window->dock(u"dock.buildings"_s)->host(), u"buildings.tree"_s);
		QTRY_VERIFY(buildings->property("rows").toInt() >= 3);
		QObject* groups = findInHost(window->dock(u"dock.groupAddresses"_s)->host(), u"groupAddresses.tree"_s);
		QTRY_VERIFY(groups->property("rows").toInt() >= 4);

		auto* problems = window->findChild<QToolButton*>(u"status.problems"_s);
		QVERIFY(problems);
		QTRY_VERIFY(problems->text().contains(u"warning"_s));
		screenshot(*window, u"open-project"_s);
	}

	void fr_prj_2__undoRedoActionsDriveTheModelAndTheView()
	{
		auto window = makeWindow();
		QVERIFY(window->openProject(m_projectDir));
		showDock(*window, u"dock.topology"_s);
		QObject* tree = findInHost(window->dock(u"dock.topology"_s)->host(), u"topology.tree"_s);
		QTRY_COMPARE(tree->property("rows").toInt(), 7);
		viewmodels::ActionRegistry* actions = window->workspace()->actions();
		auto* dirty = window->findChild<QLabel*>(u"status.dirty"_s);
		QCOMPARE(dirty->text(), u"Saved"_s);
		QVERIFY(!actions->isEnabled(u"edit.undo"_s));

		QVERIFY(actions->trigger(u"topology.addArea"_s));
		QTRY_COMPARE(tree->property("rows").toInt(), 8);
		QCOMPARE(window->workspace()->session()->project().areas().size(), 2u);
		QCOMPARE(dirty->text(), u"Modified"_s);
		QVERIFY(window->isWindowModified());
		QVERIFY(actions->isEnabled(u"edit.undo"_s));
		QCOMPARE(actions->action(u"edit.undo"_s)->text(), u"&Undo Add area"_s);

		QVERIFY(actions->trigger(u"edit.undo"_s));
		QTRY_COMPARE(tree->property("rows").toInt(), 7);
		QCOMPARE(dirty->text(), u"Saved"_s);
		QVERIFY(actions->trigger(u"edit.redo"_s));
		QTRY_COMPARE(tree->property("rows").toInt(), 8);
		QVERIFY(actions->trigger(u"edit.undo"_s));
		QTRY_COMPARE(tree->property("rows").toInt(), 7);
	}

	void fr_top_1__addActionsFollowTheSelection()
	{
		auto window = makeWindow();
		QVERIFY(window->openProject(m_projectDir));
		showDock(*window, u"dock.topology"_s);
		showDock(*window, u"dock.properties"_s);
		viewmodels::Workspace* ws = window->workspace();
		viewmodels::ActionRegistry* actions = ws->actions();

		QVERIFY(!actions->isEnabled(u"topology.addDevice"_s));
		ws->selection()->select(m_ids.line12, u"test"_s);
		QVERIFY(actions->isEnabled(u"topology.addDevice"_s));
		QVERIFY(actions->trigger(u"topology.addDevice"_s));
		QCOMPARE(ws->session()->project().devicesOf(m_ids.line12).size(), 2u);
		// The new device becomes the selection, and the properties panel shows it.
		const auto created = ws->selection()->current();
		QVERIFY(created && created->is<model::Device>());
		QObject* address = findInHost(window->dock(u"dock.properties"_s)->host(), u"properties.address"_s);
		QTRY_COMPARE(address->property("text").toString(), u"1.2.2"_s);

		// Delete through the action; the deleted device leaves the selection.
		QVERIFY(actions->trigger(u"edit.delete"_s));
		QCOMPARE(ws->session()->project().devicesOf(m_ids.line12).size(), 1u);
		QVERIFY(ws->selection()->current() != created);
	}

	void nfr_2__contextMenusAreNativeAndUseRegistryActions()
	{
		auto window = makeWindow();
		QVERIFY(window->openProject(m_projectDir));
		viewmodels::TopologyPanelVM* topology = window->workspace()->topology();
		topology->contextMenuAt(topology->indexOfRef(model::EntityRef(m_ids.dimmer).toString()));
		QMenu* menu = window->lastContextMenu();
		QVERIFY(menu);
		QStringList names;
		for (QAction* action : menu->actions())
			names.append(action->isSeparator() ? u"-"_s : action->objectName());
		QCOMPARE(names,
			(QStringList{u"device.open"_s, u"topology.addDevice"_s, u"catalog.addToLine"_s, u"-"_s, u"device.programAddress"_s,
				u"device.download"_s, u"device.readConfiguration"_s, u"device.readInfo"_s, u"-"_s, u"edit.rename"_s,
				u"edit.delete"_s}));
		QCOMPARE(window->workspace()->selection()->currentText(), model::EntityRef(m_ids.dimmer).toString());
		menu->close();
	}

	void fr_ga_3__deviceEditorOpensOnActivationAndLinksGroupAddresses()
	{
		auto window = makeWindow();
		QVERIFY(window->openProject(m_projectDir));
		viewmodels::Workspace* ws = window->workspace();
		ws->selection()->activate(m_ids.dimmer, u"test"_s);
		const QString id = shell::DocumentArea::deviceDocumentId(m_ids.dimmer);
		QCOMPARE(window->documents()->documentIds(), QStringList{id}); // the welcome tab gave way
		shell::QmlHost* host = window->documents()->host(id);
		QTRY_VERIFY(host->isLoaded());
		QObject* table = findInHost(host, u"deviceEditor.comObjects"_s);
		QVERIFY(table);
		QTRY_COMPARE(table->property("rows").toInt(), 3);
		const QStringList unnamed = unnamedInteractiveItems(host->rootObject());
		QVERIFY2(unnamed.isEmpty(), qPrintable(unnamed.join(u", "_s)));

		auto* editor = qobject_cast<viewmodels::DeviceEditorVM*>(host->rootObject()->property("vm").value<QObject*>());
		QVERIFY(editor);
		const QString status = model::EntityRef(m_ids.dimmerStatus).toString();
		QVERIFY(editor->linkGroupAddress(status, u"0/0/3"_s));
		editor->setCurrent(status);
		QCOMPARE(editor->currentLinks().size(), 2);
		QVERIFY(!editor->linkGroupAddress(status, u"7/7/7"_s));
		QVERIFY(editor->lastError().contains(u"does not exist"_s));
		QVERIFY(editor->addComObject(u"Lock"_s, u"Disable"_s, 1, u"CW"_s, u"1.003"_s).size() > 0);
		QTRY_COMPARE(table->property("rows").toInt(), 4);
		screenshot(*window, u"device-editor"_s);

		// Activating again focuses the same tab; deleting the device closes it and brings back the welcome tab.
		ws->selection()->activate(m_ids.dimmerSwitch, u"test"_s);
		QCOMPARE(window->documents()->count(), 1);
		QVERIFY(model::removeDevice(ws->session()->project(), m_ids.dimmer).has_value());
		QTRY_COMPARE(window->documents()->documentIds(), QStringList{shell::DocumentArea::kWelcomeId});
		// Undo restores the device (the editor stays closed).
		QVERIFY(ws->session()->undo());
		QVERIFY(ws->session()->project().devices().contains(m_ids.dimmer));
	}

	void fr_prj_1__saveAndNewProject()
	{
		QTemporaryDir target;
		auto window = makeWindow();
		QVERIFY(window->openProject(m_projectDir));
		viewmodels::ActionRegistry* actions = window->workspace()->actions();
		QVERIFY(actions->trigger(u"ga.addMainGroup"_s));
		QVERIFY(window->workspace()->session()->isDirty());
		QVERIFY(window->saveProjectAs(target.filePath(u"copy"_s)));
		QVERIFY(!window->workspace()->session()->isDirty());
		QCOMPARE(window->findChild<QLabel*>(u"status.dirty"_s)->text(), u"Saved"_s);
		const auto reloaded = persist::load(target.filePath(u"copy"_s));
		QVERIFY(reloaded.has_value());
		QCOMPARE(reloaded->groupRanges().size(), 5u);

		QVERIFY(window->newProject());
		QCOMPARE(window->workspace()->session()->project().devices().size(), 0u);
		QVERIFY(window->workspace()->session()->path().isEmpty());
		QVERIFY(!window->openProject(target.filePath(u"missing"_s)));
		QVERIFY(!window->workspace()->session()->lastError().isEmpty());
	}

	void nfr_2__layoutResetAndRestore()
	{
		{
			shell::MainWindowOptions options;
			options.confirmDiscard = false;
			MainWindow window(options);
			window.show();
			QVERIFY(QTest::qWaitForWindowExposed(&window));
			window.dock(u"dock.problems"_s)->hide();
			window.dock(u"dock.busConnection"_s)->hide();
			window.close(); // saves the layout
		}
		{
			shell::MainWindowOptions options;
			options.confirmDiscard = false;
			MainWindow window(options);
			window.show();
			QVERIFY(QTest::qWaitForWindowExposed(&window));
			QVERIFY(!window.dock(u"dock.problems"_s)->isVisible());
			QVERIFY(!window.dock(u"dock.busConnection"_s)->isVisible());
			QVERIFY(window.workspace()->actions()->trigger(u"view.resetLayout"_s));
			QVERIFY(window.dock(u"dock.problems"_s)->isVisible());
			QCOMPARE(window.tabifiedDockWidgets(window.dock(u"dock.problems"_s)).size(), 3); // + catalog, commissioning, bus
			QCOMPARE(window.dockWidgetArea(window.dock(u"dock.properties"_s)), Qt::RightDockWidgetArea);
			QCOMPARE(window.tabifiedDockWidgets(window.dock(u"dock.topology"_s)).size(), 2);
			window.close();
		}
		// A layout saved by another layout version is ignored.
		QSettings().setValue(u"mainWindow/state"_s, QByteArray("garbage"));
		shell::MainWindowOptions options;
		options.confirmDiscard = false;
		MainWindow window(options);
		window.show();
		QVERIFY(window.dock(u"dock.problems"_s)->isVisible());
		QSettings().clear();
	}

	void fr_bus_5__groupMonitorTabShowsLiveTelegrams()
	{
		auto scenario = simnet::ScenarioLoader::fromJson(R"({ "interfaceAddress": "1.1.255", "devices": [
			{ "name": "lamp", "address": "1.1.1", "groupObjects": [ { "ga": "0/0/1", "flags": "CWTU", "dpt": "1.001", "value": "00" } ] } ] })");
		QVERIFY(scenario.has_value());
		simnet::Simulation sim(*scenario);
		QVERIFY(sim.listen(0).has_value());

		auto window = makeWindow();
		QVERIFY(window->openProject(m_projectDir));
		viewmodels::Workspace* ws = window->workspace();
		showDock(*window, u"dock.busConnection"_s);
		viewmodels::BusConnectionVM* bus = ws->busConnection();
		bus->setHost(u"127.0.0.1"_s);
		bus->setPort(sim.port());
		bus->connectToBus();
		QTRY_VERIFY(bus->isConnected());
		QObject* state = findInHost(window->dock(u"dock.busConnection"_s)->host(), u"bus.state"_s);
		QTRY_VERIFY(state->property("text").toString().startsWith(u"Connected"_s));

		QVERIFY(ws->actions()->trigger(u"bus.groupMonitor"_s));
		shell::QmlHost* host = window->documents()->host(shell::DocumentArea::kGroupMonitorId);
		QVERIFY(host);
		QTRY_VERIFY(host->isLoaded());
		const QStringList unnamed = unnamedInteractiveItems(host->rootObject());
		QVERIFY2(unnamed.isEmpty(), qPrintable(unnamed.join(u", "_s)));
		auto* monitor = qobject_cast<viewmodels::GroupMonitorVM*>(host->rootObject()->property("vm").value<QObject*>());
		QVERIFY(monitor);
		QVERIFY(monitor->write(u"0/0/1"_s, u"on"_s));
		QVERIFY(monitor->write(u"0/0/1"_s, u"off"_s));
		QVERIFY(monitor->read(u"0/0/1"_s));
		QObject* table = findInHost(host, u"monitor.table"_s);
		QTRY_VERIFY(table->property("rows").toInt() >= 3);
		QTRY_VERIFY(!monitor->lastRead().contains(u"Reading"_s));

		// Opening a project keeps the monitor; a second trigger focuses the same tab.
		QVERIFY(ws->actions()->trigger(u"bus.groupMonitor"_s));
		QCOMPARE(window->documents()->documentIds(), QStringList{shell::DocumentArea::kGroupMonitorId});
		ws->selection()->select(m_ids.dimmer, u"test"_s);
		QVERIFY(ws->actions()->isEnabled(u"device.readInfo"_s));
		screenshot(*window, u"group-monitor"_s);
		QVERIFY(window->openProject(m_projectDir));
		QCOMPARE(window->documents()->documentIds(), QStringList{shell::DocumentArea::kGroupMonitorId});
	}

	void fr_cat_3__catalogDockAddsProductsAndTheEditorLinksThem()
	{
		auto window = makeWindow();
		QVERIFY(window->openProject(m_projectDir));
		viewmodels::Workspace* ws = window->workspace();
		QCOMPARE(ws->catalog()->directory(), m_catalogDir);
		showDock(*window, u"dock.topology"_s);
		showDock(*window, u"dock.catalog"_s); // first show reads the catalog folder
		QVERIFY(ws->catalog()->waitForIdle());
		shell::QmlHost* catalogHost = window->dock(u"dock.catalog"_s)->host();
		QObject* products = findInHost(catalogHost, u"catalog.products"_s);
		QVERIFY(products);
		QTRY_COMPARE(products->property("rows").toInt(), 2);
		const QStringList unnamedCatalog = unnamedInteractiveItems(catalogHost->rootObject());
		QVERIFY2(unnamedCatalog.isEmpty(), qPrintable(unnamedCatalog.join(u", "_s)));

		// Add the push button to line 1.1 (one undo step), then open it.
		ws->selection()->select(m_ids.line11, u"topology"_s);
		viewmodels::CatalogPanelVM* catalog = ws->catalogPanel();
		catalog->setCurrentRow(0);
		QVERIFY(ws->actions()->isEnabled(u"catalog.addToLine"_s));
		QVERIFY(ws->actions()->trigger(u"catalog.addToLine"_s));
		const auto device = ws->selection()->current()->as<model::Device>();
		QCOMPARE(ws->session()->project().devices().at(device).appProgramRef, u"M-FFFF_A-0001-10-0001"_s);
		QObject* target = findInHost(catalogHost, u"catalog.target"_s);
		QTRY_VERIFY(target->property("text").toString().contains(u"1.1"_s));
		screenshot(*window, u"catalog"_s);

		QVERIFY(ws->actions()->trigger(u"device.open"_s));
		const QString id = shell::DocumentArea::deviceDocumentId(device);
		shell::QmlHost* host = window->documents()->host(id);
		QVERIFY(host);
		QTRY_VERIFY(host->isLoaded());
		auto* editor = qobject_cast<viewmodels::DeviceEditorVM*>(host->rootObject()->property("vm").value<QObject*>());
		QVERIFY(editor);
		QTRY_COMPARE(editor->catalogState(), u"ready"_s);
		QTRY_VERIFY(findInHost(host, u"deviceEditor.catalogComObjects"_s));
		QObject* table = findInHost(host, u"deviceEditor.catalogComObjects"_s);
		QTRY_COMPARE(table->property("rows").toInt(), 4);
		QVERIFY(editor->linkObject(u"M-FFFF_A-0001-10-0001_O-0_R-1"_s, u"0/0/1"_s));
		editor->setCurrentObject(u"M-FFFF_A-0001-10-0001_O-0_R-1"_s);
		const QStringList unnamed = unnamedInteractiveItems(host->rootObject());
		QVERIFY2(unnamed.isEmpty(), qPrintable(unnamed.join(u", "_s)));
		screenshot(*window, u"catalog-device-editor"_s);

		// Parameters tab: one row per block and active parameter, edits re-evaluate the objects.
		QObject* tabs = findInHost(host, u"deviceEditor.tabs"_s);
		QVERIFY(tabs);
		tabs->setProperty("currentIndex", 1);
		QObject* parameters = findInHost(host, u"deviceEditor.parameters"_s);
		QVERIFY(parameters);
		QTRY_VERIFY(parameters->property("count").toInt() > 5);
		QVERIFY(editor->setParameter(u"M-FFFF_A-0001-10-0001_P-4_R-1"_s, u"2"_s));
		// Button A deactivated: two objects left, plus the linked switch object shown greyed with a warning.
		QTRY_COMPARE(table->property("rows").toInt(), 3);
		QObject* warning = findInHost(host, u"deviceEditor.inactiveWarning"_s);
		QVERIFY(warning);
		QTRY_VERIFY(warning->property("visible").toBool());
		const QStringList unnamedParameters = unnamedInteractiveItems(host->rootObject());
		QVERIFY2(unnamedParameters.isEmpty(), qPrintable(unnamedParameters.join(u", "_s)));
		screenshot(*window, u"device-parameters"_s);
		tabs->setProperty("currentIndex", 0);
		showDock(*window, u"dock.problems"_s);
		QTRY_VERIFY(ws->deviceStatus()->diagnostics().size() == 1); // CO-101, computed off the GUI thread
		screenshot(*window, u"inactive-objects"_s);
		QVERIFY(ws->session()->undo());
		QTRY_COMPARE(table->property("rows").toInt(), 4);
	}

	void fr_com_1__commissioningDockShowsTheRunningOperation()
	{
		auto scenario = simnet::ScenarioLoader::fromJson(R"({ "interfaceAddress": "1.1.255", "devices": [] })");
		QVERIFY(scenario.has_value());
		simnet::Simulation sim(*scenario);
		QVERIFY(sim.listen(0).has_value());

		auto window = makeWindow();
		QVERIFY(window->openProject(m_projectDir));
		viewmodels::Workspace* ws = window->workspace();
		viewmodels::BusConnectionVM* bus = ws->busConnection();
		bus->setHost(u"127.0.0.1"_s);
		bus->setPort(sim.port());
		bus->connectToBus();
		QTRY_VERIFY(bus->isConnected());
		showDock(*window, u"dock.topology"_s);
		ws->selection()->select(m_ids.button, u"topology"_s);

		// No device is in programming mode: the dock asks for the button until cancelled.
		QVERIFY(ws->actions()->trigger(u"device.programAddress"_s));
		shell::QmlDock* dock = window->dock(u"dock.commissioning"_s);
		QTRY_VERIFY(dock->isVisible() && dock->host()->isLoaded()); // shown by the operation
		QObject* banner = findInHost(dock->host(), u"commissioning.button"_s);
		QVERIFY(banner);
		QTRY_VERIFY(banner->property("visible").toBool());
		const QStringList unnamed = unnamedInteractiveItems(dock->host()->rootObject());
		QVERIFY2(unnamed.isEmpty(), qPrintable(unnamed.join(u", "_s)));
		screenshot(*window, u"commissioning"_s);
		QVERIFY(ws->actions()->isEnabled(u"device.cancelOperation"_s));
		QVERIFY(ws->actions()->trigger(u"device.cancelOperation"_s));
		QTRY_VERIFY(!ws->commissioning()->isOperationActive());
		QVERIFY(ws->commissioning()->lastResult().contains(u"cancelled"_s));
		QCOMPARE(ws->commissioning()->log().size(), 1);
	}

	void nfr_2__repeatedConstructionAndTeardown()
	{
		for (int i = 0; i < 3; ++i)
		{
			auto window = makeWindow();
			QVERIFY(window->openProject(m_projectDir));
			for (shell::QmlDock* dock : window->docks())
			{
				dock->show();
				dock->raise();
			}
			window->workspace()->selection()->activate(m_ids.actuator, u"test"_s);
			QTRY_VERIFY(window->documents()->host(shell::DocumentArea::deviceDocumentId(m_ids.actuator))->isLoaded());
			QTest::qWait(20);
		}
	}
};

QTEST_MAIN(MainWindowTest)
#include "tst_MainWindow.moc"
