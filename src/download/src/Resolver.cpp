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
		if (name == "LdCtrlConnect" || name == "LdCtrlDisconnect")
		{
			return ok(); // the executor's management session connects on demand and closes at the end
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
			return ok();
		}
		return makeError(ErrorCode::Unsupported, "load procedure element " + name + " is not supported yet");
	}

	const ResolveInput& m_input;
	Plan m_plan;
	bool m_tolerate = false;
};

} // namespace

Result<Plan> resolve(const ResolveInput& input)
{
	return Resolver(input).run();
}

} // namespace kestrel::download
