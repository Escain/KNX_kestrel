#pragma once

// Reading a configured device back into a project: the inverse of planDeviceDownload(). Used to take over an
// installation that was configured with another tool (e.g. ETS6) without its project file.

#include "kestrel/appconfig/Images.hpp"
#include "kestrel/model/Project.hpp"

namespace kestrel::appconfig
{

/// Applies System B segment images (1 address table, 2 association table, 4 parameters) to `device`, whose
/// appProgramRef must be `app.id`, in one undo step:
/// - non-default parameter values (keyed by an active ParameterRef of the parameter);
/// - one catalog-bound com object per associated object number;
/// - group addresses (created when missing, named after the address) and links, with the first association
///   of an object as its sending link.
/// Existing parameter values, com objects and links of the device are replaced. Returns warnings.
Result<QStringList> importSystemBImages(model::Project& project, model::Id<model::Device> device,
	const catalog::ApplicationProgram& app, const std::map<std::uint8_t, Bytes>& images,
	const QString& language = {});

/// Links described by System B address and association tables (object number → addresses, sending first).
Result<Links> decodeSystemBTables(const Bytes& addressTable, const Bytes& associationTable);

} // namespace kestrel::appconfig
