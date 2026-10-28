#pragma once

// KNX addresses (KNX Standard 3/3/2 and 3/5/1).
//  - Individual address: area (4 bits) . line (4 bits) . device (8 bits)
//  - Group address: raw 16 bits, presented in 3-level (5/3/8), 2-level (5/11) or free style.

#include "kestrel/base/Result.hpp"

#include <compare>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

namespace kestrel::knx
{

class IndividualAddress
{
	std::uint16_t m_raw = 0;

public:
	constexpr IndividualAddress() = default;
	constexpr explicit IndividualAddress(std::uint16_t raw) : m_raw(raw) {}

	/// Returns InvalidArgument if area/line > 15 or device > 255.
	static Result<IndividualAddress> fromParts(unsigned area, unsigned line, unsigned device);
	/// Parses "1.1.5".
	static Result<IndividualAddress> parse(std::string_view text);

	constexpr std::uint16_t raw() const { return m_raw; }
	constexpr unsigned area() const { return m_raw >> 12; }
	constexpr unsigned line() const { return (m_raw >> 8) & 0x0F; }
	constexpr unsigned device() const { return m_raw & 0xFF; }

	std::string toString() const;

	friend constexpr auto operator<=>(IndividualAddress, IndividualAddress) = default;
};

enum class GroupAddressStyle
{
	ThreeLevel, ///< main/middle/sub  5/3/8 bits
	TwoLevel,   ///< main/sub         5/11 bits
	Free,       ///< 0..65535
};

class GroupAddress
{
	std::uint16_t m_raw = 0;

public:
	constexpr GroupAddress() = default;
	constexpr explicit GroupAddress(std::uint16_t raw) : m_raw(raw) {}

	static Result<GroupAddress> fromThreeLevel(unsigned main, unsigned middle, unsigned sub);
	static Result<GroupAddress> fromTwoLevel(unsigned main, unsigned sub);
	/// Parses "1/2/3" (3-level), "1/515" (2-level) or "4660" (free).
	static Result<GroupAddress> parse(std::string_view text);

	constexpr std::uint16_t raw() const { return m_raw; }
	constexpr unsigned main() const { return m_raw >> 11; }
	constexpr unsigned middle() const { return (m_raw >> 8) & 0x07; }
	constexpr unsigned sub() const { return m_raw & 0xFF; }
	constexpr unsigned sub2Level() const { return m_raw & 0x07FF; }

	std::string toString(GroupAddressStyle style = GroupAddressStyle::ThreeLevel) const;

	friend constexpr auto operator<=>(GroupAddress, GroupAddress) = default;
};

std::ostream& operator<<(std::ostream& os, IndividualAddress address);
std::ostream& operator<<(std::ostream& os, GroupAddress address);

} // namespace kestrel::knx
