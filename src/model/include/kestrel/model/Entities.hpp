#pragma once

// Project entities, milestone M1 (specs/03-domain-model.md §2). Plain copyable values; references by id.

#include "kestrel/knx/Address.hpp"
#include "kestrel/model/Id.hpp"

#include <QString>
#include <QStringView>

#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
#include <utility>

namespace kestrel::model
{

struct BuildingPart;
struct Area;
struct Line;
struct Device;
struct ComObject;
struct GroupRange;
struct GroupAddress;
struct Link;

// clang-format off
template <> struct EntityTraits<BuildingPart> { static constexpr EntityKind kKind = EntityKind::BuildingPart; };
template <> struct EntityTraits<Area>         { static constexpr EntityKind kKind = EntityKind::Area; };
template <> struct EntityTraits<Line>         { static constexpr EntityKind kKind = EntityKind::Line; };
template <> struct EntityTraits<Device>       { static constexpr EntityKind kKind = EntityKind::Device; };
template <> struct EntityTraits<ComObject>    { static constexpr EntityKind kKind = EntityKind::ComObject; };
template <> struct EntityTraits<GroupRange>   { static constexpr EntityKind kKind = EntityKind::GroupRange; };
template <> struct EntityTraits<GroupAddress> { static constexpr EntityKind kKind = EntityKind::GroupAddress; };
template <> struct EntityTraits<Link>         { static constexpr EntityKind kKind = EntityKind::Link; };
// clang-format on

inline constexpr unsigned kMaxAreaNumber = 15;
inline constexpr unsigned kMaxLineNumber = 15;
inline constexpr unsigned kMaxDeviceNumber = 255;
inline constexpr unsigned kMaxComObjectSizeInBit = 255 * 8;

enum class BuildingPartKind
{
	Building,
	Floor,
	Room,
	Corridor,
	Stairway,
	DistributionBoard,
};

enum class Medium
{
	TP,
	IP,
	RF,
	PL,
};

QString toString(BuildingPartKind kind);
QString toString(Medium medium);
QString toString(knx::GroupAddressStyle style);
std::optional<BuildingPartKind> parseBuildingPartKind(QStringView text);
std::optional<Medium> parseMedium(QStringView text);
std::optional<knx::GroupAddressStyle> parseGroupAddressStyle(QStringView text);

std::ostream& operator<<(std::ostream& os, BuildingPartKind kind);
std::ostream& operator<<(std::ostream& os, Medium medium);

/// True for "" (no DPT), "DPT-<main>" and "DPST-<main>-<sub>".
bool isValidDptRef(QStringView dpt);

struct ProjectSettings
{
	QString name;
	knx::GroupAddressStyle gaStyle = knx::GroupAddressStyle::ThreeLevel;
	QString contentLanguage = QStringLiteral("en-US");

	friend bool operator==(const ProjectSettings&, const ProjectSettings&) = default;
};

struct BuildingPart
{
	Id<BuildingPart> id;
	std::optional<Id<BuildingPart>> parent;
	BuildingPartKind kind = BuildingPartKind::Building;
	QString name;
	QString description;

	friend bool operator==(const BuildingPart&, const BuildingPart&) = default;
};

struct Area
{
	Id<Area> id;
	unsigned number = 0; ///< 0..15
	QString name;
	Medium medium = Medium::TP;

	friend bool operator==(const Area&, const Area&) = default;
};

struct Line
{
	Id<Line> id;
	Id<Area> area;
	unsigned number = 0; ///< 0..15
	QString name;
	Medium medium = Medium::TP;

	friend bool operator==(const Line&, const Line&) = default;
};

struct Device
{
	Id<Device> id;
	Id<Line> line;
	std::optional<unsigned> number; ///< 0..255, nullopt = not yet addressed
	QString name;
	QString description;
	std::optional<Id<BuildingPart>> place;
	QString productRef;    ///< catalog key, may be empty
	QString appProgramRef; ///< catalog key, may be empty
	std::map<QString, QString> params; ///< paramRefId -> canonical value (non-default values only)

	friend bool operator==(const Device&, const Device&) = default;
};

struct ComObjectFlags
{
	bool communication = true; ///< C
	bool read = false;         ///< R
	bool write = false;        ///< W
	bool transmit = false;     ///< T
	bool update = false;       ///< U
	bool readOnInit = false;   ///< I

