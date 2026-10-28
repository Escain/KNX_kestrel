// bus::Discovery against a simnet::KnxIpServer on loopback (FR-BUS-1).

#include "kestrel/bus/Discovery.hpp"
#include "kestrel/simnet/ScenarioLoader.hpp"
#include "kestrel/simnet/Simulation.hpp"

#include <QSignalSpy>
#include <QTest>

using namespace kestrel;
using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

class TestDiscovery : public QObject
{
	Q_OBJECT

private Q_SLOTS:
	void fr_bus_1__discoversUnicastTarget()
	{
		simnet::Scenario scenario;
		scenario.friendlyName = "Test interface";
		scenario.serial = *knx::SerialNumber::parse("FFFF:00000042");
		simnet::Simulation sim(scenario);
		QVERIFY(sim.listen(0).has_value());

		bus::Discovery discovery;
		discovery.setMulticastEnabled(false);
		discovery.addUnicastTarget(QHostAddress::LocalHost, sim.port());
		discovery.setTimeout(500ms);
		QSignalSpy found(&discovery, &bus::Discovery::interfaceFound);
		QSignalSpy finished(&discovery, &bus::Discovery::finished);
		QVERIFY(discovery.start().has_value());
		QVERIFY(discovery.isRunning());
		QVERIFY(!discovery.start().has_value()); // already running

		QVERIFY(finished.wait(3000));
		QVERIFY(!discovery.isRunning());
		QCOMPARE(found.size(), 1);
		const auto results = finished.at(0).at(0).value<QList<bus::DiscoveredInterface>>();
		QCOMPARE(results.size(), 1);
		const bus::DiscoveredInterface& entry = results.first();
		QCOMPARE(entry.host, QHostAddress(QHostAddress::LocalHost));
		QCOMPARE(entry.port, sim.port());
		QCOMPARE(entry.friendlyName(), u"Test interface"_s);
		QCOMPARE(entry.info.address, knx::IndividualAddress(0x11FF));
		QCOMPARE(entry.info.serial.toString(), std::string("ffff:00000042"));
		QVERIFY(entry.supportsTunnelling());
		QVERIFY(!entry.supportsRouting());
	}

	void fr_bus_1__duplicateResponsesAreMerged()
	{
		simnet::Simulation sim(simnet::Scenario{});
		QVERIFY(sim.listen(0).has_value());
		bus::Discovery discovery;
		discovery.setMulticastEnabled(false);
		discovery.addUnicastTarget(QHostAddress::LocalHost, sim.port());
		discovery.addUnicastTarget(QHostAddress::LocalHost, sim.port());
		discovery.setTimeout(500ms);
		QSignalSpy finished(&discovery, &bus::Discovery::finished);
		QVERIFY(discovery.start().has_value());
		QVERIFY(finished.wait(3000));
		QCOMPARE(discovery.results().size(), 1);
	}

	void fr_bus_1__multicastSearchDoesNotBreakUnicast()
	{
		// Multicast reachability depends on the host; the unicast answer must be found regardless.
		simnet::Simulation sim(simnet::Scenario{});
		QVERIFY(sim.listen(0).has_value());
		bus::Discovery discovery;
		discovery.addUnicastTarget(QHostAddress::LocalHost, sim.port());
		discovery.setTimeout(500ms);
		QSignalSpy finished(&discovery, &bus::Discovery::finished);
		QVERIFY(discovery.start().has_value());
		QVERIFY(finished.wait(3000));
		const auto& results = discovery.results();
		QVERIFY(std::any_of(results.begin(), results.end(),
			[&](const bus::DiscoveredInterface& r) { return r.port == sim.port() && r.host.isLoopback(); }));
	}

	void fr_bus_1__nothingFoundFinishesEmpty()
	{
		bus::Discovery discovery;
		discovery.setMulticastEnabled(false);
		discovery.addUnicastTarget(QHostAddress::LocalHost, 9); // discard port: nobody answers
		discovery.setTimeout(200ms);
		QSignalSpy finished(&discovery, &bus::Discovery::finished);
		QVERIFY(discovery.start().has_value());
		QVERIFY(finished.wait(3000));
		QVERIFY(discovery.results().isEmpty());
	}
};

QTEST_GUILESS_MAIN(TestDiscovery)
#include "tst_Discovery.moc"
