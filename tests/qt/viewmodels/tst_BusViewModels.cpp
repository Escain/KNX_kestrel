// Bus view models against an in-process simnet::Simulation over loopback (NFR-5): connection (FR-BUS-2),
// group monitor and group read/write (FR-BUS-5/6), IA programming (FR-COM-1), download (FR-COM-2),
// device info (FR-COM-4).

#include "SampleProject.hpp"

#include "kestrel/app/BusSession.hpp"
#include "kestrel/model/Operations.hpp"
#include "kestrel/simnet/ScenarioLoader.hpp"
#include "kestrel/simnet/Simulation.hpp"
#include "kestrel/viewmodels/ActionRegistry.hpp"
#include "kestrel/viewmodels/BusConnectionVM.hpp"
#include "kestrel/viewmodels/CatalogPanelVM.hpp"
#include "kestrel/viewmodels/CatalogService.hpp"
#include "kestrel/viewmodels/CommissioningVM.hpp"
#include "kestrel/viewmodels/GroupMonitorVM.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"
#include "kestrel/viewmodels/SelectionService.hpp"
#include "kestrel/viewmodels/TelegramModel.hpp"
#include "kestrel/viewmodels/Workspace.hpp"

#include "QtWait.hpp"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using namespace kestrel;
using kestrel::test::waitFor;

namespace
{

const QByteArray kScenario = R"({
	"interfaceAddress": "1.1.255",
	"devices": [
		{ "name": "switch-a", "address": "1.1.1", "serial": "FFFF:00001234", "maskVersion": "0705",
		  "groupObjects": [ { "ga": "1/1/1", "flags": "CWTU", "dpt": "1.001", "value": "00" },
		                    { "ga": "1/1/2", "flags": "CRT", "dpt": "9.001", "value": "0c 33" } ] },
		{ "name": "fresh", "address": "15.15.255", "serial": "FFFF:00005678" },
		{ "name": "systemb", "address": "1.1.21", "serial": "FFFF:00009999", "maskVersion": "07B0" } ] })";

Bytes hex(std::initializer_list<int> values)
{
	Bytes bytes;
	for (int v : values)
		bytes.push_back(static_cast<std::uint8_t>(v));
	return bytes;
}

/// A minimal System B load: application program state machine and its version property.
download::Plan simplePlan()
{
	using namespace download;
	return Plan{{Authorize{}, Unload{4, false}, Load{4}, WriteProp{4, kPidProgramVersion, hex({0xff, 0xff, 0x00, 0x01, 0x10}), true},
		LoadCompleted{4}, Restart{}}};
}

app::TelegramRecord record(std::uint16_t destination, std::uint16_t source = 0x1101)
{
	app::TelegramRecord r;
	r.timestamp = QDateTime::currentDateTimeUtc();
	r.source = knx::IndividualAddress(source);
	r.destination = destination;
	r.apci = knx::Apci::GroupValueWrite;
	r.payload = QByteArray(1, '\x01');
	return r;
}

} // namespace

class BusViewModelsTest : public QObject
{
	Q_OBJECT

private:
	std::unique_ptr<simnet::Simulation> m_sim;
	std::unique_ptr<QTemporaryDir> m_catalogDir;
	std::unique_ptr<viewmodels::Workspace> m_ws;
	testing::SampleIds m_ids;
	model::Id<model::Device> m_newDevice;
	model::Id<model::Device> m_systemB; ///< 1.1.21, a catalog device (button program)

	model::Project& project() { return m_ws->session()->project(); }

	bool connect()
	{
		viewmodels::BusConnectionVM* bus = m_ws->busConnection();
		bus->setHost(u"127.0.0.1"_s);
		bus->setPort(m_sim->port());
		bus->connectToBus();
		return waitFor([bus] { return bus->isConnected(); });
	}

private Q_SLOTS:
	void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

