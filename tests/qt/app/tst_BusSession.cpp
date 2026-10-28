// app::BusSession (bus thread + UdpTunnelLink) against an in-process simnet::Simulation over loopback.

#include "kestrel/app/BusSession.hpp"
#include "kestrel/simnet/ScenarioLoader.hpp"
#include "kestrel/simnet/Simulation.hpp"

#include <QDeadlineTimer>
#include <QSignalSpy>
#include <QTest>

#include "QtWait.hpp"
#include <QUdpSocket>

#include <functional>
#include <memory>
#include <optional>

using namespace kestrel;
using namespace Qt::StringLiterals;
using kestrel::test::waitFor;
using namespace std::chrono_literals;

namespace
{


const knx::GroupAddress kSwitch(0x0901);      // 1/1/1
const knx::GroupAddress kTemperature(0x0902); // 1/1/2
const dpt::DptId kSwitchDpt{1, 1};
const dpt::DptId kTemperatureDpt{9, 1};

const QByteArray kScenario = R"({
	"interfaceAddress": "1.1.255",
	"devices": [
		{ "name": "switch-a", "address": "1.1.1", "serial": "FFFF:00001234", "maskVersion": "0705",
		  "groupObjects": [ { "ga": "1/1/1", "flags": "CWTU", "dpt": "1.001", "value": "00" },
		                    { "ga": "1/1/2", "flags": "CRT", "dpt": "9.001", "value": "0c 33" } ] },
		{ "name": "fresh", "address": "15.15.255", "serial": "FFFF:00005678" },
		{ "name": "other", "address": "15.15.254", "serial": "FFFF:00009999" },
		{ "name": "twin", "address": "15.15.255", "serial": "FFFF:00007777" } ] })";

template <class T>
struct Outcome
{
	std::optional<T> value;
	auto callback()
	{
		return [this](T result) { value = std::move(result); };
	}
	bool wait(int timeoutMs = 5000)
	{
		return waitFor([this] { return value.has_value(); }, timeoutMs);
	}
};

} // namespace

class TestBusSession : public QObject
{
	Q_OBJECT

	std::unique_ptr<simnet::Simulation> m_sim;
	std::unique_ptr<app::BusSession> m_session;

	bool connectSession()
	{
		Outcome<Status> connected;
		m_session->connectToInterface(u"127.0.0.1"_s, m_sim->port(), connected.callback());
		return connected.wait() && connected.value->has_value() && m_session->isConnected();
	}

private Q_SLOTS:
	void init()
	{
		auto scenario = simnet::ScenarioLoader::fromJson(kScenario);
		QVERIFY(scenario.has_value());
		m_sim = std::make_unique<simnet::Simulation>(*scenario);
		QVERIFY(m_sim->listen(0).has_value());
		m_session = std::make_unique<app::BusSession>();
	}

	void cleanup()
	{
		m_session.reset();
		m_sim.reset();
	}

	void fr_bus_2__sessionConnectsAndDisconnects()
	{
		QSignalSpy states(m_session.get(), &app::BusSession::stateChanged);
		QVERIFY(connectSession());
		QCOMPARE(m_session->state(), knx::LinkState::Connected);
		QCOMPARE(m_session->localAddress(), knx::IndividualAddress(0x11FF));
		QCOMPARE(states.size(), 2);
		QCOMPARE(states.at(0).at(0).value<knx::LinkState>(), knx::LinkState::Connecting);
		QCOMPARE(m_sim->server().connectionCount(), std::size_t{1});

		// Connecting twice is a conflict.
		Outcome<Status> again;
		m_session->connectToInterface(u"127.0.0.1"_s, m_sim->port(), again.callback());
		QVERIFY(again.wait());
		QCOMPARE(again.value->error().code, ErrorCode::Conflict);

		m_session->disconnectFromInterface();
		QVERIFY(waitFor([&] { return m_session->state() == knx::LinkState::Disconnected; }));
		QVERIFY(waitFor([&] { return m_sim->server().connectionCount() == 0; }));
	}

