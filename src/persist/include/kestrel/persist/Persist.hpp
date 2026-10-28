#pragma once

// Native project format, folder form (specs/03-domain-model.md §4):
//
//   kestrel.json           { "format": "kestrel-project", "formatVersion": 1, "generator": "Kestrel x.y.z" }
//   project.json           settings
//   buildings.json         building parts (ordered)
//   topology.json          areas -> lines (ordered) -> device ids
//   devices/<dev-id>.json  device + com objects (+ links, sending first) + params + state
//   groupaddresses.json    group ranges + group addresses
//
// Output is deterministic (sorted keys, stable arrays, 2-space indent, final newline); files are written
// atomically (QSaveFile) and stale device files are removed. Loading runs the JSON->JSON migration chain,
// then checks every value and reference: errors name the file (relative to the project folder) and the id.

#include "kestrel/base/Result.hpp"
#include "kestrel/model/Id.hpp"
#include "kestrel/model/Project.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace kestrel::persist
{

inline constexpr int kFormatVersion = 1;

/// The logical document tree: relative path ("kestrel.json", "devices/dev-….json") -> root object.
struct ProjectTree
{
	std::map<QString, QJsonObject> files;

	friend bool operator==(const ProjectTree&, const ProjectTree&) = default;
};

/// Upgrades a tree from `fromVersion` to `fromVersion + 1` (formatVersion in kestrel.json is updated by migrate()).
struct Migration
{
	int fromVersion = 0;
	std::function<Status(ProjectTree&)> apply;
};

using MigrationChain = std::vector<Migration>;

/// The migrations of this build (empty while the format is at v1).
const MigrationChain& builtinMigrations();

struct LoadOptions
{
	MigrationChain migrations = builtinMigrations();
	/// Generator of the loaded project (for ids created after loading). Default: random.
	std::optional<model::IdGenerator> idGenerator;
};

/// Writes the project into `dir` (created if needed). The project's clean state is not touched: callers
/// mark it with project.history().setClean() on success.
Status save(const model::Project& project, const QString& dir);

/// Reads, migrates and checks a project folder. The result has an empty history and is clean.
Result<model::Project> load(const QString& dir, const LoadOptions& options = {});

// --- Lower-level steps (tools, tests) ------------------------------------------------------------------------

ProjectTree toTree(const model::Project& project);
/// Expects a tree at kFormatVersion.
Result<model::Project> fromTree(
	const ProjectTree& tree, std::optional<model::IdGenerator> idGenerator = std::nullopt);
Result<ProjectTree> readTree(const QString& dir);
Status writeTree(const ProjectTree& tree, const QString& dir);
/// Checks kestrel.json and runs the chain up to kFormatVersion. Newer versions are refused (Unsupported).
Status migrate(ProjectTree& tree, const MigrationChain& chain);
/// Deterministic serialisation: sorted keys, 2-space indent, final newline, UTF-8.
QByteArray toJson(const QJsonObject& object);

} // namespace kestrel::persist
