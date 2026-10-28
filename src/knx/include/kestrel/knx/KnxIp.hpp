#pragma once

// KNXnet/IP frames (KNX Standard 3/8/2 Core, 3/8/3 Device Management, 3/8/4 Tunnelling,
// 3/8/5 Routing). Every frame: header [06][10][service type u16][total length u16] + body.
// Only the services Kestrel needs are modelled; `decode` returns Unsupported for others.

#include "kestrel/base/Bytes.hpp"
#include "kestrel/base/Result.hpp"
#include "kestrel/knx/Address.hpp"
#include "kestrel/knx/SerialNumber.hpp"

#include <array>
#include <cstdint>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

namespace kestrel::knx::knxip
{

constexpr std::uint8_t kHeaderLength = 0x06;
constexpr std::uint8_t kProtocolVersion = 0x10;
constexpr std::uint16_t kDefaultPort = 3671;
/// 224.0.23.12, the KNXnet/IP system setup multicast address.
constexpr std::array<std::uint8_t, 4> kMulticastAddress{224, 0, 23, 12};

enum class ServiceType : std::uint16_t
{
	SearchRequest = 0x0201,
	SearchResponse = 0x0202,
	DescriptionRequest = 0x0203,
	DescriptionResponse = 0x0204,
	ConnectRequest = 0x0205,
	ConnectResponse = 0x0206,
	ConnectionStateRequest = 0x0207,
	ConnectionStateResponse = 0x0208,
	DisconnectRequest = 0x0209,
	DisconnectResponse = 0x020A,
	TunnellingRequest = 0x0420,
	TunnellingAck = 0x0421,
	RoutingIndication = 0x0530,
};

/// Status / error codes (3/8/2 §7.5.4 and 3/8/4).
namespace status
{
constexpr std::uint8_t kNoError = 0x00;
constexpr std::uint8_t kHostProtocolType = 0x01;
constexpr std::uint8_t kVersionNotSupported = 0x02;
constexpr std::uint8_t kSequenceNumber = 0x04;
constexpr std::uint8_t kConnectionId = 0x21;
constexpr std::uint8_t kConnectionType = 0x22;
constexpr std::uint8_t kConnectionOption = 0x23;
constexpr std::uint8_t kNoMoreConnections = 0x24;
constexpr std::uint8_t kDataConnection = 0x26;
constexpr std::uint8_t kKnxConnection = 0x27;
constexpr std::uint8_t kTunnellingLayer = 0x29;
} // namespace status

/// Connection types (CRI/CRD) and tunnelling layers.
constexpr std::uint8_t kDeviceMgmtConnection = 0x03;
constexpr std::uint8_t kTunnelConnection = 0x04;
constexpr std::uint8_t kTunnelLinkLayer = 0x02;

enum class HostProtocol : std::uint8_t
{
	Udp = 0x01,
	Tcp = 0x02,
};

struct IpEndpoint
{
	std::array<std::uint8_t, 4> address{};
	std::uint16_t port = 0;

	/// 0.0.0.0:0 — "answer to the sender" (NAT mode, 3/8/2 §8.6.3.2).
	bool isNat() const { return port == 0 && address == std::array<std::uint8_t, 4>{}; }

	friend auto operator<=>(const IpEndpoint&, const IpEndpoint&) = default;
};

/// Host Protocol Address Information.
struct Hpai
{
	HostProtocol protocol = HostProtocol::Udp;
	IpEndpoint endpoint;

	static Hpai nat() { return {}; }

	friend bool operator==(const Hpai&, const Hpai&) = default;
};

/// Service family ids for the supported-service-families DIB.
namespace family
{
constexpr std::uint8_t kCore = 0x02;
constexpr std::uint8_t kDeviceManagement = 0x03;
constexpr std::uint8_t kTunnelling = 0x04;
constexpr std::uint8_t kRouting = 0x05;
} // namespace family

struct ServiceFamily
{
	std::uint8_t id = 0;
	std::uint8_t version = 1;

	friend bool operator==(const ServiceFamily&, const ServiceFamily&) = default;
};

/// Device information DIB (type 0x01, 54 bytes).
struct DeviceInfo
{
	std::uint8_t medium = 0x02; ///< 0x02 = TP1
	bool programmingMode = false;
	IndividualAddress address;
	std::uint16_t projectInstallationId = 0;
	SerialNumber serial;
	std::array<std::uint8_t, 4> multicastAddress = kMulticastAddress;
	std::array<std::uint8_t, 6> macAddress{};
	/// At most 30 characters (ISO 8859-1); longer names are truncated on encode.
	std::string friendlyName;

	friend bool operator==(const DeviceInfo&, const DeviceInfo&) = default;
};

/// Connection request information. Tunnel connections carry the KNX layer.
struct Cri
{
	std::uint8_t connectionType = kTunnelConnection;
	std::uint8_t knxLayer = kTunnelLinkLayer;

