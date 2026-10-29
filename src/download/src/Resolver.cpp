#include "kestrel/download/Resolver.hpp"

#include <charconv>
#include <optional>
#include <sstream>

namespace kestrel::download
{

namespace
{

std::optional<std::string> attribute(const LdCtrl& element, const std::string& name)
{
	const auto it = element.attributes.find(name);
	if (it == element.attributes.end())
	{
		return std::nullopt;
	}
	return it->second;
}

Result<std::uint64_t> number(const LdCtrl& element, const std::string& name)
{
	const auto text = attribute(element, name);
	if (!text)
	{
		return makeError(ErrorCode::InvalidArgument, element.name + ": missing attribute " + name);
	}
	std::uint64_t value = 0;
	const auto [end, ec] = std::from_chars(text->data(), text->data() + text->size(), value);
	if (ec != std::errc() || end != text->data() + text->size())
	{
		return makeError(ErrorCode::InvalidArgument, element.name + ": invalid " + name + " \"" + *text + "\"");
	}
	return value;
}

Result<std::uint8_t> byteAttribute(const LdCtrl& element, const std::string& name, std::uint64_t fallback, bool required)
{
	if (!required && !attribute(element, name))
	{
		return static_cast<std::uint8_t>(fallback);
	}
	KESTREL_TRY(auto value, number(element, name));
	if (value > 0xFF)
	{
		return makeError(ErrorCode::InvalidArgument, element.name + ": " + name + " out of range");
	}
	return static_cast<std::uint8_t>(value);
}

Result<std::uint16_t> word(const LdCtrl& element, const std::string& name)
{
	KESTREL_TRY(auto value, number(element, name));
	if (value > 0xFFFF)
	{
		return makeError(ErrorCode::InvalidArgument, element.name + ": " + name + " out of range");
	}
	return static_cast<std::uint16_t>(value);
}

constexpr std::uint8_t kMemTypeEeprom = 3;
constexpr std::uint8_t kSegFlagChecksum = 0x80;

bool flag(const LdCtrl& element, const std::string& name)
{
	const auto text = attribute(element, name);
	return text && (*text == "true" || *text == "1");
}

/// AppliesTo="full,par": does the element apply to `kind`?
bool applies(const LdCtrl& element, const std::string& kind)
{
	const auto text = attribute(element, "AppliesTo");
	if (!text)
	{
		return true;
	}
	std::stringstream list(*text);
	std::string item;
	while (std::getline(list, item, ','))
	{
		if (item == kind)
		{
			return true;
		}
	}
	return false;
}

class Resolver
{
public:
	explicit Resolver(const ResolveInput& input) : m_input(input) {}

