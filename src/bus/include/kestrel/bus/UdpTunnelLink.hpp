#pragma once

// Real KNXnet/IP tunnelling link (Kestrel spec 04 §6): a QUdpSocket and a 100 ms QTimer around the
// sans-IO knx::TunnelClient. NAT mode (HPAI 0.0.0.0:0) is used, so one socket serves the control and
// data endpoints and the link works behind NAT and on loopback.
//
// The object is thread-affine like any QObject: create and use it on one thread (app::BusSession
// keeps it on its bus thread). Both the IBusLink callbacks and the Qt signals fire on that thread.

#include "kestrel/base/Result.hpp"
#include "kestrel/knx/Address.hpp"
#include "kestrel/knx/BusLink.hpp"
#include "kestrel/knx/Cemi.hpp"
#include "kestrel/knx/TunnelClient.hpp"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <optional>

class QTimer;
class QUdpSocket;

namespace kestrel::bus
{

class UdpTunnelLink : public QObject, public knx::IBusLink
{
	Q_OBJECT

public:
	explicit UdpTunnelLink(QObject* parent = nullptr);
	explicit UdpTunnelLink(knx::TunnelClientConfig config, QObject* parent = nullptr);
	/// Sends DISCONNECT_REQUEST (best effort) when still connected; no callbacks/signals fire.
	~UdpTunnelLink() override;

	/// Binds the local socket (any IPv4, ephemeral port) and sends CONNECT_REQUEST to host:port.
	/// The outcome arrives as stateChanged(Connected) or stateChanged(Disconnected) + connectionLost().
	/// Fails immediately when already connecting/connected or when the socket cannot be bound.
	Status connectTo(const QHostAddress& host, quint16 port);
	/// Sends DISCONNECT_REQUEST and reports Disconnected. No-op when disconnected.
	void disconnectFromInterface();

	Status send(const knx::cemi::LData& frame) override;

	knx::LinkState state() const { return m_state; }
	/// Individual address assigned by the server (valid when connected).
	knx::IndividualAddress address() const { return m_client.address(); }
	QHostAddress remoteHost() const { return m_remoteHost; }
	quint16 remotePort() const { return m_remotePort; }
	/// Human-readable reason of the last unrequested disconnect (empty otherwise).
	QString lastError() const { return m_lastError; }
	std::optional<knx::DisconnectReason> lastDisconnectReason() const { return m_lastReason; }

Q_SIGNALS:
	void stateChanged(kestrel::knx::LinkState state);
	void frameReceived(const kestrel::knx::cemi::LData& frame);
	void sendConfirmed(bool ok);
	/// The connection ended without disconnectFromInterface(): connect failure, timeout, server disconnect.
	void connectionLost(const QString& reason);

private:
	void onReadyRead();
	void onTick();
	void onSocketError(QAbstractSocket::SocketError error);
	void processActions();
	void setState(knx::LinkState state);
	void writeDatagram(const Bytes& bytes);

	knx::TunnelClient m_client;
	QUdpSocket* m_socket = nullptr;
	QTimer* m_timer = nullptr;
	QHostAddress m_remoteHost;
	quint16 m_remotePort = 0;
	knx::LinkState m_state = knx::LinkState::Disconnected;
	QString m_lastError;
	QString m_abortReason; ///< set while aborting a connect attempt after a socket error
	std::optional<knx::DisconnectReason> m_lastReason;
	bool m_processing = false;
	bool m_silent = false; ///< set in the destructor: no callbacks, no signals
};

/// Human-readable text of a tunnel disconnect reason.
QString describe(knx::DisconnectReason reason, std::uint8_t status);

} // namespace kestrel::bus

Q_DECLARE_METATYPE(kestrel::knx::LinkState)
Q_DECLARE_METATYPE(kestrel::knx::cemi::LData)