	friend bool operator==(const Cri&, const Cri&) = default;
};

struct SearchRequest
{
	Hpai discoveryEndpoint;
	friend bool operator==(const SearchRequest&, const SearchRequest&) = default;
};

struct SearchResponse
{
	Hpai controlEndpoint;
	DeviceInfo deviceInfo;
	std::vector<ServiceFamily> serviceFamilies;
	friend bool operator==(const SearchResponse&, const SearchResponse&) = default;
};

struct DescriptionRequest
{
	Hpai controlEndpoint;
	friend bool operator==(const DescriptionRequest&, const DescriptionRequest&) = default;
};

struct DescriptionResponse
{
	DeviceInfo deviceInfo;
	std::vector<ServiceFamily> serviceFamilies;
	friend bool operator==(const DescriptionResponse&, const DescriptionResponse&) = default;
};

struct ConnectRequest
{
	Hpai controlEndpoint;
	Hpai dataEndpoint;
	Cri cri;
	friend bool operator==(const ConnectRequest&, const ConnectRequest&) = default;
};

/// On error only channelId and status are encoded.
struct ConnectResponse
{
	std::uint8_t channelId = 0;
	std::uint8_t status = status::kNoError;
	Hpai dataEndpoint;
	std::uint8_t connectionType = kTunnelConnection;
	/// Tunnel CRD: the individual address assigned to the tunnel.
	IndividualAddress address;
	friend bool operator==(const ConnectResponse&, const ConnectResponse&) = default;
};

struct ConnectionStateRequest
{
	std::uint8_t channelId = 0;
	Hpai controlEndpoint;
	friend bool operator==(const ConnectionStateRequest&, const ConnectionStateRequest&) = default;
};

struct ConnectionStateResponse
{
	std::uint8_t channelId = 0;
	std::uint8_t status = status::kNoError;
	friend bool operator==(const ConnectionStateResponse&, const ConnectionStateResponse&) = default;
};

struct DisconnectRequest
{
	std::uint8_t channelId = 0;
	Hpai controlEndpoint;
	friend bool operator==(const DisconnectRequest&, const DisconnectRequest&) = default;
};

struct DisconnectResponse
{
	std::uint8_t channelId = 0;
	std::uint8_t status = status::kNoError;
	friend bool operator==(const DisconnectResponse&, const DisconnectResponse&) = default;
};

/// The cEMI payload is kept raw: decoding it is the receiver's business (it may be a
/// non-L_Data message such as M_PropRead).
struct TunnellingRequest
{
	std::uint8_t channelId = 0;
	std::uint8_t sequence = 0;
	Bytes cemi;
	friend bool operator==(const TunnellingRequest&, const TunnellingRequest&) = default;
};

struct TunnellingAck
{
	std::uint8_t channelId = 0;
	std::uint8_t sequence = 0;
	std::uint8_t status = status::kNoError;
	friend bool operator==(const TunnellingAck&, const TunnellingAck&) = default;
};

struct RoutingIndication
{
	Bytes cemi;
	friend bool operator==(const RoutingIndication&, const RoutingIndication&) = default;
};

using Frame = std::variant<SearchRequest, SearchResponse, DescriptionRequest, DescriptionResponse, ConnectRequest,
	ConnectResponse, ConnectionStateRequest, ConnectionStateResponse, DisconnectRequest, DisconnectResponse,
	TunnellingRequest, TunnellingAck, RoutingIndication>;

ServiceType serviceType(const Frame& frame);
Bytes encode(const Frame& frame);
/// Validates header, version and total length; unknown service types → Unsupported.
Result<Frame> decode(ByteView bytes);

std::ostream& operator<<(std::ostream& os, ServiceType type);
std::ostream& operator<<(std::ostream& os, const IpEndpoint& endpoint);
std::ostream& operator<<(std::ostream& os, const Hpai& hpai);
std::ostream& operator<<(std::ostream& os, const ServiceFamily& family);
std::ostream& operator<<(std::ostream& os, const DeviceInfo& info);
std::ostream& operator<<(std::ostream& os, const Cri& cri);
std::ostream& operator<<(std::ostream& os, const SearchRequest& frame);
std::ostream& operator<<(std::ostream& os, const SearchResponse& frame);
std::ostream& operator<<(std::ostream& os, const DescriptionRequest& frame);
std::ostream& operator<<(std::ostream& os, const DescriptionResponse& frame);
std::ostream& operator<<(std::ostream& os, const ConnectRequest& frame);
std::ostream& operator<<(std::ostream& os, const ConnectResponse& frame);
std::ostream& operator<<(std::ostream& os, const ConnectionStateRequest& frame);
std::ostream& operator<<(std::ostream& os, const ConnectionStateResponse& frame);
std::ostream& operator<<(std::ostream& os, const DisconnectRequest& frame);
std::ostream& operator<<(std::ostream& os, const DisconnectResponse& frame);
std::ostream& operator<<(std::ostream& os, const TunnellingRequest& frame);
std::ostream& operator<<(std::ostream& os, const TunnellingAck& frame);
std::ostream& operator<<(std::ostream& os, const RoutingIndication& frame);
std::ostream& operator<<(std::ostream& os, const Frame& frame);

} // namespace kestrel::knx::knxip
