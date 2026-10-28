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
			self->m_discovered = true;
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

} // namespace kestrel::viewmodels
