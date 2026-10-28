#include "kestrel/sim/TunnelServer.hpp"

#include <algorithm>

namespace kestrel::sim
{

namespace knxip = knx::knxip;

namespace
{

std::vector<knxip::ServiceFamily> supportedFamilies()
{
	return {{knxip::family::kCore, 1}, {knxip::family::kDeviceManagement, 1}, {knxip::family::kTunnelling, 1}};
}

} // namespace

TunnelServer::TunnelServer(VirtualBus& bus, TunnelServerConfig config) : m_bus(bus), m_config(std::move(config))
{
	m_endpoint = m_bus.attach([this](const knx::cemi::LData& frame) { onBusFrame(frame); });
}

TunnelServer::~TunnelServer()
{
	m_bus.detach(m_endpoint);
}

knxip::IpEndpoint TunnelServer::resolve(const knxip::Hpai& hpai, const knxip::IpEndpoint& from)
{
	return hpai.endpoint.isNat() ? from : hpai.endpoint;
}

void TunnelServer::send(const knxip::IpEndpoint& to, const knxip::Frame& frame)
{
	m_actions.push_back(SendDatagram{to, knxip::encode(frame)});
}

TunnelServer::Channel* TunnelServer::find(std::uint8_t id)
{
	const auto it = std::find_if(m_channels.begin(), m_channels.end(), [id](const Channel& c) { return c.id == id; });
	return it == m_channels.end() ? nullptr : &*it;
}

void TunnelServer::closeChannel(std::uint8_t id, bool notifyClient)
{
	const auto it = std::find_if(m_channels.begin(), m_channels.end(), [id](const Channel& c) { return c.id == id; });
	if (it == m_channels.end())
	{
		return;
	}
	if (notifyClient)
	{
		send(it->control, knxip::DisconnectRequest{id, knxip::Hpai{knxip::HostProtocol::Udp, m_config.localEndpoint}});
	}
	m_channels.erase(it);
	m_actions.push_back(ClientDisconnected{id});
}

void TunnelServer::enqueue(Channel& channel, Bytes cemi)
{
	channel.outQueue.push_back(std::move(cemi));
	pump(channel);
}

void TunnelServer::pump(Channel& channel)
{
	if (channel.awaitingAck || channel.outQueue.empty())
	{
		return;
	}
	channel.awaitingAck = true;
	channel.retries = 0;
	channel.ackDeadline = m_now + m_config.ackTimeout;
	send(channel.data, knxip::TunnellingRequest{channel.id, channel.outSeq, channel.outQueue.front()});
}

void TunnelServer::onDatagram(const knxip::IpEndpoint& from, ByteView bytes, knx::TimePoint now)
{
	m_now = now;
	auto decoded = knxip::decode(bytes);
	if (!decoded)
	{
		return;
	}
	const knxip::Frame& frame = *decoded;
	const knxip::Hpai self{knxip::HostProtocol::Udp, m_config.localEndpoint};

	if (const auto* search = std::get_if<knxip::SearchRequest>(&frame))
	{
		send(resolve(search->discoveryEndpoint, from),
			knxip::SearchResponse{self, m_config.deviceInfo, supportedFamilies()});
	}
	else if (const auto* description = std::get_if<knxip::DescriptionRequest>(&frame))
	{
		send(resolve(description->controlEndpoint, from),
			knxip::DescriptionResponse{m_config.deviceInfo, supportedFamilies()});
	}
	else if (const auto* connect = std::get_if<knxip::ConnectRequest>(&frame))
	{
		handleConnect(from, *connect);
	}
	else if (const auto* state = std::get_if<knxip::ConnectionStateRequest>(&frame))
	{
		auto* channel = find(state->channelId);
		if (channel)
		{
			channel->lastHeartbeat = now;
		}
		send(resolve(state->controlEndpoint, from),
			knxip::ConnectionStateResponse{state->channelId,
				channel ? knxip::status::kNoError : knxip::status::kConnectionId});
	}
	else if (const auto* disconnect = std::get_if<knxip::DisconnectRequest>(&frame))
	{
		const bool known = find(disconnect->channelId) != nullptr;
		send(resolve(disconnect->controlEndpoint, from),
			knxip::DisconnectResponse{disconnect->channelId,
				known ? knxip::status::kNoError : knxip::status::kConnectionId});
		closeChannel(disconnect->channelId, false);
	}
	else if (const auto* tunnelling = std::get_if<knxip::TunnellingRequest>(&frame))
	{
		handleTunnelling(from, *tunnelling);
	}
	else if (const auto* ack = std::get_if<knxip::TunnellingAck>(&frame))
	{
		auto* channel = find(ack->channelId);
		if (!channel || !channel->awaitingAck || ack->sequence != channel->outSeq)
		{
			return;
		}
		channel->awaitingAck = false;
		channel->outQueue.pop_front();
		++channel->outSeq;
		pump(*channel);
	}
}

void TunnelServer::handleConnect(const knxip::IpEndpoint& from, const knxip::ConnectRequest& request)
{
	const auto control = resolve(request.controlEndpoint, from);
	auto reject = [&](std::uint8_t status) { send(control, knxip::ConnectResponse{0, status, {}, {}, {}}); };

	if (request.cri.connectionType != knxip::kTunnelConnection)
	{
		reject(knxip::status::kConnectionType);
		return;
	}
	if (request.cri.knxLayer != knxip::kTunnelLinkLayer)
	{
		reject(knxip::status::kTunnellingLayer);
		return;
	}
	const auto freeAddress = std::find_if(m_config.tunnelAddresses.begin(), m_config.tunnelAddresses.end(),
		[this](knx::IndividualAddress address)
		{
			return std::none_of(m_channels.begin(), m_channels.end(),
				[address](const Channel& c) { return c.address == address; });
		});
	if (freeAddress == m_config.tunnelAddresses.end())
	{
		reject(knxip::status::kNoMoreConnections);
		return;
	}

	Channel channel;
	do
	{
		channel.id = m_nextChannel++;
	} while (channel.id == 0 || find(channel.id) != nullptr);
	channel.address = *freeAddress;
	channel.control = control;
	channel.data = resolve(request.dataEndpoint, from);
	channel.lastHeartbeat = m_now;
	m_channels.push_back(channel);

	knxip::ConnectResponse response;
	response.channelId = channel.id;
	response.dataEndpoint = knxip::Hpai{knxip::HostProtocol::Udp, m_config.localEndpoint};
	response.address = channel.address;
	send(control, response);
	m_actions.push_back(ClientConnected{channel.id, channel.address});
}

void TunnelServer::handleTunnelling(const knxip::IpEndpoint& /*from*/, const knxip::TunnellingRequest& request)
{
	auto* channel = find(request.channelId);
	if (!channel)
	{
		return;
	}
	if (request.sequence == static_cast<std::uint8_t>(channel->inSeq - 1))
	{
		send(channel->data, knxip::TunnellingAck{channel->id, request.sequence, knxip::status::kNoError});
		return; // repetition: acknowledge, do not forward twice
	}
	if (request.sequence != channel->inSeq)
	{
		return;
	}
	send(channel->data, knxip::TunnellingAck{channel->id, request.sequence, knxip::status::kNoError});
	++channel->inSeq;

	auto frame = knx::cemi::decode(request.cemi);
	if (!frame || frame->messageCode != knx::cemi::MessageCode::LDataReq)
	{
		return;
	}
	if (frame->source == 0)
	{
		frame->source = channel->address.raw();
	}
	const auto senderId = channel->id;

	auto confirmation = *frame;
	confirmation.messageCode = knx::cemi::MessageCode::LDataCon;
	confirmation.confirmError = false;
	enqueue(*channel, knx::cemi::encode(confirmation));

	auto indication = *frame;
	indication.messageCode = knx::cemi::MessageCode::LDataInd;
	const auto indicationBytes = knx::cemi::encode(indication);
	for (auto& other : m_channels)
	{
		if (other.id != senderId)
		{
			enqueue(other, indicationBytes);
		}
	}
	// May synchronously produce device answers, which come back through onBusFrame().
	m_bus.transmit(m_endpoint, std::move(*frame));
}

void TunnelServer::onBusFrame(const knx::cemi::LData& frame)
{
	auto indication = frame;
	indication.messageCode = knx::cemi::MessageCode::LDataInd;
	const auto bytes = knx::cemi::encode(indication);
	for (auto& channel : m_channels)
	{
		enqueue(channel, bytes);
	}
}

void TunnelServer::tick(knx::TimePoint now)
{
	m_now = now;
	std::vector<std::uint8_t> toClose;
	for (auto& channel : m_channels)
	{
		if (now - channel.lastHeartbeat >= m_config.connectionTimeout)
		{
			toClose.push_back(channel.id);
			continue;
		}
		if (channel.awaitingAck && now >= channel.ackDeadline)
		{
			if (channel.retries >= m_config.ackRetries)
			{
				toClose.push_back(channel.id); // 3/8/4 §2.6: the client is gone
				continue;
			}
			++channel.retries;
			channel.ackDeadline = now + m_config.ackTimeout;
			send(channel.data, knxip::TunnellingRequest{channel.id, channel.outSeq, channel.outQueue.front()});
		}
	}
	for (const auto id : toClose)
	{
		closeChannel(id, true);
	}
}

std::vector<TunnelServer::Action> TunnelServer::takeActions()
{
	std::vector<Action> out;
	out.swap(m_actions);
	return out;
}

std::ostream& operator<<(std::ostream& os, const TunnelServer::SendDatagram& a)
{
	os << "SendDatagram{" << a.to << ' ';
	if (auto frame = knxip::decode(a.bytes))
	{
		os << *frame;
	}
	else
	{
		os << toHex(a.bytes);
	}
	return os << '}';
}

std::ostream& operator<<(std::ostream& os, const TunnelServer::ClientConnected& a)
{
	return os << "ClientConnected{ch=" << static_cast<unsigned>(a.channel) << ' ' << a.address << '}';
}

std::ostream& operator<<(std::ostream& os, const TunnelServer::ClientDisconnected& a)
{
	return os << "ClientDisconnected{ch=" << static_cast<unsigned>(a.channel) << '}';
}

std::ostream& operator<<(std::ostream& os, const TunnelServer::Action& action)
{
	std::visit([&os](const auto& a) { os << a; }, action);
	return os;
}

} // namespace kestrel::sim
