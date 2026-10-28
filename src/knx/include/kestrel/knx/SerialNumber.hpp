#pragma once

// KNX serial number: 6 bytes, the first two being the manufacturer id
// (KNX Standard 3/5/1, PID_SERIAL_NUMBER). Text form "0083:00001234".

#include "kestrel/base/Result.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

namespace kestrel::knx
{

struct SerialNumber
{
	std::array<std::uint8_t, 6> bytes{};

	/// Parses "0083:00001234" or "008300001234" (12 hex digits).
	static Result<SerialNumber> parse(std::string_view text);

	std::uint16_t manufacturer() const { return static_cast<std::uint16_t>((bytes[0] << 8) | bytes[1]); }
	/// "0083:00001234"
	std::string toString() const;

	friend auto operator<=>(const SerialNumber&, const SerialNumber&) = default;
};

std::ostream& operator<<(std::ostream& os, const SerialNumber& serial);

} // namespace kestrel::knx
