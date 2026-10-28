#pragma once

// Transport layer (KNX Standard 3/3/4) and application layer (3/3/7) PDUs.
//
// TPCI octet:  b7 control | b6 numbered | b5..2 sequence | b1..0 (data: APCI high bits, control: kind)
//   T_Data_Group/Broadcast/Individual 000000xx, T_Data_Connected 01ssssxx,
//   T_Connect 0x80, T_Disconnect 0x81, T_ACK 11ssss10, T_NAK 11ssss11.
// The 10-bit APCI spans the low 2 bits of the TPCI octet and the next octet. Services with a
// 4-bit APCI carry 6 bits of data in the low bits of the second octet ("short" data: small group
// values, memory byte counts, descriptor types, restart type) — `Apdu::shortData`.

#include "kestrel/base/Bytes.hpp"
#include "kestrel/base/Result.hpp"
#include "kestrel/knx/Address.hpp"
#include "kestrel/knx/Cemi.hpp"

#include <cstdint>
#include <ostream>

namespace kestrel::knx
{

/// 10-bit application control field values. Unknown values are kept as-is (static_cast).
enum class Apci : std::uint16_t
{
	GroupValueRead = 0x000,
	GroupValueResponse = 0x040,
	GroupValueWrite = 0x080,
	IndividualAddressWrite = 0x0C0,
	IndividualAddressRead = 0x100,
	IndividualAddressResponse = 0x140,
	AdcRead = 0x180,
	MemoryRead = 0x200,
	MemoryResponse = 0x240,
	MemoryWrite = 0x280,
	DeviceDescriptorRead = 0x300,
	DeviceDescriptorResponse = 0x340,
	Restart = 0x380,
	AuthorizeRequest = 0x3D1,
	AuthorizeResponse = 0x3D2,
	PropertyValueRead = 0x3D5,
	PropertyValueResponse = 0x3D6,
	PropertyValueWrite = 0x3D7,
	PropertyDescriptionRead = 0x3D8,
	PropertyDescriptionResponse = 0x3D9,
	IndividualAddressSerialNumberRead = 0x3DC,
	IndividualAddressSerialNumberResponse = 0x3DD,
	IndividualAddressSerialNumberWrite = 0x3DE,
};

/// True for APCIs whose low 6 bits carry data (4-bit service codes).
bool hasShortData(Apci apci);

struct Apdu
{
	Apci apci = Apci::GroupValueRead;
	/// Low 6 bits of the APCI octet for 4-bit services; 0 otherwise.
	std::uint8_t shortData = 0;
	/// Octets following the APCI.
	Bytes data;

	/// Group value of a GroupValueWrite/Response: `{shortData}` when `data` is empty (short form),
	/// otherwise `data`.
	Bytes groupValue() const;

	friend bool operator==(const Apdu&, const Apdu&) = default;
};

std::ostream& operator<<(std::ostream& os, Apci apci);
std::ostream& operator<<(std::ostream& os, const Apdu& apdu);

enum class TpduType : std::uint8_t
{
	DataGroup,
	DataBroadcast,
	DataIndividual,
	DataConnected,
	Connect,
	Disconnect,
	Ack,
	Nak,
};

std::ostream& operator<<(std::ostream& os, TpduType type);

struct Tpdu
{
	TpduType type = TpduType::DataGroup;
	/// 4-bit sequence number for DataConnected / Ack / Nak.
	std::uint8_t seq = 0;
	/// Only meaningful for the Data* types.
	Apdu apdu;

	static Tpdu dataGroup(Apdu apdu) { return {TpduType::DataGroup, 0, std::move(apdu)}; }
	static Tpdu dataBroadcast(Apdu apdu) { return {TpduType::DataBroadcast, 0, std::move(apdu)}; }
	static Tpdu dataIndividual(Apdu apdu) { return {TpduType::DataIndividual, 0, std::move(apdu)}; }
	static Tpdu dataConnected(std::uint8_t seq, Apdu apdu) { return {TpduType::DataConnected, seq, std::move(apdu)}; }
	static Tpdu connect() { return {TpduType::Connect, 0, {}}; }
	static Tpdu disconnect() { return {TpduType::Disconnect, 0, {}}; }
	static Tpdu ack(std::uint8_t seq) { return {TpduType::Ack, seq, {}}; }
	static Tpdu nak(std::uint8_t seq) { return {TpduType::Nak, seq, {}}; }

