#include "kestrel/sim/SimLink.hpp"

namespace kestrel::sim
{

SimLink::SimLink(VirtualBus& bus, knx::IndividualAddress address) : m_bus(bus), m_address(address) {}

SimLink::~SimLink()
{
	if (m_endpoint != 0)
	{
		m_bus.detach(m_endpoint);
	}
}

void SimLink::open()
{
	if (m_endpoint != 0)
	{
		return;
	}
	m_endpoint = m_bus.attach(
		[this](const knx::cemi::LData& frame)
		{
			if (onFrame)
			{
				onFrame(frame);
			}
		});
	if (onState)
	{
		onState(knx::LinkState::Connected);
	}
}

void SimLink::close()
{
	if (m_endpoint == 0)
	{
		return;
	}
	m_bus.detach(m_endpoint);
	m_endpoint = 0;
	if (onState)
	{
		onState(knx::LinkState::Disconnected);
	}
}

Status SimLink::send(const knx::cemi::LData& frame)
{
	if (m_endpoint == 0)
	{
		return makeError(ErrorCode::IoError, "simulated link is closed");
	}
	if (frame.tpdu.empty())
	{
		return makeError(ErrorCode::InvalidArgument, "L_Data without TPDU");
	}
	auto copy = frame;
	if (copy.source == 0)
	{
		copy.source = m_address.raw();
	}
	m_bus.post(m_endpoint, std::move(copy));
	if (onConfirm)
	{
		onConfirm(true);
	}
	m_bus.flush();
	return ok();
}

} // namespace kestrel::sim
