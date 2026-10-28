#pragma once

// Simulator scenario files (Kestrel spec 04 §5):
//
//   { "interfaceAddress": "1.1.255",                       optional, default 1.1.255
//     "tunnelAddresses": ["1.1.250", "1.1.251"],           optional, default [interfaceAddress]
//     "friendlyName": "Kestrel simulator",                 optional
//     "serial": "FFFF:00000001",                           optional, the interface's serial
//     "devices": [ { "name": "switch-a", "address": "15.15.255", "serial": "FFFF:00001234",
//                    "progMode": false, "maskVersion": "0705",                 (optional, hex)
//                    "memory": [ { "address": "0x4000", "data": "01 02" } ],  (optional)
//                    "groupObjects": [ { "ga": "1/1/1", "flags": "CWTU", "dpt": "1.001", "value": "00" } ] } ] }
//
// "value" and "data" are hex; "dpt" and "value" are optional. Unknown keys are ignored. Device names
// must be unique and non-empty.

#include "kestrel/base/Bytes.hpp"
#include "kestrel/base/Result.hpp"
#include "kestrel/knx/Address.hpp"
#include "kestrel/knx/SerialNumber.hpp"
#include "kestrel/sim/TunnelServer.hpp"
#include "kestrel/sim/VirtualDevice.hpp"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <string>
#include <vector>

namespace kestrel::simnet
{

struct MemoryBlock
{
	std::uint16_t address = 0;
	Bytes data;

	friend bool operator==(const MemoryBlock&, const MemoryBlock&) = default;
};

struct ScenarioDevice
{
	sim::DeviceConfig config;
	std::vector<MemoryBlock> memory;

	friend bool operator==(const ScenarioDevice&, const ScenarioDevice&) = default;
};

struct Scenario
{
	knx::IndividualAddress interfaceAddress{0x11FF};
	std::vector<knx::IndividualAddress> tunnelAddresses; ///< empty = {interfaceAddress}
	std::string friendlyName = "Kestrel simulator";
	knx::SerialNumber serial;
	std::vector<ScenarioDevice> devices;

	/// TunnelServer configuration for this scenario (device info + tunnel slots).
	sim::TunnelServerConfig serverConfig() const;

	friend bool operator==(const Scenario&, const Scenario&) = default;
};

class ScenarioLoader
{
public:
	/// Errors are ParseError / InvalidArgument with a JSON path in the message ("devices[1].address: …").
	static Result<Scenario> fromJson(const QByteArray& json);
	/// IoError when the file cannot be read.
	static Result<Scenario> fromFile(const QString& path);
};

} // namespace kestrel::simnet
