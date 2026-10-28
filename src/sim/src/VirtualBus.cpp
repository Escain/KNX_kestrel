#include "kestrel/sim/VirtualBus.hpp"

namespace kestrel::sim
{

std::ostream& operator<<(std::ostream& os, const Telegram& telegram)
{
	return os << '#' << telegram.index << " from endpoint " << telegram.sender << ": " << telegram.frame;
}

EndpointId VirtualBus::attach(Receiver receiver)
{
	const auto id = m_nextEndpoint++;
	m_receivers.emplace(id, std::move(receiver));
	return id;
}

void VirtualBus::detach(EndpointId endpoint)
{
	m_receivers.erase(endpoint);
}

void VirtualBus::transmit(EndpointId sender, knx::cemi::LData frame)
{
	post(sender, std::move(frame));
	flush();
}

void VirtualBus::post(EndpointId sender, knx::cemi::LData frame)
{
	frame.messageCode = knx::cemi::MessageCode::LDataInd;
	frame.confirmError = false;

	int copies = 1;
	for (auto& rule : m_faults)
	{
		if (rule.remaining == 0 || !rule.match || !rule.match(frame))
		{
			continue;
		}
		if (rule.remaining > 0)
		{
			--rule.remaining;
		}
		copies = rule.action == FaultAction::Drop ? 0 : 2;
		break;
	}
	for (int i = 0; i < copies; ++i)
	{
		m_queue.push_back(Pending{sender, frame});
	}
}

void VirtualBus::flush()
{
	if (m_delivering)
	{
		return;
	}
	m_delivering = true;
	while (!m_queue.empty())
	{
		Pending pending = std::move(m_queue.front());
		m_queue.pop_front();
		m_log.push_back(Telegram{m_nextIndex++, pending.sender, pending.frame});
		if (onTelegram)
		{
			onTelegram(m_log.back());
		}
		// Snapshot the ids: receivers may attach/detach while handling a frame.
		std::vector<EndpointId> ids;
		ids.reserve(m_receivers.size());
		for (const auto& entry : m_receivers)
		{
			ids.push_back(entry.first);
		}
		for (const auto id : ids)
		{
			if (id == pending.sender)
			{
				continue;
			}
			const auto it = m_receivers.find(id);
			if (it != m_receivers.end() && it->second)
			{
				auto receiver = it->second; // copy: the receiver may detach itself
				receiver(pending.frame);
			}
		}
	}
	m_delivering = false;
}

} // namespace kestrel::sim
