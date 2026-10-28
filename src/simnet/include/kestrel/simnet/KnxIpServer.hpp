#pragma once

// Exposes a sim::VirtualBus on a real UDP port (Kestrel spec 04 §5): a QUdpSocket bound to
// 127.0.0.1 (port 0 = ephemeral) drives a sim::TunnelServer. SEARCH (unicast), DESCRIPTION, CONNECT,
// CONNECTIONSTATE, DISCONNECT and TUNNELLING are served; a 100 ms timer drives the server timeouts.
//
// Devices may put frames on the bus at any time (a stdin command, a timer, another client). The
// server listens on the bus itself and schedules a flush() on the event loop whenever a telegram
// appears, so the resulting L_Data.ind always reaches the clients. flush() can also be called directly.
//
// The bus must outlive the server.

#include "kestrel/base/Result.hpp"
#include "kestrel/sim/TunnelServer.hpp"
#include "kestrel/sim/VirtualBus.hpp"

#include <QHostAddress>
#include <QObject>
#include <QString>

#include <memory>

class QTimer;
class QUdpSocket;

namespace kestrel::simnet
{

class KnxIpServer : public QObject
{
	Q_OBJECT

public:
	explicit KnxIpServer(sim::VirtualBus& bus, sim::TunnelServerConfig config = {}, QObject* parent = nullptr);
	~KnxIpServer() override;

	/// Binds the socket. port 0 picks an ephemeral port: read it back with port().
	Status listen(quint16 port = 0, const QHostAddress& address = QHostAddress(QHostAddress::LocalHost));
	void close();
	bool isListening() const;
	quint16 port() const;
	QHostAddress address() const;

	sim::TunnelServer& tunnelServer() { return *m_server; }
	std::size_t connectionCount() const { return m_server->connectionCount(); }

	/// Executes the pending TunnelServer actions (sends datagrams, emits client signals).
	void flush();

Q_SIGNALS:
	void clientConnected(quint8 channel, const QString& address);
	void clientDisconnected(quint8 channel);

private:
	void onReadyRead();
	void onTick();
	void scheduleFlush();

	sim::VirtualBus& m_bus;
	std::unique_ptr<sim::TunnelServer> m_server;
	sim::EndpointId m_monitor = 0;
	QUdpSocket* m_socket = nullptr;
	QTimer* m_timer = nullptr;
	bool m_flushScheduled = false;
};

} // namespace kestrel::simnet