	void init()
	{
		auto scenario = simnet::ScenarioLoader::fromJson(kScenario);
		QVERIFY(scenario.has_value());
		m_sim = std::make_unique<simnet::Simulation>(*scenario);
		QVERIFY(m_sim->listen(0).has_value());

		m_catalogDir = std::make_unique<QTemporaryDir>();
		m_ws = std::make_unique<viewmodels::Workspace>(nullptr, m_catalogDir->path());
		model::Project p(model::IdGenerator::sequential());
		m_ids = testing::buildSampleProject(p);
		m_systemB = *model::addDevice(p, m_ids.line11, 21, u"Button"_s, u"M-FFFF_H-0001_P-KTB.2"_s, u"M-FFFF_A-0001-10-0001"_s);
		QVERIFY(model::addGroupAddress(p, knx::GroupAddress(0x0901), u"Hall switch"_s, u"DPST-1-1"_s, m_ids.heating).has_value());
		QVERIFY(model::addGroupAddress(p, knx::GroupAddress(0x0902), u"Hall temperature"_s, u"DPST-9-1"_s, m_ids.heating).has_value());
		m_newDevice = *model::addDevice(p, m_ids.line11, 20, u"New device"_s);
		p.history().clear();
		p.history().setClean();
		m_ws->session()->replaceProject(std::move(p));
	}

	void cleanup()
	{
		m_ws.reset();
		m_sim.reset();
		m_catalogDir.reset();
	}

	void nfr_7__telegramModelAppendsBatchesWithinItsCapacity()
	{
		viewmodels::TelegramModel model(m_ws->session(), 5);
		QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
		QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
		QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
		model.append({record(0x0901), record(0x0902), record(0x0001)});
		QCOMPARE(inserted.count(), 1); // one insert per batch
		QCOMPARE(model.rowCount(), 3);
		QCOMPARE(model.index(0, viewmodels::TelegramModel::DestinationColumn).data().toString(), u"1/1/1"_s);
		QCOMPARE(model.index(0, viewmodels::TelegramModel::NameColumn).data().toString(), u"Hall switch"_s);
		QCOMPARE(model.index(2, viewmodels::TelegramModel::NameColumn).data().toString(), u"Living room light"_s);

		model.append({record(0x0903), record(0x0904), record(0x0905), record(0x0906)});
		QCOMPARE(inserted.count(), 2);
		QCOMPARE(removed.count(), 1); // the two oldest pushed out in one removal
		QCOMPARE(model.rowCount(), 5);
		QCOMPARE(model.index(0, viewmodels::TelegramModel::DestinationColumn).data().toString(), u"0/0/1"_s);

		model.setFilter(u"1/1/"_s);
		QCOMPARE(model.rowCount(), 4);
		model.append({record(0x0001), record(0x0907)}); // only 1/1/7 is shown
		QCOMPARE(model.rowCount(), 4);
		QCOMPARE(model.index(3, viewmodels::TelegramModel::DestinationColumn).data().toString(), u"1/1/7"_s);
		model.setFilter(u"living"_s); // GA names match too
		QCOMPARE(model.rowCount(), 1);
		model.setFilter({});
		QCOMPARE(model.rowCount(), 5);

		// A batch larger than the capacity keeps its newest records.
		QList<app::TelegramRecord> big;
		for (std::uint16_t i = 1; i <= 8; ++i)
			big.append(record(static_cast<std::uint16_t>(0x0A00 + i)));
		model.append(big);
		QCOMPARE(model.rowCount(), 5);
		QCOMPARE(model.index(4, viewmodels::TelegramModel::DestinationColumn).data().toString(), u"1/2/8"_s);
		model.clear();
		QCOMPARE(model.rowCount(), 0);
	}

	void fr_bus_2__connectAndDisconnect()
	{
		viewmodels::BusConnectionVM* bus = m_ws->busConnection();
		QCOMPARE(bus->stateText(), u"Disconnected"_s);
		QVERIFY(!m_ws->actions()->isEnabled(u"bus.disconnect"_s));
		QVERIFY(connect());
		QCOMPARE(bus->localAddress(), u"1.1.255"_s);
		QVERIFY(m_ws->actions()->isEnabled(u"bus.disconnect"_s));
		QVERIFY(m_ws->actions()->trigger(u"bus.disconnect"_s));
		QVERIFY(waitFor([bus] { return !bus->isConnected() && !bus->isConnecting(); }));

		bus->setPort(0);
		bus->connectToBus();
		QVERIFY(!bus->lastError().isEmpty());
	}

