#pragma once

// operator<< for types used in ASSERT_EQ that have no printer of their own.
// Global namespace on purpose: UNIT_TESTs live at file scope and find these
// by ordinary lookup. Include before <UnitTests.hpp>.

#include <cstdint>
#include <optional>
#include <ostream>
#include <utility>
#include <vector>

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::optional<T>& value)
{
	if (!value)
	{
		return os << "nullopt";
	}
	return os << *value;
}

template <typename A, typename B>
std::ostream& operator<<(std::ostream& os, const std::pair<A, B>& value)
{
	return os << '(' << value.first << ", " << value.second << ')';
}

// Byte vectors print as hex; other vectors as [a, b, c].
inline std::ostream& operator<<(std::ostream& os, const std::vector<std::uint8_t>& bytes)
{
	static constexpr char kDigits[] = "0123456789abcdef";
	os << '[';
	for (std::size_t i = 0; i < bytes.size(); ++i)
	{
		os << (i ? " " : "") << kDigits[bytes[i] >> 4] << kDigits[bytes[i] & 0x0F];
	}
	return os << ']';
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& values)
{
	os << '[';
	for (std::size_t i = 0; i < values.size(); ++i)
	{
		os << (i ? ", " : "") << values[i];
	}
	return os << ']';
}
