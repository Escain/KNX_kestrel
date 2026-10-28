#include "kestrel/bus/UdpTunnelLink.hpp"

#include "kestrel/bus/Logging.hpp"

#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

#include <type_traits>
#include <variant>

namespace kestrel::bus
{

using namespace Qt::StringLiterals;

namespace
{

constexpr int kTickMs = 100;

template <class... Ts>
struct Overloaded : Ts...
{
	using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

} // namespace

QString describe(knx::DisconnectReason reason, std::uint8_t status)
{
	QString text;
	switch (reason)
	{
		case knx::DisconnectReason::UserRequest: text = u"disconnected"_s; break;
		case knx::DisconnectReason::ConnectTimeout: text = u"no answer from the interface (connect timeout)"_s; break;
		case knx::DisconnectReason::ConnectRejected: text = u"the interface rejected the connection"_s; break;
		case knx::DisconnectReason::AckTimeout: text = u"the interface stopped acknowledging frames"_s; break;
		case knx::DisconnectReason::HeartbeatLost: text = u"the interface stopped answering heartbeats"_s; break;
		case knx::DisconnectReason::ServerDisconnect: text = u"the interface closed the connection"_s; break;
	}
	if (status != 0)
	{
		text += u" (status 0x%1)"_s.arg(status, 2, 16, QLatin1Char('0'));
	}
	return text;
}

UdpTunnelLink::UdpTunnelLink(QObject* parent) : UdpTunnelLink(knx::TunnelClientConfig{}, parent)
{
}

UdpTunnelLink::UdpTunnelLink(knx::TunnelClientConfig config, QObject* parent)
	: QObject(parent), m_client(config), m_socket(new QUdpSocket(this)), m_timer(new QTimer(this))
{
	m_timer->setInterval(kTickMs);
	QObject::connect(m_timer, &QTimer::timeout, this, &UdpTunnelLink::onTick);
	QObject::connect(m_socket, &QUdpSocket::readyRead, this, &UdpTunnelLink::onReadyRead);
	QObject::connect(m_socket, &QUdpSocket::errorOccurred, this, &UdpTunnelLink::onSocketError);
}

UdpTunnelLink::~UdpTunnelLink()
{
	m_silent = true;
	if (m_client.state() != knx::TunnelState::Disconnected)
	{
		m_client.disconnect(knx::Clock::now());
		processActions();
	}
}

Status UdpTunnelLink::connectTo(const QHostAddress& host, quint16 port)
{
	if (m_client.state() != knx::TunnelState::Disconnected)
	{
		return makeError(ErrorCode::Conflict, "the link is already connecting or connected");
	}
	if (host.protocol() != QAbstractSocket::IPv4Protocol)
	{
		return makeError(ErrorCode::InvalidArgument, "only IPv4 interfaces are supported");
	}
	if (m_socket->state() != QAbstractSocket::BoundState
		&& !m_socket->bind(QHostAddress(QHostAddress::AnyIPv4), 0))
	{
		return makeError(ErrorCode::IoError, "cannot bind UDP socket: " + m_socket->errorString().toStdString());
	}
	m_remoteHost = host;
	m_remotePort = port;
	m_lastError.clear();
	m_lastReason.reset();
	qCDebug(lcBus) << "connecting to" << host.toString() << port;
	m_client.connect(knx::Clock::now());
	setState(knx::LinkState::Connecting);
	m_timer->start();
	processActions();
	return ok();
}

void UdpTunnelLink::disconnectFromInterface()
{
	if (m_client.state() == knx::TunnelState::Disconnected)
	{
		return;
	}
	m_client.disconnect(knx::Clock::now());
	processActions();
}

Status UdpTunnelLink::send(const knx::cemi::LData& frame)
{
	Status status = m_client.send(frame, knx::Clock::now());
	if (status)
	{
		processActions();
	}
	return status;
}

void UdpTunnelLink::onReadyRead()
{
	while (m_socket->hasPendingDatagrams())
	{
		const QNetworkDatagram datagram = m_socket->receiveDatagram();
		if (!datagram.isValid())
		{
			continue;
		}
		// NAT mode: the server answers from its control/data endpoint; accept only the interface host.
		if (!datagram.senderAddress().isEqual(m_remoteHost, QHostAddress::TolerantConversion))
		{
			continue;
		}
		const QByteArray data = datagram.data();
		qCDebug(lcBusFrames).noquote() << "<-" << data.toHex(' ');
		const auto* begin = reinterpret_cast<const std::uint8_t*>(data.constData());
		m_client.onDatagram(ByteView(begin, static_cast<std::size_t>(data.size())), knx::Clock::now());
		processActions();
	}
}

void UdpTunnelLink::onTick()
{
	m_client.tick(knx::Clock::now());
	processActions();
}

void UdpTunnelLink::onSocketError(QAbstractSocket::SocketError error)
{
	qCDebug(lcBus) << "socket error" << error << m_socket->errorString();
	// An ICMP "port unreachable" while connecting means nobody listens: fail fast instead of
	// waiting for the 10 s connect timeout.
	if (error == QAbstractSocket::ConnectionRefusedError && m_client.state() == knx::TunnelState::Connecting)
	{
		m_abortReason = u"connection refused by %1:%2"_s.arg(m_remoteHost.toString()).arg(m_remotePort);
		m_client.disconnect(knx::Clock::now());
		processActions();
	}
}

void UdpTunnelLink::writeDatagram(const Bytes& bytes)
{
	const QByteArray data(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
	qCDebug(lcBusFrames).noquote() << "->" << data.toHex(' ');
	if (m_socket->writeDatagram(data, m_remoteHost, m_remotePort) < 0)
	{
		qCWarning(lcBus) << "writeDatagram failed:" << m_socket->errorString();
	}
}

void UdpTunnelLink::setState(knx::LinkState state)
{
	if (state == m_state)
	{
		return;
	}
	m_state = state;
	if (state == knx::LinkState::Disconnected)
	{
		m_timer->stop();
	}
	if (m_silent)
	{
		return;
	}
	if (onState)
	{
		onState(state);
	}
	Q_EMIT stateChanged(state);
}

void UdpTunnelLink::processActions()
{
	// Callbacks may call send() again: the outer loop picks up the new actions, keeping the order.
	if (m_processing)
	{
		return;
	}
	m_processing = true;
	for (auto actions = m_client.takeActions(); !actions.empty(); actions = m_client.takeActions())
	{
		for (auto& action : actions)
		{
			std::visit(Overloaded{
						   [this](const knx::TunnelClient::SendControl& a) { writeDatagram(a.bytes); },
						   [this](const knx::TunnelClient::SendData& a) { writeDatagram(a.bytes); },
						   [this](const knx::TunnelClient::Connected& a)
						   {
							   qCInfo(lcBus).noquote() << u"connected, channel %1, address %2"_s.arg(a.channel).arg(
								   QString::fromStdString(a.address.toString()));
							   setState(knx::LinkState::Connected);
						   },
						   [this](const knx::TunnelClient::Disconnected& a)
						   {
							   const bool aborted = !m_abortReason.isEmpty();
							   const bool requested = a.reason == knx::DisconnectReason::UserRequest && !aborted;
							   if (!requested)
							   {
								   m_lastReason = aborted ? knx::DisconnectReason::ConnectRejected : a.reason;
								   m_lastError = aborted ? m_abortReason : describe(a.reason, a.status);
								   m_abortReason.clear();
								   qCInfo(lcBus).noquote() << u"connection lost: "_s + m_lastError;
							   }
							   setState(knx::LinkState::Disconnected);
							   if (!requested && !m_silent)
							   {
								   Q_EMIT connectionLost(m_lastError);
							   }
						   },
						   [this](const knx::TunnelClient::FrameReceived& a)
						   {
							   if (m_silent)
							   {
								   return;
							   }
							   if (onFrame)
							   {
								   onFrame(a.frame);
							   }
							   Q_EMIT frameReceived(a.frame);
						   },
						   [this](const knx::TunnelClient::SendConfirmed& a)
						   {
							   if (m_silent)
							   {
								   return;
							   }
							   if (onConfirm)
							   {
								   onConfirm(a.ok);
							   }
							   Q_EMIT sendConfirmed(a.ok);
						   },
					   },
				action);
		}
	}
	m_processing = false;
}

} // namespace kestrel::bus