	void fr_bus_2__connectFailureIsReported()
	{
		quint16 port = 0;
		{
			QUdpSocket probe;
			QVERIFY(probe.bind(QHostAddress::LocalHost, 0));
			port = probe.localPort();
		}
		QSignalSpy lost(m_session.get(), &app::BusSession::connectionLost);
		Outcome<Status> connected;
		m_session->connectToInterface(u"127.0.0.1"_s, port, connected.callback());
		QVERIFY(connected.wait(12000));
		QVERIFY(!connected.value->has_value());
		QCOMPARE(connected.value->error().code, ErrorCode::IoError);
		QVERIFY(waitFor([&] { return lost.size() == 1; }));
	}

	void fr_bus_2__destroyingSessionDisconnects()
	{
		QVERIFY(connectSession());
		m_session.reset();
		QVERIFY(waitFor([&] { return m_sim->server().connectionCount() == 0; }));
	}

	void fr_bus_6__groupWriteChangesDeviceValue()
	{
		QVERIFY(connectSession());
		Outcome<Status> written;
		m_session->groupWrite(kSwitch, kSwitchDpt, true, written.callback());
		QVERIFY(written.wait());
		QVERIFY(written.value->has_value());
		QCOMPARE(*m_sim->device("switch-a")->groupValue(kSwitch), Bytes{0x01});
	}

	void fr_bus_6__groupWriteRejectsBadValue()
	{
		QVERIFY(connectSession());
		Outcome<Status> written;
		m_session->groupWrite(kTemperature, dpt::DptId{5, 1}, 150.0, written.callback());
		QVERIFY(written.wait());
		QCOMPARE(written.value->error().code, ErrorCode::InvalidArgument);
	}

	void fr_bus_6__groupReadDecodesResponse()
	{
		QVERIFY(connectSession());
		Outcome<Result<app::GroupReadResult>> read;
		m_session->groupRead(kTemperature, kTemperatureDpt, read.callback());
		QVERIFY(read.wait());
		QVERIFY2(read.value->has_value(), read.value->has_value() ? "" : read.value->error().message.c_str());
		const app::GroupReadResult& result = **read.value;
		QCOMPARE(result.responder, knx::IndividualAddress(0x1101));
		QCOMPARE(result.raw, QByteArray::fromHex("0c33"));
		QCOMPARE(std::get<double>(result.value), 21.5);
		QVERIFY2(result.text.startsWith(u"21.5"_s), qPrintable(result.text));
	}

	void fr_bus_6__groupReadTimesOut()
	{
		QVERIFY(connectSession());
		Outcome<Result<app::GroupReadResult>> read;
		m_session->groupRead(kSwitch, kSwitchDpt, read.callback(), 300ms); // no R flag: no answer
		QVERIFY(read.wait());
		QCOMPARE(read.value->error().code, ErrorCode::Timeout);
	}

	void fr_bus_6__requestsFailWhenNotConnected()
	{
		Outcome<Status> written;
		m_session->groupWrite(kSwitch, kSwitchDpt, true, written.callback());
		QVERIFY(written.wait());
		QCOMPARE(written.value->error().code, ErrorCode::IoError);
		Outcome<Result<app::DeviceInfo>> info;
		m_session->readDeviceInfo(knx::IndividualAddress(0x1101), info.callback());
		QVERIFY(info.wait());
		QCOMPARE(info.value->error().code, ErrorCode::IoError);
	}

	void fr_com_1__programsIndividualAddress()
	{
		QVERIFY(connectSession());
		m_sim->device("fresh")->pressProgButton();
		std::vector<knx::IaProgrammerState> progress;
		Outcome<Result<knx::IndividualAddress>> programmed;
		m_session->programIndividualAddress(knx::IndividualAddress(0x1114), {10s, false}, programmed.callback(),
			[&progress](knx::IaProgrammerState state) { progress.push_back(state); });
		QVERIFY(programmed.wait(15000));
		QVERIFY2(programmed.value->has_value(),
			programmed.value->has_value() ? "" : programmed.value->error().message.c_str());
		QCOMPARE(**programmed.value, knx::IndividualAddress(0xFFFF));
		QCOMPARE(m_sim->device("fresh")->address(), knx::IndividualAddress(0x1114));
		QVERIFY(!m_sim->device("fresh")->progMode());
		QVERIFY(!progress.empty());
		QCOMPARE(progress.front(), knx::IaProgrammerState::WaitProgMode);
		QCOMPARE(m_sim->device("twin")->address(), knx::IndividualAddress(0xFFFF));
	}