	/// Letters of the set flags in "CRWTUI" order, e.g. "CWT".
	QString toString() const;
	/// Inverse of toString(); any order, no duplicates, letters from "CRWTUI" only.
	static std::optional<ComObjectFlags> parse(QStringView text);

	friend bool operator==(const ComObjectFlags&, const ComObjectFlags&) = default;
};

struct ComObject
{
	Id<ComObject> id;
	Id<Device> device;
	unsigned number = 0;
	QString name;
	QString function;
	unsigned sizeInBit = 1;
	ComObjectFlags flags;
	QString dpt;        ///< "DPST-1-1", may be empty
	QString catalogRef; ///< empty for manually defined objects

	friend bool operator==(const ComObject&, const ComObject&) = default;
};

/// Main / middle group folder.
struct GroupRange
{
	Id<GroupRange> id;
	std::optional<Id<GroupRange>> parent;
	QString name;
	std::uint16_t start = 0;
	std::uint16_t end = 0;

	bool contains(std::uint16_t raw) const { return raw >= start && raw <= end; }

	friend bool operator==(const GroupRange&, const GroupRange&) = default;
};

struct GroupAddress
{
	Id<GroupAddress> id;
	std::optional<Id<GroupRange>> range;
	std::uint16_t raw = 0; ///< same encoding as knx::GroupAddress
	QString name;
	QString dpt; ///< may be empty
	QString description;

	knx::GroupAddress address() const { return knx::GroupAddress(raw); }

	friend bool operator==(const GroupAddress&, const GroupAddress&) = default;
};

/// Com object <-> group address association. At most one sending link per com object.
struct Link
{
	Id<Link> id;
	Id<ComObject> comObject;
	Id<GroupAddress> groupAddress;
	bool sending = false;

	friend bool operator==(const Link&, const Link&) = default;
};

/// Commissioning facts (from the bus). Not undoable: written with Project::recordState().
struct DeviceState
{
	bool iaProgrammed = false;
	QString serial;
	QString downloadedHash;

	friend bool operator==(const DeviceState&, const DeviceState&) = default;
};

/// Owner/parent ids of an entity. A change of this key is a structural change (a move).
inline std::pair<std::uint64_t, std::uint64_t> structuralKey(const BuildingPart& e)
{
	return {e.parent ? e.parent->value() : 0, 0};
}
inline std::pair<std::uint64_t, std::uint64_t> structuralKey(const Area&)
{
	return {0, 0};
}
inline std::pair<std::uint64_t, std::uint64_t> structuralKey(const Line& e)
{
	return {e.area.value(), 0};
}
inline std::pair<std::uint64_t, std::uint64_t> structuralKey(const Device& e)
{
	return {e.line.value(), e.place ? e.place->value() : 0};
}
inline std::pair<std::uint64_t, std::uint64_t> structuralKey(const ComObject& e)
{
	return {e.device.value(), 0};
}
inline std::pair<std::uint64_t, std::uint64_t> structuralKey(const GroupRange& e)
{
	return {e.parent ? e.parent->value() : 0, 0};
}
inline std::pair<std::uint64_t, std::uint64_t> structuralKey(const GroupAddress& e)
{
	return {e.range ? e.range->value() : 0, 0};
}
inline std::pair<std::uint64_t, std::uint64_t> structuralKey(const Link& e)
{
	return {e.comObject.value(), e.groupAddress.value()};
}

std::ostream& operator<<(std::ostream& os, const ProjectSettings& value);
std::ostream& operator<<(std::ostream& os, const BuildingPart& value);
std::ostream& operator<<(std::ostream& os, const Area& value);
std::ostream& operator<<(std::ostream& os, const Line& value);
std::ostream& operator<<(std::ostream& os, const Device& value);
std::ostream& operator<<(std::ostream& os, const ComObjectFlags& value);
std::ostream& operator<<(std::ostream& os, const ComObject& value);
std::ostream& operator<<(std::ostream& os, const GroupRange& value);
std::ostream& operator<<(std::ostream& os, const GroupAddress& value);
std::ostream& operator<<(std::ostream& os, const Link& value);
std::ostream& operator<<(std::ostream& os, const DeviceState& value);

} // namespace kestrel::model
