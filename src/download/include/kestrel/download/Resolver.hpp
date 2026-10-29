#pragma once

// ProcedureResolver (ADR-008): mask procedure (knx_master MaskVersion/Procedures/Procedure) + application
// LoadProcedures (merged by MergeId) + memory images → download::Plan.

#include "kestrel/base/Result.hpp"
#include "kestrel/download/Plan.hpp"

#include <map>
#include <string>
#include <vector>

namespace kestrel::download
{

/// One load-control element as it appears in the XML (e.g. "LdCtrlRelSegment" with its attributes).
struct LdCtrl
{
	std::string name;
	std::map<std::string, std::string> attributes;
	friend bool operator==(const LdCtrl&, const LdCtrl&) = default;
};

struct ResolveInput
{
	/// The mask's load procedure for the chosen subtype (e.g. System B "ap1" or "all").
	std::vector<LdCtrl> procedure;
	/// The application's LoadProcedure elements, keyed by MergeId.
	std::map<int, std::vector<LdCtrl>> merges;
	/// Download kind matched against AppliesTo ("full", "par", ...). Elements without AppliesTo always apply.
	std::string appliesTo = "full";
	/// Segment image per interface object (1 address table, 2 association table, 3 group object table,
	/// 4 application parameters, ...).
	std::map<std::uint8_t, Bytes> images;
	/// Value written for PID_PROGRAM_VERSION per object, replacing the procedure's placeholder InlineData.
	std::map<std::uint8_t, Bytes> programVersions;
	/// System 2: image per absolute segment address, written right after the segment is allocated. Only
	/// EEPROM segments (MemType 3) are written; RAM segments are only allocated.
	std::map<std::uint16_t, Bytes> absoluteImages;
	/// System 2: the segment's <Mask> per address; only octets with a non-zero mask are written (in runs).
	std::map<std::uint16_t, Bytes> absoluteMasks;
	/// System 2: application id of the task segments (manufacturer(2), number(2), version(1)) and the octet
	/// before it (the program's PEI type).
	Bytes applicationId;
	std::uint8_t peiType = 0;
};

/// Errors: Unsupported (unknown LdCtrl element), InvalidArgument (missing image, bad attribute).
Result<Plan> resolve(const ResolveInput& input);

} // namespace kestrel::download
