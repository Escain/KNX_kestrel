#include "kestrel/knx/Cemi.hpp"

#include <cassert>

namespace kestrel::knx::cemi
{

Bytes encode(const LData& frame)
{
	assert(!frame.tpdu.empty());
	Bytes out;
	out.reserve(9 + frame.tpdu.size());
	ByteWriter writer(out);
	writer.u8(static_cast<std::uint8_t>(frame.messageCode));
	writer.u8(0); // no additional info

	std::uint8_t ctrl1 = 0;
	ctrl1 |= frame.frameType == FrameType::Standard ? 0x80 : 0x00;
	ctrl1 |= frame.repeat ? 0x00 : 0x20;
	ctrl1 |= frame.broadcast ? 0x10 : 0x00;
	ctrl1 |= static_cast<std::uint8_t>((static_cast<unsigned>(frame.priority) & 0x03) << 2);
	ctrl1 |= frame.ackRequest ? 0x02 : 0x00;
	ctrl1 |= frame.confirmError ? 0x01 : 0x00;
	writer.u8(ctrl1);

	std::uint8_t ctrl2 = frame.groupDestination ? 0x80 : 0x00;
	ctrl2 |= static_cast<std::uint8_t>((frame.hopCount & 0x07) << 4);
	writer.u8(ctrl2);

	writer.u16(frame.source);
	writer.u16(frame.destination);
	writer.u8(static_cast<std::uint8_t>(frame.tpdu.size() - 1));
	writer.append(frame.tpdu);
	return out;
}

Result<LData> decode(ByteView bytes)
{
	ByteReader reader(bytes);
	LData frame;
	const auto code = reader.u8();
	if (reader.failed())
	{
		return makeError(ErrorCode::ParseError, "cEMI: empty frame");
	}
	switch (code)
	{
	case 0x11: frame.messageCode = MessageCode::LDataReq; break;
	case 0x2E: frame.messageCode = MessageCode::LDataCon; break;
	case 0x29: frame.messageCode = MessageCode::LDataInd; break;
	default:
		return makeError(ErrorCode::Unsupported, "cEMI: unsupported message code " + toHex(bytes.first(1)));
	}
	reader.skip(reader.u8()); // additional info

	const auto ctrl1 = reader.u8();
	const auto ctrl2 = reader.u8();
	frame.frameType = (ctrl1 & 0x80) ? FrameType::Standard : FrameType::Extended;
	frame.repeat = (ctrl1 & 0x20) == 0;
	frame.broadcast = (ctrl1 & 0x10) != 0;
	frame.priority = static_cast<Priority>((ctrl1 >> 2) & 0x03);
	frame.ackRequest = (ctrl1 & 0x02) != 0;
	frame.confirmError = (ctrl1 & 0x01) != 0;
	frame.groupDestination = (ctrl2 & 0x80) != 0;
	frame.hopCount = static_cast<std::uint8_t>((ctrl2 >> 4) & 0x07);
	frame.source = reader.u16();
	frame.destination = reader.u16();
	const std::size_t length = reader.u8();
	const auto tpdu = reader.take(length + 1);
	if (reader.failed())
	{
		return makeError(ErrorCode::ParseError, "cEMI: truncated L_Data frame");
	}
	if (!reader.atEnd())
	{
		return makeError(ErrorCode::ParseError, "cEMI: trailing bytes after TPDU");
	}
	frame.tpdu.assign(tpdu.begin(), tpdu.end());
	return frame;
}

std::ostream& operator<<(std::ostream& os, MessageCode code)
{
	switch (code)
	{
	case MessageCode::LDataReq: return os << "L_Data.req";
	case MessageCode::LDataCon: return os << "L_Data.con";
	case MessageCode::LDataInd: return os << "L_Data.ind";
	}
	return os << "cEMI(" << static_cast<unsigned>(code) << ')';
}

std::ostream& operator<<(std::ostream& os, Priority priority)
{
	switch (priority)
	{
	case Priority::System: return os << "system";
	case Priority::Normal: return os << "normal";
	case Priority::Urgent: return os << "urgent";
	case Priority::Low: return os << "low";
	}
	return os << "priority(" << static_cast<unsigned>(priority) << ')';
}

std::ostream& operator<<(std::ostream& os, const LData& frame)
{
	os << frame.messageCode << ' ' << frame.sourceAddress() << " -> ";
	if (frame.groupDestination)
	{
		os << frame.groupAddress();
	}
	else
	{
		os << frame.individualDestination();
	}
	os << " prio=" << frame.priority << " hop=" << static_cast<unsigned>(frame.hopCount);
	if (frame.frameType == FrameType::Extended)
	{
		os << " ext";
	}
	if (!frame.broadcast)
	{
		os << " sysbc";
	}
	if (frame.repeat)
	{
		os << " rep";
	}
	if (frame.confirmError)
	{
		os << " ERR";
	}
	return os << " [" << toHex(frame.tpdu) << ']';
}

} // namespace kestrel::knx::cemi
