#pragma once

// Device configuration in the GUI: which com objects of a catalog device are active, and how its download plan
// is built. Both are injectable (Workspace::setActiveComObjectFilter / setDownloadPlanner); the defaults use
// kestrel::appconfig (the parameter evaluator and the System B plan builder).

#include "kestrel/appconfig/Evaluator.hpp"
#include "kestrel/base/Result.hpp"
#include "kestrel/catalog/Model.hpp"
#include "kestrel/download/Plan.hpp"
#include "kestrel/model/Entities.hpp"
#include "kestrel/model/Id.hpp"
#include "kestrel/model/Project.hpp"

#include <QString>

#include <functional>
#include <vector>

namespace kestrel::viewmodels
{

/// The com objects of a catalog device that are active with its current parameters (device.params), in display
/// order. Called on the GUI thread whenever the editor opens, the device's parameters change or the catalog
/// program is (re)loaded. `language` is the project's content language.
using ActiveComObjectFilter = std::function<std::vector<catalog::EffectiveComObject>(
	const catalog::ApplicationProgram& program, const model::Device& device, const QString& language)>;

/// Every effective com object of the program, whatever the parameters (no evaluation).
std::vector<catalog::EffectiveComObject> allEffectiveComObjects(
	const catalog::ApplicationProgram& program, const model::Device& device, const QString& language);

/// Default filter: the com objects appconfig::evaluate() finds active with the device's parameters. When several
/// active refs share one object number, the first in document order wins. Ordered by number. If the evaluation
/// fails (oscillating conditions), every effective com object is returned.
std::vector<catalog::EffectiveComObject> activeComObjects(
	const catalog::ApplicationProgram& program, const model::Device& device, const QString& language);

/// device.params (keyed by ParameterRef id) as appconfig::ParameterValues (keyed by Parameter id).
appconfig::ParameterValues parameterValues(const catalog::ApplicationProgram& program, const model::Device& device);

/// Builds the download plan of a device from the project (parameters, links) and the catalog.
/// Runs on a worker thread with a private snapshot copy of the project: it must not touch GUI objects.
/// CatalogService::databaseForProgram() is thread-safe and may be used to get the product database.
using DownloadPlanner = std::function<Result<download::Plan>(const model::Project& project, model::Id<model::Device> device)>;

class CatalogService;

/// Default planner: finds the device's product database in the catalog (by appProgramRef) and calls
/// appconfig::planDeviceDownload (System B and System 2; other masks report Unsupported).
DownloadPlanner catalogDownloadPlanner(CatalogService* catalog);

/// SHA-256 (hex) of the exact content of a plan; recorded as DeviceState::downloadedHash after a download.
QString planHash(const download::Plan& plan);

} // namespace kestrel::viewmodels
