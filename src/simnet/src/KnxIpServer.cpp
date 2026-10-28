#include "kestrel/simnet/KnxIpServer.hpp"

#include "Logging.hpp"

#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

#include <variant>

namespace kestrel::simnet
{

namespace knxip = knx::knxip;

namespace
{

knxip::IpEndpoint toEndpoint(const QHostAddress& address, quint16 port)
{
	knxip::IpEndpoint endpoint;
	bool ok = false;
	const quint32 ip = address.toIPv4Address(&ok);
	if (ok)
	{
		endpoint.address = {static_cast<std::uint8_t>(ip >> 24), static_cast<std::uint8_t>(ip >> 16),
			static_cast<std::uint8_t>(ip >> 8), static_cast<std::uint8_t>(ip)};
	}
	endpoint.port = port;
	return endpoint;
}

QHostAddress toHostAddress(const knxip::IpEndpoint& endpoint)
{
	const auto& a = endpoint.address;
	return QHostAddress((quint32{a[0]} << 24) | (quint32{a[1]} << 16) | (quint32{a[2]} << 8) | quint32{a[3]});
}

} // namespace

KnxIpServer::KnxIpServer(sim::VirtualBus& bus, sim::TunnelServerConfig config, QObject* parent)
	: QObject(parent), m_bus(bus), m_server(std::make_unique<sim::TunnelServer>(bus, std::move(config))),
	  m_socket(new QUdpSocket(this)), m_timer(new QTimer(this))
{
	m_monitor = m_bus.attach([this](const knx::cemi::LData&) { scheduleFlush(); });
	m_timer->setInterval(100);
	QObject::connect(m_timer, &QTimer::timeout, this, &KnxIpServer::onTick);
	QObject::connect(m_socket, &QUdpSocket::readyRead, this, &KnxIpServer::onReadyRead);
}

KnxIpServer::~KnxIpServer()
{
	m_bus.detach(m_monitor);
}

Status KnxIpServer::listen(quint16 port, const QHostAddress& address)
{
	if (isListening())
	{
		return makeError(ErrorCode::Conflict, "already listening");
	}
	if (!m_socket->bind(address, port))
	{
		return makeError(ErrorCode::IoError,
			"cannot bind " + address.toString().toStdString() + ":" + std::to_string(port) + ": "
				+ m_socket->errorString().toStdString());
	}
	m_server->setLocalEndpoint(toEndpoint(m_socket->localAddress(), m_socket->localPort()));
	m_timer->start();
	qCInfo(lcSimnet) << "KNXnet/IP server listening on" << m_socket->localAddress().toString() << m_socket->localPort();
	return ok();
}

void KnxIpServer::close()
{
	m_timer->stop();
	m_socket->close();
}

bool KnxIpServer::isListening() const
{
	return m_socket->state() == QAbstractSocket::BoundState;
}

quint16 KnxIpServer::port() const
{
	return m_socket->localPort();
}

QHostAddress KnxIpServer::address() const
{
	return m_socket->localAddress();
}

void KnxIpServer::onReadyRead()
{
	while (m_socket->hasPendingDatagrams())
	{
		const QNetworkDatagram datagram = m_socket->receiveDatagram();
		if (!datagram.isValid())
		{
			continue;
		}
		const QByteArray data = datagram.data();
		const auto from = toEndpoint(datagram.senderAddress(), static_cast<quint16>(datagram.senderPort()));
		m_server->onDatagram(from,
			ByteView(reinterpret_cast<const std::uint8_t*>(data.constData()), static_cast<std::size_t>(data.size())),
			knx::Clock::now());
		flush();
	}
}

void KnxIpServer::onTick()
{
	m_server->tick(knx::Clock::now());
	flush();
}

void KnxIpServer::scheduleFlush()
{
	if (m_flushScheduled)
	{
		return;
	}
	m_flushScheduled = true;
	QMetaObject::invokeMethod(
		this,
		[this]
		{
			m_flushScheduled = false;
			flush();
		},
		Qt::QueuedConnection);
}

void KnxIpServer::flush()
{
	for (auto actions = m_server->takeActions(); !actions.empty(); actions = m_server->takeActions())
	{
		for (const auto& action : actions)
		{
			if (const auto* send = std::get_if<sim::TunnelServer::SendDatagram>(&action))
			{
				if (!isListening())
				{
					continue;
				}
				const QByteArray bytes(
					reinterpret_cast<const char*>(send->bytes.data()), static_cast<qsizetype>(send->bytes.size()));
				m_socket->writeDatagram(bytes, toHostAddress(send->to), send->to.port);
			}
			else if (const auto* connected = std::get_if<sim::TunnelServer::ClientConnected>(&action))
			{
				qCInfo(lcSimnet) << "client connected, channel" << connected->channel;
				Q_EMIT clientConnected(connected->channel, QString::fromStdString(connected->address.toString()));
			}
			else if (const auto* disconnected = std::get_if<sim::TunnelServer::ClientDisconnected>(&action))
			{
				qCInfo(lcSimnet) << "client disconnected, channel" << disconnected->channel;
				Q_EMIT clientDisconnected(disconnected->channel);
			}
		}
	}
}

} // namespace kestrel::simnet
