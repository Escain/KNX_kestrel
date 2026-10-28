#pragma once

// IBusLink attached directly to a VirtualBus (no KNXnet/IP in between): for in-process tests of
// code written against IBusLink (NFR-5).
//
// send() puts the frame on the medium, reports onConfirm(true), then delivers; so the
// confirmation precedes any answer, as on a real interface. Callbacks run synchronously inside
// send()/bus activity; calling send() from a callback is allowed (the bus queues).

#include "kestrel/knx/Address.hpp"
#include "kestrel/knx/BusLink.hpp"
#include "kestrel/sim/VirtualBus.hpp"

namespace kestrel::sim
{

class SimLink : public knx::IBusLink
{
public:
	explicit SimLink(VirtualBus& bus, knx::IndividualAddress address = knx::IndividualAddress(0x11FE));
	~SimLink() override;
	SimLink(const SimLink&) = delete;
	SimLink& operator=(const SimLink&) = delete;

	/// Attaches to the bus and reports Connected.
	void open();
	/// Detaches and reports Disconnected.
	void close();
	bool isOpen() const { return m_endpoint != 0; }

	Status send(const knx::cemi::LData& frame) override;

	knx::IndividualAddress address() const { return m_address; }

private:
	VirtualBus& m_bus;
	knx::IndividualAddress m_address;
	EndpointId m_endpoint = 0;
};

} // namespace kestrel::sim
