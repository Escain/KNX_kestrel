#pragma once

// A complete simulated installation built from a Scenario: VirtualBus + VirtualDevices + KnxIpServer.
// Used by the kestrel-sim executable and by in-process Qt tests. A 100 ms timer drives the devices'
// transport-connection timeouts.

#include "kestrel/base/Result.hpp"
#include "kestrel/sim/VirtualBus.hpp"
#include "kestrel/sim/VirtualDevice.hpp"
#include "kestrel/simnet/KnxIpServer.hpp"
#include "kestrel/simnet/ScenarioLoader.hpp"

#include <QMetaType>
#include <QObject>

#include <memory>
#include <string_view>
#include <vector>

class QTimer;

namespace kestrel::simnet
{

class Simulation : public QObject
{
	Q_OBJECT

public:
	explicit Simulation(const Scenario& scenario, QObject* parent = nullptr);
	~Simulation() override;

	/// Starts the KNXnet/IP server on 127.0.0.1:port (0 = ephemeral).
	Status listen(quint16 port = 0);
	quint16 port() const { return m_server->port(); }

	sim::VirtualBus& bus() { return m_bus; }
	KnxIpServer& server() { return *m_server; }
	/// nullptr when no device has that name.
	sim::VirtualDevice* device(std::string_view name);
	const std::vector<std::unique_ptr<sim::VirtualDevice>>& devices() const { return m_devices; }

Q_SIGNALS:
	/// Every telegram put on the medium, synchronously (before delivery).
	void telegram(const kestrel::sim::Telegram& telegram);

private:
	sim::VirtualBus m_bus;
	std::vector<std::unique_ptr<sim::VirtualDevice>> m_devices;
	std::unique_ptr<KnxIpServer> m_server;
	QTimer* m_timer = nullptr;
};

} // namespace kestrel::simnet

Q_DECLARE_METATYPE(kestrel::sim::Telegram)
