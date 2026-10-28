#pragma once

// From a product database and segment images to a download::Plan: the mask's load procedure (knx_master.xml)
// with the application's LoadProcedures merged in (ADR-008). Used by the GUI's download action and the CLI.

#include "kestrel/appconfig/DeviceImages.hpp"
#include "kestrel/catalog/Model.hpp"
#include "kestrel/download/Resolver.hpp"

namespace kestrel::appconfig
{

std::vector<download::LdCtrl> toLdCtrls(const std::vector<catalog::LoadControl>& controls);

/// Errors: Unsupported (load procedure style other than MergedProcedure), NotFound (no such mask procedure),
/// or the resolver's errors.
Result<download::Plan> planDownload(const catalog::ProductDatabase& database, const catalog::ApplicationProgram& app,
	std::map<std::uint8_t, Bytes> images, const Bytes& programVersion, const QString& subType = QStringLiteral("ap1"));

struct DevicePlan
{
	download::Plan plan;
	std::map<std::uint8_t, Bytes> images;
	QStringList warnings;
};

/// Everything for a project device: configuration → System B images → plan (program version of `app`).
Result<DevicePlan> planDeviceDownload(const catalog::ProductDatabase& database, const catalog::ApplicationProgram& app,
	const model::Project& project, model::Id<model::Device> device, const QString& subType = QStringLiteral("ap1"));

} // namespace kestrel::appconfig
