#include "kestrel/simnet/Simulation.hpp"

#include <QTimer>

namespace kestrel::simnet
{

Simulation::Simulation(const Scenario& scenario, QObject* parent) : QObject(parent), m_timer(new QTimer(this))
{
	for (const ScenarioDevice& entry : scenario.devices)
	{
		auto device = std::make_unique<sim::VirtualDevice>(m_bus, entry.config);
		for (const MemoryBlock& block : entry.memory)
		{
			device->writeMemory(block.address, block.data);
		}
		m_devices.push_back(std::move(device));
	}
	m_server = std::make_unique<KnxIpServer>(m_bus, scenario.serverConfig());
	m_bus.onTelegram = [this](const sim::Telegram& telegram) { Q_EMIT this->telegram(telegram); };

	m_timer->setInterval(100);
	QObject::connect(m_timer, &QTimer::timeout, this,
		[this]
		{
			const auto now = knx::Clock::now();
			for (auto& device : m_devices)
			{
				device->tick(now);
			}
		});
	m_timer->start();
}

Simulation::~Simulation()
{
	m_bus.onTelegram = nullptr;
	m_server.reset();
	m_devices.clear();
}

Status Simulation::listen(quint16 port)
{
	return m_server->listen(port);
}

sim::VirtualDevice* Simulation::device(std::string_view name)
{
	for (auto& device : m_devices)
	{
		if (device->name() == name)
		{
			return device.get();
		}
	}
	return nullptr;
}

} // namespace kestrel::simnet
