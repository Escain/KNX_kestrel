// bus::UdpTunnelLink against simnet::Simulation over loopback UDP (NFR-5: no hardware).

#include "kestrel/bus/UdpTunnelLink.hpp"
#include "kestrel/knx/Apdu.hpp"
#include "kestrel/knx/IaProgrammer.hpp"
#include "kestrel/simnet/ScenarioLoader.hpp"
#include "kestrel/simnet/Simulation.hpp"

#include <QSignalSpy>
#include <QDeadlineTimer>
#include <QTest>

#include "QtWait.hpp"
#include <QTimer>
#include <QUdpSocket>

#include <functional>

#include <memory>

using namespace kestrel;
using namespace Qt::StringLiterals;
using kestrel::test::waitFor;
using namespace std::chrono_literals;

namespace
{


const knx::GroupAddress kSwitch(0x0901);      // 1/1/1
const knx::GroupAddress kTemperature(0x0902); // 1/1/2

const QByteArray kScenario = R"({
	"interfaceAddress": "1.1.255",
	"devices": [
		{ "name": "switch-a", "address": "1.1.1", "serial": "FFFF:00001234",
		  "groupObjects": [ { "ga": "1/1/1", "flags": "CWTU", "dpt": "1.001", "value": "00" },
		                    { "ga": "1/1/2", "flags": "CRT", "dpt": "9.001", "value": "0c 33" } ] },
		{ "name": "fresh", "address": "15.15.255", "serial": "FFFF:00005678" } ] })";

Bytes bytes(std::initializer_list<std::uint8_t> values)
{
	return Bytes(values);
}

} // namespace

class TestUdpTunnelLink : public QObject
{
	Q_OBJECT

	std::unique_ptr<simnet::Simulation> m_sim;
	std::unique_ptr<bus::UdpTunnelLink> m_link;

	void connectLink()
	{
		m_link = std::make_unique<bus::UdpTunnelLink>();
		QVERIFY(m_link->connectTo(QHostAddress::LocalHost, m_sim->port()).has_value());
		QCOMPARE(m_link->state(), knx::LinkState::Connecting);
		QVERIFY(waitFor([&] { return m_link->state() == knx::LinkState::Connected; }));
	}

private Q_SLOTS:
	void init()
	{
		auto scenario = simnet::ScenarioLoader::fromJson(kScenario);
		QVERIFY(scenario.has_value());
		m_sim = std::make_unique<simnet::Simulation>(*scenario);
		QVERIFY(m_sim->listen(0).has_value());
		QVERIFY(m_sim->port() != 0);
	}

	void cleanup()
	{
		m_link.reset();
		m_sim.reset();
	}

	void fr_bus_2__tunnelConnects()
	{
		m_link = std::make_unique<bus::UdpTunnelLink>();
		QSignalSpy stateSpy(m_link.get(), &bus::UdpTunnelLink::stateChanged);
		QVERIFY(m_link->connectTo(QHostAddress::LocalHost, m_sim->port()).has_value());
		QVERIFY(waitFor([&] { return m_link->state() == knx::LinkState::Connected; }));
		QCOMPARE(m_link->address(), knx::IndividualAddress(0x11FF));
		QCOMPARE(m_sim->server().connectionCount(), std::size_t{1});
		QCOMPARE(stateSpy.size(), 2);
		QCOMPARE(stateSpy.at(0).at(0).value<knx::LinkState>(), knx::LinkState::Connecting);
		QCOMPARE(stateSpy.at(1).at(0).value<knx::LinkState>(), knx::LinkState::Connected);

		// A second connect while connected is refused.
		QVERIFY(!m_link->connectTo(QHostAddress::LocalHost, m_sim->port()).has_value());

		m_link->disconnectFromInterface();
		QCOMPARE(m_link->state(), knx::LinkState::Disconnected);
		QVERIFY(waitFor([&] { return m_sim->server().connectionCount() == std::size_t{0}; }));
		QVERIFY(m_link->lastError().isEmpty());
	}

	void fr_bus_2__connectFailsWhenNobodyListens()
	{
		// Grab a free port, then release it: nothing listens there any more.
		quint16 port = 0;
		{
			QUdpSocket probe;
			QVERIFY(probe.bind(QHostAddress::LocalHost, 0));
			port = probe.localPort();
		}
		knx::TunnelClientConfig config;
		config.connectTimeout = 1s;
		bus::UdpTunnelLink link(config);
		QSignalSpy lost(&link, &bus::UdpTunnelLink::connectionLost);
		QVERIFY(link.connectTo(QHostAddress::LocalHost, port).has_value());
		QVERIFY(waitFor([&] { return link.state() == knx::LinkState::Disconnected; }, 3000));
		QCOMPARE(lost.size(), 1);
		QVERIFY(!link.lastError().isEmpty());
	}

