#include "kestrel/sim/VirtualDevice.hpp"

#include <cctype>

namespace kestrel::sim
{

using knx::Apci;
using knx::Apdu;
using knx::Tpdu;
using knx::TpduType;

namespace
{

constexpr std::uint8_t kSeqMask = 0x0F;
/// Largest memory block answered in one standard frame.
constexpr std::uint8_t kMaxMemoryChunk = 12;
constexpr std::uint8_t kDescriptorTypeUnsupported = 0x3F;

} // namespace

// ---- configuration structs ---------------------------------------------------------------------

Result<GroupObjectFlags> GroupObjectFlags::parse(std::string_view text)
{
	GroupObjectFlags flags{false, false, false, false, false, false};
	for (char c : text)
	{
		switch (std::toupper(static_cast<unsigned char>(c)))
		{
		case 'C': flags.communication = true; break;
		case 'R': flags.read = true; break;
		case 'W': flags.write = true; break;
		case 'T': flags.transmit = true; break;
		case 'U': flags.update = true; break;
		case 'I': flags.readOnInit = true; break;
		default:
			return makeError(ErrorCode::ParseError, "unknown group object flag '" + std::string(1, c) + "'");
		}
	}
	return flags;
}

std::string GroupObjectFlags::toString() const
{
	std::string out;
	if (communication) out += 'C';
	if (read) out += 'R';
	if (write) out += 'W';
	if (transmit) out += 'T';
	if (update) out += 'U';
	if (readOnInit) out += 'I';
	return out;
}

std::ostream& operator<<(std::ostream& os, const GroupObjectFlags& flags)
{
	return os << flags.toString();
}

std::ostream& operator<<(std::ostream& os, const GroupObjectConfig& config)
{
	return os << "GroupObject{" << config.address << ' ' << config.flags << ' ' << config.dpt << " ["
			  << toHex(config.value) << "]}";
}

std::ostream& operator<<(std::ostream& os, const DeviceConfig& config)
{
	os << "Device{'" << config.name << "' " << config.address << " serial=" << config.serial << " mask=0x" << std::hex
	   << config.maskVersion << std::dec << (config.progMode ? " prog" : "") << " objects=[";
	for (std::size_t i = 0; i < config.groupObjects.size(); ++i)
	{
		os << (i ? ", " : "") << config.groupObjects[i];
	}
	return os << "]}";
}

// ---- VirtualDevice -----------------------------------------------------------------------------

VirtualDevice::VirtualDevice(VirtualBus& bus, DeviceConfig config) : VirtualDevice(bus, std::move(config), Config{}) {}

VirtualDevice::VirtualDevice(VirtualBus& bus, DeviceConfig config, Config timing)
	: m_bus(bus), m_config(std::move(config)), m_timing(timing)
{
	const auto& serial = m_config.serial.bytes;
	m_properties[{0, pid::kObjectType}] = Bytes{0x00, 0x00};
	m_properties[{0, pid::kSerialNumber}] = Bytes(serial.begin(), serial.end());
	m_properties[{0, pid::kManufacturerId}] = Bytes(serial.begin(), serial.begin() + 2);
	if (m_config.maskVersion == 0x07B0)
	{
		// The interface objects of a System B device, laid out like the MEAN WELL KAA-8R this was checked
		// against: object index = load state machine index, fixed segment addresses (specs/04 field notes).
		for (const auto& [index, type] : kSystemBObjects)
		{
			m_properties[{index, pid::kObjectType}] = Bytes{static_cast<std::uint8_t>(type >> 8), static_cast<std::uint8_t>(type)};
			m_properties[{index, pid::kLoadStateControl}] = Bytes{0x00};
			m_properties[{index, pid::kTableReference}] = Bytes{0x00, 0x00, 0x00, 0x00};
			m_properties[{index, pid::kProgramVersion}] = Bytes(5, 0x00);
			m_properties[{index, pid::kMcbTable}] = Bytes(8, 0x00);
		}
	}

	for (const auto& object : m_config.groupObjects)
	{
		const bool shortForm = object.dpt.main != 0 ? dpt::isShortForm(object.dpt)
													 : object.value.size() == 1 && object.value[0] <= 0x3F;
		m_shortForm.push_back(shortForm);
	}
	m_endpoint = m_bus.attach([this](const knx::cemi::LData& frame) { onFrame(frame); });
}

std::optional<std::uint8_t> VirtualDevice::loadState(std::uint8_t objectIndex) const
{
	const auto it = m_properties.find({objectIndex, pid::kLoadStateControl});
	if (m_config.maskVersion != 0x07B0 || it == m_properties.end() || it->second.empty())
	{
		return std::nullopt;
	}
	return it->second.front();
}

std::uint8_t VirtualDevice::onLoadControl(std::uint8_t objectIndex, ByteView data)
{
	// States: 0 unloaded, 1 loaded, 2 loading, 3 error. Events: 1 start loading, 2 load completed,
	// 3 additional load control, 4 unload.
	std::uint8_t& state = m_properties[{objectIndex, pid::kLoadStateControl}].at(0);
	const std::uint8_t event = data.empty() ? 0 : data[0];
	switch (event)
	{
	case 1: state = 2; break;
	case 2: state = state == 2 ? 1 : 3; break;
	case 3:
	{
		// 0x0B: data relative allocation (size, mode, fill, 2 reserved octets).
		if (state != 2 || data.size() < 10 || data[1] != 0x0B)
		{
			state = 3;
			break;
		}
		const std::uint32_t size = (std::uint32_t{data[2]} << 24) | (std::uint32_t{data[3]} << 16)
			| (std::uint32_t{data[4]} << 8) | data[5];
		std::uint16_t base = 0;
		for (const auto& [index, type] : kSystemBObjects)
		{
			if (index == objectIndex)
			{
				base = kSystemBSegments[index - 1];
			}
		}
		if (base == 0 || std::uint32_t{base} + size > 0x10000)
		{
			state = 3;
			break;
		}
		writeMemory(base, Bytes(size, data[7]));
		m_properties[{objectIndex, pid::kTableReference}] =
			Bytes{0x00, 0x00, static_cast<std::uint8_t>(base >> 8), static_cast<std::uint8_t>(base)};
		break;
	}
	case 4: state = 0; break;
	default: state = 3; break;
	}
	return state;
}

VirtualDevice::~VirtualDevice()
{
	m_bus.detach(m_endpoint);
}

Bytes VirtualDevice::readMemory(std::uint16_t address, std::size_t length) const
{
	Bytes out;
	out.reserve(length);
	for (std::size_t i = 0; i < length; ++i)
	{
		const auto it = m_memory.find(static_cast<std::uint16_t>(address + i));
		out.push_back(it == m_memory.end() ? 0 : it->second);
	}
	return out;
}

void VirtualDevice::writeMemory(std::uint16_t address, ByteView data)
{
	for (std::size_t i = 0; i < data.size(); ++i)
	{
		m_memory[static_cast<std::uint16_t>(address + i)] = data[i];
	}
}

void VirtualDevice::setProperty(std::uint8_t objectIndex, std::uint8_t propertyId, Bytes value)
{
	m_properties[{objectIndex, propertyId}] = std::move(value);
}

std::optional<Bytes> VirtualDevice::property(std::uint8_t objectIndex, std::uint8_t propertyId) const
{
	const auto it = m_properties.find({objectIndex, propertyId});
	if (it == m_properties.end())
	{
		return std::nullopt;
	}
	return it->second;
}

std::optional<Bytes> VirtualDevice::groupValue(knx::GroupAddress address) const
{
	for (const auto& object : m_config.groupObjects)
	{
		if (object.address == address)
		{
			return object.value;
		}
	}
	return std::nullopt;
}

Status VirtualDevice::setGroupValue(knx::GroupAddress address, ByteView value)
{
	for (std::size_t i = 0; i < m_config.groupObjects.size(); ++i)
	{
		auto& object = m_config.groupObjects[i];
		if (object.address != address)
		{
			continue;
		}
		object.value.assign(value.begin(), value.end());
		if (object.flags.communication && object.flags.transmit)
		{
			sendGroup(address, Apci::GroupValueWrite, i);
		}
		return ok();
	}
	return makeError(ErrorCode::NotFound, "no group object bound to " + address.toString());
}

bool VirtualDevice::isShort(std::size_t objectIndex) const
{
	return m_shortForm[objectIndex];
}

void VirtualDevice::sendGroup(knx::GroupAddress address, Apci apci, std::size_t objectIndex)
{
	const auto& value = m_config.groupObjects[objectIndex].value;
	Apdu apdu;
	if (isShort(objectIndex))
	{
		const std::uint8_t raw = value.empty() ? 0 : static_cast<std::uint8_t>(value[0] & 0x3F);
		apdu = apci == Apci::GroupValueWrite ? knx::groupValueWriteShort(raw) : knx::groupValueResponseShort(raw);
	}
	else
	{
		apdu = apci == Apci::GroupValueWrite ? knx::groupValueWrite(value) : knx::groupValueResponse(value);
	}
	auto frame = knx::makeGroupFrame(address, apdu);
	frame.source = m_config.address.raw();
	m_bus.transmit(m_endpoint, std::move(frame));
}

void VirtualDevice::sendTo(knx::IndividualAddress destination, const Tpdu& tpdu)
{
	auto frame = knx::makeIndividualFrame(destination, tpdu);
	frame.source = m_config.address.raw();
	m_bus.transmit(m_endpoint, std::move(frame));
}

void VirtualDevice::sendBroadcast(const Apdu& apdu)
{
	auto frame = knx::makeBroadcastFrame(apdu);
	frame.source = m_config.address.raw();
	m_bus.transmit(m_endpoint, std::move(frame));
}

void VirtualDevice::onFrame(const knx::cemi::LData& frame)
{
	auto tpdu = knx::decodeTpdu(frame);
	if (!tpdu)
	{
		return;
	}
	if (tpdu->type == TpduType::DataBroadcast)
	{
		onBroadcast(tpdu->apdu);
	}
	else if (tpdu->type == TpduType::DataGroup)
	{
		onGroup(frame.groupAddress(), tpdu->apdu);
	}
	else if (!frame.groupDestination && frame.destination == m_config.address.raw())
	{
		onTransport(frame.sourceAddress(), *tpdu);
	}
}

void VirtualDevice::onGroup(knx::GroupAddress address, const Apdu& apdu)
{
	for (std::size_t i = 0; i < m_config.groupObjects.size(); ++i)
	{
		auto& object = m_config.groupObjects[i];
		if (object.address != address || !object.flags.communication)
		{
			continue;
		}
		const bool writes = (apdu.apci == Apci::GroupValueWrite && object.flags.write)
			|| (apdu.apci == Apci::GroupValueResponse && object.flags.update);
		if (writes)
		{
			object.value = apdu.groupValue();
			if (object.dpt.main == 0)
			{
				m_shortForm[i] = apdu.data.empty();
			}
		}
		else if (apdu.apci == Apci::GroupValueRead && object.flags.read)
		{
			sendGroup(address, Apci::GroupValueResponse, i);
			return; // one answer per read, even when several objects share the address
		}
	}
}

void VirtualDevice::onBroadcast(const Apdu& apdu)
{
	if (!m_config.progMode)
	{
		return;
	}
	if (apdu.apci == Apci::IndividualAddressRead)
	{
		sendBroadcast(knx::individualAddressResponse());
	}
	else if (apdu.apci == Apci::IndividualAddressWrite)
	{
		if (auto address = knx::decodeIndividualAddressWrite(apdu))
		{
			m_config.address = *address;
		}
	}
}

void VirtualDevice::onTransport(knx::IndividualAddress source, const Tpdu& tpdu)
{
	const bool fromPartner = m_partner && *m_partner == source;
	if (fromPartner)
	{
		m_trafficSinceTick = true;
	}

	switch (tpdu.type)
	{
	case TpduType::DataIndividual:
	{
		if (auto response = serve(tpdu.apdu))
		{
			sendTo(source, Tpdu::dataIndividual(std::move(*response)));
		}
		break;
	}
	case TpduType::Connect:
		if (m_partner && !fromPartner)
		{
			sendTo(source, Tpdu::disconnect()); // busy: one connection at a time
			return;
		}
		if (fromPartner)
		{
			// Already connected to this partner: keep the sequence numbers, as a real System B actuator
			// does (specs/04 field notes). A client must T_Disconnect a stale connection first.
			return;
		}
		m_partner = source;
		m_authorized = false;
		m_sendSeq = 0;
		m_recvSeq = 0;
		m_trafficSinceTick = true;
		m_lastActivity.reset();
		return;
	case TpduType::Disconnect:
		if (fromPartner)
		{
			m_partner.reset();
			m_authorized = false;
		}
		return;
	case TpduType::DataConnected:
	{
		if (!fromPartner)
		{
			sendTo(source, Tpdu::disconnect());
			return;
		}
		if (tpdu.seq == ((m_recvSeq - 1) & kSeqMask))
		{
			sendTo(source, Tpdu::ack(tpdu.seq)); // repetition of an already executed request
			return;
		}
		if (tpdu.seq != m_recvSeq)
		{
			sendTo(source, Tpdu::nak(tpdu.seq));
			return;
		}
		sendTo(source, Tpdu::ack(tpdu.seq));
		m_recvSeq = static_cast<std::uint8_t>((m_recvSeq + 1) & kSeqMask);
		if (auto response = serve(tpdu.apdu))
		{
			sendTo(source, Tpdu::dataConnected(m_sendSeq, std::move(*response)));
		}
		break;
	}
	case TpduType::Ack:
		if (fromPartner && tpdu.seq == m_sendSeq)
		{
			m_sendSeq = static_cast<std::uint8_t>((m_sendSeq + 1) & kSeqMask);
		}
		return;
	case TpduType::Nak:
	case TpduType::DataGroup:
	case TpduType::DataBroadcast:
		return;
	}

	if (m_restartPending)
	{
		doRestart();
	}
}

std::optional<Apdu> VirtualDevice::serve(const Apdu& apdu)
{
	switch (apdu.apci)
	{
	case Apci::DeviceDescriptorRead:
	{
		if (apdu.shortData != 0)
		{
			return knx::deviceDescriptorResponse(kDescriptorTypeUnsupported, {});
		}
		const Bytes mask{static_cast<std::uint8_t>(m_config.maskVersion >> 8),
			static_cast<std::uint8_t>(m_config.maskVersion & 0xFF)};
		return knx::deviceDescriptorResponse(0, mask);
	}
	case Apci::MemoryRead:
	{
		auto access = knx::decodeMemoryAccess(apdu);
		if (!access)
		{
			return std::nullopt;
		}
		if (access->count == 0 || access->count > kMaxMemoryChunk)
		{
			return knx::memoryResponse(access->address, {}); // count 0 = refused
		}
		return knx::memoryResponse(access->address, readMemory(access->address, access->count));
	}
	case Apci::MemoryWrite:
	{
		if (auto access = knx::decodeMemoryAccess(apdu))
		{
			writeMemory(access->address, access->data);
		}
		return std::nullopt;
	}
	case Apci::PropertyValueRead:
	case Apci::PropertyValueWrite:
	{
		auto access = knx::decodePropertyAccess(apdu);
		if (!access)
		{
			return std::nullopt;
		}
		const auto key = std::make_pair(access->objectIndex, access->propertyId);
		auto it = m_properties.find(key);
		const auto refuse = knx::propertyValueResponse(access->objectIndex, access->propertyId, 0,
			access->startIndex, {});
		if (it == m_properties.end())
		{
			return refuse;
		}
		if (apdu.apci == Apci::PropertyValueWrite)
		{
			if (access->count != 1 || access->startIndex != 1)
			{
				return refuse;
			}
			if (m_config.maskVersion == 0x07B0 && !m_authorized)
			{
				return refuse; // like the real device: writes need A_Authorize first
			}
			if (access->propertyId == pid::kLoadStateControl && loadState(access->objectIndex))
			{
				if (access->data.size() != 10)
				{
					return refuse; // the real device refuses anything but the 10-octet form
				}
				const std::uint8_t state = onLoadControl(access->objectIndex, access->data);
				return knx::propertyValueResponse(access->objectIndex, access->propertyId, 1, 1, Bytes{state});
			}
			it->second = access->data;
		}
		// Every simulated property holds one element; index 0 is the element count (3/4/1).
		if (access->startIndex == 0 && access->count == 1)
		{
			const Bytes elements{0x00, 0x01};
			return knx::propertyValueResponse(access->objectIndex, access->propertyId, 1, 0, elements);
		}
		if (access->startIndex != 1 || access->count != 1)
		{
			return refuse;
		}
		return knx::propertyValueResponse(access->objectIndex, access->propertyId, 1, 1, it->second);
	}
	case Apci::AuthorizeRequest:
	{
		// Only the default key is known: it grants the highest access level.
		const bool defaultKey = apdu.data == Bytes{0x00, 0xFF, 0xFF, 0xFF, 0xFF};
		m_authorized = defaultKey;
		return knx::authorizeResponse(defaultKey ? 0 : 15);
	}
	case Apci::Restart:
		m_restartPending = true;
		return std::nullopt;
	default:
		return std::nullopt;
	}
}

void VirtualDevice::doRestart()
{
	m_restartPending = false;
	++m_restarts;
	m_config.progMode = false;
	m_partner.reset(); // a restart silently drops the transport connection
	m_authorized = false;
}

void VirtualDevice::tick(knx::TimePoint now)
{
	if (!m_partner)
	{
		m_lastActivity.reset();
		m_trafficSinceTick = false;
		return;
	}
	if (m_trafficSinceTick || !m_lastActivity)
	{
		m_lastActivity = now;
		m_trafficSinceTick = false;
		return;
	}
	if (now - *m_lastActivity >= m_timing.connectionTimeout)
	{
		const auto partner = *m_partner;
		m_partner.reset();
		m_lastActivity.reset();
		sendTo(partner, Tpdu::disconnect());
	}
}

} // namespace kestrel::sim
