#include "kestrel/knx/BusLink.hpp"

namespace kestrel::knx
{

std::ostream& operator<<(std::ostream& os, LinkState state)
{
	switch (state)
	{
	case LinkState::Disconnected: return os << "Disconnected";
	case LinkState::Connecting: return os << "Connecting";
	case LinkState::Connected: return os << "Connected";
	}
	return os << "LinkState(" << static_cast<int>(state) << ')';
}

} // namespace kestrel::knx