	void fr_bus_2__reconnectsAfterDisconnect()
	{
		connectLink();
		m_link->disconnectFromInterface();
		QVERIFY(waitFor([&] { return m_sim->server().connectionCount() == std::size_t{0}; }));
		QVERIFY(m_link->connectTo(QHostAddress::LocalHost, m_sim->port()).has_value());
		QVERIFY(waitFor([&] { return m_link->state() == knx::LinkState::Connected; }));
		QCOMPARE(m_sim->server().connectionCount(), std::size_t{1});
	}

	void fr_bus_6__groupWriteReachesDevice()
	{
		connectLink();
		QSignalSpy confirmed(m_link.get(), &bus::UdpTunnelLink::sendConfirmed);
		QVERIFY(m_link->send(knx::makeGroupFrame(kSwitch, knx::groupValueWriteShort(1))).has_value());
		QVERIFY(waitFor([&] { return confirmed.size() == 1; }));
		QCOMPARE(confirmed.at(0).at(0).toBool(), true);
		QCOMPARE(*m_sim->device("switch-a")->groupValue(kSwitch), bytes({0x01}));
		// The tunnel's address replaced the 0.0.0 source.
		QCOMPARE(m_sim->bus().telegrams().back().frame.source, std::uint16_t{0x11FF});
	}

	void fr_bus_6__sendFailsWhenDisconnected()
	{
		bus::UdpTunnelLink link;
		QVERIFY(!link.send(knx::makeGroupFrame(kSwitch, knx::groupValueWriteShort(1))).has_value());
	}

	void fr_bus_6__groupReadGetsResponse()
	{
		connectLink();
		QSignalSpy frames(m_link.get(), &bus::UdpTunnelLink::frameReceived);
		QVERIFY(m_link->send(knx::makeGroupFrame(kTemperature, knx::groupValueRead())).has_value());
		QVERIFY(waitFor([&] { return frames.size() == 1; }));
		const auto frame = frames.at(0).at(0).value<knx::cemi::LData>();
		QCOMPARE(frame.sourceAddress(), knx::IndividualAddress(0x1101));
		QCOMPARE(frame.groupAddress(), kTemperature);
		auto tpdu = knx::decodeTpdu(frame);
		QVERIFY(tpdu.has_value());
		QCOMPARE(tpdu->apdu.apci, knx::Apci::GroupValueResponse);
		QCOMPARE(tpdu->apdu.groupValue(), bytes({0x0C, 0x33}));
	}

	void fr_bus_5__deviceTelegramReachesClient()
	{
		connectLink();
		std::vector<knx::cemi::LData> received;
		m_link->onFrame = [&received](const knx::cemi::LData& frame) { received.push_back(frame); };
		// The device application changes a value outside any datagram: the server flushes on its own.
		QVERIFY(m_sim->device("switch-a")->setGroupValue(kSwitch, bytes({0x01})).has_value());
		QVERIFY(waitFor([&] { return received.size() == std::size_t{1}; }));
		QCOMPARE(received[0].groupAddress(), kSwitch);
		QCOMPARE(knx::decodeTpdu(received[0])->apdu, knx::groupValueWriteShort(1));
	}

	void fr_com_1__programIndividualAddressEndToEnd()
	{
		connectLink();
		sim::VirtualDevice* fresh = m_sim->device("fresh");
		fresh->pressProgButton();

		knx::IaProgrammerConfig config;
		config.readInterval = 300ms;
		config.progModeTimeout = 5s;
		knx::IaProgrammer programmer(config);
		std::optional<knx::IaProgrammer::Finished> finished;
		const auto pump = [&]
		{
			for (auto& action : programmer.takeActions())
			{
				if (auto* send = std::get_if<knx::IaProgrammer::SendFrame>(&action))
				{
					QVERIFY(m_link->send(send->frame).has_value());
				}
				else if (auto* done = std::get_if<knx::IaProgrammer::Finished>(&action))
				{
					finished = *done;
				}
			}
		};
		m_link->onFrame = [&](const knx::cemi::LData& frame)
		{
			programmer.onFrame(frame, knx::Clock::now());
			pump();
		};
		QTimer ticker;
		QObject::connect(&ticker, &QTimer::timeout, [&] { programmer.tick(knx::Clock::now()); pump(); });
		ticker.start(50);

		programmer.start(knx::IndividualAddress(0x1114), knx::Clock::now()); // 1.1.20
		pump();
		QVERIFY(waitFor([&] { return finished.has_value(); }, 8000));
		QCOMPARE(finished->failure, knx::IaProgrammerFailure::None);
		QCOMPARE(finished->previousAddress, knx::IndividualAddress(0xFFFF));
		QCOMPARE(fresh->address(), knx::IndividualAddress(0x1114));
		QVERIFY(!fresh->progMode());
		QVERIFY(waitFor([&] { return fresh->restartCount() == std::uint32_t{1}; }));
	}
};

QTEST_GUILESS_MAIN(TestUdpTunnelLink)
#include "tst_UdpTunnelLink.moc"
