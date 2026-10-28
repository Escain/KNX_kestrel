#pragma once

// Sans-IO "program individual address" procedure (FR-COM-1, Kestrel spec 04 §3; KNX 3/5/2
// "Network management procedures", NM_IndividualAddress_Write).
//
//   start → WaitProgMode: broadcast A_IndividualAddress_Read every readInterval and count the
//           A_IndividualAddress_Response frames of each window (frames, not distinct addresses: two
//           factory-new devices both answer from 15.15.255). 0 → keep waiting (until
//           progModeTimeout); ≥ 2 → Failed;
//           1 → CheckAddress (skipped when the device already has `target` or allowOverwrite is set)
//   CheckAddress: T_Connect + A_DeviceDescriptor_Read to `target`. Any frame from `target` within
//           addressCheckTimeout → T_Disconnect, Failed(AddressInUse). Silence → T_Disconnect,
//           broadcast A_IndividualAddress_Write(target) → Verify
//   Verify: A_IndividualAddress_Read per window; exactly `target` must answer. Other answers (a late
//           one from the old address, a second device) only fail once verifyAttempts windows are used
//           up, or earlier when the same unexpected third address answers in two consecutive windows.
//           Success → connection-less A_Restart to target (ends programming mode) → Done
//
// The caller forwards every received L_Data.ind to onFrame() and sends the SendFrame actions on its
// IBusLink / TunnelClient.

#include "kestrel/knx/Address.hpp"
#include "kestrel/knx/Cemi.hpp"
#include "kestrel/knx/Clock.hpp"

#include <chrono>
#include <ostream>
#include <set>
#include <variant>
#include <vector>

namespace kestrel::knx
{

struct IaProgrammerConfig
{
	Duration readInterval = std::chrono::seconds(1);
	Duration progModeTimeout = std::chrono::seconds(60);
	int verifyAttempts = 3;
	/// Skip the "is the target address free?" check and program over an existing device.
	bool allowOverwrite = false;
	/// How long a device at the target address has to answer T_Connect + A_DeviceDescriptor_Read.
	Duration addressCheckTimeout = std::chrono::seconds(3);
};

enum class IaProgrammerState
{
	Idle,
	WaitProgMode,
	CheckAddress,
	Verify,
	Done,
	Failed,
};

enum class IaProgrammerFailure
{
	None,
	Timeout,                   ///< no device in programming mode within progModeTimeout
	MultipleDevicesInProgMode, ///< ≥ 2 responders: refusing to program both
	VerifyFailed,              ///< the read-back did not return exactly the target address
	Cancelled,
	AddressInUse, ///< another device already answers at the target address
};

std::ostream& operator<<(std::ostream& os, IaProgrammerState state);
std::ostream& operator<<(std::ostream& os, IaProgrammerFailure failure);

class IaProgrammer
{
public:
	struct SendFrame
	{
		cemi::LData frame;
		friend bool operator==(const SendFrame&, const SendFrame&) = default;
	};
	struct StateChanged
	{
		IaProgrammerState state = IaProgrammerState::Idle;
		friend bool operator==(const StateChanged&, const StateChanged&) = default;
	};
	/// Terminal: failure == None means success.
	struct Finished
	{
		IaProgrammerFailure failure = IaProgrammerFailure::None;
		/// Address the device had while in programming mode (when one was found).
		IndividualAddress previousAddress;
		friend bool operator==(const Finished&, const Finished&) = default;
	};

	using Action = std::variant<SendFrame, StateChanged, Finished>;

	explicit IaProgrammer(IaProgrammerConfig config = {});

	/// Starts programming `target`. Restarts the procedure when already running.
	void start(IndividualAddress target, TimePoint now);
	void cancel(TimePoint now);
	void onFrame(const cemi::LData& frame, TimePoint now);
	void tick(TimePoint now);
	std::vector<Action> takeActions();

	IaProgrammerState state() const { return m_state; }
	IndividualAddress target() const { return m_target; }
	/// Responders collected in the last completed window.
	const std::set<IndividualAddress>& responders() const { return m_lastResponders; }
	bool finished() const { return m_state == IaProgrammerState::Done || m_state == IaProgrammerState::Failed; }

private:
	void setState(IaProgrammerState state);
	void sendRead(TimePoint now);
	void finish(IaProgrammerFailure failure);
	void closeWindow(TimePoint now);
	void closeVerifyWindow(TimePoint now);
	void writeAndVerify(TimePoint now);
	bool running() const;

	IaProgrammerConfig m_config;
	IaProgrammerState m_state = IaProgrammerState::Idle;
	IndividualAddress m_target;
	IndividualAddress m_previous;
	TimePoint m_deadline{};
	TimePoint m_windowEnd{};
	int m_verifyAttempt = 0;
	std::set<IndividualAddress> m_responders;
	std::size_t m_responseCount = 0; ///< responses in the current window, duplicates included
	std::set<IndividualAddress> m_lastResponders;
	/// Unexpected (neither target nor previous) responders of the previous verify window.
	std::set<IndividualAddress> m_lastForeign;
	std::vector<Action> m_actions;
};

std::ostream& operator<<(std::ostream& os, const IaProgrammer::SendFrame& action);
std::ostream& operator<<(std::ostream& os, const IaProgrammer::StateChanged& action);
std::ostream& operator<<(std::ostream& os, const IaProgrammer::Finished& action);
std::ostream& operator<<(std::ostream& os, const IaProgrammer::Action& action);

} // namespace kestrel::knx
