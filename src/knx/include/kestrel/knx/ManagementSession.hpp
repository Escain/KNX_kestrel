#pragma once

// Sans-IO connection-oriented point-to-point management client (Kestrel spec 04 §3; KNX 3/3/4
// transport layer "connection-oriented communication mode", 3/3/7 application services).
//
// Requests are queued and executed one at a time over a single transport connection to `target`:
//   - T_Connect is sent before the first request (and after any disconnect).
//   - Each T_Data_Connected waits for its T_ACK (ackTimeout 3 s, max 3 repetitions; a T_NAK
//     triggers an immediate repetition). Exhausting the repetitions fails every request and
//     disconnects (AckTimeout).
//   - Responses are T_Data_Connected from the target: acknowledged in order; a duplicate
//     (seq = expected − 1) is acknowledged again but not delivered; other sequence numbers get T_NAK.
//   - A T_ACK/T_NAK whose sequence number is not the one awaited fails every request and disconnects.
//   - Responses must match the request (descriptor type, memory address, property object/id/start);
//     a stray response, e.g. the late answer to a request that already timed out, is acknowledged and
//     dropped.
//   - After idleTimeout (6 s) without traffic and with nothing queued, T_Disconnect is sent: devices
//     drop idle connections after 6 s anyway.
// Memory services are split into chunks of at most 12 bytes (one standard frame).

#include "kestrel/base/Bytes.hpp"
#include "kestrel/base/Result.hpp"
#include "kestrel/knx/Address.hpp"
#include "kestrel/knx/Apdu.hpp"
#include "kestrel/knx/Cemi.hpp"
#include "kestrel/knx/Clock.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <ostream>
#include <variant>
#include <vector>

namespace kestrel::knx
{

struct ManagementSessionConfig
{
	Duration ackTimeout = std::chrono::seconds(3);
	int maxRepetitions = 3;
	/// Time for the application response once the request was acknowledged.
	Duration responseTimeout = std::chrono::seconds(3);
	Duration idleTimeout = std::chrono::seconds(6);
	std::uint8_t maxMemoryChunk = 12;
};

enum class ManagementDisconnectReason
{
	UserClose,
	IdleTimeout,
	RemoteDisconnect, ///< T_Disconnect from the device (busy, or its own timeout)
	AckTimeout,       ///< no T_ACK after the repetitions
	DeviceRestart,    ///< after a successful A_Restart the device has dropped the connection
	ProtocolError,    ///< T_ACK/T_NAK with an unexpected sequence number (3/3/4 style 1)
};

std::ostream& operator<<(std::ostream& os, ManagementDisconnectReason reason);

class ManagementSession
{
public:
	using RequestId = std::uint32_t;

	struct SendFrame
	{
		cemi::LData frame;
		friend bool operator==(const SendFrame&, const SendFrame&) = default;
	};
	/// Result data: descriptor bytes (deviceDescriptorRead), memory bytes (memoryRead), property
	/// data (propertyValueRead); empty for memoryWrite and restart.
	struct Completed
	{
		RequestId id = 0;
		Bytes data;
		friend bool operator==(const Completed&, const Completed&) = default;
	};
	struct Failed
	{
		RequestId id = 0;
		Error error;
		friend bool operator==(const Failed&, const Failed&) = default;
	};
	struct Disconnected
	{
		ManagementDisconnectReason reason = ManagementDisconnectReason::UserClose;
		friend bool operator==(const Disconnected&, const Disconnected&) = default;
	};

	using Action = std::variant<SendFrame, Completed, Failed, Disconnected>;

	explicit ManagementSession(IndividualAddress target, ManagementSessionConfig config = {});

	/// Our own individual address (e.g. the tunnel's); when set, frames addressed elsewhere are ignored.
	void setLocalAddress(IndividualAddress address) { m_local = address; }

