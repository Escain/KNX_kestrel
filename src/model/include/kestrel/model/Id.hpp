#pragma once

// Kestrel project-entity identity (specs/03-domain-model.md §1.4).
//  - Id<T>: strong random 64-bit id, text form "<prefix>-<13 base32 chars>" (e.g. "dev-7f3k2m9qv1abc").
//  - EntityRef: a kind-tagged id usable across entity types (ChangeSets, selection, diagnostics).
//  - IdGenerator: injectable source of ids (random in production, sequential in tests).

#include <QString>
#include <QStringView>

#include <cassert>
#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <ostream>

namespace kestrel::model
{

enum class EntityKind : std::uint8_t
{
	Project, ///< The project itself (settings)
	BuildingPart,
	Area,
	Line,
	Device,
	ComObject,
	GroupRange,
	GroupAddress,
	Link,
};

const char* toString(EntityKind kind);
/// Text prefix of ids of this kind: "prj", "bp", "area", "line", "dev", "co", "gr", "ga", "lnk".
QString idPrefix(EntityKind kind);
std::ostream& operator<<(std::ostream& os, EntityKind kind);

namespace detail
{
QString formatId(EntityKind kind, std::uint64_t value);
/// Parses "<prefix>-<13 base32>" for the given kind. Rejects the null id.
std::optional<std::uint64_t> parseId(EntityKind kind, QStringView text);
} // namespace detail

/// Specialised for every entity type in Entities.hpp: `static constexpr EntityKind kKind`.
template <class T>
struct EntityTraits;

template <class T>
class Id
{
	std::uint64_t m_value = 0;

public:
	constexpr Id() = default;
	constexpr explicit Id(std::uint64_t value) : m_value(value) {}

	constexpr std::uint64_t value() const { return m_value; }
	constexpr bool isNull() const { return m_value == 0; }

	QString toString() const { return detail::formatId(EntityTraits<T>::kKind, m_value); }

	static std::optional<Id> parse(QStringView text)
	{
		const auto value = detail::parseId(EntityTraits<T>::kKind, text);
		if (!value)
		{
			return std::nullopt;
		}
		return Id(*value);
	}

	friend constexpr bool operator==(const Id&, const Id&) = default;
	friend constexpr auto operator<=>(const Id&, const Id&) = default;

	friend std::ostream& operator<<(std::ostream& os, const Id& id) { return os << id.toString().toStdString(); }
};

/// Reference to any entity: kind + id value. Implicitly constructible from any Id<T>.
struct EntityRef
{
	EntityKind kind = EntityKind::Project;
	std::uint64_t id = 0;

	constexpr EntityRef() = default;
	constexpr EntityRef(EntityKind entityKind, std::uint64_t value) : kind(entityKind), id(value) {}
	template <class T>
	constexpr EntityRef(Id<T> entityId) : kind(EntityTraits<T>::kKind), id(entityId.value())
	{
	}

	/// The project itself (target of settings changes).
	static constexpr EntityRef project() { return EntityRef(EntityKind::Project, 0); }

	template <class T>
	constexpr bool is() const
	{
		return kind == EntityTraits<T>::kKind;
	}
	template <class T>
	constexpr Id<T> as() const
	{
		assert(is<T>());
		return Id<T>(id);
	}

	/// "dev-…" form; "project" for the project itself.
	QString toString() const;
	static std::optional<EntityRef> parse(QStringView text);

	friend constexpr bool operator==(const EntityRef&, const EntityRef&) = default;
	friend constexpr auto operator<=>(const EntityRef&, const EntityRef&) = default;
};

std::ostream& operator<<(std::ostream& os, const EntityRef& ref);

/// Source of fresh id values. Never returns 0.
class IdGenerator
{
	std::function<std::uint64_t()> m_next;

public:
	explicit IdGenerator(std::function<std::uint64_t()> next);

	/// Cryptographically irrelevant, collision-resistant random ids (QRandomGenerator).
	static IdGenerator random();
	/// seed, seed+1, … (0 is skipped). Deterministic ids for tests.
	static IdGenerator sequential(std::uint64_t seed = 1);

	std::uint64_t next();
};

} // namespace kestrel::model
