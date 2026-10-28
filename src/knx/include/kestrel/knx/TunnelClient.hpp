#pragma once

// Sans-IO KNXnet/IP tunnelling client (KNX Standard 3/8/4; Kestrel spec 04 §2).
//
//   Disconnected --connect--> Connecting --CONNECT_RESPONSE ok--> Connected
//   Connected: one outstanding TUNNELLING_REQUEST, waiting first for TUNNELLING_ACK (1 s, 1 retry,
//   then connection lost) and then for the L_Data.con (3 s) before sending the next queued frame.
//   Heartbeat: CONNECTIONSTATE_REQUEST every 60 s, 10 s timeout; 1 request + 3 repetitions (4 timeouts)
//   → HeartbeatLost (3/8/2 §5.4).
//   An L_Data.con only confirms the outstanding frame when destination and TPDU match it.
//   A connect abandoned by disconnect() or timeout is cleaned up: a late successful CONNECT_RESPONSE
//   is answered with DISCONNECT_REQUEST so the server slot is not leaked.
//
// NAT mode is used (HPAI 0.0.0.0:0) so one UDP socket serves both control and data endpoints:
// SendControl and SendData go to the same remote address; the distinction is informative.
// The caller feeds datagrams and clock ticks in, and executes the returned actions.

#include "kestrel/base/Bytes.hpp"
#include "kestrel/base/Result.hpp"
#include "kestrel/knx/Address.hpp"
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

struct TunnelClientConfig
{
	Duration connectTimeout = std::chrono::seconds(10);
	Duration ackTimeout = std::chrono::seconds(1);
	int ackRetries = 1;
	Duration confirmTimeout = std::chrono::seconds(3);
	Duration heartbeatInterval = std::chrono::seconds(60);
	Duration heartbeatTimeout = std::chrono::seconds(10);
	/// Timed-out CONNECTIONSTATE_REQUESTs before giving up: 1 request + 3 repetitions.
	int heartbeatMaxFailures = 4;
};

enum class TunnelState
{
	Disconnected,
	Connecting,
	Connected,
};

enum class DisconnectReason
{
	UserRequest,
	ConnectTimeout,
	ConnectRejected,  ///< CONNECT_RESPONSE with an error status (see Disconnected::status)
	AckTimeout,       ///< TUNNELLING_ACK missing after the retry
	HeartbeatLost,    ///< CONNECTIONSTATE failures or an error status from the server
	ServerDisconnect, ///< DISCONNECT_REQUEST from the server
};

std::ostream& operator<<(std::ostream& os, TunnelState state);
std::ostream& operator<<(std::ostream& os, DisconnectReason reason);

class TunnelClient
{
public:
	/// KNXnet/IP datagram for the server's control endpoint.
	struct SendControl
	{
		Bytes bytes;
		friend bool operator==(const SendControl&, const SendControl&) = default;
	};
	/// KNXnet/IP datagram for the server's data endpoint.
	struct SendData
	{
		Bytes bytes;
		friend bool operator==(const SendData&, const SendData&) = default;
	};
	struct Connected
	{
		std::uint8_t channel = 0;
		IndividualAddress address;
		friend bool operator==(const Connected&, const Connected&) = default;
	};
	struct Disconnected
	{
		DisconnectReason reason = DisconnectReason::UserRequest;
		/// KNXnet/IP status for ConnectRejected / HeartbeatLost, else 0.
		std::uint8_t status = 0;
		friend bool operator==(const Disconnected&, const Disconnected&) = default;
	};
	/// L_Data.ind from the bus.
	struct FrameReceived
	{
		cemi::LData frame;
		friend bool operator==(const FrameReceived&, const FrameReceived&) = default;
	};
	/// One per frame accepted by send(), in order.
	struct SendConfirmed
	{
		bool ok = false;
		friend bool operator==(const SendConfirmed&, const SendConfirmed&) = default;
	};

	using Action = std::variant<SendControl, SendData, Connected, Disconnected, FrameReceived, SendConfirmed>;

	explicit TunnelClient(TunnelClientConfig config = {});

	/// Starts connecting; ignored unless Disconnected.
	void connect(TimePoint now);
	/// Sends DISCONNECT_REQUEST and drops the connection immediately. While Connecting, the
	/// attempt is abandoned (a late CONNECT_RESPONSE is then disconnected).
	void disconnect(TimePoint now);
	/// Queues an L_Data (message code forced to L_Data.req). Fails when not connected.
	Status send(const cemi::LData& frame, TimePoint now);
	void onDatagram(ByteView bytes, TimePoint now);
	void tick(TimePoint now);
	std::vector<Action> takeActions();

	TunnelState state() const { return m_state; }
	std::uint8_t channel() const { return m_channel; }
	/// Individual address assigned by the server (valid when connected).
	IndividualAddress address() const { return m_address; }
	/// Frames waiting (including the outstanding one).
	std::size_t pendingFrames() const { return m_queue.size(); }

private:
	enum class SendPhase
	{
		Idle,
		WaitAck,
		WaitConfirm,
	};

	void sendTunnellingRequest(TimePoint now);
	void pump(TimePoint now);
	void finishOutstanding(bool ok, TimePoint now);
	void sendHeartbeat(TimePoint now);
	void drop(DisconnectReason reason, std::uint8_t status, bool sendDisconnect);
	void handleTunnellingRequest(std::uint8_t channel, std::uint8_t sequence, ByteView cemiBytes, TimePoint now);

	TunnelClientConfig m_config;
	TunnelState m_state = TunnelState::Disconnected;
	std::uint8_t m_channel = 0;
	IndividualAddress m_address;
	std::vector<Action> m_actions;

	TimePoint m_connectDeadline{};
	/// A connect was abandoned: a late positive CONNECT_RESPONSE must be disconnected.
	bool m_abandonedConnect = false;

	std::deque<cemi::LData> m_queue; ///< L_Data.req frames; front() is outstanding when phase != Idle
	SendPhase m_phase = SendPhase::Idle;
	std::uint8_t m_outSeq = 0;
	std::uint8_t m_inSeq = 0;
	int m_retries = 0;
	TimePoint m_phaseDeadline{};

	TimePoint m_nextHeartbeat{};
	std::optional<TimePoint> m_heartbeatDeadline;
	int m_heartbeatFailures = 0;
};

std::ostream& operator<<(std::ostream& os, const TunnelClient::SendControl& action);
std::ostream& operator<<(std::ostream& os, const TunnelClient::SendData& action);
std::ostream& operator<<(std::ostream& os, const TunnelClient::Connected& action);
std::ostream& operator<<(std::ostream& os, const TunnelClient::Disconnected& action);
std::ostream& operator<<(std::ostream& os, const TunnelClient::FrameReceived& action);
std::ostream& operator<<(std::ostream& os, const TunnelClient::SendConfirmed& action);
std::ostream& operator<<(std::ostream& os, const TunnelClient::Action& action);

} // namespace kestrel::knx
