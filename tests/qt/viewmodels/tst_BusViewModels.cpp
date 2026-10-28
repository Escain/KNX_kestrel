// Bus view models against an in-process simnet::Simulation over loopback (NFR-5): connection (FR-BUS-2),
// group monitor and group read/write (FR-BUS-5/6), IA programming (FR-COM-1), device info (FR-COM-4).

#include "SampleProject.hpp"

#include "kestrel/app/BusSession.hpp"
#include "kestrel/model/Operations.hpp"
#include "kestrel/simnet/ScenarioLoader.hpp"
#include "kestrel/simnet/Simulation.hpp"
#include "kestrel/viewmodels/ActionRegistry.hpp"
#include "kestrel/viewmodels/BusConnectionVM.hpp"
#include "kestrel/viewmodels/GroupMonitorVM.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"
#include "kestrel/viewmodels/SelectionService.hpp"
#include "kestrel/viewmodels/TelegramModel.hpp"
#include "kestrel/viewmodels/Workspace.hpp"

#include "QtWait.hpp"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QStandardPaths>
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
		{ "name": "fresh", "address": "15.15.255", "serial": "FFFF:00005678" } ] })";

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
	std::unique_ptr<viewmodels::Workspace> m_ws;
	testing::SampleIds m_ids;
	model::Id<model::Device> m_newDevice;

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

		m_ws = std::make_unique<viewmodels::Workspace>();
		model::Project p(model::IdGenerator::sequential());
		m_ids = testing::buildSampleProject(p);
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
		viewmodels::BusConnectionVM* bus = m_ws->busConnection();
		m_ws->selection()->select(m_newDevice, u"test"_s);
		QVERIFY(m_ws->actions()->isEnabled(u"device.programAddress"_s));
		QVERIFY(m_ws->actions()->trigger(u"device.programAddress"_s));
		QVERIFY(bus->isOperationActive());
		QVERIFY(bus->isCancellable());
		QVERIFY(!m_ws->actions()->isEnabled(u"device.readInfo"_s)); // one operation at a time
		QVERIFY(waitFor([bus] { return !bus->isOperationActive(); }, 20000));
		QVERIFY2(bus->lastError().isEmpty(), qPrintable(bus->lastError()));
		QVERIFY(project().deviceState(m_newDevice).iaProgrammed);
		QVERIFY(m_ws->session()->isDirty());
		QVERIFY(!m_ws->session()->canUndo()); // a commissioning fact, not an edit
		QCOMPARE(m_sim->device("fresh")->address(), knx::IndividualAddress(0x1114));
		QVERIFY(bus->lastResult().contains(u"1.1.20"_s));
	}

	void fr_com_1__programmingCanBeCancelled()
	{
		QVERIFY(connect());
		viewmodels::BusConnectionVM* bus = m_ws->busConnection();
		QVERIFY(bus->programAddress(model::EntityRef(m_newDevice).toString()));
		QVERIFY(waitFor([bus] { return bus->operation().contains(u"programming button"_s); }));
		QVERIFY(m_ws->actions()->trigger(u"device.cancelProgramming"_s));
		QVERIFY(waitFor([bus] { return !bus->isOperationActive(); }));
		QVERIFY(!bus->lastError().isEmpty());
		QVERIFY(!project().deviceState(m_newDevice).iaProgrammed);
	}

	void fr_com_4__readDeviceInfoRecordsTheSerial()
	{
		QVERIFY(connect());
		viewmodels::BusConnectionVM* bus = m_ws->busConnection();
		QVERIFY(!bus->readDeviceInfo(model::EntityRef(m_ids.house).toString())); // not a device
		QVERIFY(bus->readDeviceInfo(model::EntityRef(m_ids.dimmer).toString())); // 1.1.1 = switch-a
		QVERIFY(waitFor([bus] { return !bus->isOperationActive(); }, 10000));
		QVERIFY2(bus->lastError().isEmpty(), qPrintable(bus->lastError()));
		QVERIFY(bus->lastResult().contains(u"0705"_s));
		QVERIFY(!project().deviceState(m_ids.dimmer).serial.isEmpty());
		QVERIFY(!m_ws->session()->canUndo());
	}

	void fr_bus_2__operationsNeedAConnection()
	{
		viewmodels::BusConnectionVM* bus = m_ws->busConnection();
		m_ws->selection()->select(m_newDevice, u"test"_s);
		QVERIFY(!m_ws->actions()->isEnabled(u"device.programAddress"_s));
		QVERIFY(!bus->programAddress(model::EntityRef(m_newDevice).toString()));
		auto monitor = m_ws->createGroupMonitor();
		QVERIFY(!monitor->write(u"1/1/1"_s, u"on"_s));
		QVERIFY(monitor->lastError().contains(u"Not connected"_s));
	}
};

QTEST_MAIN(BusViewModelsTest)
#include "tst_BusViewModels.moc"
