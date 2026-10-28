#include "kestrel/app/TelegramRecord.hpp"

#include <sstream>

namespace kestrel::app
{

QString TelegramRecord::destinationText(knx::GroupAddressStyle style) const
{
	return QString::fromStdString(isGroup ? groupAddress().toString(style) : individualDestination().toString());
}

QString TelegramRecord::serviceText() const
{
	std::ostringstream os;
	if (apci)
	{
		os << *apci;
	}
	else
	{
		os << tpduType;
	}
	return QString::fromStdString(os.str());
}

} // namespace kestrel::app
