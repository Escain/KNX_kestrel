#include "kestrel/knx/SerialNumber.hpp"

#include "kestrel/base/Bytes.hpp"

#include <algorithm>

namespace kestrel::knx
{

Result<SerialNumber> SerialNumber::parse(std::string_view text)
{
	std::string digits;
	for (char c : text)
	{
		if (c != ':')
		{
			digits.push_back(c);
		}
	}
	if (digits.size() != 12)
	{
		return makeError(ErrorCode::ParseError, "serial number needs 12 hex digits: '" + std::string(text) + "'");
	}
	KESTREL_TRY(const Bytes raw, fromHex(digits));
	SerialNumber serial;
	std::copy(raw.begin(), raw.end(), serial.bytes.begin());
	return serial;
}

std::string SerialNumber::toString() const
{
	std::string hex = toHex(bytes);
	hex.erase(std::remove(hex.begin(), hex.end(), ' '), hex.end());
	return hex.substr(0, 4) + ':' + hex.substr(4);
}

std::ostream& operator<<(std::ostream& os, const SerialNumber& serial)
{
	return os << serial.toString();
}

} // namespace kestrel::knx
