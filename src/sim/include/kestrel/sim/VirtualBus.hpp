#pragma once

// Deterministic in-process KNX medium (Kestrel spec 04 §5). Every transmitted frame is delivered,
// as L_Data.ind, to every attached endpoint except the sender. Delivery is FIFO and
// non-reentrant: a frame sent from inside a receiver is queued and delivered after the current
// one, so causality is preserved without recursion.

#include "kestrel/knx/Cemi.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <ostream>
#include <vector>

namespace kestrel::sim
{

using EndpointId = std::uint32_t;

struct Telegram
{
	std::uint64_t index = 0;
	EndpointId sender = 0;
	knx::cemi::LData frame;

	friend bool operator==(const Telegram&, const Telegram&) = default;
};

std::ostream& operator<<(std::ostream& os, const Telegram& telegram);

enum class FaultAction
{
	Drop,      ///< the frame never reaches the medium (not logged)
	Duplicate, ///< the frame is delivered (and logged) twice, like a repetition on TP1
};

struct FaultRule
{
	FaultAction action = FaultAction::Drop;
	std::function<bool(const knx::cemi::LData&)> match;
	/// Number of matching frames still affected; negative = unlimited.
	int remaining = -1;
};

class VirtualBus
{
public:
	using Receiver = std::function<void(const knx::cemi::LData&)>;

	VirtualBus() = default;
	VirtualBus(const VirtualBus&) = delete;
	VirtualBus& operator=(const VirtualBus&) = delete;

	EndpointId attach(Receiver receiver);
	void detach(EndpointId endpoint);

	/// Queues `frame` and delivers everything queued unless a delivery is already in progress.
	void transmit(EndpointId sender, knx::cemi::LData frame);
	/// Queues without delivering; pair with flush() to run code between "on the medium" and delivery.
	void post(EndpointId sender, knx::cemi::LData frame);
	void flush();

	void addFaultRule(FaultRule rule) { m_faults.push_back(std::move(rule)); }
	void clearFaultRules() { m_faults.clear(); }

	const std::vector<Telegram>& telegrams() const { return m_log; }
	void clearTelegrams() { m_log.clear(); }
	/// Monitor hook, called for every telegram put on the medium (before delivery).
	std::function<void(const Telegram&)> onTelegram;

private:
	struct Pending
	{
		EndpointId sender;
		knx::cemi::LData frame;
	};

	std::map<EndpointId, Receiver> m_receivers;
	EndpointId m_nextEndpoint = 1;
	std::deque<Pending> m_queue;
	bool m_delivering = false;
	std::vector<FaultRule> m_faults;
	std::vector<Telegram> m_log;
	std::uint64_t m_nextIndex = 0;
};

} // namespace kestrel::sim
