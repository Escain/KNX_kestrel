#pragma once

// cEMI L_Data frames (KNX Standard 3/6/3 "External Message Interface", §4.1.5.3).
//
//   [msg code][add-info len][add-info...][ctrl1][ctrl2][src u16][dst u16][L][TPDU (L+1 bytes)]
//
// ctrl1: b7 frame type (1 = standard) | b5 repeat (0 = repeat on error) | b4 broadcast (1 = normal,
//        0 = system broadcast) | b3..2 priority | b1 ack request | b0 confirm (1 = error)
// ctrl2: b7 destination address type (1 = group) | b6..4 hop count | b3..0 extended frame format
// L counts the TPDU octets after the TPCI octet, as on TP1.

#include "kestrel/base/Bytes.hpp"
#include "kestrel/base/Result.hpp"
#include "kestrel/knx/Address.hpp"

#include <cstdint>
#include <ostream>

namespace kestrel::knx::cemi
{

enum class MessageCode : std::uint8_t
{
	LDataReq = 0x11,
	LDataCon = 0x2E,
	LDataInd = 0x29,
};

enum class FrameType : std::uint8_t
{
	Extended = 0,
	Standard = 1,
};

enum class Priority : std::uint8_t
{
	System = 0,
	Normal = 1,
	Urgent = 2,
	Low = 3,
};

struct LData
{
	MessageCode messageCode = MessageCode::LDataReq;
	FrameType frameType = FrameType::Standard;
	/// true: L_Data.req may be repeated on error / L_Data.ind is a repetition.
	/// Stored inverted in ctrl1 bit 5, so the usual 0xBC ctrl1 means repeat == false.
	bool repeat = false;
	/// false = system broadcast (ctrl1 bit 4 clear).
	bool broadcast = true;
	Priority priority = Priority::Low;
	bool ackRequest = false;
	/// L_Data.con only: true when the frame could not be sent.
	bool confirmError = false;
	bool groupDestination = true;
	std::uint8_t hopCount = 6;
	std::uint16_t source = 0;
	std::uint16_t destination = 0;
	/// TPCI octet onwards; never empty for a valid frame.
	Bytes tpdu;

	IndividualAddress sourceAddress() const { return IndividualAddress(source); }
	GroupAddress groupAddress() const { return GroupAddress(destination); }
	IndividualAddress individualDestination() const { return IndividualAddress(destination); }

	friend bool operator==(const LData&, const LData&) = default;
};

/// Encodes without additional info. Precondition: !frame.tpdu.empty().
Bytes encode(const LData& frame);
/// Decodes L_Data.req/.con/.ind; additional info is skipped. Other message codes → Unsupported.
Result<LData> decode(ByteView bytes);

std::ostream& operator<<(std::ostream& os, MessageCode code);
std::ostream& operator<<(std::ostream& os, Priority priority);
std::ostream& operator<<(std::ostream& os, const LData& frame);

} // namespace kestrel::knx::cemi
