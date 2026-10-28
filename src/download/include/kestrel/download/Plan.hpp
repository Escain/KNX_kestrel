#pragma once

// A resolved load procedure: the concrete management steps that download an application to a device
// (ADR-008). Plans are produced by download::resolve() and run by download::Executor.
//
// System B (mask 07B0) conventions, verified on a real device (specs/04 field notes):
// - The load state machine index (LsmIdx) is the interface object index (1 address table,
//   2 association table, 3 group object table, 4 application program, 5 interface program).
// - Load state machine events are written to PID_LOAD_STATE_CONTROL (5); the answer carries the new state.
// - A relative segment's memory address is PID_TABLE_REFERENCE (7) once it is allocated.
// - PID_MCB_TABLE (27) holds one memory control block per segment: size (4 octets, big endian), 0x00, 0xFF,
//   CRC-16/AUG-CCITT of the segment (2 octets).

#include "kestrel/base/Bytes.hpp"

#include <cstdint>
#include <ostream>
#include <variant>
#include <vector>

namespace kestrel::download
{

/// Interface object properties used by load procedures.
inline constexpr std::uint8_t kPidLoadStateControl = 5;
inline constexpr std::uint8_t kPidTableReference = 7;
inline constexpr std::uint8_t kPidProgramVersion = 13;
inline constexpr std::uint8_t kPidMcbTable = 27;

/// Load states reported by PID_LOAD_STATE_CONTROL.
enum class LoadState : std::uint8_t
{
	Unloaded = 0,
	Loaded = 1,
	Loading = 2,
	Error = 3,
	Unloading = 4,
	LoadCompleting = 5,
};

std::ostream& operator<<(std::ostream& os, LoadState state);

/// Load state machine events (first octet written to PID_LOAD_STATE_CONTROL).
enum class LoadEvent : std::uint8_t
{
	StartLoading = 1,
	LoadCompleted = 2,
	AdditionalLoadControl = 3,
	Unload = 4,
};

/// A_Authorize_Request after connecting (LdCtrlConnect); devices refuse writes at the default access level.
struct Authorize
{
	std::uint32_t key = 0xFFFFFFFF;
	friend bool operator==(const Authorize&, const Authorize&) = default;
};

struct Unload
{
	std::uint8_t lsm = 0;
	/// The procedure maps this step's errors to success (LdCtrlMapError), e.g. an absent interface program.
	bool tolerateError = false;
	friend bool operator==(const Unload&, const Unload&) = default;
};

struct Load
{
	std::uint8_t lsm = 0;
	friend bool operator==(const Load&, const Load&) = default;
};

struct LoadCompleted
{
	std::uint8_t lsm = 0;
	friend bool operator==(const LoadCompleted&, const LoadCompleted&) = default;
};

/// Additional load control "data relative allocation" (type 0x0B).
struct AllocRelSegment
{
	std::uint8_t lsm = 0;
	std::uint32_t size = 0;
	std::uint8_t mode = 0;
	std::uint8_t fill = 0;
	friend bool operator==(const AllocRelSegment&, const AllocRelSegment&) = default;
};

/// Writes `data` at `offset` into the relative segment of `object` (address from PID_TABLE_REFERENCE).
struct WriteRelMem
{
	std::uint8_t object = 0;
	std::uint32_t offset = 0;
	Bytes data;
	bool verify = true;
	friend bool operator==(const WriteRelMem&, const WriteRelMem&) = default;
};

struct WriteProp
{
	std::uint8_t object = 0;
	std::uint8_t pid = 0;
	Bytes data;
	bool verify = true;
	friend bool operator==(const WriteProp&, const WriteProp&) = default;
};

/// Reads a property and fails the download unless it equals `data`.
struct CompareProp
{
	std::uint8_t object = 0;
	std::uint8_t pid = 0;
	Bytes data;
	friend bool operator==(const CompareProp&, const CompareProp&) = default;
};

/// Writes the memory control block of the segment written to `object` (PID_MCB_TABLE).
struct LoadImageProp
{
	std::uint8_t object = 0;
	std::uint8_t pid = kPidMcbTable;
	friend bool operator==(const LoadImageProp&, const LoadImageProp&) = default;
};

struct Restart
{
	friend bool operator==(const Restart&, const Restart&) = default;
};

using Step = std::variant<Authorize, Unload, Load, LoadCompleted, AllocRelSegment, WriteRelMem, WriteProp, CompareProp,
	LoadImageProp, Restart>;

std::ostream& operator<<(std::ostream& os, const Step& step);

struct Plan
{
	std::vector<Step> steps;
	friend bool operator==(const Plan&, const Plan&) = default;
};

/// PID_LOAD_STATE_CONTROL payload for a step that drives the load state machine (always 10 octets).
Bytes loadControlPayload(LoadEvent event);
Bytes allocRelSegmentPayload(const AllocRelSegment& step);

/// CRC-16/AUG-CCITT (poly 0x1021, init 0x1D0F, no reflection, no final xor).
std::uint16_t crc16AugCcitt(ByteView data);
/// Memory control block of a segment image: size, 0x00, 0xFF, CRC.
Bytes memoryControlBlock(ByteView image);

} // namespace kestrel::download
