#include "kestrel/bus/Endpoint.hpp"

namespace kestrel::bus
{

using namespace Qt::StringLiterals;

knx::knxip::IpEndpoint toEndpoint(const QHostAddress& address, quint16 port)
{
	knx::knxip::IpEndpoint endpoint;
	bool ok = false;
	const quint32 ip = address.toIPv4Address(&ok);
	if (ok)
	{
		endpoint.address = {static_cast<std::uint8_t>(ip >> 24), static_cast<std::uint8_t>(ip >> 16),
			static_cast<std::uint8_t>(ip >> 8), static_cast<std::uint8_t>(ip)};
	}
	endpoint.port = port;
	return endpoint;
}

QHostAddress toHostAddress(const knx::knxip::IpEndpoint& endpoint)
{
	const auto& a = endpoint.address;
	return QHostAddress((quint32{a[0]} << 24) | (quint32{a[1]} << 16) | (quint32{a[2]} << 8) | quint32{a[3]});
}

QString toString(const knx::knxip::IpEndpoint& endpoint)
{
	return u"%1:%2"_s.arg(toHostAddress(endpoint).toString()).arg(endpoint.port);
}

} // namespace kestrel::bus
