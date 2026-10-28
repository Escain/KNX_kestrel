#include "kestrel/knx/ManagementSession.hpp"

#include <algorithm>

namespace kestrel::knx
{

namespace
{

constexpr std::uint8_t kSeqMask = 0x0F;
/// Descriptor type answered by devices that do not support the requested type (3/3/7 §3.4.3).
constexpr std::uint8_t kDescriptorTypeUnsupported = 0x3F;

std::uint8_t nextSeq(std::uint8_t seq)
{
	return static_cast<std::uint8_t>((seq + 1) & kSeqMask);
}

} // namespace

ManagementSession::ManagementSession(IndividualAddress target, ManagementSessionConfig config)
	: m_target(target), m_config(config)
{
}

ManagementSession::RequestId ManagementSession::enqueue(Operation operation, TimePoint now)
{
	operation.id = m_nextId++;
	const auto id = operation.id;
	m_operations.push_back(std::move(operation));
	if (m_operations.size() == 1)
	{
		startNext(now);
	}
	return id;
}

ManagementSession::RequestId ManagementSession::deviceDescriptorRead(TimePoint now, std::uint8_t descriptorType)
{
	Operation operation;
	operation.kind = Kind::DeviceDescriptorRead;
	operation.descriptorType = descriptorType;
	return enqueue(std::move(operation), now);
}

ManagementSession::RequestId ManagementSession::memoryRead(std::uint16_t address, std::size_t length, TimePoint now)
{
	Operation operation;
	operation.kind = Kind::MemoryRead;
	operation.address = address;
	operation.length = length;
	return enqueue(std::move(operation), now);
}

ManagementSession::RequestId ManagementSession::memoryWrite(std::uint16_t address, ByteView data, TimePoint now)
{
	Operation operation;
	operation.kind = Kind::MemoryWrite;
	operation.address = address;
	operation.payload.assign(data.begin(), data.end());
	operation.length = data.size();
	return enqueue(std::move(operation), now);
}

ManagementSession::RequestId ManagementSession::propertyValueRead(std::uint8_t objectIndex, std::uint8_t propertyId,
	std::uint8_t count, std::uint16_t startIndex, TimePoint now)
{
	Operation operation;
	operation.kind = Kind::PropertyValueRead;
	operation.objectIndex = objectIndex;
	operation.propertyId = propertyId;
	operation.count = count;
	operation.startIndex = startIndex;
	return enqueue(std::move(operation), now);
}

ManagementSession::RequestId ManagementSession::propertyValueWrite(std::uint8_t objectIndex, std::uint8_t propertyId,
	std::uint8_t count, std::uint16_t startIndex, ByteView data, TimePoint now)
{
	Operation operation;
	operation.kind = Kind::PropertyValueWrite;
	operation.objectIndex = objectIndex;
	operation.propertyId = propertyId;
	operation.count = count;
	operation.startIndex = startIndex;
	operation.payload.assign(data.begin(), data.end());
	return enqueue(std::move(operation), now);
}

ManagementSession::RequestId ManagementSession::restart(TimePoint now)
{
	Operation operation;
	operation.kind = Kind::Restart;
	return enqueue(std::move(operation), now);
}

void ManagementSession::close(TimePoint /*now*/)
{
	failAll(Error{ErrorCode::Cancelled, "management session closed"});
	if (m_connected)
	{
		disconnect(ManagementDisconnectReason::UserClose, true);
	}
}

void ManagementSession::transmit(const Tpdu& tpdu, TimePoint now)
{
	m_lastActivity = now;
	m_actions.push_back(SendFrame{makeIndividualFrame(m_target, tpdu)});
}

void ManagementSession::startNext(TimePoint now)
{
	if (m_phase != Phase::Idle || m_operations.empty())
	{
		return;
	}
	auto& operation = m_operations.front();
	if (operation.kind == Kind::MemoryRead || operation.kind == Kind::MemoryWrite)
	{
		if (operation.length == 0 || operation.address + operation.length > 0x10000)
		{
			failCurrent(Error{ErrorCode::InvalidArgument, "memory range is empty or exceeds 64 KiB"}, now);
			return;
		}
	}
	if (!m_connected)
	{
		m_connected = true;
		m_sendSeq = 0;
		m_recvSeq = 0;
		m_anyAcked = false;
		// A connection left open by an earlier client run with our address (crash, interface teardown
		// before the T_Disconnect went out) would keep the device's sequence numbers, so the device would
		// take our first request for a repetition and never answer. A T_Disconnect from anyone but the
		// device's current partner is ignored, so closing first is always safe.
		transmit(Tpdu::disconnect(), now);
		transmit(Tpdu::connect(), now);
	}
	m_request = buildRequest(operation);
	m_repetitions = 0;
	sendRequest(now);
}

Apdu ManagementSession::buildRequest(const Operation& operation) const
{
	const auto chunk = static_cast<std::uint8_t>(
		std::min<std::size_t>(operation.length - std::min(operation.offset, operation.length), m_config.maxMemoryChunk));
	const auto address = static_cast<std::uint16_t>(operation.address + operation.offset);
	switch (operation.kind)
	{
	case Kind::DeviceDescriptorRead: return knx::deviceDescriptorRead(operation.descriptorType);
	case Kind::MemoryRead: return knx::memoryRead(address, chunk);
	case Kind::MemoryWrite:
		return knx::memoryWrite(address, ByteView(operation.payload).subspan(operation.offset, chunk));
	case Kind::PropertyValueRead:
		return knx::propertyValueRead(operation.objectIndex, operation.propertyId, operation.count, operation.startIndex);
	case Kind::PropertyValueWrite:
		return knx::propertyValueWrite(
			operation.objectIndex, operation.propertyId, operation.count, operation.startIndex, operation.payload);
	case Kind::Restart: return knx::restart();
	}
	return knx::restart();
}

bool ManagementSession::expectsResponse(const Operation& operation) const
{
	return operation.kind == Kind::DeviceDescriptorRead || operation.kind == Kind::MemoryRead
		|| operation.kind == Kind::PropertyValueRead || operation.kind == Kind::PropertyValueWrite;
}

void ManagementSession::sendRequest(TimePoint now)
{
	m_phase = Phase::WaitAck;
	m_deadline = now + m_config.ackTimeout;
	transmit(Tpdu::dataConnected(m_sendSeq, m_request), now);
}

void ManagementSession::onAcked(TimePoint now)
{
	m_sendSeq = nextSeq(m_sendSeq);
	m_anyAcked = true;
	// A response that overtook the T_ACK (possibly across repetitions) belongs to this exchange.
	auto early = std::move(m_earlyResponse);
	m_earlyResponse.reset();
	auto& operation = m_operations.front();
	if (!expectsResponse(operation))
	{
		if (operation.kind == Kind::MemoryWrite)
		{
			operation.offset += m_request.shortData;
			if (operation.offset < operation.length)
			{
				m_request = buildRequest(operation);
				m_repetitions = 0;
				sendRequest(now);
				return;
			}
		}
		completeCurrent(now);
		return;
	}
	m_phase = Phase::WaitResponse;
	m_deadline = now + m_config.responseTimeout;
	if (early)
	{
		onResponse(*early, now);
	}
}

bool ManagementSession::answers(const Operation& operation, const Apdu& apdu) const
{
	switch (operation.kind)
	{
	case Kind::DeviceDescriptorRead:
		return apdu.apci == Apci::DeviceDescriptorResponse
			&& (apdu.shortData == operation.descriptorType || apdu.shortData == kDescriptorTypeUnsupported);
	case Kind::MemoryRead:
	{
		if (apdu.apci != Apci::MemoryResponse)
		{
			return false;
		}
		auto access = decodeMemoryAccess(apdu);
		return access && access->address == static_cast<std::uint16_t>(operation.address + operation.offset);
	}
	case Kind::PropertyValueRead:
	case Kind::PropertyValueWrite:
	{
		if (apdu.apci != Apci::PropertyValueResponse)
		{
			return false;
		}
		auto access = decodePropertyAccess(apdu);
		return access && access->objectIndex == operation.objectIndex && access->propertyId == operation.propertyId
			&& access->startIndex == operation.startIndex;
	}
	case Kind::MemoryWrite:
	case Kind::Restart:
		return false;
	}
	return false;
}

void ManagementSession::onResponse(const Apdu& apdu, TimePoint now)
{
	auto& operation = m_operations.front();
	if (!answers(operation, apdu))
	{
		return; // not the answer to our request (e.g. a late answer to a timed-out one)
	}
	switch (operation.kind)
	{
	case Kind::DeviceDescriptorRead:
	{
		if (apdu.shortData == kDescriptorTypeUnsupported)
		{
			failCurrent(Error{ErrorCode::Unsupported, "device does not support this descriptor type"}, now);
			return;
		}
		operation.result = apdu.data;
		completeCurrent(now);
		return;
	}
	case Kind::MemoryRead:
	{
		if (apdu.apci != Apci::MemoryResponse)
		{
			return;
		}
		auto access = decodeMemoryAccess(apdu);
		if (!access || access->address != static_cast<std::uint16_t>(operation.address + operation.offset))
		{
			return; // not the answer to our request
		}
		if (access->count == 0)
		{
			failCurrent(Error{ErrorCode::ProtocolError, "memory read refused by the device"}, now);
			return;
		}
		if (access->count != m_request.shortData)
		{
			failCurrent(Error{ErrorCode::ProtocolError, "memory response with unexpected length"}, now);
			return;
		}
		operation.result.insert(operation.result.end(), access->data.begin(), access->data.end());
		operation.offset += access->count;
		if (operation.offset < operation.length)
		{
			m_request = buildRequest(operation);
			m_repetitions = 0;
			sendRequest(now);
			return;
		}
		completeCurrent(now);
		return;
	}
	case Kind::PropertyValueRead:
	case Kind::PropertyValueWrite:
	{
		if (apdu.apci != Apci::PropertyValueResponse)
		{
			return;
		}
		auto access = decodePropertyAccess(apdu);
		if (!access || access->objectIndex != operation.objectIndex || access->propertyId != operation.propertyId
			|| access->startIndex != operation.startIndex)
		{
			return;
		}
		if (access->count == 0)
		{
			failCurrent(Error{ErrorCode::ProtocolError,
							operation.kind == Kind::PropertyValueWrite ? "property write refused by the device"
																		: "property read refused by the device"},
				now);
			return;
		}
		operation.result = std::move(access->data);
		completeCurrent(now);
		return;
	}
	case Kind::MemoryWrite:
	case Kind::Restart:
		return;
	}
}

void ManagementSession::completeCurrent(TimePoint now)
{
	auto operation = std::move(m_operations.front());
	m_operations.pop_front();
	m_phase = Phase::Idle;
	m_earlyResponse.reset();
	m_actions.push_back(Completed{operation.id, std::move(operation.result)});
	if (operation.kind == Kind::Restart)
	{
		// The device restarts and forgets the connection; tell it explicitly as ETS does.
		disconnect(ManagementDisconnectReason::DeviceRestart, true);
	}
	startNext(now);
}

void ManagementSession::failCurrent(Error error, TimePoint now)
{
	const auto id = m_operations.front().id;
	m_operations.pop_front();
	m_phase = Phase::Idle;
	m_earlyResponse.reset();
	m_actions.push_back(Failed{id, std::move(error)});
	startNext(now);
}

void ManagementSession::failAll(const Error& error)
{
	m_earlyResponse.reset();
	for (const auto& operation : m_operations)
	{
		m_actions.push_back(Failed{operation.id, error});
	}
	m_operations.clear();
	m_phase = Phase::Idle;
}

void ManagementSession::disconnect(ManagementDisconnectReason reason, bool sendDisconnect)
{
	m_earlyResponse.reset();
	if (sendDisconnect)
	{
		m_actions.push_back(SendFrame{makeIndividualFrame(m_target, Tpdu::disconnect())});
	}
	m_connected = false;
	m_phase = Phase::Idle;
	m_actions.push_back(Disconnected{reason});
}

void ManagementSession::onFrame(const cemi::LData& frame, TimePoint now)
{
	if (frame.groupDestination || frame.source != m_target.raw())
	{
		return;
	}
	if (m_local.raw() != 0 && frame.destination != m_local.raw())
	{
		return;
	}
	auto tpdu = decodeTpdu(frame);
	if (!tpdu || !m_connected)
	{
		return;
	}
	m_lastActivity = now;

	switch (tpdu->type)
	{
	case TpduType::Disconnect:
	{
		failAll(Error{ErrorCode::ProtocolError, "connection closed by the device"});
		disconnect(ManagementDisconnectReason::RemoteDisconnect, false);
		return;
	}
	case TpduType::Ack:
	case TpduType::Nak:
		if (m_phase != Phase::WaitAck || tpdu->seq != m_sendSeq)
		{
			// Style 1 transport: an acknowledgement we are not waiting for means the two ends are out of
			// step; continuing would risk executing or reporting the wrong request.
			// A second T_ACK for the request just acknowledged is what two devices sharing the target address
			// produce (each acknowledges; seen on a real installation), so say so instead of a bare mismatch.
			const bool repeated = tpdu->type == TpduType::Ack && m_anyAcked
				&& tpdu->seq == static_cast<std::uint8_t>((m_sendSeq - 1) & kSeqMask);
			failAll(Error{ErrorCode::ProtocolError,
				repeated ? "second T_ACK from " + m_target.toString()
						+ " for the same request: more than one device may be using this individual address"
						 : "unexpected T_ACK/T_NAK sequence number from " + m_target.toString()});
			disconnect(ManagementDisconnectReason::ProtocolError, true);
			return;
		}
		if (tpdu->type == TpduType::Ack)
		{
			onAcked(now);
		}
		else
		{
			if (++m_repetitions > m_config.maxRepetitions)
			{
				failAll(Error{ErrorCode::ProtocolError, "request rejected (T_NAK) by the device"});
				disconnect(ManagementDisconnectReason::AckTimeout, true);
				return;
			}
			sendRequest(now);
		}
		return;
	case TpduType::DataConnected:
	{
		if (tpdu->seq == static_cast<std::uint8_t>((m_recvSeq - 1) & kSeqMask))
		{
			transmit(Tpdu::ack(tpdu->seq), now); // our T_ACK was lost; do not deliver twice
			return;
		}
		if (tpdu->seq != m_recvSeq)
		{
			transmit(Tpdu::nak(tpdu->seq), now);
			return;
		}
		transmit(Tpdu::ack(tpdu->seq), now);
		m_recvSeq = nextSeq(m_recvSeq);
		if (m_phase == Phase::WaitResponse)
		{
			onResponse(tpdu->apdu, now);
		}
		else if (m_phase == Phase::WaitAck && !m_operations.empty() && answers(m_operations.front(), tpdu->apdu))
		{
			m_earlyResponse = std::move(tpdu->apdu);
		}
		return;
	}
	case TpduType::DataGroup:
	case TpduType::DataBroadcast:
	case TpduType::DataIndividual:
	case TpduType::Connect:
		return;
	}
}

void ManagementSession::tick(TimePoint now)
{
	if (m_phase == Phase::WaitAck && now >= m_deadline)
	{
		if (m_repetitions >= m_config.maxRepetitions)
		{
			failAll(Error{ErrorCode::Timeout, "no T_ACK from " + m_target.toString()});
			disconnect(ManagementDisconnectReason::AckTimeout, true);
			return;
		}
		++m_repetitions;
		sendRequest(now);
		return;
	}
	if (m_phase == Phase::WaitResponse && now >= m_deadline)
	{
		failCurrent(Error{ErrorCode::Timeout, "no response from " + m_target.toString()}, now);
		return;
	}
	if (m_connected && m_operations.empty() && now - m_lastActivity >= m_config.idleTimeout)
	{
		disconnect(ManagementDisconnectReason::IdleTimeout, true);
	}
}

std::vector<ManagementSession::Action> ManagementSession::takeActions()
{
	std::vector<Action> out;
	out.swap(m_actions);
	return out;
}

std::ostream& operator<<(std::ostream& os, ManagementDisconnectReason reason)
{
	switch (reason)
	{
	case ManagementDisconnectReason::UserClose: return os << "UserClose";
	case ManagementDisconnectReason::IdleTimeout: return os << "IdleTimeout";
	case ManagementDisconnectReason::RemoteDisconnect: return os << "RemoteDisconnect";
	case ManagementDisconnectReason::AckTimeout: return os << "AckTimeout";
	case ManagementDisconnectReason::DeviceRestart: return os << "DeviceRestart";
	case ManagementDisconnectReason::ProtocolError: return os << "ProtocolError";
	}
	return os << "ManagementDisconnectReason(" << static_cast<int>(reason) << ')';
}

std::ostream& operator<<(std::ostream& os, const ManagementSession::SendFrame& a)
{
	return os << "SendFrame{" << a.frame << '}';
}

std::ostream& operator<<(std::ostream& os, const ManagementSession::Completed& a)
{
	return os << "Completed{#" << a.id << " [" << toHex(a.data) << "]}";
}

std::ostream& operator<<(std::ostream& os, const ManagementSession::Failed& a)
{
	return os << "Failed{#" << a.id << ' ' << a.error << '}';
}

std::ostream& operator<<(std::ostream& os, const ManagementSession::Disconnected& a)
{
	return os << "Disconnected{" << a.reason << '}';
}

std::ostream& operator<<(std::ostream& os, const ManagementSession::Action& action)
{
	std::visit([&os](const auto& a) { os << a; }, action);
	return os;
}

} // namespace kestrel::knx
