#pragma once

// Bus-thread half of app::BusSession (private). Everything here runs on BusSession::m_thread.

#include "kestrel/app/BusSession.hpp"
#include "kestrel/bus/UdpTunnelLink.hpp"
#include "kestrel/knx/ManagementSession.hpp"

#include <QObject>
#include <QTimer>

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace kestrel::app::detail
{

/// State shared between the session thread and the bus thread (monitor buffer, DPT hints).
struct SharedState
{
	mutable std::mutex mutex;
	std::deque<TelegramRecord> records;
	std::size_t capacity = 10000;
	std::uint64_t dropped = 0;
	bool notifyPending = false;
	std::map<std::uint16_t, dpt::DptId> dpts;
};

class BusWorker : public QObject
{
public:
	BusWorker(BusSession* session, std::shared_ptr<SharedState> shared);
	~BusWorker() override;

	void connectTo(const QString& host, quint16 port, BusSession::StatusCallback done);
	void disconnect();
	void groupWrite(knx::GroupAddress address, dpt::DptId dpt, const dpt::DptValue& value,
		BusSession::StatusCallback done);
	void groupRead(knx::GroupAddress address, dpt::DptId dpt, std::chrono::milliseconds timeout,
		BusSession::GroupReadCallback done);
	void program(knx::IndividualAddress target, const BusSession::ProgramOptions& options,
		BusSession::ProgramCallback done, BusSession::ProgressCallback progress);
	void cancelProgramming();
	void readDeviceInfo(knx::IndividualAddress device, BusSession::DeviceInfoCallback done);
	void discover(const BusSession::DiscoveryOptions& options, BusSession::DiscoveryCallback done);

private:
	struct PendingConfirm
	{
		knx::cemi::LData frame;
		std::function<void(bool)> done;
	};
	struct GroupReadOp
	{
		knx::GroupAddress address;
		dpt::DptId dpt;
		knx::TimePoint deadline;
		BusSession::GroupReadCallback done;
	};
	struct InfoOp
	{
		std::unique_ptr<knx::ManagementSession> session;
		knx::ManagementSession::RequestId descriptorId = 0;
		knx::ManagementSession::RequestId serialId = 0;
		knx::ManagementSession::RequestId manufacturerId = 0;
		int remaining = 3;
		DeviceInfo info;
		BusSession::DeviceInfoCallback done;
		bool finished = false;
	};

	/// Runs `fn` on the session's thread (dropped if the session is gone).
	void toSession(std::function<void()> fn);
	template <class Callback, class Value>
	void deliver(const Callback& callback, Value value)
	{
		if (callback)
		{
			toSession([callback, value = std::move(value)]() mutable { callback(std::move(value)); });
		}
	}

	void onLinkState(knx::LinkState state);
	void onLinkFrame(const knx::cemi::LData& frame);
	void onLinkConfirm(bool ok);
	void onTick();
	void ensureTimer();
	Status sendFrame(const knx::cemi::LData& frame, std::function<void(bool)> confirmed = {});
	void record(const knx::cemi::LData& frame, bool outgoing);

	void pumpProgrammer();
	void pumpInfo(InfoOp& op);
	void finishInfo(InfoOp& op, Result<DeviceInfo> result);
	void failAll(const Error& error);

	BusSession* m_session;
	std::shared_ptr<SharedState> m_shared;
	bus::UdpTunnelLink* m_link;
	QTimer* m_timer;

	BusSession::StatusCallback m_connectDone;
	std::deque<PendingConfirm> m_pending;
	std::vector<GroupReadOp> m_reads;

	std::unique_ptr<knx::IaProgrammer> m_programmer;
	BusSession::ProgramCallback m_programDone;
	BusSession::ProgressCallback m_programProgress;
	std::chrono::milliseconds m_programTimeout{0};

	std::vector<std::unique_ptr<InfoOp>> m_infos;
};

} // namespace kestrel::app::detail
