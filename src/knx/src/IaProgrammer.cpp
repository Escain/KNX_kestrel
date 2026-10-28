#include "kestrel/knx/IaProgrammer.hpp"

#include "kestrel/knx/Apdu.hpp"

namespace kestrel::knx
{

IaProgrammer::IaProgrammer(IaProgrammerConfig config) : m_config(config) {}

void IaProgrammer::setState(IaProgrammerState state)
{
	if (m_state != state)
	{
		m_state = state;
		m_actions.push_back(StateChanged{state});
	}
}

void IaProgrammer::start(IndividualAddress target, TimePoint now)
{
	m_target = target;
	m_previous = IndividualAddress();
	m_verifyAttempt = 0;
	m_responders.clear();
	m_lastResponders.clear();
	m_lastForeign.clear();
	m_deadline = now + m_config.progModeTimeout;
	m_state = IaProgrammerState::Idle; // force a StateChanged even on restart
	setState(IaProgrammerState::WaitProgMode);
	sendRead(now);
}

bool IaProgrammer::running() const
{
	return m_state == IaProgrammerState::WaitProgMode || m_state == IaProgrammerState::CheckAddress
		|| m_state == IaProgrammerState::Verify;
}

void IaProgrammer::cancel(TimePoint /*now*/)
{
	if (!running())
	{
		return;
	}
	if (m_state == IaProgrammerState::CheckAddress)
	{
		m_actions.push_back(SendFrame{makeIndividualFrame(m_target, Tpdu::disconnect())});
	}
	finish(IaProgrammerFailure::Cancelled);
}

void IaProgrammer::sendRead(TimePoint now)
{
	m_responders.clear();
	m_responseCount = 0;
	m_windowEnd = now + m_config.readInterval;
	m_actions.push_back(SendFrame{makeBroadcastFrame(individualAddressRead())});
}

void IaProgrammer::finish(IaProgrammerFailure failure)
{
	setState(failure == IaProgrammerFailure::None ? IaProgrammerState::Done : IaProgrammerState::Failed);
	m_actions.push_back(Finished{failure, m_previous});
}

void IaProgrammer::onFrame(const cemi::LData& frame, TimePoint /*now*/)
{
	if (m_state == IaProgrammerState::CheckAddress)
	{
		// Whatever the target sends (T_ACK, the descriptor, or T_Disconnect because it is busy) proves
		// that a device already owns the address.
		if (!frame.groupDestination && frame.source == m_target.raw())
		{
			auto tpdu = decodeTpdu(frame);
			if (!tpdu || tpdu->type != TpduType::Disconnect)
			{
				m_actions.push_back(SendFrame{makeIndividualFrame(m_target, Tpdu::disconnect())});
			}
			finish(IaProgrammerFailure::AddressInUse);
		}
		return;
	}
	if (m_state != IaProgrammerState::WaitProgMode && m_state != IaProgrammerState::Verify)
	{
		return;
	}
	auto tpdu = decodeTpdu(frame);
	if (!tpdu || tpdu->type != TpduType::DataBroadcast || tpdu->apdu.apci != Apci::IndividualAddressResponse)
	{
		return;
	}
	m_responders.insert(frame.sourceAddress());
	++m_responseCount;
}

void IaProgrammer::writeAndVerify(TimePoint now)
{
	m_actions.push_back(SendFrame{makeBroadcastFrame(individualAddressWrite(m_target))});
	setState(IaProgrammerState::Verify);
	m_verifyAttempt = 1;
	m_lastForeign.clear();
	sendRead(now);
}

void IaProgrammer::closeWindow(TimePoint now)
{
	m_lastResponders = m_responders;
	if (m_state == IaProgrammerState::CheckAddress)
	{
		// Nobody answered at the target address: close our half-open connection and program.
		m_actions.push_back(SendFrame{makeIndividualFrame(m_target, Tpdu::disconnect())});
		writeAndVerify(now);
		return;
	}
	if (m_state == IaProgrammerState::Verify)
	{
		closeVerifyWindow(now);
		return;
	}

	// WaitProgMode. Safety: count frames, so that two devices sharing an address are not both programmed.
	if (m_responseCount >= 2)
	{
		finish(IaProgrammerFailure::MultipleDevicesInProgMode);
		return;
	}
	if (m_responders.size() == 1)
	{
		m_previous = *m_responders.begin();
		if (m_config.allowOverwrite || m_previous == m_target)
		{
			writeAndVerify(now);
			return;
		}
		// 3/5/2 NM_IndividualAddress_Write: make sure no other device already uses the address.
		setState(IaProgrammerState::CheckAddress);
		m_windowEnd = now + m_config.addressCheckTimeout;
		m_actions.push_back(SendFrame{makeIndividualFrame(m_target, Tpdu::connect())});
		m_actions.push_back(SendFrame{makeIndividualFrame(m_target, Tpdu::dataConnected(0, deviceDescriptorRead()))});
		return;
	}
	if (now >= m_deadline)
	{
		finish(IaProgrammerFailure::Timeout);
		return;
	}
	sendRead(now);
}

void IaProgrammer::closeVerifyWindow(TimePoint now)
{
	if (m_responseCount == 1 && *m_responders.begin() == m_target)
	{
		m_actions.push_back(SendFrame{makeIndividualFrame(m_target, Tpdu::dataIndividual(restart()))});
		finish(IaProgrammerFailure::None);
		return;
	}
	std::set<IndividualAddress> foreign;
	bool persists = false;
	for (const auto address : m_responders)
	{
		if (address != m_target && address != m_previous)
		{
			foreign.insert(address);
			persists = persists || m_lastForeign.count(address) != 0;
		}
	}
	m_lastForeign = std::move(foreign);
	if (persists || m_verifyAttempt >= m_config.verifyAttempts)
	{
		finish(IaProgrammerFailure::VerifyFailed);
		return;
	}
	++m_verifyAttempt;
	sendRead(now);
}

void IaProgrammer::tick(TimePoint now)
{
	if (running() && now >= m_windowEnd)
	{
		closeWindow(now);
	}
}

std::vector<IaProgrammer::Action> IaProgrammer::takeActions()
{
	std::vector<Action> out;
	out.swap(m_actions);
	return out;
}

std::ostream& operator<<(std::ostream& os, IaProgrammerState state)
{
	switch (state)
	{
	case IaProgrammerState::Idle: return os << "Idle";
	case IaProgrammerState::WaitProgMode: return os << "WaitProgMode";
	case IaProgrammerState::CheckAddress: return os << "CheckAddress";
	case IaProgrammerState::Verify: return os << "Verify";
	case IaProgrammerState::Done: return os << "Done";
	case IaProgrammerState::Failed: return os << "Failed";
	}
	return os << "IaProgrammerState(" << static_cast<int>(state) << ')';
}

std::ostream& operator<<(std::ostream& os, IaProgrammerFailure failure)
{
	switch (failure)
	{
	case IaProgrammerFailure::None: return os << "None";
	case IaProgrammerFailure::Timeout: return os << "Timeout";
	case IaProgrammerFailure::MultipleDevicesInProgMode: return os << "MultipleDevicesInProgMode";
	case IaProgrammerFailure::VerifyFailed: return os << "VerifyFailed";
	case IaProgrammerFailure::Cancelled: return os << "Cancelled";
	case IaProgrammerFailure::AddressInUse: return os << "AddressInUse";
	}
	return os << "IaProgrammerFailure(" << static_cast<int>(failure) << ')';
}

std::ostream& operator<<(std::ostream& os, const IaProgrammer::SendFrame& a)
{
	return os << "SendFrame{" << a.frame << '}';
}

std::ostream& operator<<(std::ostream& os, const IaProgrammer::StateChanged& a)
{
	return os << "StateChanged{" << a.state << '}';
}

std::ostream& operator<<(std::ostream& os, const IaProgrammer::Finished& a)
{
	return os << "Finished{" << a.failure << " previous=" << a.previousAddress << '}';
}

std::ostream& operator<<(std::ostream& os, const IaProgrammer::Action& action)
{
	std::visit([&os](const auto& a) { os << a; }, action);
	return os;
}

} // namespace kestrel::knx
