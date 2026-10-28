#include "kestrel/appconfig/DeviceImages.hpp"

#include <algorithm>

namespace kestrel::appconfig
{

using namespace Qt::StringLiterals;

Result<DeviceConfiguration> deviceConfiguration(const model::Project& project, model::Id<model::Device> deviceId,
	const catalog::ApplicationProgram& app)
{
	const model::Device* device = project.devices().find(deviceId);
	if (!device)
	{
		return makeError(ErrorCode::NotFound, "no such device");
	}
	DeviceConfiguration configuration;
	for (const auto& [refId, value] : device->params)
	{
		const catalog::ParameterRef* ref = app.findParameterRef(refId);
		if (!ref)
		{
			configuration.warnings.append(u"parameter %1 is not part of %2"_s.arg(refId, app.id));
			continue;
		}
		configuration.values[ref->refId] = value;
	}

	for (const model::ComObject& object : project.comObjects().rows())
	{
		if (object.device != deviceId || object.catalogRef.isEmpty())
		{
			continue;
		}
		const catalog::ComObjectRef* ref = app.findComObjectRef(object.catalogRef);
		const catalog::ComObject* definition = ref ? app.findComObject(ref->refId) : nullptr;
		if (!definition)
		{
			configuration.warnings.append(u"com object %1 is not part of %2"_s.arg(object.catalogRef, app.id));
			continue;
		}
		std::vector<std::pair<bool, std::uint16_t>> addresses;
		for (const model::Link& link : project.links().rows())
		{
			if (link.comObject != object.id)
			{
				continue;
			}
			if (const model::GroupAddress* address = project.groupAddresses().find(link.groupAddress))
			{
				addresses.emplace_back(!link.sending, address->raw);
			}
		}
		if (addresses.empty())
		{
			continue;
		}
		std::sort(addresses.begin(), addresses.end()); // sending (false) first, then by address
		auto& list = configuration.links[definition->number];
		for (const auto& [notSending, raw] : addresses)
		{
			if (std::find(list.begin(), list.end(), raw) == list.end())
			{
				list.push_back(raw);
			}
		}
	}
	return configuration;
}

} // namespace kestrel::appconfig
