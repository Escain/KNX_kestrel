#include "kestrel/appconfig/DownloadPlan.hpp"

namespace kestrel::appconfig
{

using namespace Qt::StringLiterals;

std::vector<download::LdCtrl> toLdCtrls(const std::vector<catalog::LoadControl>& controls)
{
	std::vector<download::LdCtrl> out;
	out.reserve(controls.size());
	for (const catalog::LoadControl& control : controls)
	{
		download::LdCtrl element{control.element.toStdString(), {}};
		for (const auto& [name, value] : control.attributes)
		{
			element.attributes[name.toStdString()] = value.toStdString();
		}
		out.push_back(std::move(element));
	}
	return out;
}

Result<download::Plan> planDownload(const catalog::ProductDatabase& database, const catalog::ApplicationProgram& app,
	std::map<std::uint8_t, Bytes> images, const Bytes& programVersion, const QString& subType)
{
	if (app.loadProcedureStyle != u"MergedProcedure")
	{
		return makeError(ErrorCode::Unsupported,
			"load procedure style " + app.loadProcedureStyle.toStdString() + " is not supported yet");
	}
	const catalog::MaskVersionInfo* mask = database.findMaskVersion(app.maskVersion);
	const catalog::MaskProcedure* procedure = mask ? mask->procedure(u"Load", subType) : nullptr;
	if (!procedure)
	{
		return makeError(ErrorCode::NotFound, "no Load procedure \"" + subType.toStdString() + "\" for "
				+ app.maskVersion.toStdString() + " in the product file's knx_master.xml");
	}
	download::ResolveInput input;
	input.procedure = toLdCtrls(procedure->controls);
	for (const catalog::LoadProcedure& merge : app.loadProcedures)
	{
		if (merge.mergeId)
		{
			auto& list = input.merges[static_cast<int>(*merge.mergeId)];
			const auto controls = toLdCtrls(merge.controls);
			list.insert(list.end(), controls.begin(), controls.end());
		}
	}
	input.images = std::move(images);
	input.programVersions[4] = programVersion;
	return download::resolve(input);
}

Result<DevicePlan> planDeviceDownload(const catalog::ProductDatabase& database, const catalog::ApplicationProgram& app,
	const model::Project& project, model::Id<model::Device> device, const QString& subType)
{
	if (app.maskVersion != u"MV-07B0")
	{
		return makeError(ErrorCode::Unsupported,
			"downloads for mask " + app.maskVersion.toStdString() + " are not supported yet (System B only)");
	}
	KESTREL_TRY(DeviceConfiguration configuration, deviceConfiguration(project, device, app));
	KESTREL_TRY(SystemBImages built, buildSystemB(app, configuration.values, configuration.links));
	DevicePlan result;
	result.warnings = configuration.warnings + built.warnings;
	result.images = built.images;
	KESTREL_TRY(download::Plan plan,
		planDownload(database, app, std::move(built.images), programVersion(database.manufacturerId, app), subType));
	result.plan = std::move(plan);
	return result;
}

} // namespace kestrel::appconfig
