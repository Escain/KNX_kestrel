#pragma once

// Helpers for the DPT unit tests. They turn Results into plain comparable values so that
// ASSERT_EQ prints something useful on failure (asserts must stay in the UNIT_TEST bodies).

#include "kestrel/base/Bytes.hpp"
#include "kestrel/dpt/Dpt.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace dpttest
{

using kestrel::dpt::DptId;
using kestrel::dpt::DptValue;

/// Parsed id, or main 0 (unsupported) when the text is malformed.
inline DptId dptId(std::string_view text)
{
	auto id = DptId::parse(text);
	return id ? *id : DptId{};
}

inline DptValue i64(std::int64_t value)
{
	return DptValue{value};
}

inline DptValue u64(std::uint64_t value)
{
	return DptValue{value};
}

inline DptValue str(std::string_view value)
{
	return DptValue{std::string(value)};
}

template <typename T>
std::string errorName(const kestrel::Result<T>& result)
{
	return result ? std::string("ok") : std::string(kestrel::toString(result.error().code));
}

/// Encoded bytes as lowercase hex ("0c 33"), or "error:<Code>".
inline std::string enc(std::string_view id, const DptValue& value)
{
	auto bytes = kestrel::dpt::encode(dptId(id), value);
	return bytes ? kestrel::toHex(*bytes) : "error:" + errorName(bytes);
}

/// Decoded value, or the text "error:<Code>" in place of the value.
inline DptValue dec(std::string_view id, std::string_view hex)
{
	auto bytes = kestrel::fromHex(hex);
	if (!bytes)
	{
		return str("error:bad hex in test");
	}
	auto value = kestrel::dpt::decode(dptId(id), *bytes);
	return value ? *value : str("error:" + errorName(value));
}

/// Parsed value, or the text "error:<Code>".
inline DptValue prs(std::string_view id, std::string_view text)
{
	auto value = kestrel::dpt::parse(dptId(id), text);
	return value ? *value : str("error:" + errorName(value));
}

inline std::string encErr(std::string_view id, const DptValue& value)
{
	return errorName(kestrel::dpt::encode(dptId(id), value));
}

inline std::string decErr(std::string_view id, std::string_view hex)
{
	auto bytes = kestrel::fromHex(hex);
	if (!bytes)
	{
		return "bad hex in test";
	}
	return errorName(kestrel::dpt::decode(dptId(id), *bytes));
}

inline std::string prsErr(std::string_view id, std::string_view text)
{
	return errorName(kestrel::dpt::parse(dptId(id), text));
}

inline std::string fmt(std::string_view id, const DptValue& value)
{
	return kestrel::dpt::format(dptId(id), value);
}

} // namespace dpttest
