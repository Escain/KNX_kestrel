#pragma once

// Sans-IO KNXnet/IP tunnelling server in front of a VirtualBus (Kestrel spec 04 §5; KNX 3/8/2,
// 3/8/4). The I/O owner (simnet::KnxIpServer, or a test) feeds received datagrams with their
// sender endpoint and executes the SendDatagram actions.
//
// Handled: SEARCH, DESCRIPTION, CONNECT (tunnel, link layer), CONNECTIONSTATE, DISCONNECT and
// TUNNELLING_REQUEST (→ TUNNELLING_ACK, L_Data.con, then the frame goes on the bus). Bus frames are
// forwarded to every connected client as L_Data.ind, one outstanding TUNNELLING_REQUEST per
// channel (ack timeout 1 s, 1 retry, then the channel is closed). A channel without
// CONNECTIONSTATE_REQUEST for 120 s is dropped.
//
// Frames from the bus may arrive between calls (devices react synchronously): call takeActions()
// after anything that can touch the bus, not only after onDatagram()/tick().

#include "kestrel/base/Bytes.hpp"
#include "kestrel/knx/Address.hpp"
#include "kestrel/knx/Cemi.hpp"
#include "kestrel/knx/Clock.hpp"
#include "kestrel/knx/KnxIp.hpp"
#include "kestrel/sim/VirtualBus.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <ostream>
#include <variant>
#include <vector>

namespace kestrel::sim
{

struct TunnelServerConfig
{
	/// Our own endpoint as announced in HPAIs; simnet sets it once the socket is bound.
	knx::knxip::IpEndpoint localEndpoint{{127, 0, 0, 1}, knx::knxip::kDefaultPort};
	/// Device information DIB (its address is the interface's own IA).
	knx::knxip::DeviceInfo deviceInfo{0x02, false, knx::IndividualAddress(0x11FF), 0, {}, knx::knxip::kMulticastAddress,
		{}, "Kestrel simulator"};
	/// One tunnel slot per address; the default single slot uses 1.1.255.
	std::vector<knx::IndividualAddress> tunnelAddresses{knx::IndividualAddress(0x11FF)};
	knx::Duration ackTimeout = std::chrono::seconds(1);
	int ackRetries = 1;
	knx::Duration connectionTimeout = std::chrono::seconds(120);
};

class TunnelServer
{
public:
	struct SendDatagram
	{
		knx::knxip::IpEndpoint to;
		Bytes bytes;
		friend bool operator==(const SendDatagram&, const SendDatagram&) = default;
	};
	struct ClientConnected
	{
		std::uint8_t channel = 0;
		knx::IndividualAddress address;
		friend bool operator==(const ClientConnected&, const ClientConnected&) = default;
	};
	struct ClientDisconnected
	{
		std::uint8_t channel = 0;
		friend bool operator==(const ClientDisconnected&, const ClientDisconnected&) = default;
	};

	using Action = std::variant<SendDatagram, ClientConnected, ClientDisconnected>;

	explicit TunnelServer(VirtualBus& bus, TunnelServerConfig config = {});
	~TunnelServer();
	TunnelServer(const TunnelServer&) = delete;
	TunnelServer& operator=(const TunnelServer&) = delete;

	void setLocalEndpoint(const knx::knxip::IpEndpoint& endpoint) { m_config.localEndpoint = endpoint; }
	void setProgrammingMode(bool on) { m_config.deviceInfo.programmingMode = on; }

	void onDatagram(const knx::knxip::IpEndpoint& from, ByteView bytes, knx::TimePoint now);
	void tick(knx::TimePoint now);
	std::vector<Action> takeActions();

	std::size_t connectionCount() const { return m_channels.size(); }
	EndpointId endpoint() const { return m_endpoint; }

private:
	struct Channel
	{
		std::uint8_t id = 0;
		knx::IndividualAddress address;
		knx::knxip::IpEndpoint control;
		knx::knxip::IpEndpoint data;
		std::uint8_t inSeq = 0;
		std::uint8_t outSeq = 0;
		std::deque<Bytes> outQueue; ///< cEMI frames; front() is in flight when awaitingAck
		bool awaitingAck = false;
		knx::TimePoint ackDeadline{};
		int retries = 0;
		knx::TimePoint lastHeartbeat{};
	};

	void send(const knx::knxip::IpEndpoint& to, const knx::knxip::Frame& frame);
	Channel* find(std::uint8_t id);
	void closeChannel(std::uint8_t id, bool notifyClient);
	void enqueue(Channel& channel, Bytes cemi);
	void pump(Channel& channel);
	void handleConnect(const knx::knxip::IpEndpoint& from, const knx::knxip::ConnectRequest& request);
	void handleTunnelling(const knx::knxip::IpEndpoint& from, const knx::knxip::TunnellingRequest& request);
	void onBusFrame(const knx::cemi::LData& frame);
	static knx::knxip::IpEndpoint resolve(const knx::knxip::Hpai& hpai, const knx::knxip::IpEndpoint& from);

	VirtualBus& m_bus;
	TunnelServerConfig m_config;
	EndpointId m_endpoint = 0;
	std::vector<Channel> m_channels;
	std::uint8_t m_nextChannel = 1;
	std::vector<Action> m_actions;
	/// Latest time seen through onDatagram()/tick(); bus frames carry no time of their own.
	knx::TimePoint m_now{};
};

std::ostream& operator<<(std::ostream& os, const TunnelServer::SendDatagram& action);
std::ostream& operator<<(std::ostream& os, const TunnelServer::ClientConnected& action);
std::ostream& operator<<(std::ostream& os, const TunnelServer::ClientDisconnected& action);
std::ostream& operator<<(std::ostream& os, const TunnelServer::Action& action);

} // namespace kestrel::sim
