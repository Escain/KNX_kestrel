#include "kestrel/viewmodels/BusConnectionVM.hpp"

#include "kestrel/app/BusSession.hpp"
#include "kestrel/viewmodels/Format.hpp"

#include <QPointer>
#include <QSettings>
#include <QVariantMap>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

namespace
{
const QString kHostKey = u"bus/host"_s;
const QString kPortKey = u"bus/port"_s;
} // namespace

BusConnectionVM::BusConnectionVM(const Services& services, app::BusSession* bus, QObject* parent)
	: PanelVM(services, u"busConnection"_s, parent), m_bus(bus)
{
	const QSettings settings;
	m_host = settings.value(kHostKey, u"127.0.0.1"_s).toString();
	m_port = settings.value(kPortKey, kDefaultPort).toInt();
	connect(m_bus, &app::BusSession::stateChanged, this, &BusConnectionVM::stateChanged);
	connect(m_bus, &app::BusSession::connectionLost, this,
		[this](const QString& reason) { setError(tr("Connection lost: %1").arg(reason)); });
}

QString BusConnectionVM::stateText() const
{
	switch (m_bus->state())
	{
		case knx::LinkState::Disconnected: return tr("Disconnected");
		case knx::LinkState::Connecting: return tr("Connecting…");
		case knx::LinkState::Connected: return tr("Connected");
	}
	return {};
}

bool BusConnectionVM::isConnected() const
{
	return m_bus->isConnected();
}

bool BusConnectionVM::isConnecting() const
{
	return m_bus->state() == knx::LinkState::Connecting;
}

QString BusConnectionVM::localAddress() const
{
	return m_bus->isConnected() ? QString::fromStdString(m_bus->localAddress().toString()) : QString();
}

void BusConnectionVM::setHost(const QString& host)
{
	if (host.trimmed() != m_host)
	{
		m_host = host.trimmed();
		Q_EMIT endpointChanged();
	}
}

void BusConnectionVM::setPort(int port)
{
	if (port != m_port)
	{
		m_port = port;
		Q_EMIT endpointChanged();
	}
}

void BusConnectionVM::discover()
{
	if (m_discovering)
		return;
	m_discovering = true;
	Q_EMIT interfacesChanged();
	QPointer<BusConnectionVM> self(this);
	m_bus->discover({},
		[self](Result<QList<bus::DiscoveredInterface>> result)
		{
			if (!self)
				return;
			self->m_discovering = false;
			if (!result)
			{
				self->setError(errorText(result.error()));
			}
			else
			{
				self->m_interfaces.clear();
				for (const bus::DiscoveredInterface& found : *result)
				{
					self->m_interfaces.append(QVariantMap{
						{u"name"_s, found.friendlyName()},
						{u"host"_s, found.host.toString()},
						{u"port"_s, found.port},
						{u"tunnelling"_s, found.supportsTunnelling()},
					});
				}
			}
			Q_EMIT self->interfacesChanged();
		});
}

void BusConnectionVM::connectToBus()
{
	if (m_host.isEmpty() || m_port <= 0 || m_port > 65535)
	{
		setError(tr("Enter the host and port of a KNXnet/IP interface."));
		return;
	}
	QSettings settings;
	settings.setValue(kHostKey, m_host);
	settings.setValue(kPortKey, m_port);
	clearError();
	QPointer<BusConnectionVM> self(this);
	m_bus->connectToInterface(m_host, static_cast<quint16>(m_port),
		[self](Status status)
		{
			if (self && !status)
				self->setError(errorText(status.error()));
		});
}

void BusConnectionVM::connectToInterface(int index)
{
	if (index < 0 || index >= m_interfaces.size())
		return;
	const QVariantMap found = m_interfaces.at(index).toMap();
	setHost(found.value(u"host"_s).toString());
	setPort(found.value(u"port"_s).toInt());
	connectToBus();
}

void BusConnectionVM::disconnectFromBus()
{
	m_bus->disconnectFromInterface();
}

