#include "kestrel/knx/Address.hpp"

#include <charconv>
#include <vector>

namespace kestrel::knx
{

namespace
{

/// Splits "a<sep>b<sep>c" into unsigned numbers; fails on empty or non-numeric parts.
Result<std::vector<unsigned>> splitNumbers(std::string_view text, char separator)
{
	std::vector<unsigned> parts;
	while (true)
	{
		const auto pos = text.find(separator);
		const auto token = text.substr(0, pos);
		unsigned value = 0;
		const auto* end = token.data() + token.size();
		const auto [ptr, ec] = std::from_chars(token.data(), end, value);
		if (token.empty() || ec != std::errc{} || ptr != end)
		{
			return makeError(ErrorCode::ParseError, "invalid address component '" + std::string(token) + "'");
		}
		parts.push_back(value);
		if (pos == std::string_view::npos)
		{
			break;
		}
		text.remove_prefix(pos + 1);
	}
	return parts;
}

} // namespace

Result<IndividualAddress> IndividualAddress::fromParts(unsigned area, unsigned line, unsigned device)
{
	if (area > 15 || line > 15 || device > 255)
	{
		return makeError(ErrorCode::InvalidArgument,
			"individual address out of range: " + std::to_string(area) + "." + std::to_string(line) + "."
				+ std::to_string(device));
	}
	return IndividualAddress(static_cast<std::uint16_t>((area << 12) | (line << 8) | device));
}

Result<IndividualAddress> IndividualAddress::parse(std::string_view text)
{
	KESTREL_TRY(auto parts, splitNumbers(text, '.'));
	if (parts.size() != 3)
	{
		return makeError(ErrorCode::ParseError, "individual address must be A.L.D: '" + std::string(text) + "'");
	}
	return fromParts(parts[0], parts[1], parts[2]);
}

std::string IndividualAddress::toString() const
{
	return std::to_string(area()) + "." + std::to_string(line()) + "." + std::to_string(device());
}

Result<GroupAddress> GroupAddress::fromThreeLevel(unsigned main, unsigned middle, unsigned sub)
{
	if (main > 31 || middle > 7 || sub > 255)
	{
		return makeError(ErrorCode::InvalidArgument, "3-level group address out of range");
	}
	return GroupAddress(static_cast<std::uint16_t>((main << 11) | (middle << 8) | sub));
}

Result<GroupAddress> GroupAddress::fromTwoLevel(unsigned main, unsigned sub)
{
	if (main > 31 || sub > 2047)
	{
		return makeError(ErrorCode::InvalidArgument, "2-level group address out of range");
	}
	return GroupAddress(static_cast<std::uint16_t>((main << 11) | sub));
}

Result<GroupAddress> GroupAddress::parse(std::string_view text)
{
	KESTREL_TRY(auto parts, splitNumbers(text, '/'));
	switch (parts.size())
	{
		case 3: return fromThreeLevel(parts[0], parts[1], parts[2]);
		case 2: return fromTwoLevel(parts[0], parts[1]);
		case 1:
			if (parts[0] > 0xFFFF)
			{
				return makeError(ErrorCode::InvalidArgument, "free group address out of range");
			}
			return GroupAddress(static_cast<std::uint16_t>(parts[0]));
		default: return makeError(ErrorCode::ParseError, "invalid group address '" + std::string(text) + "'");
	}
}

std::string GroupAddress::toString(GroupAddressStyle style) const
{
	switch (style)
	{
		case GroupAddressStyle::ThreeLevel:
			return std::to_string(main()) + "/" + std::to_string(middle()) + "/" + std::to_string(sub());
		case GroupAddressStyle::TwoLevel: return std::to_string(main()) + "/" + std::to_string(sub2Level());
		case GroupAddressStyle::Free: return std::to_string(m_raw);
	}
	return {};
}

std::ostream& operator<<(std::ostream& os, IndividualAddress address)
{
	return os << address.toString();
}

std::ostream& operator<<(std::ostream& os, GroupAddress address)
{
	return os << address.toString();
}

} // namespace kestrel::knx
