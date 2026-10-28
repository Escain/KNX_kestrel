#pragma once

// Simulated KNX device on a VirtualBus (Kestrel spec 04 §5). It answers the management services
// Kestrel uses (individual address read/write in programming mode, device descriptor, memory,
// properties, restart) connection-less or over a transport-layer connection, and the group
// services of its group objects according to their flags.
//
// DeviceConfig / GroupObjectConfig are plain structs so that a scenario loader (JSON, in the
// Qt-side kestrel-sim app) can fill them.

#include "kestrel/base/Bytes.hpp"
#include "kestrel/base/Result.hpp"
#include "kestrel/dpt/Dpt.hpp"
#include "kestrel/knx/Address.hpp"
#include "kestrel/knx/Apdu.hpp"
#include "kestrel/knx/Cemi.hpp"
#include "kestrel/knx/Clock.hpp"
#include "kestrel/knx/SerialNumber.hpp"
#include "kestrel/sim/VirtualBus.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kestrel::sim
{

/// ETS communication flags: C(ommunication) R(ead) W(rite) T(ransmit) U(pdate) I(nit read).
struct GroupObjectFlags
{
	bool communication = true;
	bool read = false;
	bool write = true;
	bool transmit = true;
	bool update = false;
	bool readOnInit = false;

	/// Letters in any order, case-insensitive: "CWTU". Unknown letters → ParseError.
	static Result<GroupObjectFlags> parse(std::string_view text);
	/// Canonical order "CRWTUI" (only the set flags).
	std::string toString() const;

	friend bool operator==(const GroupObjectFlags&, const GroupObjectFlags&) = default;
};

struct GroupObjectConfig
{
	knx::GroupAddress address;
	GroupObjectFlags flags;
	/// Decides short (≤ 6 bit, in the APCI octet) versus long form on the wire. When unknown
	/// (main == 0) the form of the last received write is mirrored; initially a single byte
	/// ≤ 0x3F is treated as short.
	dpt::DptId dpt;
	Bytes value;

	friend bool operator==(const GroupObjectConfig&, const GroupObjectConfig&) = default;
};

struct DeviceConfig
{
	std::string name;
	knx::IndividualAddress address{0xFFFF}; ///< 15.15.255, the factory default
	knx::SerialNumber serial;
	std::uint16_t maskVersion = 0x0705; ///< System 7 (BCU 2 / BIM M112)
	bool progMode = false;
	std::vector<GroupObjectConfig> groupObjects;

	friend bool operator==(const DeviceConfig&, const DeviceConfig&) = default;
};

std::ostream& operator<<(std::ostream& os, const GroupObjectFlags& flags);
std::ostream& operator<<(std::ostream& os, const GroupObjectConfig& config);
std::ostream& operator<<(std::ostream& os, const DeviceConfig& config);

/// Property ids of the device object (object index 0) that the device answers by default.
namespace pid
{
constexpr std::uint8_t kObjectType = 1;
constexpr std::uint8_t kSerialNumber = 11;
constexpr std::uint8_t kManufacturerId = 12;
constexpr std::uint8_t kLoadStateControl = 5;
constexpr std::uint8_t kTableReference = 7;
constexpr std::uint8_t kProgramVersion = 13;
constexpr std::uint8_t kMcbTable = 27;
} // namespace pid

/// System B (mask 07B0) interface objects: (object index, object type) and the segment address the
/// simulated device allocates for each.
inline constexpr std::pair<std::uint8_t, std::uint16_t> kSystemBObjects[] = {{1, 1}, {2, 2}, {3, 9}, {4, 3}, {5, 4}};
inline constexpr std::uint16_t kSystemBSegments[] = {0x1002, 0x1804, 0x2806, 0x2A18, 0x4000};

class VirtualDevice
{
public:
	struct Config
	{
		/// Transport connections are closed after this much silence (3/3/4: 6 s).
		knx::Duration connectionTimeout = std::chrono::seconds(6);
	};

	VirtualDevice(VirtualBus& bus, DeviceConfig config);
	VirtualDevice(VirtualBus& bus, DeviceConfig config, Config timing);
	~VirtualDevice();
	VirtualDevice(const VirtualDevice&) = delete;
	VirtualDevice& operator=(const VirtualDevice&) = delete;

	const std::string& name() const { return m_config.name; }
	knx::IndividualAddress address() const { return m_config.address; }
	const knx::SerialNumber& serial() const { return m_config.serial; }
	std::uint16_t maskVersion() const { return m_config.maskVersion; }
	EndpointId endpoint() const { return m_endpoint; }

	bool progMode() const { return m_config.progMode; }
	void setProgMode(bool on) { m_config.progMode = on; }
	/// Toggles programming mode, like the physical button.
	void pressProgButton() { m_config.progMode = !m_config.progMode; }

	/// Unwritten memory reads as 0x00.
	Bytes readMemory(std::uint16_t address, std::size_t length) const;
	void writeMemory(std::uint16_t address, ByteView data);

	void setProperty(std::uint8_t objectIndex, std::uint8_t propertyId, Bytes value);
	std::optional<Bytes> property(std::uint8_t objectIndex, std::uint8_t propertyId) const;
	/// System B (mask 07B0) only: state of the load state machine of an interface object (1..5).
	std::optional<std::uint8_t> loadState(std::uint8_t objectIndex) const;

	const std::vector<GroupObjectConfig>& groupObjects() const { return m_config.groupObjects; }
	/// Value of the first group object bound to `address`.
	std::optional<Bytes> groupValue(knx::GroupAddress address) const;
	/// The application changes a value: stores it and, with the T flag, sends A_GroupValue_Write.
	Status setGroupValue(knx::GroupAddress address, ByteView value);

	/// Drives the transport connection timeout.
	void tick(knx::TimePoint now);

	std::uint32_t restartCount() const { return m_restarts; }
	/// Partner of the open transport connection, if any.
	std::optional<knx::IndividualAddress> connectionPartner() const { return m_partner; }

private:
	void onFrame(const knx::cemi::LData& frame);
	void onGroup(knx::GroupAddress address, const knx::Apdu& apdu);
	void onBroadcast(const knx::Apdu& apdu);
	void onTransport(knx::IndividualAddress source, const knx::Tpdu& tpdu);
	/// Executes a point-to-point service; returns the response APDU, if any.
	std::optional<knx::Apdu> serve(const knx::Apdu& apdu);
	/// PID_LOAD_STATE_CONTROL write on a System B interface object; returns the new state.
	std::uint8_t onLoadControl(std::uint8_t objectIndex, ByteView data);
	void sendGroup(knx::GroupAddress address, knx::Apci apci, std::size_t objectIndex);
	void sendTo(knx::IndividualAddress destination, const knx::Tpdu& tpdu);
	void sendBroadcast(const knx::Apdu& apdu);
	void doRestart();
	bool isShort(std::size_t objectIndex) const;

	VirtualBus& m_bus;
	DeviceConfig m_config;
	Config m_timing;
	EndpointId m_endpoint = 0;
	std::map<std::uint16_t, std::uint8_t> m_memory;
	std::map<std::pair<std::uint8_t, std::uint8_t>, Bytes> m_properties;
	std::vector<bool> m_shortForm;
	std::uint32_t m_restarts = 0;

	std::optional<knx::IndividualAddress> m_partner;
	std::uint8_t m_sendSeq = 0;
	std::uint8_t m_recvSeq = 0;
	bool m_trafficSinceTick = false;
	std::optional<knx::TimePoint> m_lastActivity;
	bool m_restartPending = false;
};

} // namespace kestrel::sim
