// simnet::ScenarioLoader: scenario JSON → sim::DeviceConfig (Kestrel spec 04 §5).

#include "kestrel/simnet/ScenarioLoader.hpp"

#include <QTest>

using namespace kestrel;
using namespace Qt::StringLiterals;

class TestScenarioLoader : public QObject
{
	Q_OBJECT

private Q_SLOTS:
	void nfr_5__parsesSpecExample()
	{
		const QByteArray json = R"({ "interfaceAddress": "1.1.255",
			"devices": [ { "name": "switch-a", "address": "15.15.255", "serial": "0083:00001234", "progMode": false,
			               "groupObjects": [ { "ga": "1/1/1", "flags": "CWTU", "dpt": "1.001", "value": "00" } ] } ] })";
		auto scenario = simnet::ScenarioLoader::fromJson(json);
		QVERIFY2(scenario.has_value(), scenario ? "" : scenario.error().message.c_str());
		QCOMPARE(scenario->interfaceAddress, knx::IndividualAddress(0x11FF));
		QCOMPARE(scenario->devices.size(), std::size_t{1});
		const sim::DeviceConfig& device = scenario->devices[0].config;
		QCOMPARE(device.name, std::string("switch-a"));
		QCOMPARE(device.address, knx::IndividualAddress(0xFFFF));
		QCOMPARE(device.serial.toString(), std::string("0083:00001234"));
		QCOMPARE(device.progMode, false);
		QCOMPARE(device.groupObjects.size(), std::size_t{1});
		const sim::GroupObjectConfig& object = device.groupObjects[0];
		QCOMPARE(object.address, knx::GroupAddress(0x0901));
		QCOMPARE(object.flags.toString(), std::string("CWTU"));
		QCOMPARE(object.dpt, (dpt::DptId{1, 1}));
		QCOMPARE(object.value, Bytes{0x00});

		const sim::TunnelServerConfig server = scenario->serverConfig();
		QCOMPARE(server.tunnelAddresses, std::vector<knx::IndividualAddress>{knx::IndividualAddress(0x11FF)});
		QCOMPARE(server.deviceInfo.address, knx::IndividualAddress(0x11FF));
	}

	void nfr_5__parsesExtensions()
	{
		const QByteArray json = R"({ "interfaceAddress": "2.1.0", "tunnelAddresses": ["2.1.250", "2.1.251"],
			"friendlyName": "Lab", "devices": [ { "name": "d", "maskVersion": "07b0",
			"memory": [ { "address": "0x4000", "data": "01 02 03" } ],
			"groupObjects": [ { "ga": "2/0/1", "dpt": "9.001" } ] } ] })";
		auto scenario = simnet::ScenarioLoader::fromJson(json);
		QVERIFY2(scenario.has_value(), scenario ? "" : scenario.error().message.c_str());
		QCOMPARE(scenario->serverConfig().tunnelAddresses.size(), std::size_t{2});
		QCOMPARE(scenario->serverConfig().deviceInfo.friendlyName, std::string("Lab"));
		const simnet::ScenarioDevice& device = scenario->devices[0];
		QCOMPARE(device.config.address, knx::IndividualAddress(0xFFFF)); // default
		QCOMPARE(device.config.maskVersion, std::uint16_t{0x07B0});
		QCOMPARE(device.memory.size(), std::size_t{1});
		QCOMPARE(device.memory[0].address, std::uint16_t{0x4000});
		QCOMPARE(device.memory[0].data, (Bytes{1, 2, 3}));
		// Missing value: zero bytes of the DPT size.
		QCOMPARE(device.config.groupObjects[0].value, (Bytes{0, 0}));
	}

	void nfr_5__rejectsInvalidInput_data()
	{
		QTest::addColumn<QByteArray>("json");
		QTest::addColumn<QString>("fragment");
		QTest::newRow("syntax") << QByteArray("{") << u"scenario"_s;
		QTest::newRow("root") << QByteArray("[]") << u"root"_s;
		QTest::newRow("address") << QByteArray(R"({"devices":[{"name":"a","address":"1.1.300"}]})")
								 << u"devices[0].address"_s;
		QTest::newRow("name") << QByteArray(R"({"devices":[{"address":"1.1.1"}]})") << u"devices[0].name"_s;
		QTest::newRow("duplicate") << QByteArray(R"({"devices":[{"name":"a"},{"name":"a"}]})")
								   << u"duplicate device name"_s;
		QTest::newRow("ga") << QByteArray(R"({"devices":[{"name":"a","groupObjects":[{"ga":"x"}]}]})")
							<< u"devices[0].groupObjects[0].ga"_s;
		QTest::newRow("flags") << QByteArray(R"({"devices":[{"name":"a","groupObjects":[{"ga":"1/1/1","flags":"CQ"}]}]})")
							   << u"flags"_s;
		QTest::newRow("value") << QByteArray(R"({"devices":[{"name":"a","groupObjects":[{"ga":"1/1/1","value":"zz"}]}]})")
							   << u"value"_s;
		QTest::newRow("serial") << QByteArray(R"({"devices":[{"name":"a","serial":"12"}]})") << u"serial"_s;
		QTest::newRow("progMode") << QByteArray(R"({"devices":[{"name":"a","progMode":"yes"}]})") << u"progMode"_s;
	}

	void nfr_5__rejectsInvalidInput()
	{
		QFETCH(QByteArray, json);
		QFETCH(QString, fragment);
		auto scenario = simnet::ScenarioLoader::fromJson(json);
		QVERIFY(!scenario.has_value());
		const QString message = QString::fromStdString(scenario.error().message);
		QVERIFY2(message.contains(fragment), qPrintable(message));
	}

	void nfr_5__missingFileIsIoError()
	{
		auto scenario = simnet::ScenarioLoader::fromFile(u"/nonexistent/kestrel/scenario.json"_s);
		QVERIFY(!scenario.has_value());
		QCOMPARE(scenario.error().code, ErrorCode::IoError);
	}
};

QTEST_GUILESS_MAIN(TestScenarioLoader)
#include "tst_ScenarioLoader.moc"