std::optional<knx::IndividualAddress> BusConnectionVM::addressOf(const QString& deviceRef)
{
	const auto ref = parseRef(deviceRef);
	if (!ref)
		return std::nullopt;
	if (!ref->is<model::Device>())
	{
		setError(tr("Select a device."));
		return std::nullopt;
	}
	const auto address = project().individualAddress(ref->as<model::Device>());
	if (!address)
		setError(tr("The device has no individual address yet."));
	return address;
}

void BusConnectionVM::setOperation(bool active, const QString& text)
{
	m_operationActive = active;
	m_operation = text;
	Q_EMIT operationChanged();
}

void BusConnectionVM::finish(const QString& result)
{
	m_operationActive = false;
	m_programming = false;
	m_operation.clear();
	m_lastResult = result;
	Q_EMIT operationChanged();
}

bool BusConnectionVM::programAddress(const QString& deviceRef)
{
	if (m_operationActive)
	{
		setError(tr("Another bus operation is running."));
		return false;
	}
	if (!m_bus->isConnected())
	{
		setError(tr("Not connected to the bus."));
		return false;
	}
	const auto target = addressOf(deviceRef);
	if (!target)
		return false;
	clearError();
	const auto device = model::EntityRef::parse(deviceRef)->as<model::Device>();
	const QString address = QString::fromStdString(target->toString());
	m_programming = true;
	setOperation(true, tr("Programming %1: press the programming button of the device.").arg(address));
	QPointer<BusConnectionVM> self(this);
	m_bus->programIndividualAddress(*target, {},
		[self, device, address](Result<knx::IndividualAddress> result)
		{
			if (!self)
				return;
			if (!result)
			{
				self->setError(tr("Programming %1 failed: %2").arg(address, errorText(result.error())));
				self->finish(tr("Programming %1 failed.").arg(address));
				return;
			}
			// A commissioning fact: dirty, but not undoable.
			model::DeviceState state = self->project().deviceState(device);
			state.iaProgrammed = true;
			self->project().recordState(device, state);
			self->finish(tr("Individual address %1 programmed.").arg(address));
		},
		[self, address](knx::IaProgrammerState progress)
		{
			if (!self)
				return;
			switch (progress)
			{
				case knx::IaProgrammerState::WaitProgMode:
					self->setOperation(true, tr("Programming %1: press the programming button of the device.").arg(address));
					break;
				case knx::IaProgrammerState::CheckAddress:
					self->setOperation(true, tr("Programming %1: checking that the address is free…").arg(address));
					break;
				case knx::IaProgrammerState::Verify:
					self->setOperation(true, tr("Programming %1: verifying…").arg(address));
					break;
				default: break;
			}
		});
	return true;
}

void BusConnectionVM::cancelProgramming()
{
	if (m_programming)
		m_bus->cancelProgramming();
}

bool BusConnectionVM::readDeviceInfo(const QString& deviceRef)
{
	if (m_operationActive)
	{
		setError(tr("Another bus operation is running."));
		return false;
	}
	if (!m_bus->isConnected())
	{
		setError(tr("Not connected to the bus."));
		return false;
	}
	const auto target = addressOf(deviceRef);
	if (!target)
		return false;
	clearError();
	const auto device = model::EntityRef::parse(deviceRef)->as<model::Device>();
	const QString address = QString::fromStdString(target->toString());
	setOperation(true, tr("Reading device info of %1…").arg(address));
	QPointer<BusConnectionVM> self(this);
	m_bus->readDeviceInfo(*target,
		[self, device, address](Result<app::DeviceInfo> result)
		{
			if (!self)
				return;
			if (!result)
			{
				self->setError(tr("Reading %1 failed: %2").arg(address, errorText(result.error())));
				self->finish(tr("No device info from %1.").arg(address));
				return;
			}
			const QString serial = QString::fromStdString(result->serial.toString());
			model::DeviceState state = self->project().deviceState(device);
			state.serial = serial;
			self->project().recordState(device, state);
			self->finish(tr("%1: mask version %2, manufacturer %3, serial %4")
							 .arg(address)
							 .arg(result->maskVersion, 4, 16, QLatin1Char('0'))
							 .arg(result->manufacturerId, 4, 16, QLatin1Char('0'))
							 .arg(serial));
		});
	return true;
}

} // namespace kestrel::viewmodels
