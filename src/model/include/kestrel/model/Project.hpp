#pragma once

// The project domain model (specs/03-domain-model.md §3). A copyable value; mutation only through edit().
// Threading: mutate on one thread only. Const queries build a lazy reverse index, so do not share one
// Project object between threads: give workers their own copy (copies are cheap, tables are copy-on-write).

#include "kestrel/base/Result.hpp"
#include "kestrel/knx/Address.hpp"
#include "kestrel/model/ChangeSet.hpp"
#include "kestrel/model/Entities.hpp"
#include "kestrel/model/History.hpp"
#include "kestrel/model/Id.hpp"
#include "kestrel/model/Table.hpp"
#include "kestrel/model/detail/Ops.hpp"

#include <QString>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <type_traits>
#include <vector>

namespace kestrel::model
{

class Tx;

namespace detail
{
struct Observers;
struct Index;
} // namespace detail

/// RAII handle of an onChanged() subscription. Unsubscribes on destruction. Move-only.
class Subscription
{
public:
	Subscription() = default;
	Subscription(Subscription&& other) noexcept;
	Subscription& operator=(Subscription&& other) noexcept;
	Subscription(const Subscription&) = delete;
	Subscription& operator=(const Subscription&) = delete;
	~Subscription();

	void reset();
	bool isActive() const;

private:
	friend class Project;
	Subscription(std::weak_ptr<detail::Observers> observers, std::uint64_t token);

	std::weak_ptr<detail::Observers> m_observers;
	std::uint64_t m_token = 0;
};

class Project
{
public:
	explicit Project(IdGenerator gen = IdGenerator::random());
	/// Copies the content, the history and the id generator; subscriptions are not copied.
	Project(const Project& other);
	/// Moves everything, subscriptions included.
	Project(Project&& other) noexcept;
	Project& operator=(const Project& other);
	Project& operator=(Project&& other) noexcept;
	~Project();

	// --- Read access ---------------------------------------------------------------------------------------
	const ProjectSettings& settings() const { return m_state.settings; }
	const Table<BuildingPart>& buildingParts() const { return m_state.buildingParts; }
	const Table<Area>& areas() const { return m_state.areas; }
	const Table<Line>& lines() const { return m_state.lines; }
	const Table<Device>& devices() const { return m_state.devices; }
	const Table<ComObject>& comObjects() const { return m_state.comObjects; }
	const Table<GroupRange>& groupRanges() const { return m_state.groupRanges; }
	const Table<GroupAddress>& groupAddresses() const { return m_state.groupAddresses; }
	const Table<Link>& links() const { return m_state.links; }

	/// Generic table access: table<Device>() == devices().
	template <class T>
	const Table<T>& table() const
	{
		return const_cast<Project*>(this)->mutableTable<T>();
	}

	/// Commissioning facts; a default DeviceState for devices never recorded.
	const DeviceState& deviceState(Id<Device> device) const;
	/// True if the referenced entity exists (EntityRef::project() always exists).
	bool contains(EntityRef ref) const;

	// --- Derived queries (results in table order) -------------------------------------------------------------
	std::vector<Id<Line>> linesOf(Id<Area> area) const;
	std::vector<Id<Device>> devicesOf(Id<Line> line) const;
	std::vector<Id<ComObject>> comObjectsOf(Id<Device> device) const;
	std::vector<Id<Link>> linksOfGroupAddress(Id<GroupAddress> groupAddress) const;
	std::vector<Id<Link>> linksOfComObject(Id<ComObject> comObject) const;
	/// Children of a building part; nullopt = top-level parts.
	std::vector<Id<BuildingPart>> buildingPartsIn(std::optional<Id<BuildingPart>> parent) const;
	/// Devices placed directly in a building part.
	std::vector<Id<Device>> devicesIn(Id<BuildingPart> place) const;
	/// Children of a group range; nullopt = top-level ranges.
	std::vector<Id<GroupRange>> groupRangesIn(std::optional<Id<GroupRange>> parent) const;
	/// Group addresses directly in a range; nullopt = addresses without range.
	std::vector<Id<GroupAddress>> groupAddressesIn(std::optional<Id<GroupRange>> range) const;
	/// area.line.device; nullopt if the device is unknown or has no number yet.
	std::optional<knx::IndividualAddress> individualAddress(Id<Device> device) const;

	// --- Mutation ------------------------------------------------------------------------------------------
	/// Runs `body` in a transaction. A non-ok Status, an exception or a failed primitive (unknown id,
	/// duplicate id) rolls back everything and returns the error; nothing is emitted.
	/// On success with at least one op: one undo step (or merged into the previous one) and one ChangeSet.
	/// Nested edit() calls fold into the outer transaction; a failing nested body only rolls back its own ops.
	Status edit(QString label, const std::function<Status(Tx&)>& body, std::optional<MergeKey> merge = {});

	/// Stores commissioning facts: bypasses undo, emits a stateOnly ChangeSet, marks the project dirty.
	/// Ignored for unknown devices.
	void recordState(Id<Device> device, DeviceState state);

	History& history() { return m_history; }
	const History& history() const { return m_history; }
	/// !history().isClean()
	bool isDirty() const { return !m_history.isClean(); }

	/// Called after every commit / undo / redo / recordState, on the mutating thread.
	[[nodiscard]] Subscription onChanged(std::function<void(const ChangeSet&)> callback);

	/// Content equality (settings, tables, device states); history and subscriptions are ignored.
	friend bool operator==(const Project& a, const Project& b) { return a.m_state == b.m_state; }

private:
	friend class Tx;
	friend class History;

