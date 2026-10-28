#pragma once

// Bus use cases shared by the GUI and the CLI (Kestrel spec 02 §3.4): one KNXnet/IP tunnel connection,
// group read/write, individual-address programming (FR-COM-1), device info (FR-COM-4), discovery
// (FR-BUS-1) and a batched telegram stream for the group monitor (FR-BUS-5).
//
// Threading: the session object lives on the caller's thread (the GUI thread). It owns a dedicated
// QThread on which a worker owns the bus::UdpTunnelLink, its timers, the knx::IaProgrammer and the
// knx::ManagementSessions. Every public method only posts a request to that thread and returns at
// once; results come back as callbacks invoked on the session's thread (never synchronously from
// inside the call). Callbacks are dropped if the session is destroyed first. Destroying the session
// disconnects (best effort DISCONNECT_REQUEST) and joins the bus thread.
//
// Group monitor: telegrams (received L_Data.ind and our own confirmed frames) go into a bounded ring
// buffer. telegramsAvailable() is emitted once per batch (coalesced until the next takeTelegrams()),
// so a GUI can drain at its own pace (e.g. a 30 Hz timer) without a signal per telegram.

#include "kestrel/app/TelegramRecord.hpp"
#include "kestrel/base/Result.hpp"
#include "kestrel/bus/Discovery.hpp"
#include "kestrel/dpt/Dpt.hpp"
#include "kestrel/knx/Address.hpp"
#include "kestrel/knx/BusLink.hpp"
#include "kestrel/knx/IaProgrammer.hpp"
#include "kestrel/knx/SerialNumber.hpp"

#include <QList>
#include <QObject>
#include <QString>
#include <QThread>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <ostream>

namespace kestrel::app
{

namespace detail
{
class BusWorker;
struct SharedState;
} // namespace detail

/// Answer to a group read.
struct GroupReadResult
{
	knx::IndividualAddress responder;
	/// Raw group value (short form expanded to one byte).
	QByteArray raw;
	dpt::DptValue value;
	/// dpt::format() of the value ("21.5 °C").
	QString text;

	friend bool operator==(const GroupReadResult&, const GroupReadResult&) = default;
};

/// Identification read from a device (FR-COM-4).
struct DeviceInfo
{
	knx::IndividualAddress address;
	/// Device descriptor type 0 (mask version), e.g. 0x0705.
	std::uint16_t maskVersion = 0;
	/// PID_SERIAL_NUMBER (11) of the device object.
	knx::SerialNumber serial;
	/// PID_MANUFACTURER_ID (12) of the device object.
	std::uint16_t manufacturerId = 0;

	friend bool operator==(const DeviceInfo&, const DeviceInfo&) = default;
};

/// One read-only management request (property value or memory), see BusSession::readManagement().
struct ManagementRead
{
	enum class Kind
	{
		Property,
		Memory,
	};
	Kind kind = Kind::Property;
	std::uint8_t objectIndex = 0;
	std::uint8_t propertyId = 0;
	std::uint8_t count = 1;
	std::uint16_t startIndex = 1;
	std::uint16_t address = 0;
	std::uint16_t length = 0;

	static ManagementRead property(std::uint8_t objectIndex, std::uint8_t propertyId, std::uint8_t count = 1,
		std::uint16_t startIndex = 1)
	{
		return {Kind::Property, objectIndex, propertyId, count, startIndex, 0, 0};
	}
	static ManagementRead memory(std::uint16_t address, std::uint16_t length)
	{
		return {Kind::Memory, 0, 0, 0, 0, address, length};
	}

	friend bool operator==(const ManagementRead&, const ManagementRead&) = default;
};

std::ostream& operator<<(std::ostream& os, const GroupReadResult& result);
std::ostream& operator<<(std::ostream& os, const DeviceInfo& info);
std::ostream& operator<<(std::ostream& os, const TelegramRecord& record);

class BusSession : public QObject
{
	Q_OBJECT

public:
	using StatusCallback = std::function<void(Status)>;
	using GroupReadCallback = std::function<void(Result<GroupReadResult>)>;
	/// Success value: the address the device had while in programming mode.
	using ProgramCallback = std::function<void(Result<knx::IndividualAddress>)>;
	using ProgressCallback = std::function<void(knx::IaProgrammerState)>;
	using DeviceInfoCallback = std::function<void(Result<DeviceInfo>)>;
	using DiscoveryCallback = std::function<void(Result<QList<bus::DiscoveredInterface>>)>;
	using AddressListCallback = std::function<void(Result<QList<knx::IndividualAddress>>)>;
	/// One result per request, in order; the outer error is only "not connected".
	using ManagementReadCallback = std::function<void(Result<QList<Result<QByteArray>>>)>;

	struct ProgramOptions
	{
		/// How long to wait for a device in programming mode.
		std::chrono::milliseconds timeout{60000};
		/// Program even if another device already answers at the target address.
		bool allowOverwrite = false;
	};

