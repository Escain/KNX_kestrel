// Catalog service, catalog dock and catalog device editor (FR-CAT-1/2/3, FR-DEV-1/2, FR-GA-3; specs/08-gui.md)
// against the synthetic M-FFFF product file (ADR-009).

#include "SampleProject.hpp"

#include "kestrel/model/Operations.hpp"
#include "kestrel/viewmodels/ActionRegistry.hpp"
#include "kestrel/viewmodels/CatalogComObjectModel.hpp"
#include "kestrel/viewmodels/CatalogPanelVM.hpp"
#include "kestrel/viewmodels/CatalogService.hpp"
#include "kestrel/viewmodels/DeviceEditorVM.hpp"
#include "kestrel/viewmodels/DeviceParameterModel.hpp"
#include "kestrel/viewmodels/DeviceStatusService.hpp"
#include "kestrel/viewmodels/ProblemsModel.hpp"
#include "kestrel/viewmodels/ProblemsPanelVM.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"
#include "kestrel/viewmodels/SelectionService.hpp"
#include "kestrel/viewmodels/TopologyPanelVM.hpp"
#include "kestrel/viewmodels/TopologyTreeModel.hpp"
#include "kestrel/viewmodels/Workspace.hpp"

#include "QtWait.hpp"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QUrl>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>
#include <set>

using namespace Qt::StringLiterals;
using namespace kestrel;
using kestrel::test::waitFor;

namespace
{

const QString kFixture = QStringLiteral(KESTREL_TEST_DATA_DIR "/catalog/M-FFFF.knxprod");
const QString kButtonProgram = u"M-FFFF_A-0001-10-0001"_s;
const QString kActuatorProgram = u"M-FFFF_A-0002-21-0001"_s;
const QString kFunctionA = u"M-FFFF_A-0001-10-0001_P-4_R-1"_s;
const QString kLedMode = u"M-FFFF_A-0001-10-0001_UP-2_R-1"_s;
const QString kLedBrightness = u"M-FFFF_A-0001-10-0001_UP-3_R-1"_s;
const QString kButtonASwitch = u"M-FFFF_A-0001-10-0001_O-0_R-1"_s;
const QString kButtonAToggle = u"M-FFFF_A-0001-10-0001_O-0_R-2"_s;
const QString kButtonALed = u"M-FFFF_A-0001-10-0001_O-1_R-1"_s;

QStringList rowKeys(const QAbstractItemModel* model)
{
	QStringList keys;
	for (int row = 0; row < model->rowCount(); ++row)
		keys.append(model->index(row, 0).data(viewmodels::KeyedTableModel::KeyRole).toString());
	return keys;
}

} // namespace

class CatalogViewModelsTest : public QObject
{
	Q_OBJECT

private:
	std::unique_ptr<QTemporaryDir> m_dir;
	std::unique_ptr<viewmodels::Workspace> m_ws;
	testing::SampleIds m_ids;

	model::Project& project() { return m_ws->session()->project(); }

	QString keyOf(const QString& programId)
	{
		for (const viewmodels::CatalogEntry& entry : m_ws->catalog()->entries())
		{
			if (entry.programId == programId)
				return entry.key;
		}
		return {};
	}

	bool importFixture()
	{
		if (!m_ws->catalogPanel()->importFiles({kFixture}))
			return false;
		return m_ws->catalog()->waitForIdle() && m_ws->catalog()->entries().size() == 2;
	}

	/// A catalog device on line 1.1 (number 30), history cleared.
	model::Id<model::Device> addCatalogDevice(const QString& programId)
	{
		const viewmodels::CatalogEntry* entry = m_ws->catalog()->entryForProgram(programId);
		auto id = model::addDevice(project(), m_ids.line11, 30, entry->productText, entry->productId, programId);
		project().history().clear();
		return *id;
	}

	std::unique_ptr<viewmodels::DeviceEditorVM> readyEditor(model::Id<model::Device> device)
	{
		auto editor = m_ws->createDeviceEditor(device);
		if (!waitFor([&] { return editor->catalogState() == u"ready"_s; }))
			return nullptr;
		return editor;
	}

private Q_SLOTS:
	void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

