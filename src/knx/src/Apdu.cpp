#include "kestrel/knx/Apdu.hpp"

#include <cassert>

namespace kestrel::knx
{

namespace
{

constexpr std::uint8_t kTpciControl = 0x80;
constexpr std::uint8_t kTpciNumbered = 0x40;

/// 4-bit service codes (APCI >> 6) whose low 6 bits carry data. Codes 7 (ADC response /
/// system network parameters), 11 (user memory) and 15 (escape) use the full 10 bits.
bool isFourBitCode(unsigned code)
{
	return code != 7 && code != 11 && code != 15;
}

Apdu makeApdu(Apci apci, std::uint8_t shortData = 0, Bytes data = {})
{
	assert(shortData <= 0x3F);
	return Apdu{apci, shortData, std::move(data)};
}

Bytes addressAndData(std::uint16_t address, ByteView data)
{
	Bytes out;
	ByteWriter writer(out);
	writer.u16(address);
	writer.append(data);
	return out;
}

Bytes propertyHeader(std::uint8_t objectIndex, std::uint8_t propertyId, std::uint8_t count, std::uint16_t startIndex,
	ByteView data)
{
	assert(count <= 0x0F && startIndex <= 0x0FFF);
	Bytes out;
	ByteWriter writer(out);
	writer.u8(objectIndex);
	writer.u8(propertyId);
	writer.u16(static_cast<std::uint16_t>(((count & 0x0F) << 12) | (startIndex & 0x0FFF)));
	writer.append(data);
	return out;
}

} // namespace

bool hasShortData(Apci apci)
{
	const auto value = static_cast<unsigned>(apci);
	return (value & 0x3F) == 0 && isFourBitCode(value >> 6);
}

Bytes Apdu::groupValue() const
{
	if (data.empty())
	{
		return Bytes{shortData};
	}
	return data;
}

std::ostream& operator<<(std::ostream& os, Apci apci)
{
	switch (apci)
	{
	case Apci::GroupValueRead: return os << "GroupValueRead";
	case Apci::GroupValueResponse: return os << "GroupValueResponse";
	case Apci::GroupValueWrite: return os << "GroupValueWrite";
	case Apci::IndividualAddressWrite: return os << "IndividualAddressWrite";
	case Apci::IndividualAddressRead: return os << "IndividualAddressRead";
	case Apci::IndividualAddressResponse: return os << "IndividualAddressResponse";
	case Apci::AdcRead: return os << "AdcRead";
	case Apci::MemoryRead: return os << "MemoryRead";
	case Apci::MemoryResponse: return os << "MemoryResponse";
	case Apci::MemoryWrite: return os << "MemoryWrite";
	case Apci::DeviceDescriptorRead: return os << "DeviceDescriptorRead";
	case Apci::DeviceDescriptorResponse: return os << "DeviceDescriptorResponse";
	case Apci::Restart: return os << "Restart";
	case Apci::AuthorizeRequest: return os << "AuthorizeRequest";
	case Apci::AuthorizeResponse: return os << "AuthorizeResponse";
	case Apci::PropertyValueRead: return os << "PropertyValueRead";
	case Apci::PropertyValueResponse: return os << "PropertyValueResponse";
	case Apci::PropertyValueWrite: return os << "PropertyValueWrite";
	case Apci::PropertyDescriptionRead: return os << "PropertyDescriptionRead";
	case Apci::PropertyDescriptionResponse: return os << "PropertyDescriptionResponse";
	case Apci::IndividualAddressSerialNumberRead: return os << "IndividualAddressSerialNumberRead";
	case Apci::IndividualAddressSerialNumberResponse: return os << "IndividualAddressSerialNumberResponse";
	case Apci::IndividualAddressSerialNumberWrite: return os << "IndividualAddressSerialNumberWrite";
	}
	return os << "Apci(0x" << std::hex << static_cast<unsigned>(apci) << std::dec << ')';
}

std::ostream& operator<<(std::ostream& os, const Apdu& apdu)
{
	os << apdu.apci;
	if (apdu.shortData != 0)
	{
		os << " short=" << static_cast<unsigned>(apdu.shortData);
	}
	if (!apdu.data.empty())
	{
		os << " [" << toHex(apdu.data) << ']';
	}
	return os;
}

std::ostream& operator<<(std::ostream& os, TpduType type)
{
	switch (type)
	{
	case TpduType::DataGroup: return os << "T_Data_Group";
	case TpduType::DataBroadcast: return os << "T_Data_Broadcast";
	case TpduType::DataIndividual: return os << "T_Data_Individual";
	case TpduType::DataConnected: return os << "T_Data_Connected";
	case TpduType::Connect: return os << "T_Connect";
	case TpduType::Disconnect: return os << "T_Disconnect";
	case TpduType::Ack: return os << "T_Ack";
	case TpduType::Nak: return os << "T_Nak";
	}
	return os << "TpduType(" << static_cast<unsigned>(type) << ')';
}

std::ostream& operator<<(std::ostream& os, const Tpdu& tpdu)
{
	os << tpdu.type;
	if (tpdu.type == TpduType::DataConnected || tpdu.type == TpduType::Ack || tpdu.type == TpduType::Nak)
	{
		os << " seq=" << static_cast<unsigned>(tpdu.seq);
	}
	if (tpdu.isData())
	{
		os << ' ' << tpdu.apdu;
	}
	return os;
}

Bytes encode(const Tpdu& tpdu)
{
	assert(tpdu.seq <= 0x0F);
	const auto seqBits = static_cast<std::uint8_t>((tpdu.seq & 0x0F) << 2);
	switch (tpdu.type)
	{
	case TpduType::Connect: return Bytes{kTpciControl};
	case TpduType::Disconnect: return Bytes{static_cast<std::uint8_t>(kTpciControl | 0x01)};
	case TpduType::Ack: return Bytes{static_cast<std::uint8_t>(kTpciControl | kTpciNumbered | seqBits | 0x02)};
	case TpduType::Nak: return Bytes{static_cast<std::uint8_t>(kTpciControl | kTpciNumbered | seqBits | 0x03)};
	case TpduType::DataGroup:
	case TpduType::DataBroadcast:
	case TpduType::DataIndividual:
	case TpduType::DataConnected:
		break;
	}
	const auto apci = static_cast<unsigned>(tpdu.apdu.apci);
	std::uint8_t tpci = tpdu.type == TpduType::DataConnected ? static_cast<std::uint8_t>(kTpciNumbered | seqBits) : 0;
	tpci = static_cast<std::uint8_t>(tpci | ((apci >> 8) & 0x03));
	Bytes out;
	out.reserve(2 + tpdu.apdu.data.size());
	out.push_back(tpci);
	out.push_back(static_cast<std::uint8_t>((apci & 0xFF) | (tpdu.apdu.shortData & 0x3F)));
	out.insert(out.end(), tpdu.apdu.data.begin(), tpdu.apdu.data.end());
	return out;
}

Result<Tpdu> decodeTpdu(ByteView bytes, bool groupDestination, std::uint16_t destination)
{
	if (bytes.empty())
	{
		return makeError(ErrorCode::ParseError, "TPDU: empty");
	}
	const std::uint8_t tpci = bytes[0];
	const auto seq = static_cast<std::uint8_t>((tpci >> 2) & 0x0F);
	Tpdu tpdu;
	if (tpci & kTpciControl)
	{
		if (groupDestination)
		{
			return makeError(ErrorCode::ParseError, "TPDU: transport control frame to a group address");
		}
		if (bytes.size() != 1)
		{
			return makeError(ErrorCode::ParseError, "TPDU: control frame with payload");
		}
		switch (tpci)
		{
		case 0x80: tpdu.type = TpduType::Connect; return tpdu;
		case 0x81: tpdu.type = TpduType::Disconnect; return tpdu;
		default: break;
		}
		if ((tpci & 0xC3) == 0xC2)
		{
			return Tpdu::ack(seq);
		}
		if ((tpci & 0xC3) == 0xC3)
		{
			return Tpdu::nak(seq);
		}
		return makeError(ErrorCode::Unsupported, "TPDU: unknown control TPCI " + toHex(bytes.first(1)));
	}

	const bool numbered = (tpci & kTpciNumbered) != 0;
	if (groupDestination)
	{
		if (numbered || (tpci & 0x3C) != 0)
		{
			// 000001xx would be T_Data_Tag_Group (not used by Kestrel).
			return makeError(ErrorCode::Unsupported, "TPDU: unsupported group TPCI " + toHex(bytes.first(1)));
		}
		tpdu.type = destination == 0 ? TpduType::DataBroadcast : TpduType::DataGroup;
	}
	else if (numbered)
	{
		tpdu.type = TpduType::DataConnected;
		tpdu.seq = seq;
	}
	else
	{
		if ((tpci & 0x3C) != 0)
		{
			return makeError(ErrorCode::ParseError, "TPDU: sequence number on unnumbered data");
		}
		tpdu.type = TpduType::DataIndividual;
	}

	if (bytes.size() < 2)
	{
		return makeError(ErrorCode::ParseError, "TPDU: data frame without APCI");
	}
	const unsigned apci10 = ((tpci & 0x03u) << 8) | bytes[1];
	if (isFourBitCode(apci10 >> 6))
	{
		tpdu.apdu.apci = static_cast<Apci>(apci10 & 0x3C0);
		tpdu.apdu.shortData = static_cast<std::uint8_t>(apci10 & 0x3F);
	}
	else
	{
		tpdu.apdu.apci = static_cast<Apci>(apci10);
	}
	tpdu.apdu.data.assign(bytes.begin() + 2, bytes.end());
	return tpdu;
}

Result<Tpdu> decodeTpdu(const cemi::LData& frame)
{
	return decodeTpdu(frame.tpdu, frame.groupDestination, frame.destination);
}

Apdu groupValueRead()
{
	return makeApdu(Apci::GroupValueRead);
}

Apdu groupValueWriteShort(std::uint8_t value)
{
	return makeApdu(Apci::GroupValueWrite, value);
}

Apdu groupValueWrite(ByteView value)
{
	return makeApdu(Apci::GroupValueWrite, 0, Bytes(value.begin(), value.end()));
}

Apdu groupValueResponseShort(std::uint8_t value)
{
	return makeApdu(Apci::GroupValueResponse, value);
}

Apdu groupValueResponse(ByteView value)
{
	return makeApdu(Apci::GroupValueResponse, 0, Bytes(value.begin(), value.end()));
}

Apdu individualAddressRead()
{
	return makeApdu(Apci::IndividualAddressRead);
}

Apdu individualAddressResponse()
{
	return makeApdu(Apci::IndividualAddressResponse);
}

Apdu individualAddressWrite(IndividualAddress address)
{
	return makeApdu(Apci::IndividualAddressWrite, 0, addressAndData(address.raw(), {}));
}

Apdu deviceDescriptorRead(std::uint8_t descriptorType)
{
	return makeApdu(Apci::DeviceDescriptorRead, descriptorType);
}

Apdu deviceDescriptorResponse(std::uint8_t descriptorType, ByteView descriptor)
{
	return makeApdu(Apci::DeviceDescriptorResponse, descriptorType, Bytes(descriptor.begin(), descriptor.end()));
}

Apdu memoryRead(std::uint16_t address, std::uint8_t count)
{
	return makeApdu(Apci::MemoryRead, count, addressAndData(address, {}));
}

Apdu memoryResponse(std::uint16_t address, ByteView data)
{
	return makeApdu(Apci::MemoryResponse, static_cast<std::uint8_t>(data.size()), addressAndData(address, data));
}

Apdu memoryWrite(std::uint16_t address, ByteView data)
{
	return makeApdu(Apci::MemoryWrite, static_cast<std::uint8_t>(data.size()), addressAndData(address, data));
}

Apdu propertyValueRead(std::uint8_t objectIndex, std::uint8_t propertyId, std::uint8_t count, std::uint16_t startIndex)
{
	return makeApdu(Apci::PropertyValueRead, 0, propertyHeader(objectIndex, propertyId, count, startIndex, {}));
}

Apdu propertyValueResponse(std::uint8_t objectIndex, std::uint8_t propertyId, std::uint8_t count,
	std::uint16_t startIndex, ByteView data)
{
	return makeApdu(Apci::PropertyValueResponse, 0, propertyHeader(objectIndex, propertyId, count, startIndex, data));
}

Apdu propertyValueWrite(std::uint8_t objectIndex, std::uint8_t propertyId, std::uint8_t count,
	std::uint16_t startIndex, ByteView data)
{
	return makeApdu(Apci::PropertyValueWrite, 0, propertyHeader(objectIndex, propertyId, count, startIndex, data));
}

Apdu restart()
{
	return makeApdu(Apci::Restart);
}

std::ostream& operator<<(std::ostream& os, const MemoryAccess& access)
{
	return os << "Memory{0x" << std::hex << access.address << std::dec << " n=" << static_cast<unsigned>(access.count)
			  << " [" << toHex(access.data) << "]}";
}

std::ostream& operator<<(std::ostream& os, const PropertyAccess& access)
{
	return os << "Property{obj=" << static_cast<unsigned>(access.objectIndex)
			  << " pid=" << static_cast<unsigned>(access.propertyId) << " n=" << static_cast<unsigned>(access.count)
			  << " start=" << access.startIndex << " [" << toHex(access.data) << "]}";
}

Result<MemoryAccess> decodeMemoryAccess(const Apdu& apdu)
{
	if (apdu.apci != Apci::MemoryRead && apdu.apci != Apci::MemoryResponse && apdu.apci != Apci::MemoryWrite)
	{
		return makeError(ErrorCode::InvalidArgument, "not a memory service");
	}
	ByteReader reader(apdu.data);
	MemoryAccess access;
	access.count = apdu.shortData;
	access.address = reader.u16();
	const auto rest = reader.rest();
	if (reader.failed())
	{
		return makeError(ErrorCode::ParseError, "memory service: missing address");
	}
	access.data.assign(rest.begin(), rest.end());
	const bool carriesData = apdu.apci != Apci::MemoryRead;
	if (carriesData && access.data.size() != access.count)
	{
		return makeError(ErrorCode::ParseError, "memory service: byte count does not match data");
	}
	if (!carriesData && !access.data.empty())
	{
		return makeError(ErrorCode::ParseError, "memory read: unexpected data");
	}
	return access;
}

Result<PropertyAccess> decodePropertyAccess(const Apdu& apdu)
{
	if (apdu.apci != Apci::PropertyValueRead && apdu.apci != Apci::PropertyValueResponse
		&& apdu.apci != Apci::PropertyValueWrite)
	{
		return makeError(ErrorCode::InvalidArgument, "not a property value service");
	}
	ByteReader reader(apdu.data);
	PropertyAccess access;
	access.objectIndex = reader.u8();
	access.propertyId = reader.u8();
	const auto countStart = reader.u16();
	const auto rest = reader.rest();
	if (reader.failed())
	{
		return makeError(ErrorCode::ParseError, "property service: truncated header");
	}
	access.count = static_cast<std::uint8_t>(countStart >> 12);
	access.startIndex = static_cast<std::uint16_t>(countStart & 0x0FFF);
	access.data.assign(rest.begin(), rest.end());
	return access;
}

Result<IndividualAddress> decodeIndividualAddressWrite(const Apdu& apdu)
{
	if (apdu.apci != Apci::IndividualAddressWrite)
	{
		return makeError(ErrorCode::InvalidArgument, "not an individual address write");
	}
	if (apdu.data.size() != 2)
	{
		return makeError(ErrorCode::ParseError, "individual address write: expected 2 data bytes");
	}
	return IndividualAddress(static_cast<std::uint16_t>((apdu.data[0] << 8) | apdu.data[1]));
}

cemi::LData makeGroupFrame(GroupAddress destination, const Apdu& apdu)
{
	cemi::LData frame;
	frame.groupDestination = true;
	frame.destination = destination.raw();
	frame.priority = cemi::Priority::Low;
	frame.tpdu = encode(Tpdu::dataGroup(apdu));
	return frame;
}

cemi::LData makeBroadcastFrame(const Apdu& apdu)
{
	cemi::LData frame;
	frame.groupDestination = true;
	frame.destination = 0;
	frame.priority = cemi::Priority::System;
	frame.tpdu = encode(Tpdu::dataBroadcast(apdu));
	return frame;
}

cemi::LData makeIndividualFrame(IndividualAddress destination, const Tpdu& tpdu)
{
	cemi::LData frame;
	frame.groupDestination = false;
	frame.destination = destination.raw();
	frame.priority = cemi::Priority::System;
	frame.tpdu = encode(tpdu);
	return frame;
}

} // namespace kestrel::knx
