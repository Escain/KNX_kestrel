#pragma once

// Logging categories of the bus I/O adapters: "kestrel.bus", and "kestrel.bus.frames" for a hex
// trace of every KNXnet/IP datagram (debug level, off by default).

#include <QLoggingCategory>

namespace kestrel::bus
{

Q_DECLARE_LOGGING_CATEGORY(lcBus)
Q_DECLARE_LOGGING_CATEGORY(lcBusFrames)

} // namespace kestrel::bus