	void init()
	{
		m_dir = std::make_unique<QTemporaryDir>();
		QVERIFY(m_dir->isValid());
		m_ws = std::make_unique<viewmodels::Workspace>(nullptr, m_dir->filePath(u"catalog"_s));
		model::Project p(model::IdGenerator::sequential());
		m_ids = testing::buildSampleProject(p);
		m_ws->session()->replaceProject(std::move(p));
	}

	void cleanup()
	{
		m_ws.reset();
		m_dir.reset();
	}

	void fr_cat_1__catalogIsLazyAndUsesTheEnvironmentFolder()
	{
		QVERIFY(!m_ws->catalog()->isScanned());
		QVERIFY(!QFile::exists(m_dir->filePath(u"catalog"_s))); // nothing touched before the dock is shown
		qputenv("KESTREL_CATALOG_DIR", m_dir->filePath(u"elsewhere"_s).toUtf8());
		QCOMPARE(viewmodels::CatalogService::defaultDirectory(), m_dir->filePath(u"elsewhere"_s));
		qunsetenv("KESTREL_CATALOG_DIR");
		m_ws->catalogPanel()->activate();
		QVERIFY(m_ws->catalog()->isScanned());
		QVERIFY(m_ws->catalogPanel()->status().contains(u"empty"_s));
	}