	bool isData() const { return type <= TpduType::DataConnected; }

	friend bool operator==(const Tpdu&, const Tpdu&) = default;
};

std::ostream& operator<<(std::ostream& os, const Tpdu& tpdu);

/// Encodes TPCI (+ APCI + data for data TPDUs).
Bytes encode(const Tpdu& tpdu);
/// The TPCI meaning depends on the destination: group 0 = broadcast, other group = group,
/// individual = individual/connected/control.
Result<Tpdu> decodeTpdu(ByteView bytes, bool groupDestination, std::uint16_t destination);
Result<Tpdu> decodeTpdu(const cemi::LData& frame);

// ---- APDU builders -------------------------------------------------------------------------

Apdu groupValueRead();
/// Short form: value (≤ 6 bits, e.g. DPT 1/2/3) in the APCI octet.
Apdu groupValueWriteShort(std::uint8_t value);
Apdu groupValueWrite(ByteView value);
Apdu groupValueResponseShort(std::uint8_t value);
Apdu groupValueResponse(ByteView value);

Apdu individualAddressRead();
Apdu individualAddressResponse();
Apdu individualAddressWrite(IndividualAddress address);

Apdu deviceDescriptorRead(std::uint8_t descriptorType = 0);
Apdu deviceDescriptorResponse(std::uint8_t descriptorType, ByteView descriptor);

/// count 1..63 (12 per standard frame).
Apdu memoryRead(std::uint16_t address, std::uint8_t count);
Apdu memoryResponse(std::uint16_t address, ByteView data);
Apdu memoryWrite(std::uint16_t address, ByteView data);

/// count 0..15 elements, startIndex 0..4095 (index 0 = number of elements).
Apdu propertyValueRead(std::uint8_t objectIndex, std::uint8_t propertyId, std::uint8_t count, std::uint16_t startIndex);
Apdu propertyValueResponse(std::uint8_t objectIndex, std::uint8_t propertyId, std::uint8_t count,
	std::uint16_t startIndex, ByteView data);
Apdu propertyValueWrite(std::uint8_t objectIndex, std::uint8_t propertyId, std::uint8_t count,
	std::uint16_t startIndex, ByteView data);

/// Basic restart (restart type 0).
Apdu restart();

// ---- APDU field decoders ---------------------------------------------------------------------

/// A_Memory_Read/Response/Write fields.
struct MemoryAccess
{
	std::uint16_t address = 0;
	std::uint8_t count = 0;
	Bytes data;

	friend bool operator==(const MemoryAccess&, const MemoryAccess&) = default;
};

/// A_PropertyValue_Read/Response/Write fields.
struct PropertyAccess
{
	std::uint8_t objectIndex = 0;
	std::uint8_t propertyId = 0;
	std::uint8_t count = 0;
	std::uint16_t startIndex = 0;
	Bytes data;

	friend bool operator==(const PropertyAccess&, const PropertyAccess&) = default;
};

std::ostream& operator<<(std::ostream& os, const MemoryAccess& access);
std::ostream& operator<<(std::ostream& os, const PropertyAccess& access);

Result<MemoryAccess> decodeMemoryAccess(const Apdu& apdu);
Result<PropertyAccess> decodePropertyAccess(const Apdu& apdu);
Result<IndividualAddress> decodeIndividualAddressWrite(const Apdu& apdu);

// ---- cEMI frame builders ---------------------------------------------------------------------
// Source 0.0.0: the interface (tunnel server) substitutes its own individual address.

/// T_Data_Group at low priority.
cemi::LData makeGroupFrame(GroupAddress destination, const Apdu& apdu);
/// T_Data_Broadcast (destination group 0) at system priority, as used for IA programming.
cemi::LData makeBroadcastFrame(const Apdu& apdu);
/// Point-to-point TPDU (individual, connected, or transport control) at system priority.
cemi::LData makeIndividualFrame(IndividualAddress destination, const Tpdu& tpdu);

} // namespace kestrel::knx
