#pragma once

// Memory images of an application program, built as ETS6 builds them (checked byte for byte against a real
// download, specs/09):
// - Parameter segments: the segment's <Data>, then every parameter with memory at its own default value,
//   then every *active* ParameterRef at its value.
// - System B group object table: 16-bit count, then one 2-octet descriptor per object number 1..count:
//   config octet (U 0x80, T 0x40, I 0x20, W 0x10, R 0x08, C 0x04, priority 0..3 with low = 3) and type octet
//   (size code). Inactive objects are 00 00. C is only set when the object has at least one group address.
// - Address table: 16-bit count, then the group addresses in ascending order.
// - Association table: 16-bit count, then (address table index from 1, object number) pairs. Sending links
//   come first, ordered by object number; the other links follow, ordered by object number and index.

#include "kestrel/appconfig/Evaluator.hpp"
#include "kestrel/base/Bytes.hpp"

#include <map>
#include <vector>

namespace kestrel::appconfig
{

/// Object number → group addresses (raw 16-bit), the sending one first.
using Links = std::map<unsigned, std::vector<std::uint16_t>>;

// --- parameter codec ---------------------------------------------------------------------------------------

/// Writes `value` (canonical text) into `segment` at `location`, MSB-first bit order.
Status writeParameter(Bytes& segment, const catalog::ParameterType& type, const catalog::MemoryLocation& location,
	const QString& value);
/// Reads the canonical text of a parameter value back from `segment`.
Result<QString> readParameter(const Bytes& segment, const catalog::ParameterType& type,
	const catalog::MemoryLocation& location);
/// KNX 2-octet float (DPT 9): 0.01 · M · 2^E with the smallest exponent, mantissa rounded.
std::uint16_t encodeDpt9(double value);
double decodeDpt9(std::uint16_t raw);

// --- images --------------------------------------------------------------------------------------------------

/// Parameter memory per code segment id (relative and absolute segments).
Result<std::map<QString, Bytes>> buildParameterSegments(const catalog::ApplicationProgram& app,
	const ParameterValues& values, const Evaluation& evaluation, QStringList& warnings);

/// The inverse: the values of the active parameters, read from segment images (fixpoint over visibility).
/// Only values that differ from the default are returned.
Result<ParameterValues> decodeParameterSegments(const catalog::ApplicationProgram& app,
	const std::map<QString, Bytes>& segments, QStringList& warnings);

Result<Bytes> buildGroupObjectTableSystemB(const catalog::ApplicationProgram& app, const Evaluation& evaluation,
	const Links& links);
Bytes buildAddressTable(const std::vector<std::uint16_t>& addresses);
/// `links` must only contain active objects; the address table must contain all their addresses.
Bytes buildAssociationTable(const Links& links, const std::vector<std::uint16_t>& addressTable);

struct SystemBImages
{
	/// Interface object index → segment image (1 address table, 2 association table, 3 group object table,
	/// and the parameter segment under its LoadStateMachine index, normally 4).
	std::map<std::uint8_t, Bytes> images;
	/// Links of inactive objects are dropped and reported here, together with codec warnings.
	QStringList warnings;
};

Result<SystemBImages> buildSystemB(const catalog::ApplicationProgram& app, const ParameterValues& values,
	const Links& links);

/// PID_PROGRAM_VERSION of an application: manufacturer (from "M-XXXX"), application number, version.
Bytes programVersion(const QString& manufacturerId, const catalog::ApplicationProgram& app);

} // namespace kestrel::appconfig