	void fr_com_1__refusesAddressInUse()
	{
		QVERIFY(connectSession());
		m_sim->device("fresh")->pressProgButton();
		Outcome<Result<knx::IndividualAddress>> programmed;
		m_session->programIndividualAddress(knx::IndividualAddress(0x1101), {10s, false}, programmed.callback());
		QVERIFY(programmed.wait(15000));
		QVERIFY(!programmed.value->has_value());
		QCOMPARE(programmed.value->error().code, ErrorCode::Conflict);
		QCOMPARE(m_sim->device("fresh")->address(), knx::IndividualAddress(0xFFFF));
	}

	void fr_com_1__refusesSeveralDevicesInProgMode()
	{
		QVERIFY(connectSession());
		m_sim->device("fresh")->pressProgButton();
		m_sim->device("other")->pressProgButton();
		Outcome<Result<knx::IndividualAddress>> programmed;
		m_session->programIndividualAddress(knx::IndividualAddress(0x1114), {10s, false}, programmed.callback());
		QVERIFY(programmed.wait(15000));
		QVERIFY(!programmed.value->has_value());
		QCOMPARE(programmed.value->error().code, ErrorCode::Conflict);
	}

	void fr_com_1__refusesTwoFactoryDefaultDevicesInProgMode()
	{
		// Both devices answer from 15.15.255: IaProgrammer counts responses, not addresses, so it refuses.
		QVERIFY(connectSession());
		m_sim->device("fresh")->pressProgButton();
		m_sim->device("twin")->pressProgButton();
		Outcome<Result<knx::IndividualAddress>> programmed;
		m_session->programIndividualAddress(knx::IndividualAddress(0x1114), {10s, false}, programmed.callback());
		QVERIFY(programmed.wait(15000));
		QCOMPARE(programmed.value->has_value(), false);
	}

	void fr_com_1__cancelProgramming()
	{
		QVERIFY(connectSession());
		Outcome<Result<knx::IndividualAddress>> programmed;
		m_session->programIndividualAddress(knx::IndividualAddress(0x1114), {30s, false}, programmed.callback());
		QTest::qWait(200);
		m_session->cancelProgramming();
		QVERIFY(programmed.wait());
		QCOMPARE(programmed.value->error().code, ErrorCode::Cancelled);
	}

	void fr_com_1__timesOutWithoutDeviceInProgMode()
	{
		QVERIFY(connectSession());
		Outcome<Result<knx::IndividualAddress>> programmed;
		m_session->programIndividualAddress(knx::IndividualAddress(0x1114), {1500ms, false}, programmed.callback());
		QVERIFY(programmed.wait(8000));
		QCOMPARE(programmed.value->error().code, ErrorCode::Timeout);
	}

	void fr_com_4__readsDeviceInfo()
	{
		QVERIFY(connectSession());
		Outcome<Result<app::DeviceInfo>> info;
		m_session->readDeviceInfo(knx::IndividualAddress(0x1101), info.callback());
		QVERIFY(info.wait(15000));
		QVERIFY2(info.value->has_value(), info.value->has_value() ? "" : info.value->error().message.c_str());
		const app::DeviceInfo& device = **info.value;
		QCOMPARE(device.address, knx::IndividualAddress(0x1101));
		QCOMPARE(device.maskVersion, std::uint16_t{0x0705});
		QCOMPARE(device.serial.toString(), std::string("ffff:00001234"));
		QCOMPARE(device.manufacturerId, std::uint16_t{0xFFFF});
		// The transport connection is closed afterwards.
		QVERIFY(waitFor([&] { return !m_sim->device("switch-a")->connectionPartner().has_value(); }));
	}

