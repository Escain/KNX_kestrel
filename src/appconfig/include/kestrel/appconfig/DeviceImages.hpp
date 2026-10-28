#pragma once

// From a project device to what the image builders need: parameter values and links.

#include "kestrel/appconfig/Images.hpp"
#include "kestrel/model/Project.hpp"

namespace kestrel::appconfig
{

struct DeviceConfiguration
{
	ParameterValues values;
	Links links;
	QStringList warnings;
};

/// `Device::params` is keyed by ParameterRef id (what the UI edits); values are stored per Parameter here.
/// Links come from the device's com objects bound to the catalog (ComObject::catalogRef = ComObjectRef id);
/// the sending link of an object comes first, the others follow by group address.
Result<DeviceConfiguration> deviceConfiguration(const model::Project& project, model::Id<model::Device> device,
	const catalog::ApplicationProgram& app);

} // namespace kestrel::appconfig
