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
	std::map<std::uint8_t, Bytes> images, const Bytes& programVersion, const QString& subType,
	std::map<std::uint16_t, Bytes> absoluteImages, std::map<std::uint16_t, Bytes> absoluteMasks)
{
	download::ResolveInput input;
	if (app.loadProcedureStyle == u"ProductProcedure")
	{
		// The application carries the whole procedure.
		for (const catalog::LoadProcedure& procedure : app.loadProcedures)
		{
			const auto controls = toLdCtrls(procedure.controls);
			input.procedure.insert(input.procedure.end(), controls.begin(), controls.end());
		}
		if (input.procedure.empty())
		{
			return makeError(ErrorCode::NotFound, "the application has no load procedure");
		}
	}
	else if (app.loadProcedureStyle == u"MergedProcedure" || app.loadProcedureStyle == u"DefaultProcedure")
	{
		const catalog::MaskVersionInfo* mask = database.findMaskVersion(app.maskVersion);
		const catalog::MaskProcedure* procedure = mask ? mask->procedure(u"Load", subType) : nullptr;
		if (!procedure)
		{
			return makeError(ErrorCode::NotFound, "no Load procedure \"" + subType.toStdString() + "\" for "
					+ app.maskVersion.toStdString() + " in the product file's knx_master.xml");
		}
		input.procedure = toLdCtrls(procedure->controls);
		if (app.loadProcedureStyle == u"MergedProcedure")
		{
			for (const catalog::LoadProcedure& merge : app.loadProcedures)
			{
				if (merge.mergeId)
				{
					auto& list = input.merges[static_cast<int>(*merge.mergeId)];
					const auto controls = toLdCtrls(merge.controls);
					list.insert(list.end(), controls.begin(), controls.end());
				}
			}
		}
	}
	else
	{
		return makeError(ErrorCode::Unsupported,
			"load procedure style \"" + app.loadProcedureStyle.toStdString() + "\" is not supported yet");
	}
	input.images = std::move(images);
	input.programVersions[4] = programVersion;
	input.absoluteImages = std::move(absoluteImages);
	input.absoluteMasks = std::move(absoluteMasks);
	input.applicationId = programVersion;
	input.peiType = static_cast<std::uint8_t>(app.peiType);
	return download::resolve(input);
}

bool canPlanDownload(const QString& maskVersion)
{
	return maskVersion == u"MV-07B0" || maskVersion == u"MV-0021";
}

Result<DevicePlan> planDeviceDownload(const catalog::ProductDatabase& database, const catalog::ApplicationProgram& app,
	const model::Project& project, model::Id<model::Device> device, const QString& subType)
{
	if (!canPlanDownload(app.maskVersion))
	{
		return makeError(ErrorCode::Unsupported, "downloads for mask " + app.maskVersion.toStdString()
				+ " are not supported yet (System B MV-07B0 and System 2 MV-0021)");
	}
	KESTREL_TRY(DeviceConfiguration configuration, deviceConfiguration(project, device, app));
	if (app.maskVersion == u"MV-0021")
	{
		const auto address = project.individualAddress(device);
		if (!address)
		{
			return makeError(ErrorCode::InvalidArgument, "the device has no individual address yet");
		}
		KESTREL_TRY(System2Images built, buildSystem2(app, configuration.values, configuration.links, address->raw()));
		DevicePlan result;
		result.warnings = configuration.warnings + built.warnings;
		result.absoluteImages = built.segments;
		KESTREL_TRY(download::Plan plan, planDownload(database, app, {}, programVersion(database.manufacturerId, app),
												 subType, std::move(built.segments), std::move(built.masks)));
		result.plan = std::move(plan);
		return result;
	}
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