	RequestId deviceDescriptorRead(TimePoint now, std::uint8_t descriptorType = 0);
	RequestId memoryRead(std::uint16_t address, std::size_t length, TimePoint now);
	RequestId memoryWrite(std::uint16_t address, ByteView data, TimePoint now);
	RequestId propertyValueRead(std::uint8_t objectIndex, std::uint8_t propertyId, std::uint8_t count,
		std::uint16_t startIndex, TimePoint now);
	/// Completed data: the property data of the device's answer (for PID_LOAD_STATE_CONTROL, the new
	/// load state); a count-0 answer fails the request.
	RequestId propertyValueWrite(std::uint8_t objectIndex, std::uint8_t propertyId, std::uint8_t count,
		std::uint16_t startIndex, ByteView data, TimePoint now);
	RequestId restart(TimePoint now);
	/// Cancels queued requests (Failed/Cancelled) and disconnects.
	void close(TimePoint now);

	void onFrame(const cemi::LData& frame, TimePoint now);
	void tick(TimePoint now);
	std::vector<Action> takeActions();

	IndividualAddress target() const { return m_target; }
	bool connected() const { return m_connected; }
	bool busy() const { return !m_operations.empty(); }

private:
	enum class Kind
	{
		DeviceDescriptorRead,
		MemoryRead,
		MemoryWrite,
		PropertyValueRead,
		PropertyValueWrite,
		Restart,
	};

	struct Operation
	{
		RequestId id = 0;
		Kind kind = Kind::DeviceDescriptorRead;
		std::uint16_t address = 0;
		std::size_t length = 0;
		Bytes payload;
		std::uint8_t descriptorType = 0;
		std::uint8_t objectIndex = 0;
		std::uint8_t propertyId = 0;
		std::uint8_t count = 0;
		std::uint16_t startIndex = 0;
		std::size_t offset = 0;
		Bytes result;
	};

	enum class Phase
	{
		Idle,
		WaitAck,
		WaitResponse,
	};

	RequestId enqueue(Operation operation, TimePoint now);
	void startNext(TimePoint now);
	Apdu buildRequest(const Operation& operation) const;
	bool expectsResponse(const Operation& operation) const;
	/// True when `apdu` is the response to the current exchange of `operation`.
	bool answers(const Operation& operation, const Apdu& apdu) const;
	void transmit(const Tpdu& tpdu, TimePoint now);
	void sendRequest(TimePoint now);
	void onAcked(TimePoint now);
	void onResponse(const Apdu& apdu, TimePoint now);
	void completeCurrent(TimePoint now);
	void failCurrent(Error error, TimePoint now);
	void failAll(const Error& error);
	void disconnect(ManagementDisconnectReason reason, bool sendDisconnect);

	IndividualAddress m_target;
	IndividualAddress m_local;
	ManagementSessionConfig m_config;
	std::vector<Action> m_actions;

	std::deque<Operation> m_operations; ///< front() is current
	RequestId m_nextId = 1;

	bool m_connected = false;
	std::uint8_t m_sendSeq = 0;
	bool m_anyAcked = false; ///< m_sendSeq - 1 was acknowledged on this connection
	std::uint8_t m_recvSeq = 0;
	Phase m_phase = Phase::Idle;
	Apdu m_request;
	int m_repetitions = 0;
	TimePoint m_deadline{};
	std::optional<Apdu> m_earlyResponse; ///< response that overtook the T_ACK
	TimePoint m_lastActivity{};
};

std::ostream& operator<<(std::ostream& os, const ManagementSession::SendFrame& action);
std::ostream& operator<<(std::ostream& os, const ManagementSession::Completed& action);
std::ostream& operator<<(std::ostream& os, const ManagementSession::Failed& action);
std::ostream& operator<<(std::ostream& os, const ManagementSession::Disconnected& action);
std::ostream& operator<<(std::ostream& os, const ManagementSession::Action& action);

} // namespace kestrel::knx
