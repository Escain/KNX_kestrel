#pragma once

// Conversions between Qt network addresses and the KNXnet/IP endpoint value type.

#include "kestrel/knx/KnxIp.hpp"

#include <QHostAddress>
#include <QString>

namespace kestrel::bus
{

/// IPv4 only; other addresses map to 0.0.0.0.
knx::knxip::IpEndpoint toEndpoint(const QHostAddress& address, quint16 port);
QHostAddress toHostAddress(const knx::knxip::IpEndpoint& endpoint);
/// "192.168.1.10:3671"
QString toString(const knx::knxip::IpEndpoint& endpoint);

} // namespace kestrel::bus