	Result<Plan> run()
	{
		for (const LdCtrl& element : m_input.procedure)
		{
			KESTREL_CHECK(add(element, true));
		}
		return std::move(m_plan);
	}

private:
	Status add(const LdCtrl& element, bool allowMerge)
	{
		const std::string& name = element.name;
		if (name == "LdCtrlMerge")
		{
			if (!allowMerge)
			{
				return makeError(ErrorCode::InvalidArgument, "nested LdCtrlMerge");
			}
			KESTREL_TRY(auto mergeId, number(element, "MergeId"));
			const auto it = m_input.merges.find(static_cast<int>(mergeId));
			if (it != m_input.merges.end())
			{
				for (const LdCtrl& merged : it->second)
				{
					if (applies(merged, m_input.appliesTo))
					{
						KESTREL_CHECK(add(merged, false));
					}
				}
			}
			return ok();
		}
		if (m_afterRestart && name != "LdCtrlDisconnect")
		{
			// The device restarts and drops the connection; what follows (e.g. the MTN670804's LsmIdx 5, which the
			// device does not have) would need a new connection.
			m_plan.warnings.push_back(name + " after the restart is left out");
			return ok();
		}
		if (name == "LdCtrlConnect")
		{
			// The management session connects on demand; authorize right after, as devices refuse writes otherwise.
			m_plan.steps.push_back(Authorize{});
			return ok();
		}
		if (name == "LdCtrlDisconnect")
		{
			return ok(); // the executor closes the connection at the end
		}
		if (name == "LdCtrlMapError")
		{
			// MapError(original → 0) starts tolerating errors, MapError(original → original) ends it.
			KESTREL_TRY(auto original, number(element, "OriginalError"));
			KESTREL_TRY(auto mapped, number(element, "MappedError"));
			m_tolerate = mapped != original;
			return ok();
		}
		if (name == "LdCtrlUnload" || name == "LdCtrlLoad" || name == "LdCtrlLoadCompleted")
		{
			KESTREL_TRY(auto lsm, byteAttribute(element, "LsmIdx", 0, true));
			if (name == "LdCtrlUnload")
			{
				m_plan.steps.push_back(Unload{lsm, m_tolerate});
			}
			else if (name == "LdCtrlLoad")
			{
				m_plan.steps.push_back(Load{lsm});
			}
			else
			{
				m_plan.steps.push_back(LoadCompleted{lsm});
			}
			return ok();
		}
		if (name == "LdCtrlRelSegment")
		{
			KESTREL_TRY(auto lsm, byteAttribute(element, "LsmIdx", 0, true));
			KESTREL_TRY(auto size, number(element, "Size"));
			KESTREL_TRY(auto mode, byteAttribute(element, "Mode", 0, false));
			KESTREL_TRY(auto fill, byteAttribute(element, "Fill", 0, false));
			// The mask procedure's table allocations carry a placeholder size; allocate what is written.
			if (const auto image = m_input.images.find(lsm); image != m_input.images.end())
			{
				size = std::max<std::uint64_t>(size, image->second.size());
			}
			if (size > 0xFFFFFFFFu)
			{
				return makeError(ErrorCode::InvalidArgument, "LdCtrlRelSegment: size out of range");
			}
			m_plan.steps.push_back(AllocRelSegment{lsm, static_cast<std::uint32_t>(size), mode, fill});
			return ok();
		}
		if (name == "LdCtrlWriteRelMem")
		{
			KESTREL_TRY(auto object, byteAttribute(element, "ObjIdx", 0, true));
			KESTREL_TRY(auto offset, number(element, "Offset"));
			KESTREL_TRY(auto size, number(element, "Size"));
			const auto image = m_input.images.find(object);
			if (image == m_input.images.end())
			{
				return makeError(
					ErrorCode::InvalidArgument, "LdCtrlWriteRelMem: no image for object " + std::to_string(object));
			}
			const Bytes& bytes = image->second;
			if (offset > bytes.size())
			{
				return makeError(ErrorCode::InvalidArgument, "LdCtrlWriteRelMem: offset beyond the image");
			}
			const std::size_t length = static_cast<std::size_t>(std::min<std::uint64_t>(size, bytes.size() - offset));
			WriteRelMem step{object, static_cast<std::uint32_t>(offset),
				Bytes(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
					bytes.begin() + static_cast<std::ptrdiff_t>(offset + length)),
				flag(element, "Verify")};
			if (!step.data.empty())
			{
				m_plan.steps.push_back(std::move(step));
			}
			return ok();
		}
		if (name == "LdCtrlWriteProp" || name == "LdCtrlCompareProp")
		{
			KESTREL_TRY(auto object, byteAttribute(element, "ObjIdx", 0, true));
			KESTREL_TRY(auto pid, byteAttribute(element, "PropId", 0, true));
			Bytes data;
			if (const auto inline_ = attribute(element, "InlineData"))
			{
				KESTREL_TRY(auto parsed, fromHex(*inline_));
				data = std::move(parsed);
			}
			if (pid == kPidProgramVersion)
			{
				if (const auto version = m_input.programVersions.find(object); version != m_input.programVersions.end())
				{
					data = version->second;
				}
			}
			if (name == "LdCtrlWriteProp")
			{
				m_plan.steps.push_back(WriteProp{object, pid, std::move(data), flag(element, "Verify")});
			}
			else
			{
				m_plan.steps.push_back(CompareProp{object, pid, std::move(data)});
			}
			return ok();
		}
		if (name == "LdCtrlLoadImageProp")
		{
			KESTREL_TRY(auto object, byteAttribute(element, "ObjIdx", 0, true));
			KESTREL_TRY(auto pid, byteAttribute(element, "PropId", kPidMcbTable, false));
			m_plan.steps.push_back(LoadImageProp{object, pid});
			return ok();
		}
		if (name == "LdCtrlRestart")
		{
			m_plan.steps.push_back(Restart{});
			m_afterRestart = true;
			return ok();
		}
		if (name == "LdCtrlAbsSegment")
		{
			KESTREL_TRY(auto lsm, byteAttribute(element, "LsmIdx", 0, true));
			KESTREL_TRY(auto segType, byteAttribute(element, "SegType", 0, false));
			KESTREL_TRY(auto address, word(element, "Address"));
			KESTREL_TRY(auto size, word(element, "Size"));
			KESTREL_TRY(auto access, byteAttribute(element, "Access", 0, false));
			KESTREL_TRY(auto memType, byteAttribute(element, "MemType", 0, false));
			KESTREL_TRY(auto segFlags, byteAttribute(element, "SegFlags", 0, false));
			const Bytes parameters{hi(address), lo(address), hi(size), lo(size), access, memType, segFlags};
			m_plan.steps.push_back(AdditionalLoadControl{lsm, additionalLoadControlPayload(segType, parameters)});
			// Only non-volatile segments (MemType 3, EEPROM) are written; RAM segments are initialised by the
			// application.
			const auto image = memType == kMemTypeEeprom ? m_input.absoluteImages.find(address) : m_input.absoluteImages.end();
			if (image != m_input.absoluteImages.end())
			{
				if (image->second.size() > size)
				{
					return makeError(ErrorCode::InvalidArgument, "LdCtrlAbsSegment: image larger than the segment");
				}
				Bytes data = image->second;
				if ((segFlags & kSegFlagChecksum) && !data.empty())
				{
					// The segment's last octet is the XOR of the others (all three flagged segments of a factory-loaded
					// MTN670804 hold it; the product file's <Data> has 0 there).
					data.resize(size, 0);
					std::uint8_t checksum = 0;
					for (std::size_t i = 0; i + 1 < data.size(); ++i)
					{
						checksum ^= data[i];
					}
					data.back() = checksum;
				}
				const auto mask = m_input.absoluteMasks.find(address);
				if (mask == m_input.absoluteMasks.end())
				{
					m_plan.steps.push_back(WriteMem{address, std::move(data), true});
					return ok();
				}
				// Only the masked octets: the others are the device's (a BCU's header holds its run-error flags and
				// the ids its load controls set; writing the <Data> there halted a MTN670804).
				std::size_t i = 0;
				while (i < data.size())
				{
					if (i >= mask->second.size() || mask->second[i] == 0)
					{
						++i;
						continue;
					}
					const std::size_t start = i;
					while (i < data.size() && i < mask->second.size() && mask->second[i] != 0)
					{
						++i;
					}
					m_plan.steps.push_back(WriteMem{static_cast<std::uint16_t>(address + start),
						Bytes(data.begin() + static_cast<std::ptrdiff_t>(start), data.begin() + static_cast<std::ptrdiff_t>(i)),
						true});
				}
			}
			return ok();
		}
		if (name == "LdCtrlTaskSegment")
		{
			KESTREL_TRY(auto lsm, byteAttribute(element, "LsmIdx", 0, true));
			KESTREL_TRY(auto address, word(element, "Address"));
			if (m_input.applicationId.size() != 5)
			{
				return makeError(ErrorCode::InvalidArgument, "LdCtrlTaskSegment: no application id");
			}
			Bytes parameters{hi(address), lo(address), m_input.peiType};
			parameters.insert(parameters.end(), m_input.applicationId.begin(), m_input.applicationId.end());
			m_plan.steps.push_back(AdditionalLoadControl{lsm, additionalLoadControlPayload(0x02, parameters)});
			return ok();
		}
		if (name == "LdCtrlTaskPtr")
		{
			KESTREL_TRY(auto lsm, byteAttribute(element, "LsmIdx", 0, true));
			KESTREL_TRY(auto init, word(element, "InitPtr"));
			KESTREL_TRY(auto save, word(element, "SavePtr"));
			KESTREL_TRY(auto serial, word(element, "SerialPtr"));
			const Bytes parameters{hi(init), lo(init), hi(save), lo(save), hi(serial), lo(serial)};
			m_plan.steps.push_back(AdditionalLoadControl{lsm, additionalLoadControlPayload(0x03, parameters)});
			return ok();
		}
		if (name == "LdCtrlTaskCtrl1")
		{
			KESTREL_TRY(auto lsm, byteAttribute(element, "LsmIdx", 0, true));
			KESTREL_TRY(auto address, word(element, "Address"));
			KESTREL_TRY(auto count, byteAttribute(element, "Count", 0, true));
			const Bytes parameters{hi(address), lo(address), count};
			m_plan.steps.push_back(AdditionalLoadControl{lsm, additionalLoadControlPayload(0x04, parameters)});
			return ok();
		}
		if (name == "LdCtrlTaskCtrl2")
		{
			KESTREL_TRY(auto lsm, byteAttribute(element, "LsmIdx", 0, true));
			KESTREL_TRY(auto callback, word(element, "Callback"));
			KESTREL_TRY(auto address, word(element, "Address"));
			KESTREL_TRY(auto seg0, word(element, "Seg0"));
			KESTREL_TRY(auto seg1, word(element, "Seg1"));
			const Bytes parameters{hi(callback), lo(callback), hi(address), lo(address), hi(seg0), lo(seg0), hi(seg1),
				lo(seg1)};
			m_plan.steps.push_back(AdditionalLoadControl{lsm, additionalLoadControlPayload(0x05, parameters)});
			return ok();
		}
		return makeError(ErrorCode::Unsupported, "load procedure element " + name + " is not supported yet");
	}

	static std::uint8_t hi(std::uint16_t value) { return static_cast<std::uint8_t>(value >> 8); }
	static std::uint8_t lo(std::uint16_t value) { return static_cast<std::uint8_t>(value); }

	const ResolveInput& m_input;
	Plan m_plan;
	bool m_tolerate = false;
	bool m_afterRestart = false;
};

} // namespace

Result<Plan> resolve(const ResolveInput& input)
{
	return Resolver(input).run();
}

} // namespace kestrel::download
