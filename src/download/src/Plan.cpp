#include "kestrel/download/Plan.hpp"

namespace kestrel::download
{

std::ostream& operator<<(std::ostream& os, LoadState state)
{
	switch (state)
	{
	case LoadState::Unloaded: return os << "unloaded";
	case LoadState::Loaded: return os << "loaded";
	case LoadState::Loading: return os << "loading";
	case LoadState::Error: return os << "error";
	case LoadState::Unloading: return os << "unloading";
	case LoadState::LoadCompleting: return os << "load completing";
	}
	return os << "load state " << int(state);
}

namespace
{

struct StepPrinter
{
	std::ostream& os;
	void operator()(const Authorize& s) const
	{
		os << "Authorize" << (s.key == 0xFFFFFFFF ? " (default key)" : " (key set)");
	}
	void operator()(const Unload& s) const
	{
		os << "Unload LSM " << int(s.lsm) << (s.tolerateError ? " (errors ignored)" : "");
	}
	void operator()(const Load& s) const { os << "Load LSM " << int(s.lsm); }
	void operator()(const LoadCompleted& s) const { os << "LoadCompleted LSM " << int(s.lsm); }
	void operator()(const AllocRelSegment& s) const
	{
		os << "AllocRelSegment LSM " << int(s.lsm) << " size " << s.size << " mode " << int(s.mode) << " fill "
		   << int(s.fill);
	}
	void operator()(const WriteRelMem& s) const
	{
		os << "WriteRelMem object " << int(s.object) << " offset " << s.offset << " size " << s.data.size()
		   << (s.verify ? " (verify)" : "");
	}
	void operator()(const WriteProp& s) const
	{
		os << "WriteProp object " << int(s.object) << " PID " << int(s.pid) << " [" << toHex(s.data) << "]";
	}
	void operator()(const CompareProp& s) const
	{
		os << "CompareProp object " << int(s.object) << " PID " << int(s.pid) << " [" << toHex(s.data) << "]";
	}
	void operator()(const LoadImageProp& s) const
	{
		os << "LoadImageProp object " << int(s.object) << " PID " << int(s.pid);
	}
	void operator()(const Restart&) const { os << "Restart"; }
};

} // namespace

std::ostream& operator<<(std::ostream& os, const Step& step)
{
	std::visit(StepPrinter{os}, step);
	return os;
}

Bytes loadControlPayload(LoadEvent event)
{
	// Load control writes have a fixed size of 10 octets (event + 9 octets of additional data); a real
	// System B device refuses a bare 1-octet event.
	Bytes out(10, 0x00);
	out[0] = static_cast<std::uint8_t>(event);
	return out;
}

Bytes allocRelSegmentPayload(const AllocRelSegment& step)
{
	// Event "additional load control", type 0x0B (data relative allocation): required size, mode, fill,
	// two reserved octets (10 octets in total, the size of a PDT_CONTROL write).
	Bytes out;
	ByteWriter writer(out);
	writer.u8(static_cast<std::uint8_t>(LoadEvent::AdditionalLoadControl));
	writer.u8(0x0B);
	writer.u32(step.size);
	writer.u8(step.mode);
	writer.u8(step.fill);
	writer.u8(0);
	writer.u8(0);
	return out;
}

std::uint16_t crc16AugCcitt(ByteView data)
{
	std::uint16_t crc = 0x1D0F;
	for (const std::uint8_t byte : data)
	{
		crc = static_cast<std::uint16_t>(crc ^ (byte << 8));
		for (int bit = 0; bit < 8; ++bit)
		{
			crc = (crc & 0x8000) ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021) : static_cast<std::uint16_t>(crc << 1);
		}
	}
	return crc;
}

Bytes memoryControlBlock(ByteView image)
{
	Bytes out;
	ByteWriter writer(out);
	writer.u32(static_cast<std::uint32_t>(image.size()));
	writer.u8(0x00);
	writer.u8(0xFF);
	writer.u16(crc16AugCcitt(image));
	return out;
}

} // namespace kestrel::download
