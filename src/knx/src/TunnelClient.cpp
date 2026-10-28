#include "kestrel/knx/TunnelClient.hpp"

#include "kestrel/knx/KnxIp.hpp"

namespace kestrel::knx
{

TunnelClient::TunnelClient(TunnelClientConfig config) : m_config(config) {}

void TunnelClient::connect(TimePoint now)
{
	if (m_state != TunnelState::Disconnected)
	{
		return;
	}
	m_state = TunnelState::Connecting;
	m_abandonedConnect = false;
	m_connectDeadline = now + m_config.connectTimeout;
	knxip::ConnectRequest request;
	request.controlEndpoint = knxip::Hpai::nat();
	request.dataEndpoint = knxip::Hpai::nat();
	m_actions.push_back(SendControl{knxip::encode(request)});
}

void TunnelClient::disconnect(TimePoint /*now*/)
{
	if (m_state == TunnelState::Disconnected)
	{
		return;
	}
	drop(DisconnectReason::UserRequest, 0, true);
}

Status TunnelClient::send(const cemi::LData& frame, TimePoint now)
{
	if (m_state != TunnelState::Connected)
	{
		return makeError(ErrorCode::IoError, "tunnel not connected");
	}
	if (frame.tpdu.empty())
	{
		return makeError(ErrorCode::InvalidArgument, "L_Data without TPDU");
	}
	cemi::LData request = frame;
	request.messageCode = cemi::MessageCode::LDataReq;
	m_queue.push_back(std::move(request));
	pump(now);
	return ok();
}

void TunnelClient::pump(TimePoint now)
{
	if (m_phase == SendPhase::Idle && !m_queue.empty() && m_state == TunnelState::Connected)
	{
		m_retries = 0;
		sendTunnellingRequest(now);
	}
}

void TunnelClient::sendTunnellingRequest(TimePoint now)
{
	m_phase = SendPhase::WaitAck;
	m_phaseDeadline = now + m_config.ackTimeout;
	m_actions.push_back(SendData{knxip::encode(knxip::TunnellingRequest{m_channel, m_outSeq, cemi::encode(m_queue.front())})});
}

void TunnelClient::finishOutstanding(bool ok, TimePoint now)
{
	m_queue.pop_front();
	m_phase = SendPhase::Idle;
	m_actions.push_back(SendConfirmed{ok});
	pump(now);
}

void TunnelClient::sendHeartbeat(TimePoint now)
{
	m_heartbeatDeadline = now + m_config.heartbeatTimeout;
	m_actions.push_back(SendControl{knxip::encode(knxip::ConnectionStateRequest{m_channel, knxip::Hpai::nat()})});
}

void TunnelClient::drop(DisconnectReason reason, std::uint8_t status, bool sendDisconnect)
{
	if (m_state == TunnelState::Connecting)
	{
		m_abandonedConnect = true;
	}
	if (sendDisconnect && m_state == TunnelState::Connected)
	{
		m_actions.push_back(SendControl{knxip::encode(knxip::DisconnectRequest{m_channel, knxip::Hpai::nat()})});
	}
	// Every accepted frame gets exactly one confirmation, even when the link goes away.
	for (std::size_t i = 0; i < m_queue.size(); ++i)
	{
		m_actions.push_back(SendConfirmed{false});
	}
	m_queue.clear();
	m_phase = SendPhase::Idle;
	m_heartbeatDeadline.reset();
	m_heartbeatFailures = 0;
	m_state = TunnelState::Disconnected;
	m_actions.push_back(Disconnected{reason, status});
}

void TunnelClient::onDatagram(ByteView bytes, TimePoint now)
{
	auto decoded = knxip::decode(bytes);
	if (!decoded)
	{
		return; // not for us / garbage: UDP gives no guarantees
	}
	const knxip::Frame& frame = *decoded;

	if (const auto* response = std::get_if<knxip::ConnectResponse>(&frame))
	{
		if (m_state != TunnelState::Connecting)
		{
			if (m_state == TunnelState::Disconnected && m_abandonedConnect
				&& response->status == knxip::status::kNoError)
			{
				// We gave up on this connect; release the slot the server allocated anyway.
				m_abandonedConnect = false;
				m_actions.push_back(
					SendControl{knxip::encode(knxip::DisconnectRequest{response->channelId, knxip::Hpai::nat()})});
			}
			return;
		}
		if (response->status != knxip::status::kNoError)
		{
			drop(DisconnectReason::ConnectRejected, response->status, false);
			return;
		}
		m_state = TunnelState::Connected;
		m_channel = response->channelId;
		m_address = response->address;
		m_outSeq = 0;
		m_inSeq = 0;
		m_nextHeartbeat = now + m_config.heartbeatInterval;
		m_actions.push_back(Connected{m_channel, m_address});
		return;
	}

	if (m_state != TunnelState::Connected)
	{
		return;
	}

	if (const auto* ack = std::get_if<knxip::TunnellingAck>(&frame))
	{
		if (ack->channelId != m_channel || m_phase != SendPhase::WaitAck || ack->sequence != m_outSeq)
		{
			return;
		}
		if (ack->status != knxip::status::kNoError)
		{
			finishOutstanding(false, now);
			return;
		}
		++m_outSeq;
		m_phase = SendPhase::WaitConfirm;
		m_phaseDeadline = now + m_config.confirmTimeout;
	}
	else if (const auto* request = std::get_if<knxip::TunnellingRequest>(&frame))
	{
		handleTunnellingRequest(request->channelId, request->sequence, request->cemi, now);
	}
	else if (const auto* state = std::get_if<knxip::ConnectionStateResponse>(&frame))
	{
		if (state->channelId != m_channel || !m_heartbeatDeadline)
		{
			return;
		}
		if (state->status != knxip::status::kNoError)
		{
			// The server no longer knows the channel: retrying cannot help. For data/KNX connection
			// errors the channel still exists on the server, so release it explicitly.
			const bool channelExists = state->status == knxip::status::kDataConnection
				|| state->status == knxip::status::kKnxConnection;
			drop(DisconnectReason::HeartbeatLost, state->status, channelExists);
			return;
		}
		m_heartbeatDeadline.reset();
		m_heartbeatFailures = 0;
		m_nextHeartbeat = now + m_config.heartbeatInterval;
	}
	else if (const auto* disconnectRequest = std::get_if<knxip::DisconnectRequest>(&frame))
	{
		if (disconnectRequest->channelId != m_channel)
		{
			return;
		}
		m_actions.push_back(SendControl{knxip::encode(knxip::DisconnectResponse{m_channel, knxip::status::kNoError})});
		drop(DisconnectReason::ServerDisconnect, 0, false);
	}
}

void TunnelClient::handleTunnellingRequest(std::uint8_t channel, std::uint8_t sequence, ByteView cemiBytes,
	TimePoint now)
{
	if (channel != m_channel)
	{
		return;
	}
	if (sequence == static_cast<std::uint8_t>(m_inSeq - 1))
	{
		// Our previous ACK was lost: acknowledge again but do not deliver twice (3/8/4 §2.6).
		m_actions.push_back(SendData{knxip::encode(knxip::TunnellingAck{m_channel, sequence, knxip::status::kNoError})});
		return;
	}
	if (sequence != m_inSeq)
	{
		return;
	}
	m_actions.push_back(SendData{knxip::encode(knxip::TunnellingAck{m_channel, sequence, knxip::status::kNoError})});
	++m_inSeq;

	auto frame = cemi::decode(cemiBytes);
	if (!frame)
	{
		return;
	}
	if (frame->messageCode == cemi::MessageCode::LDataCon)
	{
		// Only the con of the outstanding frame counts: a late con of a frame that already timed out
		// must not confirm its successor.
		const auto& pending = m_queue.empty() ? *frame : m_queue.front();
		const bool matches = !m_queue.empty() && frame->destination == pending.destination
			&& frame->groupDestination == pending.groupDestination && frame->tpdu == pending.tpdu;
		if (m_phase == SendPhase::WaitConfirm && matches)
		{
			finishOutstanding(!frame->confirmError, now);
		}
		return;
	}
	if (frame->messageCode == cemi::MessageCode::LDataInd)
	{
		m_actions.push_back(FrameReceived{std::move(*frame)});
	}
}

void TunnelClient::tick(TimePoint now)
{
	if (m_state == TunnelState::Connecting)
	{
		if (now >= m_connectDeadline)
		{
			drop(DisconnectReason::ConnectTimeout, 0, false);
		}
		return;
	}
	if (m_state != TunnelState::Connected)
	{
		return;
	}

	if (m_phase == SendPhase::WaitAck && now >= m_phaseDeadline)
	{
		if (m_retries < m_config.ackRetries)
		{
			++m_retries;
			sendTunnellingRequest(now);
		}
		else
		{
			drop(DisconnectReason::AckTimeout, 0, true);
			return;
		}
	}
	else if (m_phase == SendPhase::WaitConfirm && now >= m_phaseDeadline)
	{
		finishOutstanding(false, now);
	}

	if (m_heartbeatDeadline)
	{
		if (now >= *m_heartbeatDeadline)
		{
			++m_heartbeatFailures;
			if (m_heartbeatFailures >= m_config.heartbeatMaxFailures)
			{
				drop(DisconnectReason::HeartbeatLost, 0, true);
				return;
			}
			sendHeartbeat(now);
		}
	}
	else if (now >= m_nextHeartbeat)
	{
		sendHeartbeat(now);
	}
}

std::vector<TunnelClient::Action> TunnelClient::takeActions()
{
	std::vector<Action> out;
	out.swap(m_actions);
	return out;
}

std::ostream& operator<<(std::ostream& os, TunnelState state)
{
	switch (state)
	{
	case TunnelState::Disconnected: return os << "Disconnected";
	case TunnelState::Connecting: return os << "Connecting";
	case TunnelState::Connected: return os << "Connected";
	}
	return os << "TunnelState(" << static_cast<int>(state) << ')';
}

std::ostream& operator<<(std::ostream& os, DisconnectReason reason)
{
	switch (reason)
	{
	case DisconnectReason::UserRequest: return os << "UserRequest";
	case DisconnectReason::ConnectTimeout: return os << "ConnectTimeout";
	case DisconnectReason::ConnectRejected: return os << "ConnectRejected";
	case DisconnectReason::AckTimeout: return os << "AckTimeout";
	case DisconnectReason::HeartbeatLost: return os << "HeartbeatLost";
	case DisconnectReason::ServerDisconnect: return os << "ServerDisconnect";
	}
	return os << "DisconnectReason(" << static_cast<int>(reason) << ')';
}

namespace
{

void printDatagram(std::ostream& os, const char* label, const Bytes& bytes)
{
	os << label << '{';
	if (auto frame = knxip::decode(bytes))
	{
		os << *frame;
	}
	else
	{
		os << toHex(bytes);
	}
	os << '}';
}

} // namespace

std::ostream& operator<<(std::ostream& os, const TunnelClient::SendControl& a)
{
	printDatagram(os, "SendControl", a.bytes);
	return os;
}

std::ostream& operator<<(std::ostream& os, const TunnelClient::SendData& a)
{
	printDatagram(os, "SendData", a.bytes);
	return os;
}

std::ostream& operator<<(std::ostream& os, const TunnelClient::Connected& a)
{
	return os << "Connected{ch=" << static_cast<unsigned>(a.channel) << ' ' << a.address << '}';
}

std::ostream& operator<<(std::ostream& os, const TunnelClient::Disconnected& a)
{
	return os << "Disconnected{" << a.reason << " status=0x" << std::hex << static_cast<unsigned>(a.status) << std::dec
			  << '}';
}

std::ostream& operator<<(std::ostream& os, const TunnelClient::FrameReceived& a)
{
	return os << "FrameReceived{" << a.frame << '}';
}

std::ostream& operator<<(std::ostream& os, const TunnelClient::SendConfirmed& a)
{
	return os << "SendConfirmed{" << (a.ok ? "ok" : "failed") << '}';
}

std::ostream& operator<<(std::ostream& os, const TunnelClient::Action& action)
{
	std::visit([&os](const auto& a) { os << a; }, action);
	return os;
}

} // namespace kestrel::knx