	struct State
	{
		ProjectSettings settings;
		Table<BuildingPart> buildingParts;
		Table<Area> areas;
		Table<Line> lines;
		Table<Device> devices;
		Table<ComObject> comObjects;
		Table<GroupRange> groupRanges;
		Table<GroupAddress> groupAddresses;
		Table<Link> links;
		std::map<std::uint64_t, DeviceState> deviceStates; ///< only non-default states

		friend bool operator==(const State&, const State&) = default;
	};

	template <class T>
	Table<T>& mutableTable()
	{
		if constexpr (std::is_same_v<T, BuildingPart>)
			return m_state.buildingParts;
		else if constexpr (std::is_same_v<T, Area>)
			return m_state.areas;
		else if constexpr (std::is_same_v<T, Line>)
			return m_state.lines;
		else if constexpr (std::is_same_v<T, Device>)
			return m_state.devices;
		else if constexpr (std::is_same_v<T, ComObject>)
			return m_state.comObjects;
		else if constexpr (std::is_same_v<T, GroupRange>)
			return m_state.groupRanges;
		else if constexpr (std::is_same_v<T, GroupAddress>)
			return m_state.groupAddresses;
		else
		{
			static_assert(std::is_same_v<T, Link>, "not an entity type");
			return m_state.links;
		}
	}

	template <class T>
	void applyRow(const detail::RowOp<T>& op, bool forward);
	void applyOp(const detail::Op& op, bool forward);
	void rollback(std::vector<detail::Op>& ops, std::size_t mark);
	/// Reverts (forward = false) or re-applies a history step and notifies.
	void replay(const std::vector<detail::Op>& ops, bool forward);
	void notify(const ChangeSet& changeSet);
	const detail::Index& index() const;

	State m_state;
	IdGenerator m_gen;
	History m_history;
	std::shared_ptr<detail::Observers> m_observers;
	mutable std::shared_ptr<const detail::Index> m_index; ///< lazy reverse index, null = stale
	Tx* m_tx = nullptr;                                   ///< active transaction
};

std::ostream& operator<<(std::ostream& os, const Project& project);

/// Transaction handle passed to Project::edit() bodies. Records reversible primitive ops; nobody writes undo code.
/// Primitives do not validate domain rules (operations do) and do not cascade.
class Tx
{
public:
	/// The project including the ops applied so far in this transaction.
	const Project& project() const { return *m_project; }

	/// Appends the entity to its table; assigns a fresh id if entity.id is null. A duplicate id fails the Tx.
	template <class T>
	Id<T> insert(T entity);
	/// Removes the entity. An unknown id fails the Tx.
	template <class T>
	void erase(Id<T> id);
	/// Sets one field. No-op if unchanged. An unknown id fails the Tx.
	template <class T, class F>
	void set(Id<T> id, F T::*field, std::type_identity_t<F> value);
	/// Replaces the whole entity (matched by entity.id). An unknown id fails the Tx.
	template <class T>
	void replace(T entity);
	void setSettings(ProjectSettings settings);
	/// value = nullopt removes the parameter (back to its default).
	void setParam(Id<Device> device, QString ref, std::optional<QString> value);

	/// True once a primitive failed; later primitives are ignored and the transaction will roll back.
	bool failed() const { return m_error.has_value(); }

private:
	friend class Project;
	explicit Tx(Project& project) : m_project(&project) {}

	void record(detail::Op op);
	void fail(ErrorCode code, const QString& message);

	Project* m_project;
	std::vector<detail::Op> m_ops;
	std::optional<Error> m_error;
};

template <class T>
Id<T> Tx::insert(T entity)
{
	if (m_error)
	{
		return entity.id;
	}
	const Table<T>& table = m_project->table<T>();
	if (entity.id.isNull())
	{
		do
		{
			entity.id = Id<T>(m_project->m_gen.next());
		} while (table.contains(entity.id));
	}
	else if (table.contains(entity.id))
	{
		fail(ErrorCode::Conflict, QStringLiteral("duplicate id ") + entity.id.toString());
		return entity.id;
	}
	const Id<T> id = entity.id;
	const std::size_t pos = table.size();
	record(detail::RowOp<T>{detail::RowOp<T>::Kind::Insert, T{}, std::move(entity), pos});
	return id;
}

template <class T>
void Tx::erase(Id<T> id)
{
	if (m_error)
	{
		return;
	}
	const Table<T>& table = m_project->table<T>();
	const auto pos = table.indexOf(id);
	if (!pos)
	{
		fail(ErrorCode::NotFound, QStringLiteral("erase: unknown id ") + id.toString());
		return;
	}
	record(detail::RowOp<T>{detail::RowOp<T>::Kind::Erase, table[*pos], T{}, *pos});
}

template <class T>
void Tx::replace(T entity)
{
	if (m_error)
	{
		return;
	}
	const T* current = m_project->table<T>().find(entity.id);
	if (!current)
	{
		fail(ErrorCode::NotFound, QStringLiteral("replace: unknown id ") + entity.id.toString());
		return;
	}
	if (*current == entity)
	{
		return;
	}
	record(detail::RowOp<T>{detail::RowOp<T>::Kind::Replace, *current, std::move(entity), 0});
}

template <class T, class F>
void Tx::set(Id<T> id, F T::*field, std::type_identity_t<F> value)
{
	if (m_error)
	{
		return;
	}
	const T* current = m_project->table<T>().find(id);
	if (!current)
	{
		fail(ErrorCode::NotFound, QStringLiteral("set: unknown id ") + id.toString());
		return;
	}
	T updated = *current;
	updated.*field = std::move(value);
	replace(std::move(updated));
}

} // namespace kestrel::model
