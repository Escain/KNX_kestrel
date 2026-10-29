#pragma once

// From a product database and segment images to a download::Plan: the mask's load procedure (knx_master.xml)
// with the application's LoadProcedures merged in (ADR-008). Used by the GUI's download action and the CLI.

#include "kestrel/appconfig/DeviceImages.hpp"
#include "kestrel/catalog/Model.hpp"
#include "kestrel/download/Resolver.hpp"

namespace kestrel::appconfig
{

std::vector<download::LdCtrl> toLdCtrls(const std::vector<catalog::LoadControl>& controls);

/// Errors: Unsupported (unknown load procedure style), NotFound (no such mask procedure), or the resolver's
/// errors. `images` are System B segments per interface object, `absoluteImages` System 2 segments per address;
/// `programVersion` is also the System 2 task segments' application id.
Result<download::Plan> planDownload(const catalog::ProductDatabase& database, const catalog::ApplicationProgram& app,
	std::map<std::uint8_t, Bytes> images, const Bytes& programVersion, const QString& subType = QStringLiteral("ap1"),
	std::map<std::uint16_t, Bytes> absoluteImages = {}, std::map<std::uint16_t, Bytes> absoluteMasks = {});

struct DevicePlan
{
	download::Plan plan;
	std::map<std::uint8_t, Bytes> images;          ///< System B
	std::map<std::uint16_t, Bytes> absoluteImages; ///< System 2
	QStringList warnings;
};

/// Masks planDeviceDownload() supports: System B (MV-07B0) and System 2 (MV-0021).
bool canPlanDownload(const QString& maskVersion);

/// Everything for a project device: configuration → System B or System 2 images → plan (program version of
/// `app`). System 2 needs the device's individual address (its address table holds it).
Result<DevicePlan> planDeviceDownload(const catalog::ProductDatabase& database, const catalog::ApplicationProgram& app,
	const model::Project& project, model::Id<model::Device> device, const QString& subType = QStringLiteral("ap1"));

} // namespace kestrel::appconfig