	void fr_com_4__deviceInfoOfMissingDeviceFails()
	{
		QVERIFY(connectSession());
		Outcome<Result<app::DeviceInfo>> info;
		m_session->readDeviceInfo(knx::IndividualAddress(0x1163), info.callback()); // 1.1.99: nobody
		QVERIFY(info.wait(30000));
		QVERIFY(!info.value->has_value());
	}

	void fr_bus_5__monitorDeliversBatches()
	{
		QVERIFY(connectSession());
		QSignalSpy available(m_session.get(), &app::BusSession::telegramsAvailable);
		m_session->setGroupAddressDpt(kSwitch, kSwitchDpt);
		sim::VirtualDevice* device = m_sim->device("switch-a");
		QVERIFY(device->setGroupValue(kSwitch, Bytes{0x01}).has_value());
		QVERIFY(device->setGroupValue(kSwitch, Bytes{0x00}).has_value());
		QVERIFY(device->setGroupValue(kSwitch, Bytes{0x01}).has_value());

		QList<app::TelegramRecord> records;
		QVERIFY(waitFor(
			[&]
			{
				records += m_session->takeTelegrams();
				return records.size() >= 3;
			}));
		QVERIFY(available.size() >= 1);
		QVERIFY(available.size() <= 3); // coalesced, not one signal per telegram guaranteed
		const app::TelegramRecord& first = records.first();
		QCOMPARE(first.source, knx::IndividualAddress(0x1101));
		QCOMPARE(first.destinationText(), u"1/1/1"_s);
		QVERIFY(first.isGroup);
		QVERIFY(!first.outgoing);
		QCOMPARE(first.apci, std::optional<knx::Apci>(knx::Apci::GroupValueWrite));
		QCOMPARE(first.serviceText(), u"GroupValueWrite"_s);
		QCOMPARE(first.payload, QByteArray::fromHex("01"));
		QVERIFY(!first.decoded.isEmpty());
		QVERIFY(first.timestamp.isValid());
		QVERIFY(records[1].decoded != first.decoded);
	}

	void fr_bus_5__monitorRecordsOwnFrames()
	{
		QVERIFY(connectSession());
		Outcome<Status> written;
		m_session->groupWrite(kSwitch, kSwitchDpt, true, written.callback());
		QVERIFY(written.wait());
		QList<app::TelegramRecord> records;
		QVERIFY(waitFor(
			[&]
			{
				records += m_session->takeTelegrams();
				return !records.isEmpty();
			}));
		QVERIFY(records.first().outgoing);
		QCOMPARE(records.first().source, knx::IndividualAddress(0x11FF));
	}

	void fr_bus_5__monitorBufferIsBounded()
	{
		QVERIFY(connectSession());
		m_session->setMonitorCapacity(2);
		sim::VirtualDevice* device = m_sim->device("switch-a");
		for (int i = 0; i < 5; ++i)
		{
			QVERIFY(device->setGroupValue(kSwitch, Bytes{static_cast<std::uint8_t>(i & 1)}).has_value());
		}
		QVERIFY(waitFor([&] { return m_session->droppedTelegrams() == 3; }));
		QCOMPARE(m_session->takeTelegrams().size(), 2);
	}

	void fr_bus_1__discoversThroughSession()
	{
		Outcome<Result<QList<bus::DiscoveredInterface>>> found;
		app::BusSession::DiscoveryOptions options;
		options.timeout = 500ms;
		options.multicast = false;
		options.unicastHost = u"127.0.0.1"_s;
		options.unicastPort = m_sim->port();
		m_session->discover(options, found.callback());
		QVERIFY(found.wait());
		QVERIFY(found.value->has_value());
		QCOMPARE((*found.value)->size(), 1);
		QCOMPARE((*found.value)->first().port, m_sim->port());
	}
};

QTEST_GUILESS_MAIN(TestBusSession)
#include "tst_BusSession.moc"