	struct DiscoveryOptions
	{
		std::chrono::milliseconds timeout{3000};
		bool multicast = true;
		/// Optional unicast target (empty host = none).
		QString unicastHost;
		quint16 unicastPort = 3671;
	};

	explicit BusSession(QObject* parent = nullptr);
	~BusSession() override;
	BusSession(const BusSession&) = delete;
	BusSession& operator=(const BusSession&) = delete;

	// --- Connection ----------------------------------------------------------------------------------------
	/// Connects the tunnel to host (IPv4 address or name) : port. `done` gets ok once Connected, or the
	/// error (IoError on refusal/timeout, Conflict when already connected/connecting).
	void connectToInterface(const QString& host, quint16 port, StatusCallback done = {});
	void disconnectFromInterface();
	/// Last state reported by the bus thread.
	knx::LinkState state() const { return m_state; }
	bool isConnected() const { return m_state == knx::LinkState::Connected; }
	/// Tunnel individual address assigned by the interface (valid when connected).
	knx::IndividualAddress localAddress() const { return m_localAddress; }

	// --- Group communication (FR-BUS-6) -----------------------------------------------------------------------
	/// Encodes `value` with `dpt` and sends A_GroupValue_Write. `done`: ok once the interface confirmed it.
	void groupWrite(knx::GroupAddress address, dpt::DptId dpt, dpt::DptValue value, StatusCallback done = {});
	/// Sends A_GroupValue_Read and waits for the first A_GroupValue_Response (Timeout after `timeout`).
	void groupRead(knx::GroupAddress address, dpt::DptId dpt, GroupReadCallback done,
		std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));

	// --- Commissioning ----------------------------------------------------------------------------------------
	/// FR-COM-1: waits (up to options.timeout) for exactly one device in programming mode, checks that
	/// `target` is free, writes it, verifies and restarts the device. Errors: Timeout (no device),
	/// Conflict (several devices in programming mode / address in use / a run already active),
	/// ProtocolError (verification failed), Cancelled, IoError (not connected / connection lost).
	void programIndividualAddress(knx::IndividualAddress target, ProgramOptions options, ProgramCallback done,
		ProgressCallback progress = {});
	/// Ends an active programming run with Cancelled.
	void cancelProgramming();
	/// FR-COM-4: device descriptor 0, serial number (PID 11) and manufacturer (PID 12) over a
	/// transport connection.
	void readDeviceInfo(knx::IndividualAddress device, DeviceInfoCallback done);
	/// Read-only: broadcasts A_IndividualAddress_Read and collects every answer for `window`, i.e. the
	/// addresses of the devices in programming mode, in arrival order. One address listed twice means two
	/// devices share it.
	/// Read-only: runs `reads` in order over one transport connection to `device`. Each read succeeds or
	/// fails on its own (a refused property does not stop the rest); a lost connection fails the remainder.
	void readManagement(knx::IndividualAddress device, QList<ManagementRead> reads, ManagementReadCallback done);
	void readProgrammingMode(AddressListCallback done, std::chrono::milliseconds window = std::chrono::milliseconds(3000));

	// --- Discovery (FR-BUS-1) ---------------------------------------------------------------------------------
	/// Runs a bus::Discovery on the bus thread. Independent of the tunnel connection.
	void discover(DiscoveryOptions options, DiscoveryCallback done);

	// --- Group monitor (FR-BUS-5) -----------------------------------------------------------------------------
	/// DPT used to fill TelegramRecord::decoded for a group address (thread-safe; groupWrite/groupRead
	/// register theirs automatically).
	void setGroupAddressDpt(knx::GroupAddress address, dpt::DptId dpt);
	void clearGroupAddressDpts();
	/// Ring buffer size (default 10000); the oldest records are dropped beyond it.
	void setMonitorCapacity(std::size_t capacity);
	/// Takes every buffered record (oldest first) and re-arms telegramsAvailable().
	QList<TelegramRecord> takeTelegrams();
	/// Records dropped because the buffer was full, since the session was created.
	std::uint64_t droppedTelegrams() const;

Q_SIGNALS:
	void stateChanged(kestrel::knx::LinkState state);
	/// The connection ended without disconnectFromInterface() (connect failure, timeout, server gone).
	void connectionLost(const QString& reason);
	/// New records are waiting in the monitor buffer.
	void telegramsAvailable();

private:
	friend class detail::BusWorker;

	/// Runs `fn` on the bus thread.
	void post(std::function<void(detail::BusWorker&)> fn);

	QThread m_thread;
	detail::BusWorker* m_worker = nullptr;
	std::shared_ptr<detail::SharedState> m_shared;
	knx::LinkState m_state = knx::LinkState::Disconnected;
	knx::IndividualAddress m_localAddress;
};

} // namespace kestrel::app

Q_DECLARE_METATYPE(kestrel::app::DeviceInfo)
Q_DECLARE_METATYPE(kestrel::app::GroupReadResult)
