#pragma once

// Link-level bus access shared by the real KNXnet/IP tunnel (bus::UdpTunnelLink) and the
// in-process simulator (sim::SimLink). Callbacks are invoked on the link's thread; adapters that
// cross threads must marshal them themselves.

#include "kestrel/base/Result.hpp"
#include "kestrel/knx/Cemi.hpp"

#include <functional>
#include <ostream>

namespace kestrel::knx
{

enum class LinkState
{
	Disconnected,
	Connecting,
	Connected,
};

std::ostream& operator<<(std::ostream& os, LinkState state);

class IBusLink
{
public:
	virtual ~IBusLink() = default;

	/// Queues an L_Data.req. Fails (without queuing) when the link is not connected.
	/// Source 0.0.0 is replaced by the link's own individual address.
	virtual Status send(const cemi::LData& frame) = 0;

	/// L_Data.ind received from the bus.
	std::function<void(const cemi::LData&)> onFrame;
	/// Link state transitions.
	std::function<void(LinkState)> onState;
	/// L_Data.con for each frame accepted by send(), in order; false = not sent / negative.
	std::function<void(bool ok)> onConfirm;

protected:
	IBusLink() = default;
	IBusLink(const IBusLink&) = default;
	IBusLink& operator=(const IBusLink&) = default;
};

} // namespace kestrel::knx
