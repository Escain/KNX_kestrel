#include "kestrel/download/Executor.hpp"

#include <sstream>

namespace kestrel::download
{

namespace
{

std::string describe(const Step& step)
{
	std::ostringstream os;
	os << step;
	return os.str();
}

} // namespace

Executor::Executor(knx::IndividualAddress device, Plan plan, ExecutorConfig config)
	: m_session(device, config.session), m_plan(std::move(plan)), m_config(config)
{
}

void Executor::start(knx::TimePoint now)
{
	if (m_started)
	{
		return;
	}
	m_started = true;
	runStep(now);
	pump(now);
}

void Executor::cancel(knx::TimePoint now)
{
	fail(Error{ErrorCode::Cancelled, "download cancelled"}, now);
	pump(now);
}

std::string Executor::stepError(const std::string& what) const
{
	std::ostringstream os;
	os << "step " << (m_index + 1) << "/" << m_plan.steps.size() << " (" << m_plan.steps[m_index] << "): " << what;
	return os.str();
}

void Executor::writeLoadControl(std::uint8_t lsm, Bytes payload, knx::TimePoint now)
{
	m_wait = Wait::LoadState;
	m_pending = m_session.propertyValueWrite(lsm, kPidLoadStateControl, 1, 1, payload, now);
}

void Executor::runStep(knx::TimePoint now)
{
	if (m_finished)
	{
		return;
	}
	if (m_index >= m_plan.steps.size())
	{
		m_finished = true;
		m_wait = Wait::None;
		if (m_session.connected())
		{
			m_session.close(now);
		}
		m_actions.push_back(Finished{ok()});
		return;
	}
	const Step& step = m_plan.steps[m_index];
	m_actions.push_back(Progress{m_index, m_plan.steps.size(), describe(step)});

	if (const auto* authorize = std::get_if<Authorize>(&step))
	{
		m_wait = Wait::Authorize;
		m_pending = m_session.authorize(authorize->key, now);
	}
	else if (const auto* unload = std::get_if<Unload>(&step))
	{
		writeLoadControl(unload->lsm, loadControlPayload(LoadEvent::Unload), now);
	}
	else if (const auto* load = std::get_if<Load>(&step))
	{
		writeLoadControl(load->lsm, loadControlPayload(LoadEvent::StartLoading), now);
	}
	else if (const auto* completed = std::get_if<LoadCompleted>(&step))
	{
		writeLoadControl(completed->lsm, loadControlPayload(LoadEvent::LoadCompleted), now);
	}
	else if (const auto* alloc = std::get_if<AllocRelSegment>(&step))
	{
		writeLoadControl(alloc->lsm, allocRelSegmentPayload(*alloc), now);
	}
	else if (const auto* writeMem = std::get_if<WriteRelMem>(&step))
	{
		if (m_tableReferences.contains(writeMem->object))
		{
			startMemoryWrite(now);
		}
		else
		{
			m_wait = Wait::TableReference;
			m_pending = m_session.propertyValueRead(writeMem->object, kPidTableReference, 1, 1, now);
		}
	}
	else if (const auto* writeProp = std::get_if<WriteProp>(&step))
	{
		m_wait = Wait::PropertyWrite;
		m_pending = m_session.propertyValueWrite(writeProp->object, writeProp->pid, 1, 1, writeProp->data, now);
	}
	else if (const auto* compare = std::get_if<CompareProp>(&step))
	{
		m_wait = Wait::PropertyCompare;
		m_pending = m_session.propertyValueRead(compare->object, compare->pid, 1, 1, now);
	}
	else if (const auto* image = std::get_if<LoadImageProp>(&step))
	{
		const auto written = m_written.find(image->object);
		if (written == m_written.end())
		{
			fail(Error{ErrorCode::InvalidArgument, stepError("nothing was written to this object")}, now);
			return;
		}
		m_wait = Wait::PropertyWrite;
		m_pending = m_session.propertyValueWrite(image->object, image->pid, 1, 1, memoryControlBlock(written->second), now);
	}
	else if (std::holds_alternative<Restart>(step))
	{
		m_wait = Wait::Restart;
		m_pending = m_session.restart(now);
	}
}

void Executor::startMemoryWrite(knx::TimePoint now)
{
	const auto& step = std::get<WriteRelMem>(m_plan.steps[m_index]);
	const std::uint64_t address = std::uint64_t{m_tableReferences.at(step.object)} + step.offset;
	if (address + step.data.size() > 0x10000)
	{
		fail(Error{ErrorCode::Unsupported,
				 stepError("segment beyond 64 KiB needs extended memory services (not supported yet)")},
			now);
		return;
	}
	m_wait = Wait::MemoryWrite;
	m_pending = m_session.memoryWrite(static_cast<std::uint16_t>(address), step.data, now);
}

void Executor::advance(knx::TimePoint now)
{
	++m_index;
	m_wait = Wait::None;
	runStep(now);
}

void Executor::fail(Error error, knx::TimePoint now)
{
	if (m_finished)
	{
		return;
	}
	m_finished = true;
	m_wait = Wait::None;
	m_session.close(now);
	m_actions.push_back(Finished{Status(Unexpected{std::move(error)})});
}

void Executor::onLoadState(const Step& step, LoadState state, knx::TimePoint now)
{
	auto unexpected = [&](LoadState expected)
	{
		std::ostringstream os;
		os << "device answered load state \"" << state << "\", expected \"" << expected << "\"";
		fail(Error{ErrorCode::ProtocolError, stepError(os.str())}, now);
	};
	if (const auto* s = std::get_if<Unload>(&step))
	{
		if (state == LoadState::Unloaded || s->tolerateError)
		{
			advance(now);
		}
		else
		{
			unexpected(LoadState::Unloaded);
		}
	}
	else if (std::holds_alternative<Load>(step) || std::holds_alternative<AllocRelSegment>(step))
	{
		state == LoadState::Loading ? advance(now) : unexpected(LoadState::Loading);
	}
	else if (std::holds_alternative<LoadCompleted>(step))
	{
		if (state == LoadState::Loaded)
		{
			m_completingDeadline.reset();
			advance(now);
		}
		else if (state == LoadState::LoadCompleting)
		{
			if (!m_completingDeadline)
			{
				m_completingDeadline = now + m_config.loadCompletingTimeout;
			}
			if (now >= *m_completingDeadline)
			{
				unexpected(LoadState::Loaded);
				return;
			}
			m_wait = Wait::PollDelay;
			m_pollAt = now + m_config.pollInterval;
		}
		else
		{
			unexpected(LoadState::Loaded);
		}
	}
}

void Executor::onCompleted(Bytes data, knx::TimePoint now)
{
	const Step& step = m_plan.steps[m_index];
	switch (m_wait)
	{
	case Wait::LoadState:
	case Wait::PollLoadState:
		if (data.empty())
		{
			fail(Error{ErrorCode::ProtocolError, stepError("empty load state")}, now);
			return;
		}
		onLoadState(step, static_cast<LoadState>(data.front()), now);
		return;
	case Wait::TableReference:
	{
		if (data.empty() || data.size() > 4)
		{
			fail(Error{ErrorCode::ProtocolError, stepError("invalid table reference")}, now);
			return;
		}
		std::uint32_t address = 0;
		for (const std::uint8_t byte : data)
		{
			address = (address << 8) | byte;
		}
		m_tableReferences[std::get<WriteRelMem>(step).object] = address;
		startMemoryWrite(now);
		return;
	}
	case Wait::MemoryWrite:
	{
		const auto& s = std::get<WriteRelMem>(step);
		if (s.verify)
		{
			const auto address = static_cast<std::uint16_t>(m_tableReferences.at(s.object) + s.offset);
			m_wait = Wait::MemoryVerify;
			m_pending = m_session.memoryRead(address, s.data.size(), now);
			return;
		}
		[[fallthrough]];
	}
	case Wait::MemoryVerify:
	{
		const auto& s = std::get<WriteRelMem>(step);
		if (m_wait == Wait::MemoryVerify && data != s.data)
		{
			fail(Error{ErrorCode::ProtocolError, stepError("memory verification failed")}, now);
			return;
		}
		Bytes& segment = m_written[s.object];
		if (segment.size() < s.offset + s.data.size())
		{
			segment.resize(s.offset + s.data.size(), 0);
		}
		std::copy(s.data.begin(), s.data.end(), segment.begin() + static_cast<std::ptrdiff_t>(s.offset));
		advance(now);
		return;
	}
	case Wait::PropertyWrite:
	{
		const auto* write = std::get_if<WriteProp>(&step);
		const Bytes expected = write ? write->data : memoryControlBlock(m_written.at(std::get<LoadImageProp>(step).object));
		if ((!write || write->verify) && data != expected)
		{
			fail(Error{ErrorCode::ProtocolError, stepError("device answered [" + toHex(data) + "]")}, now);
			return;
		}
		advance(now);
		return;
	}
	case Wait::PropertyCompare:
		if (data != std::get<CompareProp>(step).data)
		{
			fail(Error{ErrorCode::Conflict, stepError("property is [" + toHex(data) + "]")}, now);
			return;
		}
		advance(now);
		return;
	case Wait::Authorize:
		if (data.size() != 1)
		{
			fail(Error{ErrorCode::ProtocolError, stepError("invalid authorize response")}, now);
			return;
		}
		m_accessLevel = data.front();
		advance(now);
		return;
	case Wait::Restart:
		advance(now);
		return;
	case Wait::None:
	case Wait::PollDelay:
		return;
	}
}

void Executor::pump(knx::TimePoint now)
{
	for (;;)
	{
		auto actions = m_session.takeActions();
		if (actions.empty())
		{
			return;
		}
		for (auto& action : actions)
		{
			if (auto* send = std::get_if<knx::ManagementSession::SendFrame>(&action))
			{
				m_actions.push_back(SendFrame{std::move(send->frame)});
			}
			else if (m_finished)
			{
				continue; // only the closing frames still matter
			}
			else if (auto* completed = std::get_if<knx::ManagementSession::Completed>(&action))
			{
				if (completed->id == m_pending)
				{
					onCompleted(std::move(completed->data), now);
				}
			}
			else if (auto* failed = std::get_if<knx::ManagementSession::Failed>(&action))
			{
				if (failed->id != m_pending)
				{
					continue;
				}
				const auto* unload = std::get_if<Unload>(&m_plan.steps[m_index]);
				if (unload && unload->tolerateError)
				{
					advance(now);
				}
				else
				{
					std::string message = failed->error.message;
					if (m_accessLevel && message.find("refused") != std::string::npos)
					{
						message += " (access level " + std::to_string(*m_accessLevel) + ")";
					}
					fail(Error{failed->error.code, stepError(message)}, now);
				}
			}
			else if (std::holds_alternative<knx::ManagementSession::Disconnected>(action))
			{
				fail(Error{ErrorCode::ProtocolError, stepError("the device closed the connection")}, now);
			}
		}
	}
}

void Executor::onFrame(const knx::cemi::LData& frame, knx::TimePoint now)
{
	m_session.onFrame(frame, now);
	pump(now);
}

void Executor::tick(knx::TimePoint now)
{
	m_session.tick(now);
	if (!m_finished && m_wait == Wait::PollDelay && m_pollAt && now >= *m_pollAt)
	{
		m_pollAt.reset();
		m_wait = Wait::PollLoadState;
		m_pending = m_session.propertyValueRead(
			std::get<LoadCompleted>(m_plan.steps[m_index]).lsm, kPidLoadStateControl, 1, 1, now);
	}
	pump(now);
}

std::vector<Executor::Action> Executor::takeActions()
{
	return std::exchange(m_actions, {});
}

std::ostream& operator<<(std::ostream& os, const Executor::Action& action)
{
	if (const auto* send = std::get_if<Executor::SendFrame>(&action))
	{
		return os << "SendFrame{" << send->frame << "}";
	}
	if (const auto* progress = std::get_if<Executor::Progress>(&action))
	{
		return os << "Progress{" << progress->step << "/" << progress->total << " " << progress->text << "}";
	}
	const auto& finished = std::get<Executor::Finished>(action);
	return os << "Finished{" << (finished.status ? std::string("ok") : finished.status.error().message) << "}";
}

} // namespace kestrel::download
