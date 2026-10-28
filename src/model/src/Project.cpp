#include "kestrel/model/Project.hpp"

#include <exception>
#include <unordered_map>
#include <utility>

using namespace Qt::StringLiterals;

namespace kestrel::model
{

namespace detail
{

struct Observers
{
	std::uint64_t nextToken = 1;
	std::vector<std::pair<std::uint64_t, std::function<void(const ChangeSet&)>>> callbacks;
};

/// Reverse indexes, keyed by owner id value (0 = no owner). Values in table order.
struct Index
{
	using Map = std::unordered_map<std::uint64_t, std::vector<std::uint64_t>>;
	Map linesOfArea;
	Map devicesOfLine;
	Map devicesOfPlace;
	Map comObjectsOfDevice;
	Map linksOfGroupAddress;
	Map linksOfComObject;
	Map partsOfParent;
	Map rangesOfParent;
	Map addressesOfRange;
};

} // namespace detail

namespace
{

template <class T>
std::vector<Id<T>> lookup(const detail::Index::Map& map, std::uint64_t key)
{
	std::vector<Id<T>> result;
	const auto it = map.find(key);
	if (it != map.end())
	{
		result.reserve(it->second.size());
		for (const std::uint64_t value : it->second)
		{
			result.emplace_back(value);
		}
	}
	return result;
}

template <class T>
std::uint64_t valueOr0(const std::optional<Id<T>>& id)
{
	return id ? id->value() : 0;
}

template <class... F>
struct Overloaded : F...
{
	using F::operator()...;
};

template <class T>
Change changeOf(const detail::RowOp<T>& op, bool forward)
{
	using Kind = typename detail::RowOp<T>::Kind;
	switch (op.kind)
	{
		case Kind::Insert:
			return {forward ? Change::Added : Change::Removed, EntityRef(op.after.id), false};
		case Kind::Erase:
			return {forward ? Change::Removed : Change::Added, EntityRef(op.before.id), false};
		case Kind::Replace:
			break;
	}
	return {Change::Modified, EntityRef(op.after.id), structuralKey(op.before) != structuralKey(op.after)};
}

std::vector<Change> changesOf(const std::vector<detail::Op>& ops, bool forward)
{
	std::vector<Change> changes;
	changes.reserve(ops.size());
	const auto one = [&](const detail::Op& op)
	{
		changes.push_back(std::visit(Overloaded{[&](const detail::SettingsOp&)
									   { return Change{Change::Modified, EntityRef::project(), false}; },
									   [&](const auto& rowOp) { return changeOf(rowOp, forward); }},
			op));
	};
	if (forward)
	{
		for (const auto& op : ops)
			one(op);
	}
	else
	{
		for (auto it = ops.rbegin(); it != ops.rend(); ++it)
			one(*it);
	}
	return changes;
}

} // namespace

// --- Subscription ------------------------------------------------------------------------------------------

Subscription::Subscription(std::weak_ptr<detail::Observers> observers, std::uint64_t token)
	: m_observers(std::move(observers)), m_token(token)
{
}

Subscription::Subscription(Subscription&& other) noexcept
	: m_observers(std::move(other.m_observers)), m_token(std::exchange(other.m_token, 0))
{
}

Subscription& Subscription::operator=(Subscription&& other) noexcept
{
	if (this != &other)
	{
		reset();
		m_observers = std::move(other.m_observers);
		m_token = std::exchange(other.m_token, 0);
	}
	return *this;
}

Subscription::~Subscription()
{
	reset();
}

void Subscription::reset()
{
	if (const auto observers = m_observers.lock())
	{
		std::erase_if(observers->callbacks, [&](const auto& entry) { return entry.first == m_token; });
	}
	m_observers.reset();
	m_token = 0;
}

bool Subscription::isActive() const
{
	return m_token != 0 && !m_observers.expired();
}

// --- Project: lifetime -------------------------------------------------------------------------------------

Project::Project(IdGenerator gen) : m_gen(std::move(gen)), m_observers(std::make_shared<detail::Observers>())
{
	m_history.m_project = this;
}

Project::Project(const Project& other)
	: m_state(other.m_state), m_gen(other.m_gen), m_history(other.m_history),
	  m_observers(std::make_shared<detail::Observers>()), m_index(other.m_index)
{
	m_history.m_project = this;
}

Project::Project(Project&& other) noexcept
	: m_state(std::move(other.m_state)), m_gen(std::move(other.m_gen)), m_history(std::move(other.m_history)),
	  m_observers(std::exchange(other.m_observers, std::make_shared<detail::Observers>())),
	  m_index(std::move(other.m_index))
{
	m_history.m_project = this;
	other.m_history.m_project = &other;
}

Project& Project::operator=(const Project& other)
{
	if (this != &other)
	{
		m_state = other.m_state;
		m_gen = other.m_gen;
		m_history = other.m_history;
		m_history.m_project = this;
		m_index = other.m_index;
	}
	return *this;
}

Project& Project::operator=(Project&& other) noexcept
{
	if (this != &other)
	{
		m_state = std::move(other.m_state);
		m_gen = std::move(other.m_gen);
		m_history = std::move(other.m_history);
		m_history.m_project = this;
		m_observers = std::exchange(other.m_observers, std::make_shared<detail::Observers>());
		m_index = std::move(other.m_index);
		other.m_history.m_project = &other;
	}
	return *this;
}

Project::~Project() = default;

// --- Project: queries --------------------------------------------------------------------------------------

const DeviceState& Project::deviceState(Id<Device> device) const
{
	static const DeviceState kDefault;
	const auto it = m_state.deviceStates.find(device.value());
	return it == m_state.deviceStates.end() ? kDefault : it->second;
}

bool Project::contains(EntityRef ref) const
{
	switch (ref.kind)
	{
		case EntityKind::Project: return true;
		case EntityKind::BuildingPart: return buildingParts().contains(ref.as<BuildingPart>());
		case EntityKind::Area: return areas().contains(ref.as<Area>());
		case EntityKind::Line: return lines().contains(ref.as<Line>());
		case EntityKind::Device: return devices().contains(ref.as<Device>());
		case EntityKind::ComObject: return comObjects().contains(ref.as<ComObject>());
		case EntityKind::GroupRange: return groupRanges().contains(ref.as<GroupRange>());
		case EntityKind::GroupAddress: return groupAddresses().contains(ref.as<GroupAddress>());
		case EntityKind::Link: return links().contains(ref.as<Link>());
	}
	return false;
}

const detail::Index& Project::index() const
{
	if (!m_index)
	{
		auto index = std::make_shared<detail::Index>();
		for (const Line& line : lines())
			index->linesOfArea[line.area.value()].push_back(line.id.value());
		for (const Device& device : devices())
		{
			index->devicesOfLine[device.line.value()].push_back(device.id.value());
			if (device.place)
				index->devicesOfPlace[device.place->value()].push_back(device.id.value());
		}
		for (const ComObject& object : comObjects())
			index->comObjectsOfDevice[object.device.value()].push_back(object.id.value());
		for (const Link& link : links())
		{
			index->linksOfGroupAddress[link.groupAddress.value()].push_back(link.id.value());
			index->linksOfComObject[link.comObject.value()].push_back(link.id.value());
		}
		for (const BuildingPart& part : buildingParts())
			index->partsOfParent[valueOr0(part.parent)].push_back(part.id.value());
		for (const GroupRange& range : groupRanges())
			index->rangesOfParent[valueOr0(range.parent)].push_back(range.id.value());
		for (const GroupAddress& address : groupAddresses())
			index->addressesOfRange[valueOr0(address.range)].push_back(address.id.value());
		m_index = std::move(index);
	}
	return *m_index;
}

std::vector<Id<Line>> Project::linesOf(Id<Area> area) const
{
	return lookup<Line>(index().linesOfArea, area.value());
}

std::vector<Id<Device>> Project::devicesOf(Id<Line> line) const
{
	return lookup<Device>(index().devicesOfLine, line.value());
}

std::vector<Id<ComObject>> Project::comObjectsOf(Id<Device> device) const
{
	return lookup<ComObject>(index().comObjectsOfDevice, device.value());
}

std::vector<Id<Link>> Project::linksOfGroupAddress(Id<GroupAddress> groupAddress) const
{
	return lookup<Link>(index().linksOfGroupAddress, groupAddress.value());
}

std::vector<Id<Link>> Project::linksOfComObject(Id<ComObject> comObject) const
{
	return lookup<Link>(index().linksOfComObject, comObject.value());
}

std::vector<Id<BuildingPart>> Project::buildingPartsIn(std::optional<Id<BuildingPart>> parent) const
{
	return lookup<BuildingPart>(index().partsOfParent, valueOr0(parent));
}

std::vector<Id<Device>> Project::devicesIn(Id<BuildingPart> place) const
{
	return lookup<Device>(index().devicesOfPlace, place.value());
}

std::vector<Id<GroupRange>> Project::groupRangesIn(std::optional<Id<GroupRange>> parent) const
{
	return lookup<GroupRange>(index().rangesOfParent, valueOr0(parent));
}

std::vector<Id<GroupAddress>> Project::groupAddressesIn(std::optional<Id<GroupRange>> range) const
{
	return lookup<GroupAddress>(index().addressesOfRange, valueOr0(range));
}

std::optional<knx::IndividualAddress> Project::individualAddress(Id<Device> deviceId) const
{
	const Device* device = devices().find(deviceId);
	if (!device || !device->number)
	{
		return std::nullopt;
	}
	const Line* line = lines().find(device->line);
	const Area* area = line ? areas().find(line->area) : nullptr;
	if (!area)
	{
		return std::nullopt;
	}
	auto address = knx::IndividualAddress::fromParts(area->number, line->number, *device->number);
	if (!address)
	{
		return std::nullopt;
	}
	return *address;
}

// --- Project: mutation -------------------------------------------------------------------------------------

Status Project::edit(QString label, const std::function<Status(Tx&)>& body, std::optional<MergeKey> merge)
{
	const auto runBody = [&](Tx& tx) -> Status
	{
		try
		{
			Status status = body(tx);
			if (status && tx.m_error)
			{
				return Unexpected{*tx.m_error};
			}
			return status;
		}
		catch (const std::exception& e)
		{
			return makeError(ErrorCode::Cancelled, std::string("exception in transaction: ") + e.what());
		}
		catch (...)
		{
			return makeError(ErrorCode::Cancelled, "unknown exception in transaction");
		}
	};

	if (m_tx)
	{
		// Nested: fold into the outer transaction; on failure undo only our own ops.
		Tx& tx = *m_tx;
		const std::size_t mark = tx.m_ops.size();
		const bool failedBefore = tx.m_error.has_value();
		Status status = runBody(tx);
		if (!status)
		{
			rollback(tx.m_ops, mark);
			if (!failedBefore)
			{
				tx.m_error.reset();
			}
		}
		return status;
	}

	Tx tx(*this);
	m_tx = &tx;
	Status status = runBody(tx);
	m_tx = nullptr;
	if (!status)
	{
		rollback(tx.m_ops, 0);
		return status;
	}
	if (tx.m_ops.empty())
	{
		return ok();
	}

	ChangeSet changeSet = ChangeSet::normalized(changesOf(tx.m_ops, true));
	if (changeSet.empty())
	{
		// Only insert+erase pairs of the same entities: the content is unchanged. Restore the exact
		// pre-transaction tables and record nothing (no undo step, no dirty flag, no notification).
		rollback(tx.m_ops, 0);
		return ok();
	}
	m_history.push(std::move(label), std::move(tx.m_ops), std::move(merge));
	notify(changeSet);
	return ok();
}

void Project::recordState(Id<Device> device, DeviceState state)
{
	if (!devices().contains(device))
	{
		return;
	}
	if (state == DeviceState{})
	{
		m_state.deviceStates.erase(device.value());
	}
	else
	{
		m_state.deviceStates[device.value()] = std::move(state);
	}
	m_history.markStateDirty();
	ChangeSet changeSet;
	changeSet.changes.push_back({Change::Modified, EntityRef(device), false});
	changeSet.stateOnly = true;
	notify(changeSet);
}

Subscription Project::onChanged(std::function<void(const ChangeSet&)> callback)
{
	const std::uint64_t token = m_observers->nextToken++;
	m_observers->callbacks.emplace_back(token, std::move(callback));
	return Subscription(m_observers, token);
}

void Project::notify(const ChangeSet& changeSet)
{
	if (changeSet.empty())
	{
		return; // e.g. insert + erase of the same entity in one transaction
	}
	// Copy: callbacks may (un)subscribe while being notified.
	const auto callbacks = m_observers->callbacks;
	for (const auto& entry : callbacks)
	{
		entry.second(changeSet);
	}
}

template <class T>
void Project::applyRow(const detail::RowOp<T>& op, bool forward)
{
	using Kind = typename detail::RowOp<T>::Kind;
	Table<T>& table = mutableTable<T>();
	switch (op.kind)
	{
		case Kind::Insert:
			if (forward)
				table.insert(op.after, op.pos);
			else
				table.erase(op.after.id);
			break;
		case Kind::Erase:
			if (forward)
				table.erase(op.before.id);
			else
				table.insert(op.before, op.pos);
			break;
		case Kind::Replace:
			table.replace(forward ? op.after : op.before);
			break;
	}
}

void Project::applyOp(const detail::Op& op, bool forward)
{
	std::visit(Overloaded{[&](const detail::SettingsOp& settingsOp)
				   { m_state.settings = forward ? settingsOp.after : settingsOp.before; },
				   [&](const auto& rowOp) { applyRow(rowOp, forward); }},
		op);
	m_index.reset();
}

void Project::rollback(std::vector<detail::Op>& ops, std::size_t mark)
{
	while (ops.size() > mark)
	{
		applyOp(ops.back(), false);
		ops.pop_back();
	}
}

void Project::replay(const std::vector<detail::Op>& ops, bool forward)
{
	if (forward)
	{
		for (const auto& op : ops)
			applyOp(op, true);
	}
	else
	{
		for (auto it = ops.rbegin(); it != ops.rend(); ++it)
			applyOp(*it, false);
	}
	ChangeSet changeSet = ChangeSet::normalized(changesOf(ops, forward));
	changeSet.fromUndo = true;
	notify(changeSet);
}

std::ostream& operator<<(std::ostream& os, const Project& project)
{
	return os << "Project{" << project.settings() << " parts=" << project.buildingParts().size()
			  << " areas=" << project.areas().size() << " lines=" << project.lines().size()
			  << " devices=" << project.devices().size() << " comObjects=" << project.comObjects().size()
			  << " ranges=" << project.groupRanges().size() << " groupAddresses=" << project.groupAddresses().size()
			  << " links=" << project.links().size() << '}';
}

// --- Tx ----------------------------------------------------------------------------------------------------

void Tx::record(detail::Op op)
{
	m_project->applyOp(op, true);
	m_ops.push_back(std::move(op));
}

void Tx::fail(ErrorCode code, const QString& message)
{
	if (!m_error)
	{
		m_error = Error{code, message.toStdString()};
	}
}

void Tx::setSettings(ProjectSettings settings)
{
	if (m_error || settings == m_project->settings())
	{
		return;
	}
	record(detail::SettingsOp{m_project->settings(), std::move(settings)});
}

void Tx::setParam(Id<Device> device, QString ref, std::optional<QString> value)
{
	if (m_error)
	{
		return;
	}
	const Device* current = m_project->devices().find(device);
	if (!current)
	{
		fail(ErrorCode::NotFound, u"setParam: unknown id "_s + device.toString());
		return;
	}
	Device updated = *current;
	if (value)
	{
		updated.params[ref] = std::move(*value);
	}
	else
	{
		updated.params.erase(ref);
	}
	replace(std::move(updated));
}

} // namespace kestrel::model
