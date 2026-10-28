#pragma once

// KNXnet/IP interface discovery (FR-BUS-1; Kestrel spec 04 §6, KNX 3/8/2 §4.2).
//
// start() sends SEARCH_REQUEST to 224.0.23.12:3671 on every interface that is up, running and
// multicast-capable (HPAI = that interface's IPv4 address + our port), and to each unicast target
// (HPAI = NAT, so the server answers to the sender). Responses are collected until the timeout
// (default 3 s), de-duplicated by control endpoint + serial number, then finished() is emitted.

#include "kestrel/base/Result.hpp"
#include "kestrel/knx/KnxIp.hpp"

#include <QHostAddress>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <chrono>
#include <vector>

class QTimer;
class QUdpSocket;

namespace kestrel::bus
{

struct DiscoveredInterface
{
	/// Control endpoint (from the response HPAI; the sender address when the HPAI is 0.0.0.0).
	QHostAddress host;
	quint16 port = 0;
	knx::knxip::DeviceInfo info;
	std::vector<knx::knxip::ServiceFamily> families;

	bool supportsTunnelling() const;
	bool supportsRouting() const;
	QString friendlyName() const { return QString::fromLatin1(info.friendlyName); }

	friend bool operator==(const DiscoveredInterface&, const DiscoveredInterface&) = default;
};

class Discovery : public QObject
{
	Q_OBJECT

public:
	explicit Discovery(QObject* parent = nullptr);
	~Discovery() override;

	void setTimeout(std::chrono::milliseconds timeout) { m_timeout = timeout; }
	/// Multicast search on all capable interfaces (default true).
	void setMulticastEnabled(bool enabled) { m_multicast = enabled; }
	void addUnicastTarget(const QHostAddress& host, quint16 port = knx::knxip::kDefaultPort);

	/// Sends the requests. Fails when already running, when the socket cannot be bound, or when no
	/// request could be sent at all.
	Status start();
	/// Stops collecting and emits finished() with what was found so far.
	void stop();
	bool isRunning() const { return m_running; }
	const QList<DiscoveredInterface>& results() const { return m_results; }

Q_SIGNALS:
	void interfaceFound(const kestrel::bus::DiscoveredInterface& found);
	void finished(const QList<kestrel::bus::DiscoveredInterface>& results);

private:
	void onReadyRead();

	QUdpSocket* m_socket = nullptr;
	QTimer* m_timer = nullptr;
	std::chrono::milliseconds m_timeout{3000};
	bool m_multicast = true;
	bool m_running = false;
	QList<QPair<QHostAddress, quint16>> m_unicast;
	QList<DiscoveredInterface> m_results;
};

} // namespace kestrel::bus

Q_DECLARE_METATYPE(kestrel::bus::DiscoveredInterface)
