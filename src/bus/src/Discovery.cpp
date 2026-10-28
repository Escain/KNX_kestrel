#include "kestrel/bus/Discovery.hpp"

#include "kestrel/bus/Endpoint.hpp"
#include "kestrel/bus/Logging.hpp"

#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QTimer>
#include <QUdpSocket>

#include <algorithm>

namespace kestrel::bus
{

namespace knxip = knx::knxip;

namespace
{

QByteArray toByteArray(const Bytes& bytes)
{
	return QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
}

bool hasFamily(const std::vector<knxip::ServiceFamily>& families, std::uint8_t id)
{
	return std::any_of(families.begin(), families.end(), [id](const knxip::ServiceFamily& f) { return f.id == id; });
}

} // namespace

bool DiscoveredInterface::supportsTunnelling() const
{
	return hasFamily(families, knxip::family::kTunnelling);
}

bool DiscoveredInterface::supportsRouting() const
{
	return hasFamily(families, knxip::family::kRouting);
}

Discovery::Discovery(QObject* parent) : QObject(parent), m_socket(new QUdpSocket(this)), m_timer(new QTimer(this))
{
	m_timer->setSingleShot(true);
	QObject::connect(m_timer, &QTimer::timeout, this, &Discovery::stop);
	QObject::connect(m_socket, &QUdpSocket::readyRead, this, &Discovery::onReadyRead);
}

Discovery::~Discovery() = default;

void Discovery::addUnicastTarget(const QHostAddress& host, quint16 port)
{
	m_unicast.append({host, port});
}

Status Discovery::start()
{
	if (m_running)
	{
		return makeError(ErrorCode::Conflict, "discovery already running");
	}
	if (m_socket->state() != QAbstractSocket::BoundState && !m_socket->bind(QHostAddress(QHostAddress::AnyIPv4), 0))
	{
		return makeError(ErrorCode::IoError, "cannot bind UDP socket: " + m_socket->errorString().toStdString());
	}
	m_results.clear();
	const quint16 localPort = m_socket->localPort();
	const QHostAddress group(QHostAddress(QStringLiteral("224.0.23.12")));
	int sent = 0;

	if (m_multicast)
	{
		const auto interfaces = QNetworkInterface::allInterfaces();
		for (const QNetworkInterface& iface : interfaces)
		{
			const auto flags = iface.flags();
			if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning)
				|| !flags.testFlag(QNetworkInterface::CanMulticast))
			{
				continue;
			}
			for (const QNetworkAddressEntry& entry : iface.addressEntries())
			{
				if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
				{
					continue;
				}
				m_socket->setMulticastInterface(iface);
				const knxip::SearchRequest request{knxip::Hpai{knxip::HostProtocol::Udp, toEndpoint(entry.ip(), localPort)}};
				if (m_socket->writeDatagram(toByteArray(knxip::encode(request)), group, knxip::kDefaultPort) >= 0)
				{
					++sent;
					qCDebug(lcBus) << "search request via" << iface.name() << entry.ip().toString();
				}
				else
				{
					qCDebug(lcBus) << "search request via" << iface.name() << "failed:" << m_socket->errorString();
				}
			}
		}
	}
	for (const auto& [host, port] : std::as_const(m_unicast))
	{
		const knxip::SearchRequest request{knxip::Hpai::nat()};
		if (m_socket->writeDatagram(toByteArray(knxip::encode(request)), host, port) >= 0)
		{
			++sent;
		}
		else
		{
			qCDebug(lcBus) << "unicast search to" << host.toString() << "failed:" << m_socket->errorString();
		}
	}
	if (sent == 0)
	{
		return makeError(ErrorCode::IoError, "no search request could be sent (no usable network interface)");
	}
	m_running = true;
	m_timer->start(m_timeout);
	return ok();
}

void Discovery::stop()
{
	if (!m_running)
	{
		return;
	}
	m_running = false;
	m_timer->stop();
	Q_EMIT finished(m_results);
}

void Discovery::onReadyRead()
{
	while (m_socket->hasPendingDatagrams())
	{
		const QNetworkDatagram datagram = m_socket->receiveDatagram();
		if (!m_running || !datagram.isValid())
		{
			continue;
		}
		const QByteArray data = datagram.data();
		auto frame = knxip::decode(
			ByteView(reinterpret_cast<const std::uint8_t*>(data.constData()), static_cast<std::size_t>(data.size())));
		if (!frame)
		{
			continue;
		}
		const auto* response = std::get_if<knxip::SearchResponse>(&*frame);
		if (!response)
		{
			continue;
		}
		DiscoveredInterface found;
		const auto& endpoint = response->controlEndpoint.endpoint;
		const bool unspecified = endpoint.address == std::array<std::uint8_t, 4>{};
		found.host = unspecified ? QHostAddress(datagram.senderAddress().toIPv4Address()) : toHostAddress(endpoint);
		found.port = endpoint.port != 0 ? endpoint.port : static_cast<quint16>(datagram.senderPort());
		found.info = response->deviceInfo;
		found.families = response->serviceFamilies;

		const bool duplicate = std::any_of(m_results.cbegin(), m_results.cend(),
			[&found](const DiscoveredInterface& known)
			{ return known.host == found.host && known.port == found.port && known.info.serial == found.info.serial; });
		if (duplicate)
		{
			continue;
		}
		m_results.append(found);
		Q_EMIT interfaceFound(found);
	}
}

} // namespace kestrel::bus
