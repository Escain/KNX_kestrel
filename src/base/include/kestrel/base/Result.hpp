#pragma once

// Minimal Result<T> for expected failures (C++20 has no std::expected).
// The API mirrors the subset of std::expected we use so that moving to
// C++23 is a mechanical change: has_value(), operator bool, value(),
// error(), operator*, operator->, value_or().
//
// Contract violations (programmer errors) are not Results: use assert.

#include <cassert>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace kestrel
{

enum class ErrorCode
{
	InvalidArgument,  ///< Caller-supplied value out of range / malformed text
	ParseError,       ///< Malformed bytes, XML or JSON
	Unsupported,      ///< Valid input we do not handle (yet)
	NotFound,         ///< Referenced entity / file missing
	Conflict,         ///< Uniqueness or consistency violation
	IoError,          ///< File system or socket failure
	Timeout,          ///< Bus / protocol timeout
	ProtocolError,    ///< Peer answered with an error or unexpected frame
	Cancelled,        ///< Operation cancelled by the user
};

const char* toString(ErrorCode code);

struct Error
{
	ErrorCode code = ErrorCode::InvalidArgument;
	std::string message;

	friend bool operator==(const Error&, const Error&) = default;
};

std::ostream& operator<<(std::ostream& os, ErrorCode code);
std::ostream& operator<<(std::ostream& os, const Error& error);

/// Tag wrapper so that Result<Error> or Result<std::string> stay unambiguous.
struct Unexpected
{
	Error error;
};

inline Unexpected makeError(ErrorCode code, std::string message)
{
	return Unexpected{Error{code, std::move(message)}};
}

template <typename T>
class [[nodiscard]] Result
{
	std::variant<T, Error> m_data;

public:
	using value_type = T;

	Result(const T& value) : m_data(std::in_place_index<0>, value) {}
	Result(T&& value) : m_data(std::in_place_index<0>, std::move(value)) {}
	Result(Unexpected unexpected) : m_data(std::in_place_index<1>, std::move(unexpected.error)) {}

	template <typename U>
		requires(std::is_constructible_v<T, U&&> && !std::is_same_v<std::remove_cvref_t<U>, T>
				 && !std::is_same_v<std::remove_cvref_t<U>, Unexpected>
				 && !std::is_same_v<std::remove_cvref_t<U>, Result>)
	Result(U&& value) : m_data(std::in_place_index<0>, T(std::forward<U>(value)))
	{
	}

	bool has_value() const noexcept { return m_data.index() == 0; }
	explicit operator bool() const noexcept { return has_value(); }

	T& value() &
	{
		assert(has_value());
		return std::get<0>(m_data);
	}
	const T& value() const&
	{
		assert(has_value());
		return std::get<0>(m_data);
	}
	T&& value() &&
	{
		assert(has_value());
		return std::get<0>(std::move(m_data));
	}

	const Error& error() const
	{
		assert(!has_value());
		return std::get<1>(m_data);
	}

	T& operator*() & { return value(); }
	const T& operator*() const& { return value(); }
	T&& operator*() && { return std::move(*this).value(); }
	T* operator->() { return &value(); }
	const T* operator->() const { return &value(); }

	template <typename U>
	T value_or(U&& fallback) const&
	{
		return has_value() ? value() : static_cast<T>(std::forward<U>(fallback));
	}
};

/// Result<void> equivalent.
template <>
class [[nodiscard]] Result<void>
{
	bool m_ok = true;
	Error m_error;

public:
	using value_type = void;

	Result() = default;
	Result(Unexpected unexpected) : m_ok(false), m_error(std::move(unexpected.error)) {}

	bool has_value() const noexcept { return m_ok; }
	explicit operator bool() const noexcept { return m_ok; }
	void value() const { assert(m_ok); }
	const Error& error() const
	{
		assert(!m_ok);
		return m_error;
	}
};

using Status = Result<void>;

inline Status ok()
{
	return {};
}

} // namespace kestrel

/// Propagate an error: `KESTREL_TRY(auto x, parse(...));`
#define KESTREL_TRY_CONCAT_(a, b) a##b
#define KESTREL_TRY_CONCAT(a, b) KESTREL_TRY_CONCAT_(a, b)
#define KESTREL_TRY(decl, expr) KESTREL_TRY_IMPL_(decl, expr, KESTREL_TRY_CONCAT(kestrelTry_, __LINE__))
#define KESTREL_TRY_IMPL_(decl, expr, tmp)              \
	auto tmp = (expr);                                  \
	if (!tmp)                                           \
		return ::kestrel::Unexpected{tmp.error()};      \
	decl = std::move(tmp).value()

/// Propagate an error from a Status-returning expression.
#define KESTREL_CHECK(expr)                                                        \
	do                                                                             \
	{                                                                              \
		auto kestrelCheck_ = (expr);                                               \
		if (!kestrelCheck_)                                                        \
			return ::kestrel::Unexpected{kestrelCheck_.error()};                   \
	} while (false)
