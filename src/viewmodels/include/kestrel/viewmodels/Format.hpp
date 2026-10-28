#pragma once

// Text helpers shared by the item models and view models: addresses, entity labels, error texts,
// and the generic entity removal used by "Delete".

#include "kestrel/base/Result.hpp"
#include "kestrel/model/Entities.hpp"
#include "kestrel/model/Id.hpp"
#include "kestrel/model/Project.hpp"

#include <QString>
#include <QStringView>

#include <cstdint>
#include <optional>

namespace kestrel::viewmodels
{

/// Human-readable (translatable) message of a model / persist error.
QString errorText(const Error& error);

/// "1.1.5"; "1.1.–" for devices without a number yet; empty for unknown devices.
QString individualAddressText(const model::Project& project, model::Id<model::Device> device);
/// "1.1"
QString lineAddressText(const model::Project& project, model::Id<model::Line> line);
/// Group address in the project's gaStyle ("1/2/3", "1/515" or "4660").
QString groupAddressText(const model::Project& project, std::uint16_t raw);
/// "1" (main group), "1/2" (middle group) or "1/2/0 – 1/2/255" (other ranges), per the project's gaStyle.
QString groupRangeText(const model::Project& project, const model::GroupRange& range);
/// Parses "1/2/3", "1/515" or "4660".
std::optional<std::uint16_t> parseGroupAddress(QStringView text);

/// Translated singular name of an entity kind ("Device").
QString kindName(model::EntityKind kind);
QString buildingPartKindName(model::BuildingPartKind kind);
QString mediumName(model::Medium medium);
/// "1 bit", "1 byte", "2 bytes", "14 bytes".
QString sizeText(unsigned sizeInBit);

/// "1 error" / "3 errors": English plural forms without a loaded translator. `singular` and `plural`
/// contain %1 and are translated in the "kestrel::viewmodels::Format" context.
QString countText(qsizetype count, const char* singular, const char* plural);

/// Short label with the address where one exists: "1.1.5 Dimmer", "1/2/3 Kitchen light", "Kitchen".
QString entityLabel(const model::Project& project, model::EntityRef ref);
/// The entity's name (empty if it has none or does not exist).
QString entityName(const model::Project& project, model::EntityRef ref);

/// Removes any entity with the matching cascading operation.
Status removeEntity(model::Project& project, model::EntityRef ref);

} // namespace kestrel::viewmodels
