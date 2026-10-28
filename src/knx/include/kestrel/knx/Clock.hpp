#pragma once

// Time for the sans-IO state machines (ADR-002). The state machines never read
// the clock themselves: callers pass `now` into every entry point, so tests can
// drive them with a manual clock.

#include <chrono>

namespace kestrel::knx
{

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = Clock::duration;

} // namespace kestrel::knx
