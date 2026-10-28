#include "kestrel/knx/KnxIp.hpp"

#include <algorithm>
#include <type_traits>

namespace kestrel::knx::knxip
{

namespace
{

constexpr std::uint8_t kHpaiLength = 0x08;
constexpr std::uint8_t kDibDeviceInfo = 0x01;
constexpr std::uint8_t kDibServiceFamilies = 0x02;
constexpr std::uint8_t kDeviceInfoLength = 0x36;
constexpr std::size_t kFriendlyNameLength = 30;
constexpr std::uint8_t kConnectionHeaderLength = 0x04;

// ---- encoding helpers ------------------------------------------------------------------------

void writeHpai(ByteWriter& writer, const Hpai& hpai)
{
	writer.u8(kHpaiLength);
	writer.u8(static_cast<std::uint8_t>(hpai.protocol));
	writer.append(hpai.endpoint.address);
	writer.u16(hpai.endpoint.port);
}

void writeDeviceInfo(ByteWriter& writer, const DeviceInfo& info)
{
	writer.u8(kDeviceInfoLength);
	writer.u8(kDibDeviceInfo);
	writer.u8(info.medium);
	writer.u8(info.programmingMode ? 0x01 : 0x00);
	writer.u16(info.address.raw());
	writer.u16(info.projectInstallationId);
	writer.append(info.serial.bytes);
	writer.append(info.multicastAddress);
	writer.append(info.macAddress);
	for (std::size_t i = 0; i < kFriendlyNameLength; ++i)
	{
		writer.u8(i < info.friendlyName.size() ? static_cast<std::uint8_t>(info.friendlyName[i]) : 0);
	}
}

void writeServiceFamilies(ByteWriter& writer, const std::vector<ServiceFamily>& families)
{
	writer.u8(static_cast<std::uint8_t>(2 + 2 * families.size()));
	writer.u8(kDibServiceFamilies);
	for (const auto& family : families)
	{
		writer.u8(family.id);
		writer.u8(family.version);
	}
}

void writeBody(ByteWriter& w, const SearchRequest& f) { writeHpai(w, f.discoveryEndpoint); }

void writeBody(ByteWriter& w, const SearchResponse& f)
{
	writeHpai(w, f.controlEndpoint);
	writeDeviceInfo(w, f.deviceInfo);
	writeServiceFamilies(w, f.serviceFamilies);
}

void writeBody(ByteWriter& w, const DescriptionRequest& f) { writeHpai(w, f.controlEndpoint); }

void writeBody(ByteWriter& w, const DescriptionResponse& f)
{
	writeDeviceInfo(w, f.deviceInfo);
	writeServiceFamilies(w, f.serviceFamilies);
}

void writeBody(ByteWriter& w, const ConnectRequest& f)
{
	writeHpai(w, f.controlEndpoint);
	writeHpai(w, f.dataEndpoint);
	if (f.cri.connectionType == kTunnelConnection)
	{
		w.u8(0x04);
		w.u8(f.cri.connectionType);
		w.u8(f.cri.knxLayer);
		w.u8(0x00);
	}
	else
	{
		w.u8(0x02);
		w.u8(f.cri.connectionType);
	}
}

void writeBody(ByteWriter& w, const ConnectResponse& f)
{
	w.u8(f.channelId);
	w.u8(f.status);
	if (f.status != status::kNoError)
	{
		return;
	}
	writeHpai(w, f.dataEndpoint);
	if (f.connectionType == kTunnelConnection)
	{
		w.u8(0x04);
		w.u8(f.connectionType);
		w.u16(f.address.raw());
	}
	else
	{
		w.u8(0x02);
		w.u8(f.connectionType);
	}
}

void writeBody(ByteWriter& w, const ConnectionStateRequest& f)
{
	w.u8(f.channelId);
	w.u8(0x00);
	writeHpai(w, f.controlEndpoint);
}

void writeBody(ByteWriter& w, const ConnectionStateResponse& f)
{
	w.u8(f.channelId);
	w.u8(f.status);
}

void writeBody(ByteWriter& w, const DisconnectRequest& f)
{
	w.u8(f.channelId);
	w.u8(0x00);
	writeHpai(w, f.controlEndpoint);
}

void writeBody(ByteWriter& w, const DisconnectResponse& f)
{
	w.u8(f.channelId);
	w.u8(f.status);
}

void writeBody(ByteWriter& w, const TunnellingRequest& f)
{
	w.u8(kConnectionHeaderLength);
	w.u8(f.channelId);
	w.u8(f.sequence);
	w.u8(0x00);
	w.append(f.cemi);
}

void writeBody(ByteWriter& w, const TunnellingAck& f)
{
	w.u8(kConnectionHeaderLength);
	w.u8(f.channelId);
	w.u8(f.sequence);
	w.u8(f.status);
}

void writeBody(ByteWriter& w, const RoutingIndication& f) { w.append(f.cemi); }

// ---- decoding helpers ------------------------------------------------------------------------

Error parseError(const std::string& what)
{
	return Error{ErrorCode::ParseError, "KNXnet/IP: " + what};
}

bool readHpai(ByteReader& reader, Hpai& hpai)
{
	const auto length = reader.u8();
	const auto protocol = reader.u8();
	const auto address = reader.take(4);
	hpai.endpoint.port = reader.u16();
	if (reader.failed() || length != kHpaiLength || (protocol != 0x01 && protocol != 0x02))
	{
		return false;
	}
	hpai.protocol = static_cast<HostProtocol>(protocol);
	std::copy(address.begin(), address.end(), hpai.endpoint.address.begin());
	return true;
}

bool readDeviceInfo(ByteView dib, DeviceInfo& info)
{
	ByteReader reader(dib);
	if (dib.size() != kDeviceInfoLength)
	{
		return false;
	}
	reader.skip(2);
	info.medium = reader.u8();
	info.programmingMode = (reader.u8() & 0x01) != 0;
	info.address = IndividualAddress(reader.u16());
	info.projectInstallationId = reader.u16();
	const auto serial = reader.take(6);
	const auto multicast = reader.take(4);
	const auto mac = reader.take(6);
	const auto name = reader.take(kFriendlyNameLength);
	if (reader.failed())
	{
		return false;
	}
	std::copy(serial.begin(), serial.end(), info.serial.bytes.begin());
	std::copy(multicast.begin(), multicast.end(), info.multicastAddress.begin());
	std::copy(mac.begin(), mac.end(), info.macAddress.begin());
	const auto nameEnd = std::find(name.begin(), name.end(), std::uint8_t{0});
	info.friendlyName.assign(name.begin(), nameEnd);
	return true;
}

/// Reads the DIBs of a search/description response; unknown DIB types are skipped.
Result<std::pair<DeviceInfo, std::vector<ServiceFamily>>> readDibs(ByteReader& reader)
{
	std::pair<DeviceInfo, std::vector<ServiceFamily>> out;
	bool haveDeviceInfo = false;
	while (!reader.atEnd())
	{
		const auto rest = reader.rest();
		if (rest.size() < 2 || rest[0] < 2 || rest[0] > rest.size())
		{
			return Unexpected{parseError("malformed DIB")};
		}
		const auto dib = reader.take(rest[0]);
		if (dib[1] == kDibDeviceInfo)
		{
			if (!readDeviceInfo(dib, out.first))
			{
				return Unexpected{parseError("malformed device information DIB")};
			}
			haveDeviceInfo = true;
		}
		else if (dib[1] == kDibServiceFamilies)
		{
			if (dib.size() % 2 != 0)
			{
				return Unexpected{parseError("malformed service families DIB")};
			}
			for (std::size_t i = 2; i < dib.size(); i += 2)
			{
				out.second.push_back(ServiceFamily{dib[i], dib[i + 1]});
			}
		}
	}
	if (!haveDeviceInfo)
	{
		return Unexpected{parseError("missing device information DIB")};
	}
	return out;
}

/// CRI (connect request) or CRD (connect response) block: [length][type][...].
bool readConnectionBlock(ByteReader& reader, std::uint8_t& type, ByteView& payload)
{
	const auto length = reader.u8();
	if (reader.failed() || length < 2)
	{
		return false;
	}
	type = reader.u8();
	payload = reader.take(static_cast<std::size_t>(length - 2));
	return !reader.failed();
}

Result<Frame> decodeBody(ServiceType type, ByteReader& reader)
{
	switch (type)
	{
	case ServiceType::SearchRequest:
	case ServiceType::DescriptionRequest:
	{
		Hpai hpai;
		if (!readHpai(reader, hpai))
		{
			return Unexpected{parseError("malformed HPAI")};
		}
		if (type == ServiceType::SearchRequest)
		{
			return Frame{SearchRequest{hpai}};
		}
		return Frame{DescriptionRequest{hpai}};
	}
	case ServiceType::SearchResponse:
	{
		SearchResponse frame;
		if (!readHpai(reader, frame.controlEndpoint))
		{
			return Unexpected{parseError("malformed HPAI")};
		}
		KESTREL_TRY(auto dibs, readDibs(reader));
		frame.deviceInfo = std::move(dibs.first);
		frame.serviceFamilies = std::move(dibs.second);
		return Frame{std::move(frame)};
	}
	case ServiceType::DescriptionResponse:
	{
		DescriptionResponse frame;
		KESTREL_TRY(auto dibs, readDibs(reader));
		frame.deviceInfo = std::move(dibs.first);
		frame.serviceFamilies = std::move(dibs.second);
		return Frame{std::move(frame)};
	}
	case ServiceType::ConnectRequest:
	{
		ConnectRequest frame;
		if (!readHpai(reader, frame.controlEndpoint) || !readHpai(reader, frame.dataEndpoint))
		{
			return Unexpected{parseError("malformed HPAI")};
		}
		ByteView payload;
		if (!readConnectionBlock(reader, frame.cri.connectionType, payload))
		{
			return Unexpected{parseError("malformed CRI")};
		}
		frame.cri.knxLayer = payload.empty() ? 0 : payload[0];
		return Frame{std::move(frame)};
	}
	case ServiceType::ConnectResponse:
	{
		ConnectResponse frame;
		frame.channelId = reader.u8();
		frame.status = reader.u8();
		if (reader.failed())
		{
			return Unexpected{parseError("truncated CONNECT_RESPONSE")};
		}
		if (frame.status != status::kNoError)
		{
			reader.skip(reader.remaining()); // servers may or may not append HPAI/CRD on error
			return Frame{std::move(frame)};
		}
		ByteView payload;
		if (!readHpai(reader, frame.dataEndpoint) || !readConnectionBlock(reader, frame.connectionType, payload))
		{
			return Unexpected{parseError("malformed CONNECT_RESPONSE")};
		}
		if (frame.connectionType == kTunnelConnection)
		{
			if (payload.size() < 2)
			{
				return Unexpected{parseError("tunnel CRD without individual address")};
			}
			frame.address = IndividualAddress(static_cast<std::uint16_t>((payload[0] << 8) | payload[1]));
		}
		return Frame{std::move(frame)};
	}
	case ServiceType::ConnectionStateRequest:
	case ServiceType::DisconnectRequest:
	{
		const auto channel = reader.u8();
		reader.skip(1);
		Hpai hpai;
		if (!readHpai(reader, hpai))
		{
			return Unexpected{parseError("malformed HPAI")};
		}
		if (type == ServiceType::ConnectionStateRequest)
		{
			return Frame{ConnectionStateRequest{channel, hpai}};
		}
		return Frame{DisconnectRequest{channel, hpai}};
	}
	case ServiceType::ConnectionStateResponse:
	case ServiceType::DisconnectResponse:
	{
		const auto channel = reader.u8();
		const auto code = reader.u8();
		if (reader.failed())
		{
			return Unexpected{parseError("truncated response")};
		}
		if (type == ServiceType::ConnectionStateResponse)
		{
			return Frame{ConnectionStateResponse{channel, code}};
		}
		return Frame{DisconnectResponse{channel, code}};
	}
	case ServiceType::TunnellingRequest:
	case ServiceType::TunnellingAck:
	{
		const auto length = reader.u8();
		const auto channel = reader.u8();
		const auto sequence = reader.u8();
		const auto last = reader.u8();
		if (reader.failed() || length != kConnectionHeaderLength)
		{
			return Unexpected{parseError("malformed connection header")};
		}
		if (type == ServiceType::TunnellingAck)
		{
			return Frame{TunnellingAck{channel, sequence, last}};
		}
		const auto cemi = reader.rest();
		reader.skip(cemi.size());
		if (cemi.empty())
		{
			return Unexpected{parseError("TUNNELLING_REQUEST without cEMI")};
		}
		return Frame{TunnellingRequest{channel, sequence, Bytes(cemi.begin(), cemi.end())}};
	}
	case ServiceType::RoutingIndication:
	{
		const auto cemi = reader.rest();
		reader.skip(cemi.size());
		return Frame{RoutingIndication{Bytes(cemi.begin(), cemi.end())}};
	}
	}
	return makeError(ErrorCode::Unsupported, "KNXnet/IP: unsupported service type");
}

void printAddress(std::ostream& os, const std::array<std::uint8_t, 4>& address)
{
	os << static_cast<unsigned>(address[0]) << '.' << static_cast<unsigned>(address[1]) << '.'
	   << static_cast<unsigned>(address[2]) << '.' << static_cast<unsigned>(address[3]);
}

template <typename T>
void printList(std::ostream& os, const std::vector<T>& items)
{
	os << '[';
	for (std::size_t i = 0; i < items.size(); ++i)
	{
		os << (i ? ", " : "") << items[i];
	}
	os << ']';
}

std::ostream& printStatus(std::ostream& os, std::uint8_t code)
{
	return os << " status=0x" << std::hex << static_cast<unsigned>(code) << std::dec;
}

} // namespace

ServiceType serviceType(const Frame& frame)
{
	return std::visit(
		[](const auto& f) -> ServiceType
		{
			using T = std::decay_t<decltype(f)>;
			if constexpr (std::is_same_v<T, SearchRequest>) return ServiceType::SearchRequest;
			else if constexpr (std::is_same_v<T, SearchResponse>) return ServiceType::SearchResponse;
			else if constexpr (std::is_same_v<T, DescriptionRequest>) return ServiceType::DescriptionRequest;
			else if constexpr (std::is_same_v<T, DescriptionResponse>) return ServiceType::DescriptionResponse;
			else if constexpr (std::is_same_v<T, ConnectRequest>) return ServiceType::ConnectRequest;
			else if constexpr (std::is_same_v<T, ConnectResponse>) return ServiceType::ConnectResponse;
			else if constexpr (std::is_same_v<T, ConnectionStateRequest>) return ServiceType::ConnectionStateRequest;
			else if constexpr (std::is_same_v<T, ConnectionStateResponse>) return ServiceType::ConnectionStateResponse;
			else if constexpr (std::is_same_v<T, DisconnectRequest>) return ServiceType::DisconnectRequest;
			else if constexpr (std::is_same_v<T, DisconnectResponse>) return ServiceType::DisconnectResponse;
			else if constexpr (std::is_same_v<T, TunnellingRequest>) return ServiceType::TunnellingRequest;
			else if constexpr (std::is_same_v<T, TunnellingAck>) return ServiceType::TunnellingAck;
			else return ServiceType::RoutingIndication;
		},
		frame);
}

Bytes encode(const Frame& frame)
{
	Bytes out;
	ByteWriter writer(out);
	writer.u8(kHeaderLength);
	writer.u8(kProtocolVersion);
	writer.u16(static_cast<std::uint16_t>(serviceType(frame)));
	writer.u16(0); // patched below
	std::visit([&writer](const auto& f) { writeBody(writer, f); }, frame);
	writer.patchU16(4, static_cast<std::uint16_t>(out.size()));
	return out;
}

Result<Frame> decode(ByteView bytes)
{
	ByteReader reader(bytes);
	const auto headerLength = reader.u8();
	const auto version = reader.u8();
	const auto type = reader.u16();
	const auto totalLength = reader.u16();
	if (reader.failed())
	{
		return Unexpected{parseError("truncated header")};
	}
	if (headerLength != kHeaderLength)
	{
		return Unexpected{parseError("bad header length")};
	}
	if (version != kProtocolVersion)
	{
		return makeError(ErrorCode::Unsupported, "KNXnet/IP: unsupported protocol version");
	}
	if (totalLength != bytes.size())
	{
		return Unexpected{parseError("total length does not match datagram size")};
	}
	KESTREL_TRY(Frame frame, decodeBody(static_cast<ServiceType>(type), reader));
	if (reader.failed())
	{
		return Unexpected{parseError("truncated body")};
	}
	if (!reader.atEnd())
	{
		return Unexpected{parseError("trailing bytes")};
	}
	return frame;
}

std::ostream& operator<<(std::ostream& os, ServiceType type)
{
	switch (type)
	{
	case ServiceType::SearchRequest: return os << "SEARCH_REQUEST";
	case ServiceType::SearchResponse: return os << "SEARCH_RESPONSE";
	case ServiceType::DescriptionRequest: return os << "DESCRIPTION_REQUEST";
	case ServiceType::DescriptionResponse: return os << "DESCRIPTION_RESPONSE";
	case ServiceType::ConnectRequest: return os << "CONNECT_REQUEST";
	case ServiceType::ConnectResponse: return os << "CONNECT_RESPONSE";
	case ServiceType::ConnectionStateRequest: return os << "CONNECTIONSTATE_REQUEST";
	case ServiceType::ConnectionStateResponse: return os << "CONNECTIONSTATE_RESPONSE";
	case ServiceType::DisconnectRequest: return os << "DISCONNECT_REQUEST";
	case ServiceType::DisconnectResponse: return os << "DISCONNECT_RESPONSE";
	case ServiceType::TunnellingRequest: return os << "TUNNELLING_REQUEST";
	case ServiceType::TunnellingAck: return os << "TUNNELLING_ACK";
	case ServiceType::RoutingIndication: return os << "ROUTING_INDICATION";
	}
	return os << "ServiceType(0x" << std::hex << static_cast<unsigned>(type) << std::dec << ')';
}

std::ostream& operator<<(std::ostream& os, const IpEndpoint& endpoint)
{
	printAddress(os, endpoint.address);
	return os << ':' << endpoint.port;
}

std::ostream& operator<<(std::ostream& os, const Hpai& hpai)
{
	return os << (hpai.protocol == HostProtocol::Udp ? "udp " : "tcp ") << hpai.endpoint;
}

std::ostream& operator<<(std::ostream& os, const ServiceFamily& family)
{
	return os << static_cast<unsigned>(family.id) << 'v' << static_cast<unsigned>(family.version);
}

std::ostream& operator<<(std::ostream& os, const DeviceInfo& info)
{
	os << "DeviceInfo{" << info.address << " serial=" << info.serial << " medium=" << static_cast<unsigned>(info.medium)
	   << (info.programmingMode ? " prog" : "") << " name='" << info.friendlyName << "' mcast=";
	printAddress(os, info.multicastAddress);
	return os << '}';
}

std::ostream& operator<<(std::ostream& os, const Cri& cri)
{
	return os << "CRI{type=" << static_cast<unsigned>(cri.connectionType)
			  << " layer=" << static_cast<unsigned>(cri.knxLayer) << '}';
}

std::ostream& operator<<(std::ostream& os, const SearchRequest& f)
{
	return os << "SEARCH_REQUEST{" << f.discoveryEndpoint << '}';
}

std::ostream& operator<<(std::ostream& os, const SearchResponse& f)
{
	os << "SEARCH_RESPONSE{" << f.controlEndpoint << ' ' << f.deviceInfo << ' ';
	printList(os, f.serviceFamilies);
	return os << '}';
}

std::ostream& operator<<(std::ostream& os, const DescriptionRequest& f)
{
	return os << "DESCRIPTION_REQUEST{" << f.controlEndpoint << '}';
}

std::ostream& operator<<(std::ostream& os, const DescriptionResponse& f)
{
	os << "DESCRIPTION_RESPONSE{" << f.deviceInfo << ' ';
	printList(os, f.serviceFamilies);
	return os << '}';
}

std::ostream& operator<<(std::ostream& os, const ConnectRequest& f)
{
	return os << "CONNECT_REQUEST{ctrl=" << f.controlEndpoint << " data=" << f.dataEndpoint << ' ' << f.cri << '}';
}

std::ostream& operator<<(std::ostream& os, const ConnectResponse& f)
{
	os << "CONNECT_RESPONSE{ch=" << static_cast<unsigned>(f.channelId);
	printStatus(os, f.status);
	if (f.status == status::kNoError)
	{
		os << " data=" << f.dataEndpoint << " type=" << static_cast<unsigned>(f.connectionType) << " ia=" << f.address;
	}
	return os << '}';
}

std::ostream& operator<<(std::ostream& os, const ConnectionStateRequest& f)
{
	return os << "CONNECTIONSTATE_REQUEST{ch=" << static_cast<unsigned>(f.channelId) << ' ' << f.controlEndpoint << '}';
}

std::ostream& operator<<(std::ostream& os, const ConnectionStateResponse& f)
{
	os << "CONNECTIONSTATE_RESPONSE{ch=" << static_cast<unsigned>(f.channelId);
	return printStatus(os, f.status) << '}';
}

std::ostream& operator<<(std::ostream& os, const DisconnectRequest& f)
{
	return os << "DISCONNECT_REQUEST{ch=" << static_cast<unsigned>(f.channelId) << ' ' << f.controlEndpoint << '}';
}

std::ostream& operator<<(std::ostream& os, const DisconnectResponse& f)
{
	os << "DISCONNECT_RESPONSE{ch=" << static_cast<unsigned>(f.channelId);
	return printStatus(os, f.status) << '}';
}

std::ostream& operator<<(std::ostream& os, const TunnellingRequest& f)
{
	return os << "TUNNELLING_REQUEST{ch=" << static_cast<unsigned>(f.channelId)
			  << " seq=" << static_cast<unsigned>(f.sequence) << " [" << toHex(f.cemi) << "]}";
}

std::ostream& operator<<(std::ostream& os, const TunnellingAck& f)
{
	os << "TUNNELLING_ACK{ch=" << static_cast<unsigned>(f.channelId) << " seq=" << static_cast<unsigned>(f.sequence);
	return printStatus(os, f.status) << '}';
}

std::ostream& operator<<(std::ostream& os, const RoutingIndication& f)
{
	return os << "ROUTING_INDICATION{[" << toHex(f.cemi) << "]}";
}

std::ostream& operator<<(std::ostream& os, const Frame& frame)
{
	std::visit([&os](const auto& f) { os << f; }, frame);
	return os;
}

} // namespace kestrel::knx::knxip
