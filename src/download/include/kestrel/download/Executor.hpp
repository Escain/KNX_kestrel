#pragma once

// DownloadExecutor (ADR-008): runs a download::Plan against one device over a ManagementSession. Sans-IO
// like the rest of kestrel::knx: frames in via onFrame(), time via tick(), actions out via takeActions().

#include "kestrel/download/Plan.hpp"
#include "kestrel/knx/ManagementSession.hpp"

#include <map>
#include <optional>
#include <string>

namespace kestrel::download
{

struct ExecutorConfig
{
	knx::ManagementSessionConfig session{
		.ackTimeout = std::chrono::seconds(3),
		.maxRepetitions = 3,
		// Allocation and flash writes can take a while on a real device.
		.responseTimeout = std::chrono::seconds(10),
		.idleTimeout = std::chrono::seconds(20),
		.maxMemoryChunk = 12,
	};
	/// How long LoadCompleted may stay in "load completing" before the download fails.
	knx::Duration loadCompletingTimeout = std::chrono::seconds(10);
	knx::Duration pollInterval = std::chrono::milliseconds(500);
};

class Executor
{
public:
	struct SendFrame
	{
		knx::cemi::LData frame;
		friend bool operator==(const SendFrame&, const SendFrame&) = default;
	};
	/// About to run steps[step] (0-based) of `total`.
	struct Progress
	{
		std::size_t step = 0;
		std::size_t total = 0;
		std::string text;
		friend bool operator==(const Progress&, const Progress&) = default;
	};
	struct Finished
	{
		Status status;
		friend bool operator==(const Finished&, const Finished&) = default;
	};
	using Action = std::variant<SendFrame, Progress, Finished>;

	Executor(knx::IndividualAddress device, Plan plan, ExecutorConfig config = {});

	void setLocalAddress(knx::IndividualAddress address) { m_session.setLocalAddress(address); }
	void start(knx::TimePoint now);
	/// Stops after the current request; Finished carries Cancelled.
	void cancel(knx::TimePoint now);

	void onFrame(const knx::cemi::LData& frame, knx::TimePoint now);
	void tick(knx::TimePoint now);
	std::vector<Action> takeActions();

	bool finished() const { return m_finished; }
	std::size_t currentStep() const { return m_index; }
	/// Access level granted by A_Authorize (0 = highest), once authorized.
	std::optional<std::uint8_t> accessLevel() const { return m_accessLevel; }

private:
	enum class Wait
	{
		None,
		Authorize,
		LoadState,
		TableReference,
		MemoryWrite,
		MemoryVerify,
		PropertyWrite,
		PropertyCompare,
		PollLoadState,
		Restart,
		PollDelay,
	};

	void runStep(knx::TimePoint now);
	void advance(knx::TimePoint now);
	void fail(Error error, knx::TimePoint now);
	void pump(knx::TimePoint now);
	void onCompleted(Bytes data, knx::TimePoint now);
	void onLoadState(const Step& step, LoadState state, knx::TimePoint now);
	void writeLoadControl(std::uint8_t lsm, Bytes payload, knx::TimePoint now);
	void startMemoryWrite(knx::TimePoint now);
	std::string stepError(const std::string& what) const;

	knx::ManagementSession m_session;
	Plan m_plan;
	ExecutorConfig m_config;
	std::vector<Action> m_actions;
	std::size_t m_index = 0;
	Wait m_wait = Wait::None;
	knx::ManagementSession::RequestId m_pending = 0;
	std::optional<knx::TimePoint> m_completingDeadline;
	std::optional<knx::TimePoint> m_pollAt;
	/// PID_TABLE_REFERENCE per object, read once.
	std::map<std::uint8_t, std::uint32_t> m_tableReferences;
	/// Segment contents written so far per object (for the memory control block).
	std::map<std::uint8_t, Bytes> m_written;
	std::optional<std::uint8_t> m_accessLevel;
	bool m_started = false;
	bool m_finished = false;
};

std::ostream& operator<<(std::ostream& os, const Executor::Action& action);

} // namespace kestrel::download