	void fr_cat_1__importIndexesProductsInTheBackground()
	{
		viewmodels::CatalogPanelVM* panel = m_ws->catalogPanel();
		QAbstractItemModelTester tester(panel->model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
		QSignalSpy resets(panel->model(), &QAbstractItemModel::modelReset);
		QVERIFY(panel->importFiles({kFixture}));
		QVERIFY(panel->isBusy()); // parsed on a worker thread
		QVERIFY(m_ws->catalog()->waitForIdle());
		QCOMPARE(panel->model()->rowCount(), 2);
		QCOMPARE(resets.count(), 0);
		QVERIFY(QFile::exists(m_dir->filePath(u"catalog/M-FFFF.knxprod"_s)));
		QVERIFY(QFile::exists(m_dir->filePath(u"catalog/index.json"_s)));

		const viewmodels::CatalogEntry* button = m_ws->catalog()->entryForProgram(kButtonProgram);
		QVERIFY(button);
		QCOMPARE(button->productText, u"Kestrel Test Button 2-fold"_s);
		QCOMPARE(button->orderNumber, u"KTB-2"_s);
		QCOMPARE(button->manufacturerId, u"M-FFFF"_s);
		QCOMPARE(button->maskVersion, u"MV-07B0"_s);
		QCOMPARE(button->versionText(), u"1.0"_s);
		QVERIFY(button->programAvailable);
		QVERIFY(panel->status().contains(u"2 products"_s));

		// Sorted by product; the filter matches every word in any column.
		using C = viewmodels::CatalogProductModel;
		QCOMPARE(panel->model()->index(0, C::ProductColumn).data().toString(), u"Kestrel Test Button 2-fold"_s);
		panel->setFilter(u"ksa"_s);
		QCOMPARE(panel->model()->rowCount(), 1);
		panel->setFilter(u"kestrel 2-fold"_s);
		QCOMPARE(panel->model()->rowCount(), 1);
		panel->setFilter(u"nothing like this"_s);
		QCOMPARE(panel->model()->rowCount(), 0);
		panel->setFilter({});
		QCOMPARE(panel->model()->rowCount(), 2);

		// The same content is not copied twice.
		QVERIFY(panel->importFiles({kFixture}));
		QVERIFY(m_ws->catalog()->waitForIdle());
		QCOMPARE(m_ws->catalog()->entries().size(), std::size_t{2});
		QCOMPARE(QDir(m_dir->filePath(u"catalog"_s)).entryList({u"*.knxprod"_s}).size(), 1);
	}

	void fr_cat_4__productIndexDownloadsAndImports()
	{
		viewmodels::CatalogPanelVM* panel = m_ws->catalogPanel();
		panel->activate();
		QFile fixture(kFixture);
		QVERIFY(fixture.open(QIODevice::ReadOnly));
		const QByteArray bytes = fixture.readAll();
		auto entry = [&](QString order, QString product, QString url, QString sha256, QString category)
		{
			catalog::IndexedProduct p;
			p.manufacturer = u"Kestrel Test"_s;
			p.manufacturerId = u"M-FFFF"_s;
			p.orderNumber = std::move(order);
			p.product = std::move(product);
			p.category = std::move(category);
			p.url = std::move(url);
			p.format = u"knxprod"_s;
			p.sha256 = std::move(sha256);
			p.mask = u"MV-07B0"_s;
			return p;
		};
		const QString sha = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
		panel->setProductIndex({entry(u"KTB-2"_s, u"Kestrel Test Button 2-fold"_s, QUrl::fromLocalFile(kFixture).toString(), sha,
										u"push button"_s),
			entry(u"KTX-9"_s, u"Missing product"_s, QUrl::fromLocalFile(m_dir->filePath(u"missing.knxprod"_s)).toString(), {},
			u"binary input"_s)});
		QAbstractItemModelTester tester(panel->indexModel(), QAbstractItemModelTester::FailureReportingMode::QtTest);
		using R = viewmodels::ProductIndexModel;

		// The search tolerates punctuation in order numbers and a typo in longer words.
		panel->setFilter(u"ktb2"_s);
		QCOMPARE(panel->indexCount(), 1);
		panel->setFilter(u"kestrl button"_s);
		QCOMPARE(panel->indexCount(), 1);
		QCOMPARE(panel->indexModel()->index(0, 0).data(R::StateRole).toString(), u"available"_s);
		QVERIFY(panel->indexModel()->index(0, 0).data(R::DeviceDownloadRole).toBool());

		QSignalSpy finished(panel, &viewmodels::CatalogPanelVM::downloadFinished);
		panel->downloadIndexed(0);
		QVERIFY(panel->isDownloading());
		QVERIFY(finished.wait(10000));
		QCOMPARE(finished.first().at(0).toBool(), true);
		QVERIFY(!finished.first().at(1).toString().contains(u"updated"_s));
		QVERIFY(m_ws->catalog()->waitForIdle());
		QVERIFY(m_ws->catalog()->entryForProgram(kButtonProgram));
		QCOMPARE(panel->indexModel()->index(0, 0).data(R::StateRole).toString(), u"imported"_s);

		// A download that fails marks the row and reports it.
		panel->setFilter(u"missing"_s);
		QCOMPARE(panel->indexCount(), 1);
		panel->downloadIndexed(0);
		QVERIFY(finished.wait(10000));
		QCOMPARE(finished.last().at(0).toBool(), false);
		QCOMPARE(panel->indexModel()->index(0, 0).data(R::StateRole).toString(), u"failed"_s);
		QVERIFY(!panel->lastError().isEmpty());
	}

	void fr_cat_1__indexIsReusedWithoutParsing()
	{
		QVERIFY(importFixture());
		viewmodels::CatalogService again(m_dir->filePath(u"catalog"_s));
		again.ensureScanned();
		QVERIFY(!again.isBusy()); // unchanged file: read from index.json
		QCOMPARE(again.entries(), m_ws->catalog()->entries());
		QVERIFY(!again.cachedDatabase(u"M-FFFF.knxprod"_s));
		QVERIFY(again.databaseForProgram(kActuatorProgram)); // synchronous load, cached afterwards
		QVERIFY(again.cachedDatabase(u"M-FFFF.knxprod"_s));
	}

	void fr_cat_1__badFilesAreRejected()
	{
		viewmodels::CatalogPanelVM* panel = m_ws->catalogPanel();
		QVERIFY(!panel->importFiles({m_dir->filePath(u"missing.knxprod"_s)}));
		QVERIFY(!panel->lastError().isEmpty());

		const QString notZip = m_dir->filePath(u"broken.knxprod"_s);
		QFile file(notZip);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("not a zip file");
		file.close();
		QSignalSpy failed(m_ws->catalog(), &viewmodels::CatalogService::importFailed);
		QVERIFY(panel->importFiles({notZip}));
		QVERIFY(m_ws->catalog()->waitForIdle());
		QCOMPARE(failed.count(), 1);
		QVERIFY(!panel->lastError().isEmpty());
		QVERIFY(!QFile::exists(m_dir->filePath(u"catalog/broken.knxprod"_s))); // removed again
		QVERIFY(m_ws->catalog()->entries().empty());
	}

	void fr_cat_3__addToLineCreatesACatalogDeviceInOneUndoStep()
	{
		QVERIFY(importFixture());
		viewmodels::CatalogPanelVM* panel = m_ws->catalogPanel();
		m_ws->selection()->select(m_ids.house, u"test"_s);
		panel->setCurrentKey(keyOf(kButtonProgram));
		QVERIFY(!panel->canAdd());
		QVERIFY(!m_ws->actions()->isEnabled(u"catalog.addToLine"_s));
		QVERIFY(panel->addToLine().isEmpty());
		QVERIFY(!panel->lastError().isEmpty());

		// The target follows the topology selection (a line, or the line of a device) and survives other selections.
		m_ws->selection()->select(m_ids.actuator, u"topology"_s);
		m_ws->selection()->select(m_ids.livingLight, u"groupAddresses"_s);
		QVERIFY(panel->canAdd());
		QCOMPARE(panel->targetText(), u"1.1 Ground floor line"_s);
		QVERIFY(m_ws->actions()->isEnabled(u"catalog.addToLine"_s));

		const std::size_t before = project().devices().size();
		QVERIFY(m_ws->actions()->trigger(u"catalog.addToLine"_s));
		QCOMPARE(project().devices().size(), before + 1);
		const auto current = m_ws->selection()->current();
		QVERIFY(current && current->is<model::Device>());
		const model::Device& device = project().devices().at(current->as<model::Device>());
		QCOMPARE(device.name, u"Kestrel Test Button 2-fold"_s);
		QCOMPARE(device.appProgramRef, kButtonProgram);
		QCOMPARE(device.productRef, m_ws->catalog()->entryForProgram(kButtonProgram)->productId);
		QCOMPARE(device.line, m_ids.line11);
		QCOMPARE(device.number, std::optional<unsigned>(4)); // 1..3 are taken

		QVERIFY(m_ws->session()->undo());
		QCOMPARE(project().devices().size(), before);
		QVERIFY(m_ws->session()->redo());
		QCOMPARE(project().devices().size(), before + 1);

		// Drop on a topology line (catalog -> topology drag).
		viewmodels::TopologyPanelVM* topology = m_ws->topology();
		const QModelIndex line12 = topology->treeModel()->indexOf(model::EntityRef(m_ids.line12));
		QVERIFY(topology->dropFormats().contains(viewmodels::CatalogPanelVM::kMimeType));
		QVERIFY(topology->acceptsDrop(line12, viewmodels::CatalogPanelVM::kMimeType));
		QVERIFY(!topology->acceptsDrop(topology->treeModel()->indexOf(model::EntityRef(m_ids.area1)), viewmodels::CatalogPanelVM::kMimeType));
		QVERIFY(topology->dropOn(line12, viewmodels::CatalogPanelVM::kMimeType, keyOf(kActuatorProgram)));
		QCOMPARE(project().devicesOf(m_ids.line12).size(), std::size_t{2});
	}

	void fr_dev_2__editorShowsTheActiveComObjects()
	{
		QVERIFY(importFixture());
		const auto device = addCatalogDevice(kButtonProgram);
		auto editor = readyEditor(device);
		QVERIFY(editor);
		QVERIFY(editor->isCatalogDevice());
		QCOMPARE(editor->productText(), u"Kestrel Test Button 2-fold"_s);
		QCOMPARE(editor->orderNumber(), u"KTB-2"_s);
		QVERIFY(editor->applicationText().contains(u"MV-07B0"_s));

		QAbstractItemModel* objects = editor->catalogComObjects();
		QAbstractItemModelTester tester(objects, QAbstractItemModelTester::FailureReportingMode::QtTest);
		// Defaults: button A "switch" (0 and 1), button B (2 and 3); the toggle variant of #0 is inactive.
		QCOMPARE(rowKeys(objects).size(), 4);
		QVERIFY(rowKeys(objects).contains(kButtonASwitch));
		QVERIFY(!rowKeys(objects).contains(kButtonAToggle));
		using C = viewmodels::CatalogComObjectModel;
		QCOMPARE(objects->index(0, C::NumberColumn).data().toString(), u"0"_s);
		QCOMPARE(objects->index(0, C::FunctionColumn).data().toString(), u"Switch"_s);
		QCOMPARE(objects->index(0, C::FlagsColumn).data().toString(), u"CWTU"_s);
		QCOMPARE(objects->index(0, C::DptColumn).data().toString(), u"DPST-1-1"_s);
		QVERIFY(!objects->index(0, C::ChannelColumn).data().toString().isEmpty());

		// The hook can be replaced (e.g. show everything).
		m_ws->setActiveComObjectFilter(&viewmodels::allEffectiveComObjects);
		auto all = readyEditor(device);
		QVERIFY(all);
		QCOMPARE(all->catalogComObjects()->rowCount(), 6);
		m_ws->setActiveComObjectFilter({});

		// A device whose program is not in the catalog says so.
		auto missing = model::addDevice(project(), m_ids.line11, 31, u"Unknown"_s, u"M-0000_H-1"_s, u"M-0000_A-1"_s);
		QVERIFY(missing.has_value());
		auto unknown = m_ws->createDeviceEditor(*missing);
		QVERIFY(waitFor([&] { return unknown->catalogState() == u"missing"_s; }));
		QVERIFY(unknown->catalogMessage().contains(u"M-0000_A-1"_s));
	}

	void fr_ga_3__linkingCreatesTheComObjectOnDemandWithUndo()
	{
		QVERIFY(importFixture());
		const auto device = addCatalogDevice(kButtonProgram);
		auto editor = readyEditor(device);
		QVERIFY(editor);
		const std::size_t objects = project().comObjects().size();
		const std::size_t addresses = project().groupAddresses().size();

		// A new address: group address + com object + link, one undo step.
		editor->setCurrentObject(kButtonASwitch);
		QVERIFY2(editor->linkObject(kButtonASwitch, u"0/0/10"_s), qPrintable(editor->lastError()));
		QCOMPARE(project().comObjects().size(), objects + 1);
		QCOMPARE(project().groupAddresses().size(), addresses + 1);
		const auto created = editor->catalogModel()->modelObject(kButtonASwitch);
		QVERIFY(created);
		const model::ComObject& object = project().comObjects().at(*created);
		QCOMPARE(object.catalogRef, kButtonASwitch);
		QCOMPARE(object.number, 0u);
		QCOMPARE(object.flags.toString(), u"CWTU"_s);
		QCOMPARE(object.dpt, u"DPST-1-1"_s);
		QCOMPARE(editor->catalogModel()->groupAddressesText(kButtonASwitch), u"0/0/10"_s);
		QCOMPARE(editor->currentLinks().size(), 1);
		const auto ga = std::find_if(project().groupAddresses().begin(), project().groupAddresses().end(),
			[](const model::GroupAddress& a) { return a.raw == 10; });
		QCOMPARE(ga->dpt, u"DPST-1-1"_s);
		QCOMPARE(ga->range, std::optional(m_ids.switching)); // the innermost range

		QVERIFY(m_ws->session()->undo());
		QCOMPARE(project().comObjects().size(), objects);
		QCOMPARE(project().groupAddresses().size(), addresses);
		QVERIFY(m_ws->session()->redo());
		QCOMPARE(project().comObjects().size(), objects + 1);

		// An existing address dropped from the Group Addresses dock: second link, not sending.
		const int row = editor->catalogModel()->rowOf(kButtonASwitch);
		QVERIFY(editor->dropOnObjectRow(row, model::EntityRef(m_ids.kitchenLight).toString()));
		QCOMPARE(editor->catalogModel()->groupAddressesText(kButtonASwitch), u"0/0/10, 0/0/2"_s);
		QVERIFY(!editor->dropOnObjectRow(row, model::EntityRef(m_ids.house).toString()));
		QVERIFY(!editor->linkObject(kButtonASwitch, u"0/0/2"_s)); // already linked
		QVERIFY(!editor->linkObject(kButtonASwitch, u"not an address"_s));
		QVERIFY(editor->linkObject(kButtonALed, u"0/0/1  Living room light"_s)); // a groupAddressOptions entry

		// Unlinking the last address removes the model com object in the same step.
		const auto links = project().linksOfComObject(*created);
		QCOMPARE(links.size(), std::size_t{2});
		QVERIFY(editor->unlink(model::EntityRef(links[0]).toString()));
		QVERIFY(editor->catalogModel()->modelObject(kButtonASwitch));
		const auto remaining = project().linksOfComObject(*created);
		QVERIFY(editor->unlink(model::EntityRef(remaining[0]).toString()));
		QVERIFY(!editor->catalogModel()->modelObject(kButtonASwitch));
		QVERIFY(m_ws->session()->undo());
		QVERIFY(editor->catalogModel()->modelObject(kButtonASwitch));
		QCOMPARE(project().linksOfComObject(*created).size(), std::size_t{1});
	}

	void fr_dev_1__parametersChangeTheActiveComObjects()
	{
		QVERIFY(importFixture());
		const auto device = addCatalogDevice(kButtonProgram);
		auto editor = readyEditor(device);
		QVERIFY(editor);
		QAbstractItemModel* objects = editor->catalogComObjects();
		QAbstractItemModel* parameters = editor->parameters();
		QAbstractItemModelTester objectTester(objects, QAbstractItemModelTester::FailureReportingMode::QtTest);
		QAbstractItemModelTester parameterTester(parameters, QAbstractItemModelTester::FailureReportingMode::QtTest);
		QSignalSpy resets(objects, &QAbstractItemModel::modelReset);
		QSignalSpy parameterResets(parameters, &QAbstractItemModel::modelReset);

		viewmodels::DeviceParameterModel* model = editor->parameterModel();
		const viewmodels::ParameterRow* function = model->row(kFunctionA);
		QVERIFY(function);
		QCOMPARE(function->editor, u"enum"_s);
		QCOMPARE(function->value, u"0"_s);
		QCOMPARE(function->options.size(), 3);
		QVERIFY(model->row(kLedBrightness)); // LED mode 2 shows the brightness
		QVERIFY(rowKeys(parameters).first().startsWith(u"block:"_s));
		QVERIFY(editor->parameterCount() > 4);

		// Function A = toggle: the toggle object replaces the switch object #0.
		QVERIFY(editor->setParameter(kFunctionA, u"1"_s));
		QCOMPARE(project().devices().at(device).params.at(kFunctionA), u"1"_s);
		QVERIFY(rowKeys(objects).contains(kButtonAToggle));
		QVERIFY(!rowKeys(objects).contains(kButtonASwitch));
		QCOMPARE(model->row(kFunctionA)->value, u"1"_s);
		QVERIFY(!model->row(kFunctionA)->isDefault);

		// Deactivated: no button A objects at all.
		QVERIFY(editor->setParameter(kFunctionA, u"2"_s));
		QCOMPARE(objects->rowCount(), 2);
		QVERIFY(!rowKeys(objects).contains(kButtonALed));

		// LED mode "off" hides the brightness parameter.
		QVERIFY(editor->setParameter(kLedMode, u"0"_s));
		QVERIFY(!model->row(kLedBrightness));

		// Invalid values are refused; the default resets.
		QVERIFY(!editor->setParameter(kFunctionA, u"7"_s));
		QVERIFY(!editor->lastError().isEmpty());
		QVERIFY(editor->resetParameter(kFunctionA));
		QVERIFY(!project().devices().at(device).params.contains(kFunctionA));
		QCOMPARE(objects->rowCount(), 4);

		// Every edit is undoable and re-evaluates.
		QVERIFY(m_ws->session()->undo()); // reset
		QCOMPARE(objects->rowCount(), 2);
		QVERIFY(m_ws->session()->undo()); // LED mode
		QVERIFY(model->row(kLedBrightness));
		QCOMPARE(resets.count(), 0);
		QCOMPARE(parameterResets.count(), 0);
	}

	void fr_dev_3__needsDownloadIsComputedOffTheGuiThread()
	{
		QVERIFY(importFixture());
		const auto device = addCatalogDevice(kButtonProgram);
		// The real planner, wrapped to see where it runs.
		QSet<QThread*> threads;
		QMutex mutex;
		const viewmodels::DownloadPlanner real = viewmodels::catalogDownloadPlanner(m_ws->catalog());
		m_ws->setDownloadPlanner(
			[&](const model::Project& p, model::Id<model::Device> id)
			{
				{
					const QMutexLocker lock(&mutex);
					threads.insert(QThread::currentThread());
				}
				return real(p, id);
			});
		// As after a download: the device holds the plan of the current configuration.
		const auto plan = real(project(), device);
		QVERIFY2(plan.has_value(), plan ? "" : plan.error().message.c_str());
		model::DeviceState state;
		state.downloadedHash = viewmodels::planHash(*plan);
		project().recordState(device, state);

		viewmodels::DeviceStatusService* status = m_ws->deviceStatus();
		using S = viewmodels::DeviceStatusService::DownloadState;
		viewmodels::TopologyTreeModel* topology = m_ws->topology()->topologyModel();
		QAbstractItemModelTester tester(topology, QAbstractItemModelTester::FailureReportingMode::QtTest);
		const QModelIndex cell = topology->indexOf(model::EntityRef(device), viewmodels::TopologyTreeModel::DownloadColumn);
		cell.data().toString(); // the first query starts the service
		QVERIFY(status->isActive());
		QVERIFY(status->waitForIdle());
		QCOMPARE(status->downloadState(device), S::UpToDate);
		QCOMPARE(cell.data().toString(), u"Loaded"_s);
		auto editor = readyEditor(device);
		QVERIFY(editor);
		QVERIFY(editor->downloadState().contains(u"up to date"_s));
		QVERIFY(!editor->needsDownload());

		QSignalSpy changed(topology, &QAbstractItemModel::dataChanged);
		QVERIFY(editor->setParameter(kFunctionA, u"1"_s));
		QVERIFY(status->waitForIdle());
		QCOMPARE(status->downloadState(device), S::NeedsDownload);
		QCOMPARE(cell.data().toString(), u"Modified"_s);
		QVERIFY(editor->needsDownload());
		QVERIFY(changed.count() > 0); // the row was refreshed
		QVERIFY(!threads.isEmpty());
		QVERIFY(!threads.contains(QThread::currentThread())); // never on the GUI thread

		QVERIFY(m_ws->session()->undo());
		QVERIFY(status->waitForIdle());
		QCOMPARE(status->downloadState(device), S::UpToDate);

		// Rapid edits are debounced: one computation for a burst.
		int calls = 0;
		m_ws->setDownloadPlanner(
			[&](const model::Project& p, model::Id<model::Device> id)
			{
				const QMutexLocker lock(&mutex);
				++calls;
				return real(p, id);
			});
		QVERIFY(status->waitForIdle());
		calls = 0;
		for (int i = 0; i < 5; ++i)
			QVERIFY(editor->setParameter(u"M-FFFF_A-0001-10-0001_P-1_R-1"_s, QString::number(60 + i)));
		QVERIFY(status->waitForIdle());
		QCOMPARE(calls, 1);
	}

	void fr_dev_2__inactiveLinkedObjectsAreGreyedAndReported()
	{
		QVERIFY(importFixture());
		const auto device = addCatalogDevice(kButtonProgram);
		auto editor = readyEditor(device);
		QVERIFY(editor);
		QVERIFY(editor->linkObject(kButtonASwitch, u"0/0/10"_s));
		viewmodels::ProblemsModel* problems = m_ws->problems()->problemsModel();
		auto hasInactiveWarning = [&]
		{
			problems->refresh();
			return std::any_of(problems->diagnostics().begin(), problems->diagnostics().end(),
				[](const validate::Diagnostic& d) { return d.code == viewmodels::DeviceStatusService::kInactiveLinkedCode; });
		};
		QVERIFY(m_ws->deviceStatus()->waitForIdle());
		QVERIFY(!hasInactiveWarning());

		// Button A deactivated: the linked switch object stays, greyed, and is reported; its links are kept.
		QVERIFY(editor->setParameter(kFunctionA, u"2"_s));
		viewmodels::CatalogComObjectModel* objects = editor->catalogModel();
		QCOMPARE(objects->rowCount(), 3);
		QVERIFY(objects->isInactive(kButtonASwitch));
		QVERIFY(objects->index(objects->rowOf(kButtonASwitch), 0).data(viewmodels::CatalogComObjectModel::InactiveRole).toBool());
		QCOMPARE(editor->inactiveLinkedCount(), 1);
		QVERIFY(m_ws->deviceStatus()->waitForIdle());
		QVERIFY(hasInactiveWarning());
		const auto object = editor->catalogModel()->modelObject(kButtonASwitch);
		QVERIFY(object);
		QCOMPARE(project().linksOfComObject(*object).size(), std::size_t{1});
		QCOMPARE(m_ws->deviceStatus()->inactiveLinked(device), std::vector<model::Id<model::ComObject>>{*object});

		// Unlinking it removes the row and the warning.
		QVERIFY(editor->unlink(model::EntityRef(project().linksOfComObject(*object)[0]).toString()));
		QCOMPARE(objects->rowCount(), 2);
		QCOMPARE(editor->inactiveLinkedCount(), 0);
		QVERIFY(!hasInactiveWarning());
	}

	/// Opt-in (ADR-009): real product files from $KESTREL_PRIVATE_CORPUS are indexed and opened without
	/// blocking the GUI thread. Prints the timings.
	void private_corpus__realFilesLoadOffTheGuiThread()
	{
		const QString corpus = qEnvironmentVariable("KESTREL_PRIVATE_CORPUS");
		if (corpus.isEmpty())
			QSKIP("KESTREL_PRIVATE_CORPUS not set");
		QStringList files;
		QDirIterator it(corpus, {u"*.knxprod"_s}, QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext())
			files.append(it.next());
		if (files.isEmpty())
			QSKIP("no .knxprod in KESTREL_PRIVATE_CORPUS");

		// Longest pause of the GUI event loop while the catalog works.
		QElapsedTimer beat;
		qint64 longest = 0;
		QTimer ticker;
		ticker.setInterval(5);
		QObject::connect(&ticker, &QTimer::timeout, [&] { longest = std::max(longest, beat.restart()); });
		QElapsedTimer total;
		total.start();
		beat.start();
		ticker.start();
		QVERIFY(m_ws->catalogPanel()->importFiles(files));
		QVERIFY(waitFor([&] { return !m_ws->catalog()->isBusy(); }, 120000));
		qInfo("indexed %lld files (%zu products) in %lld ms; longest GUI pause %lld ms", static_cast<long long>(files.size()),
			m_ws->catalog()->entries().size(), static_cast<long long>(total.elapsed()), static_cast<long long>(longest));
		QVERIFY(m_ws->catalog()->problems().isEmpty());

		// Open each program in a device editor from a cold cache (a fresh service on the same folder).
		for (const viewmodels::CatalogEntry& entry : m_ws->catalog()->entries())
		{
			if (!entry.programAvailable)
				continue;
			viewmodels::Workspace fresh(nullptr, m_ws->catalog()->directory());
			model::Project p(model::IdGenerator::sequential());
			const auto area = *model::addArea(p, 1, u"A"_s);
			const auto line = *model::addLine(p, area, 1, u"L"_s);
			const auto device = *model::addDevice(p, line, 1, entry.productText, entry.productId, entry.programId);
			fresh.session()->replaceProject(std::move(p));
			total.restart();
			longest = 0;
			beat.restart();
			auto editor = fresh.createDeviceEditor(device);
			QVERIFY(waitFor([&] { return editor->catalogState() == u"ready"_s || editor->catalogState() == u"error"_s; }, 60000));
			QCOMPARE(editor->catalogState(), u"ready"_s);
			qInfo("%s: editor ready in %lld ms, %d com objects, %d parameters; longest GUI pause %lld ms",
				qPrintable(entry.programId), static_cast<long long>(total.elapsed()), editor->catalogComObjects()->rowCount(),
				editor->parameterCount(), static_cast<long long>(longest));
		}
	}
};

QTEST_MAIN(CatalogViewModelsTest)
#include "tst_CatalogViewModels.moc"
