// simnet::KnxIpServer / Simulation, driven with raw KNXnet/IP datagrams on loopback.

#include "kestrel/knx/KnxIp.hpp"
#include "kestrel/simnet/KnxIpServer.hpp"
#include "kestrel/simnet/Simulation.hpp"

#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QDeadlineTimer>
#include <QTest>

#include "QtWait.hpp"
#include <QUdpSocket>

#include <functional>

using namespace kestrel;
using namespace Qt::StringLiterals;
using kestrel::test::waitFor;
namespace knxip = knx::knxip;

namespace
{


QByteArray datagram(const knxip::Frame& frame)
{
	const Bytes bytes = knxip::encode(frame);
	return QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
}

std::optional<knxip::Frame> receive(QUdpSocket& socket)
{
	if (!socket.hasPendingDatagrams() && !socket.waitForReadyRead(2000))
	{
		return std::nullopt;
	}
	const QByteArray data = socket.receiveDatagram().data();
	auto frame = knxip::decode(
		ByteView(reinterpret_cast<const std::uint8_t*>(data.constData()), static_cast<std::size_t>(data.size())));
	if (!frame)
	{
		return std::nullopt;
	}
	return *frame;
}

} // namespace

class TestKnxIpServer : public QObject
{
	Q_OBJECT

private Q_SLOTS:
	void nfr_5__listensOnEphemeralLoopbackPort()
	{
		sim::VirtualBus bus;
		simnet::KnxIpServer server(bus);
		QVERIFY(!server.isListening());
		QVERIFY(server.listen(0).has_value());
		QVERIFY(server.isListening());
		QVERIFY(server.port() != 0);
		QCOMPARE(server.address(), QHostAddress(QHostAddress::LocalHost));
		QVERIFY(!server.listen(0).has_value()); // already listening

		// The same port cannot be bound twice.
		sim::VirtualBus otherBus;
		simnet::KnxIpServer other(otherBus);
		const auto status = other.listen(server.port());
		QVERIFY(!status.has_value());
		QCOMPARE(status.error().code, ErrorCode::IoError);
	}

	void fr_bus_1__answersUnicastSearch()
	{
		sim::VirtualBus bus;
		simnet::KnxIpServer server(bus);
		QVERIFY(server.listen(0).has_value());

				QUdpSocket client;
		QVERIFY(client.bind(QHostAddress::LocalHost, 0));
		client.writeDatagram(datagram(knxip::SearchRequest{knxip::Hpai::nat()}), QHostAddress::LocalHost, server.port());
		QVERIFY(waitFor([&] { return client.hasPendingDatagrams(); }));
		auto frame = receive(client);
		QVERIFY(frame.has_value());
		const auto* response = std::get_if<knxip::SearchResponse>(&*frame);
		QVERIFY(response);
		// The announced control endpoint is the bound socket, not the default 3671.
		QCOMPARE(response->controlEndpoint.endpoint.port, server.port());
		QCOMPARE(response->controlEndpoint.endpoint.address, (std::array<std::uint8_t, 4>{127, 0, 0, 1}));
	}

	void fr_bus_2__connectAndDisconnectEmitSignals()
	{
		sim::VirtualBus bus;
		simnet::KnxIpServer server(bus);
		QVERIFY(server.listen(0).has_value());
		QSignalSpy connected(&server, &simnet::KnxIpServer::clientConnected);
		QSignalSpy disconnected(&server, &simnet::KnxIpServer::clientDisconnected);

		QUdpSocket client;
		QVERIFY(client.bind(QHostAddress::LocalHost, 0));
		client.writeDatagram(datagram(knxip::ConnectRequest{knxip::Hpai::nat(), knxip::Hpai::nat(), knxip::Cri{}}),
			QHostAddress::LocalHost, server.port());
		QVERIFY(waitFor([&] { return connected.size() == 1; }));
		QCOMPARE(connected.at(0).at(1).toString(), u"1.1.255"_s);
		QCOMPARE(server.connectionCount(), std::size_t{1});
		const auto channel = static_cast<std::uint8_t>(connected.at(0).at(0).toUInt());

		client.writeDatagram(datagram(knxip::DisconnectRequest{channel, knxip::Hpai::nat()}), QHostAddress::LocalHost,
			server.port());
		QVERIFY(waitFor([&] { return disconnected.size() == 1; }));
		QCOMPARE(server.connectionCount(), std::size_t{0});
	}

	void nfr_5__simulationReportsTelegrams()
	{
		simnet::Scenario scenario;
		simnet::ScenarioDevice device;
		device.config.name = "d";
		device.config.groupObjects.push_back({knx::GroupAddress(0x0901), {}, dpt::DptId{1, 1}, Bytes{0}});
		device.memory.push_back({0x4000, Bytes{0xAA, 0xBB}});
		scenario.devices.push_back(device);
		simnet::Simulation sim(scenario);
		QVERIFY(sim.device("d"));
		QVERIFY(!sim.device("nope"));
		QCOMPARE(sim.device("d")->readMemory(0x4000, 2), (Bytes{0xAA, 0xBB}));

		QSignalSpy telegrams(&sim, &simnet::Simulation::telegram);
		QVERIFY(sim.device("d")->setGroupValue(knx::GroupAddress(0x0901), Bytes{1}).has_value());
		QCOMPARE(telegrams.size(), 1);
		QCOMPARE(telegrams.at(0).at(0).value<sim::Telegram>().frame.destination, std::uint16_t{0x0901});
	}
};

QTEST_GUILESS_MAIN(TestKnxIpServer)
#include "tst_KnxIpServer.moc"