	void fr_bus_5__monitorShowsDecodedTelegramsAndPauses()
	{
		QVERIFY(connect());
		auto monitor = m_ws->createGroupMonitor();
		QAbstractItemModelTester tester(monitor->model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
		QCOMPARE(monitor->dptOf(u"1/1/1"_s), u"DPST-1-1"_s);

		// FR-BUS-6: the value text is parsed with the project's DPT.
		QVERIFY(monitor->write(u"1/1/1"_s, u"on"_s));
		QVERIFY(waitFor([&] { return monitor->count() >= 1; }));
		QVERIFY(waitFor([&] { return m_sim->device("switch-a")->groupValue(knx::GroupAddress(0x0901)) == Bytes{0x01}; }));
		const QAbstractItemModel* model = monitor->model();
		using C = viewmodels::TelegramModel;
		QCOMPARE(model->index(0, C::DestinationColumn).data().toString(), u"1/1/1"_s);
		QCOMPARE(model->index(0, C::NameColumn).data().toString(), u"Hall switch"_s);
		QVERIFY(!model->index(0, C::ValueColumn).data().toString().isEmpty()); // decoded with DPT 1.001
		QVERIFY(model->index(0, 0).data(C::OutgoingRole).toBool());

		QVERIFY(monitor->read(u"1/1/2"_s));
		QVERIFY(waitFor([&] { return monitor->lastRead().contains(u"°C"_s); }));
		QVERIFY(monitor->lastRead().contains(u"1.1.1"_s));

		QVERIFY(!monitor->write(u"1/1/1"_s, u"maybe"_s));
		QVERIFY(!monitor->lastError().isEmpty());
		QVERIFY(!monitor->write(u"3/3/3"_s, u"1"_s)); // no DPT known
		QVERIFY(monitor->write(u"3/3/3"_s, u"1"_s, u"5.010"_s));

		const int before = monitor->count();
		monitor->setPaused(true);
		QVERIFY(monitor->write(u"1/1/1"_s, u"off"_s));
		QVERIFY(waitFor([&] { return monitor->pending() > 0; }));
		QCOMPARE(monitor->count(), before);
		monitor->setPaused(false);
		QVERIFY(monitor->count() > before);

		monitor->setFilter(u"1/1/2"_s);
		QVERIFY(monitor->count() >= 1);
		for (int row = 0; row < monitor->count(); ++row)
			QCOMPARE(model->index(row, C::DestinationColumn).data().toString(), u"1/1/2"_s);
		monitor->clear();
		QCOMPARE(monitor->count(), 0);
	}

	void fr_com_1__programIndividualAddressRecordsStateWithoutUndo()
	{
		QVERIFY(connect());
		m_sim->device("fresh")->setProgMode(true);
		viewmodels::CommissioningVM* bus = m_ws->commissioning();
		m_ws->selection()->select(m_newDevice, u"test"_s);
		QCOMPARE(bus->deviceLabel(), u"1.1.20 New device"_s);
		QVERIFY(m_ws->actions()->isEnabled(u"device.programAddress"_s));
		QSignalSpy started(bus, &viewmodels::CommissioningVM::operationStarted);
		QVERIFY(m_ws->actions()->trigger(u"device.programAddress"_s));
		QCOMPARE(started.count(), 1);
		QVERIFY(bus->isOperationActive());
		QVERIFY(bus->isCancellable());
		QVERIFY(bus->isWaitingForButton());
		QVERIFY(!m_ws->actions()->isEnabled(u"device.readInfo"_s)); // one operation at a time
		QVERIFY(waitFor([bus] { return !bus->isOperationActive(); }, 20000));
		QVERIFY2(bus->lastError().isEmpty(), qPrintable(bus->lastError()));
		QVERIFY(project().deviceState(m_newDevice).iaProgrammed);
		QVERIFY(m_ws->session()->isDirty());
		QVERIFY(!m_ws->session()->canUndo()); // a commissioning fact, not an edit
		QCOMPARE(m_sim->device("fresh")->address(), knx::IndividualAddress(0x1114));
		QVERIFY(bus->lastResult().contains(u"1.1.20"_s));
		QVERIFY(bus->lastSucceeded());
		QVERIFY(bus->log().constFirst().contains(u"1.1.20"_s));
		QVERIFY(bus->deviceStatus().contains(u"Address programmed"_s));
	}

	void fr_com_1__programmingCanBeCancelled()
	{
		QVERIFY(connect());
		viewmodels::CommissioningVM* bus = m_ws->commissioning();
		QVERIFY(bus->programAddress(model::EntityRef(m_newDevice).toString()));
		QVERIFY(waitFor([bus] { return bus->operation().contains(u"programming button"_s); }));
		QVERIFY(m_ws->actions()->trigger(u"device.cancelOperation"_s));
		QVERIFY(waitFor([bus] { return !bus->isOperationActive(); }));
		QVERIFY(!bus->lastError().isEmpty());
		QVERIFY(!project().deviceState(m_newDevice).iaProgrammed);
	}

	void fr_com_4__readDeviceInfoRecordsTheSerial()
	{
		QVERIFY(connect());
		viewmodels::CommissioningVM* bus = m_ws->commissioning();
		QVERIFY(!bus->readDeviceInfo(model::EntityRef(m_ids.house).toString())); // not a device
		QVERIFY(bus->readDeviceInfo(model::EntityRef(m_ids.dimmer).toString())); // 1.1.1 = switch-a
		QVERIFY(waitFor([bus] { return !bus->isOperationActive(); }, 10000));
		QVERIFY2(bus->lastError().isEmpty(), qPrintable(bus->lastError()));
		QVERIFY(bus->lastResult().contains(u"0705"_s));
		QVERIFY(!project().deviceState(m_ids.dimmer).serial.isEmpty());
		QVERIFY(!m_ws->session()->canUndo());
	}

	void fr_com_2__downloadWithoutPlannerExplainsWhy()
	{
		QVERIFY(connect());
		m_ws->setDownloadPlanner({});
		viewmodels::CommissioningVM* bus = m_ws->commissioning();
		QVERIFY(!bus->isPlannerAvailable());
		m_ws->selection()->select(m_ids.dimmer, u"test"_s);
		QVERIFY(!m_ws->actions()->isEnabled(u"device.download"_s)); // no application program
		m_ws->selection()->select(m_systemB, u"test"_s);
		QVERIFY(m_ws->actions()->isEnabled(u"device.download"_s));
		QVERIFY(m_ws->actions()->trigger(u"device.download"_s));
		QVERIFY(!bus->isOperationActive());
		QVERIFY(bus->lastResult().contains(u"not available yet"_s));
		QVERIFY(!bus->lastSucceeded());
	}

	void fr_com_2__downloadRunsThePlanWithProgress()
	{
		QVERIFY(connect());
		viewmodels::CommissioningVM* bus = m_ws->commissioning();
		int planned = 0;
		m_ws->setDownloadPlanner(
			[&planned, device = m_systemB](const model::Project& project, model::Id<model::Device> id) -> Result<download::Plan>
			{
				++planned;
				if (id != device || !project.devices().contains(id))
					return makeError(ErrorCode::NotFound, "wrong device");
				return simplePlan();
			});
		m_ws->selection()->select(m_systemB, u"test"_s);
		QSignalSpy changed(bus, &viewmodels::CommissioningVM::operationChanged);
		QVERIFY(m_ws->actions()->trigger(u"device.download"_s));
		QVERIFY(bus->isOperationActive());
		QCOMPARE(bus->operationKind(), u"download"_s);
		QVERIFY(waitFor([bus] { return !bus->isOperationActive(); }, 30000));
		QVERIFY2(bus->lastSucceeded(), qPrintable(bus->lastError() + bus->lastResult()));
		QCOMPARE(planned, 1);
		QVERIFY(bus->lastResult().contains(u"6 steps"_s));
		QVERIFY(changed.count() > 6); // planning, then one update per step, then the result
		QCOMPARE(project().deviceState(m_systemB).downloadedHash, viewmodels::planHash(simplePlan()));
		QVERIFY(!m_ws->session()->canUndo());
		QVERIFY(m_ws->session()->isDirty());
		QCOMPARE(m_sim->device("systemb")->property(4, download::kPidProgramVersion), std::optional(hex({0xff, 0xff, 0x00, 0x01, 0x10})));
		QVERIFY(bus->deviceStatus().contains(u"Application downloaded"_s));

		// A plan the device refuses fails cleanly; the hash is not touched.
		m_ws->setDownloadPlanner([](const model::Project&, model::Id<model::Device>) -> Result<download::Plan>
			{ return download::Plan{{download::Authorize{}, download::Unload{4, false}, download::LoadCompleted{4}}}; });
		QVERIFY(bus->download());
		QVERIFY(waitFor([bus] { return !bus->isOperationActive(); }, 30000));
		QVERIFY(!bus->lastSucceeded());
		QVERIFY(!bus->lastError().isEmpty());

		// A planner error is shown.
		m_ws->setDownloadPlanner([](const model::Project&, model::Id<model::Device>) -> Result<download::Plan>
			{ return makeError(ErrorCode::Unsupported, "mask MV-0705 is not supported"); });
		QVERIFY(bus->download());
		QVERIFY(waitFor([bus] { return !bus->isOperationActive(); }, 10000));
		QVERIFY(bus->lastError().contains(u"MV-0705"_s));
	}

	void fr_com_2__downloadFromTheCatalogWithAppconfig()
	{
		// The default planner: catalog product database + appconfig::planDeviceDownload.
		QVERIFY(m_ws->catalogPanel()->importFiles({QStringLiteral(KESTREL_TEST_DATA_DIR "/catalog/M-FFFF.knxprod")}));
		QVERIFY(m_ws->catalog()->waitForIdle());
		QVERIFY(connect());
		viewmodels::CommissioningVM* bus = m_ws->commissioning();
		QVERIFY(bus->isPlannerAvailable());
		QVERIFY(bus->download(model::EntityRef(m_systemB).toString()));
		QVERIFY(waitFor([bus] { return !bus->isOperationActive(); }, 60000));
		QVERIFY2(bus->lastSucceeded(), qPrintable(bus->lastError() + u" / "_s + bus->lastResult()));
		QVERIFY(!project().deviceState(m_systemB).downloadedHash.isEmpty());
		QCOMPARE(*m_sim->device("systemb")->loadState(4), std::uint8_t{1}); // loaded
	}

	void fr_com_1__devicesInProgrammingModeAreListed()
	{
		QVERIFY(connect());
		m_sim->device("fresh")->setProgMode(true);
		viewmodels::CommissioningVM* bus = m_ws->commissioning();
		QVERIFY(m_ws->actions()->trigger(u"bus.programmingMode"_s));
		QVERIFY(waitFor([bus] { return !bus->isOperationActive(); }, 10000));
		QVERIFY2(bus->lastResult().contains(u"15.15.255"_s), qPrintable(bus->lastResult()));
	}

	void fr_bus_2__operationsNeedAConnection()
	{
		viewmodels::CommissioningVM* bus = m_ws->commissioning();
		m_ws->selection()->select(m_newDevice, u"test"_s);
		QVERIFY(!m_ws->actions()->isEnabled(u"device.programAddress"_s));
		QVERIFY(!m_ws->actions()->isEnabled(u"device.download"_s));
		QVERIFY(!bus->programAddress(model::EntityRef(m_newDevice).toString()));
		QVERIFY(!bus->download(model::EntityRef(m_systemB).toString()));
		auto monitor = m_ws->createGroupMonitor();
		QVERIFY(!monitor->write(u"1/1/1"_s, u"on"_s));
		QVERIFY(monitor->lastError().contains(u"Not connected"_s));
	}
};

QTEST_MAIN(BusViewModelsTest)
#include "tst_BusViewModels.moc"
