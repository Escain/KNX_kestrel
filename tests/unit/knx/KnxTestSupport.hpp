#pragma once

// Helpers shared by the knx unit tests. No asserts here: UnitTests.hpp asserts only work inside
// UNIT_TEST bodies, so helpers return values that the tests check.

#include "kestrel/base/Bytes.hpp"
#include "kestrel/knx/Apdu.hpp"
#include "kestrel/knx/Cemi.hpp"
#include "kestrel/knx/Clock.hpp"
#include "kestrel/knx/KnxIp.hpp"

#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace knxtest
{

inline kestrel::Bytes hex(std::string_view text)
{
	return kestrel::fromHex(text).value();
}

/// Manual clock origin; tests add durations to it.
inline const kestrel::knx::TimePoint kT0 = kestrel::knx::TimePoint{} + std::chrono::hours(1);

/// All actions of alternative T, in order.
template <typename T, typename Action>
std::vector<T> only(const std::vector<Action>& actions)
{
	std::vector<T> out;
	for (const auto& action : actions)
	{
		if (const auto* value = std::get_if<T>(&action))
		{
			out.push_back(*value);
		}
	}
	return out;
}

/// Decodes a KNXnet/IP datagram into frame type T, or nullopt.
template <typename T>
std::optional<T> knxipAs(const kestrel::Bytes& bytes)
{
	auto frame = kestrel::knx::knxip::decode(bytes);
	if (!frame)
	{
		return std::nullopt;
	}
	if (const auto* value = std::get_if<T>(&*frame))
	{
		return *value;
	}
	return std::nullopt;
}

/// TPDU of an L_Data frame (asserting validity is the caller's job).
inline std::optional<kestrel::knx::Tpdu> tpduOf(const kestrel::knx::cemi::LData& frame)
{
	auto tpdu = kestrel::knx::decodeTpdu(frame);
	if (!tpdu)
	{
		return std::nullopt;
	}
	return *tpdu;
}

/// An L_Data.ind from `source` to individual `destination` carrying `tpdu`.
inline kestrel::knx::cemi::LData indication(std::uint16_t source, std::uint16_t destination,
	const kestrel::knx::Tpdu& tpdu)
{
	auto frame = kestrel::knx::makeIndividualFrame(kestrel::knx::IndividualAddress(destination), tpdu);
	frame.messageCode = kestrel::knx::cemi::MessageCode::LDataInd;
	frame.source = source;
	return frame;
}

} // namespace knxtest
