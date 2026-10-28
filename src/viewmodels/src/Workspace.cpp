#include "kestrel/viewmodels/Workspace.hpp"

#include "kestrel/app/BusSession.hpp"
#include "kestrel/dpt/Dpt.hpp"
#include "kestrel/knx/Address.hpp"
#include "kestrel/viewmodels/BusConnectionVM.hpp"
#include "kestrel/viewmodels/GroupMonitorVM.hpp"
#include "kestrel/viewmodels/ActionRegistry.hpp"
#include "kestrel/viewmodels/BuildingsPanelVM.hpp"
#include "kestrel/viewmodels/CatalogPanelVM.hpp"
#include "kestrel/viewmodels/CatalogService.hpp"
#include "kestrel/viewmodels/CommissioningVM.hpp"
#include "kestrel/viewmodels/DeviceEditorVM.hpp"
#include "kestrel/viewmodels/Format.hpp"
#include "kestrel/viewmodels/GroupAddressPanelVM.hpp"
#include "kestrel/viewmodels/ProblemsPanelVM.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"
#include "kestrel/viewmodels/PropertiesPanelVM.hpp"
#include "kestrel/viewmodels/SelectionService.hpp"
#include "kestrel/viewmodels/TopologyPanelVM.hpp"
#include "kestrel/viewmodels/WelcomeVM.hpp"

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

using model::EntityKind;
using model::EntityRef;

Workspace::Workspace(QObject* parent, const QString& catalogDirectory)
	: QObject(parent)
	, m_session(std::make_unique<ProjectSession>())
	, m_selection(std::make_unique<SelectionService>())
	, m_actions(std::make_unique<ActionRegistry>())
	, m_bus(std::make_unique<app::BusSession>())
	, m_catalog(std::make_unique<CatalogService>(catalogDirectory))
	, m_comObjectFilter(&activeComObjects)
{
	const Services s = services();
	m_buildings = std::make_unique<BuildingsPanelVM>(s);
	m_topology = std::make_unique<TopologyPanelVM>(s);
	m_groupAddresses = std::make_unique<GroupAddressPanelVM>(s);
	m_properties = std::make_unique<PropertiesPanelVM>(s);
	m_problems = std::make_unique<ProblemsPanelVM>(s);
	m_welcome = std::make_unique<WelcomeVM>(s);
	m_busConnection = std::make_unique<BusConnectionVM>(s, m_bus.get());
	m_catalogPanel = std::make_unique<CatalogPanelVM>(s, m_catalog.get());
	m_commissioning = std::make_unique<CommissioningVM>(s, m_bus.get());
	m_commissioning->setDownloadPlanner(catalogDownloadPlanner(m_catalog.get()));
	m_topology->setCatalogDropHandler(
		[this](const QString& key, model::Id<model::Line> line) { return m_catalogPanel->addToLine(key, line); });

	connect(m_session.get(), &ProjectSession::changed, this,
		[this](const model::ChangeSet& changeSet)
		{
			if (changeSet.touches(EntityKind::GroupAddress))
				syncGroupAddressDpts();
		});
	connect(m_session.get(), &ProjectSession::projectReplaced, this, &Workspace::syncGroupAddressDpts);
	syncGroupAddressDpts();

	connect(m_session.get(), &ProjectSession::changed, m_selection.get(), &SelectionService::prune);
	connect(m_session.get(), &ProjectSession::projectReplaced, m_selection.get(), &SelectionService::clear);
	connect(m_selection.get(), &SelectionService::activated, this,
		[this](EntityRef ref)
		{
			if (const auto device = deviceOf(ref))
				Q_EMIT openDeviceRequested(*device);
		});

	registerActions();
	connect(m_selection.get(), &SelectionService::selectionChanged, this, &Workspace::updateActions);
	connect(m_session.get(), &ProjectSession::undoStateChanged, this, &Workspace::updateActions);
	connect(m_session.get(), &ProjectSession::projectReplaced, this, &Workspace::updateActions);
	connect(m_session.get(), &ProjectSession::changed, this, &Workspace::updateActions);
	connect(m_bus.get(), &app::BusSession::stateChanged, this, &Workspace::updateActions);
	connect(m_busConnection.get(), &BusConnectionVM::interfacesChanged, this, &Workspace::updateActions);
	connect(m_commissioning.get(), &CommissioningVM::operationChanged, this, &Workspace::updateActions);
	connect(m_catalogPanel.get(), &CatalogPanelVM::targetChanged, this, &Workspace::updateActions);
	connect(m_catalogPanel.get(), &CatalogPanelVM::currentChanged, this, &Workspace::updateActions);
	updateActions();
}

Workspace::~Workspace()
{
	// View models disconnect from the services before those go away.
	m_commissioning.reset();
	m_catalogPanel.reset();
	m_busConnection.reset();
	m_welcome.reset();
	m_problems.reset();
	m_properties.reset();
	m_groupAddresses.reset();
	m_topology.reset();
	m_buildings.reset();
}

Services Workspace::services() const
{
	return {m_session.get(), m_selection.get(), m_actions.get()};
}

std::unique_ptr<DeviceEditorVM> Workspace::createDeviceEditor(model::Id<model::Device> device)
{
	return std::make_unique<DeviceEditorVM>(services(), device, m_catalog.get(), m_comObjectFilter);
}

void Workspace::setActiveComObjectFilter(ActiveComObjectFilter filter)
{
	m_comObjectFilter = filter ? std::move(filter) : ActiveComObjectFilter(&activeComObjects);
}

void Workspace::setDownloadPlanner(DownloadPlanner planner)
{
	m_commissioning->setDownloadPlanner(std::move(planner));
	updateActions();
}

std::unique_ptr<GroupMonitorVM> Workspace::createGroupMonitor()
{
	return std::make_unique<GroupMonitorVM>(services(), m_bus.get());
}

void Workspace::syncGroupAddressDpts()
{
	m_bus->clearGroupAddressDpts();
	for (const model::GroupAddress& address : m_session->project().groupAddresses())
	{
		if (address.dpt.isEmpty())
			continue;
		if (const auto id = dpt::DptId::parse(address.dpt.toStdString()))
			m_bus->setGroupAddressDpt(knx::GroupAddress(address.raw), *id);
	}
}

std::optional<model::Id<model::Device>> Workspace::deviceOf(std::optional<EntityRef> ref) const
{
	if (!ref || !m_session->project().contains(*ref))
		return std::nullopt;
	if (ref->is<model::Device>())
		return ref->as<model::Device>();
	if (ref->is<model::ComObject>())
		return m_session->project().comObjects().at(ref->as<model::ComObject>()).device;
	return std::nullopt;
}

void Workspace::selectCreated(const QString& ref)
{
	if (!ref.isEmpty())
		m_selection->selectRef(ref, u"actions"_s);
}

void Workspace::registerActions()
{
	ActionRegistry& a = *m_actions;
	auto current = [this] { return m_selection->currentText(); };

	connect(a.add(u"edit.undo"_s, tr("&Undo"), QKeySequence::Undo), &QAction::triggered, this, [this] { m_session->undo(); });
	connect(a.add(u"edit.redo"_s, tr("&Redo"), QKeySequence::Redo), &QAction::triggered, this, [this] { m_session->redo(); });
	connect(a.add(u"edit.rename"_s, tr("Re&name"), QKeySequence(Qt::Key_F2)), &QAction::triggered, m_selection.get(),
		&SelectionService::requestRename);
	connect(a.add(u"edit.delete"_s, tr("&Delete"), QKeySequence::Delete), &QAction::triggered, this,
		[this]
		{
			if (const auto ref = m_selection->current())
			{
				const Status status = removeEntity(m_session->project(), *ref);
				if (!status)
					m_session->reportError(errorText(status.error()));
			}
		});

	connect(a.add(u"building.addBuilding"_s, tr("Add &Building")), &QAction::triggered, this,
		[this] { selectCreated(m_buildings->addBuildingPart({}, u"Building"_s)); });
	connect(a.add(u"building.addFloor"_s, tr("Add &Floor")), &QAction::triggered, this,
		[this, current] { selectCreated(m_buildings->addBuildingPart(current(), u"Floor"_s)); });
	connect(a.add(u"building.addRoom"_s, tr("Add &Room")), &QAction::triggered, this,
		[this, current] { selectCreated(m_buildings->addBuildingPart(current(), u"Room"_s)); });
	connect(a.add(u"building.unplaceDevice"_s, tr("Remove from Building &Part")), &QAction::triggered, this,
		[this, current] { m_buildings->unplaceDevice(current()); });

	connect(a.add(u"topology.addArea"_s, tr("Add &Area")), &QAction::triggered, this,
		[this] { selectCreated(m_topology->addArea()); });
	connect(a.add(u"topology.addLine"_s, tr("Add &Line")), &QAction::triggered, this,
		[this, current] { selectCreated(m_topology->addLine(current())); });
	connect(a.add(u"topology.addDevice"_s, tr("Add &Device")), &QAction::triggered, this,
		[this, current] { selectCreated(m_topology->addDevice(current())); });

	connect(a.add(u"ga.addMainGroup"_s, tr("Add &Main Group")), &QAction::triggered, this,
		[this] { selectCreated(m_groupAddresses->addMainGroup()); });
	connect(a.add(u"ga.addMiddleGroup"_s, tr("Add M&iddle Group")), &QAction::triggered, this,
		[this, current] { selectCreated(m_groupAddresses->addMiddleGroup(current())); });
	connect(a.add(u"ga.addAddress"_s, tr("Add &Group Address")), &QAction::triggered, this,
		[this, current]
		{
			const auto ref = m_selection->current();
			const bool inRange = ref && (ref->is<model::GroupRange>() || ref->is<model::GroupAddress>());
			selectCreated(m_groupAddresses->addGroupAddress(inRange ? current() : QString()));
		});

	connect(a.add(u"catalog.import"_s, tr("&Import Product File…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I)),
		&QAction::triggered, this, &Workspace::importCatalogRequested);
	connect(a.add(u"catalog.addToLine"_s, tr("Add Product to &Line"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A)),
		&QAction::triggered, this, [this] { m_catalogPanel->addToLine(); });

	connect(a.add(u"bus.connect"_s, tr("&Connect…")), &QAction::triggered, this, &Workspace::showBusConnectionRequested);
	connect(a.add(u"bus.discover"_s, tr("&Search Interfaces")), &QAction::triggered, this,
		[this]
		{
			Q_EMIT showBusConnectionRequested();
			m_busConnection->discover();
		});
	connect(a.add(u"bus.disconnect"_s, tr("&Disconnect")), &QAction::triggered, m_busConnection.get(),
		&BusConnectionVM::disconnectFromBus);
	connect(a.add(u"bus.groupMonitor"_s, tr("&Group Monitor"), QKeySequence(Qt::CTRL | Qt::Key_M)), &QAction::triggered,
		this, &Workspace::openGroupMonitorRequested);
	connect(a.add(u"bus.programmingMode"_s, tr("Show Devices in Programming &Mode")), &QAction::triggered,
		m_commissioning.get(), &CommissioningVM::readProgrammingMode);
	connect(a.add(u"device.programAddress"_s, tr("&Program Individual Address"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P)),
		&QAction::triggered, this, [this] { m_commissioning->programAddress(); });
	connect(a.add(u"device.download"_s, tr("&Download Application"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D)),
		&QAction::triggered, this, [this] { m_commissioning->download(); });
	connect(a.add(u"device.readInfo"_s, tr("Read Device &Info")), &QAction::triggered, this,
		[this] { m_commissioning->readDeviceInfo(); });
	connect(a.add(u"device.cancelOperation"_s, tr("&Cancel Bus Operation")), &QAction::triggered,
		m_commissioning.get(), &CommissioningVM::cancel);

	connect(a.add(u"device.open"_s, tr("&Open Device Editor"), QKeySequence(Qt::CTRL | Qt::Key_E)), &QAction::triggered,
		this,
		[this]
		{
			if (const auto device = deviceOf(m_selection->current()))
				Q_EMIT openDeviceRequested(*device);
		});
}

void Workspace::updateActions()
{
	ActionRegistry& a = *m_actions;
	const model::Project& project = m_session->project();
	const auto ref = m_selection->current();
	const bool alive = ref && project.contains(*ref);
	const EntityKind kind = alive ? ref->kind : EntityKind::Project;

	QAction* undo = a.action(u"edit.undo"_s);
	undo->setEnabled(m_session->canUndo());
	undo->setText(m_session->canUndo() ? tr("&Undo %1").arg(m_session->undoText()) : tr("&Undo"));
	QAction* redo = a.action(u"edit.redo"_s);
	redo->setEnabled(m_session->canRedo());
	redo->setText(m_session->canRedo() ? tr("&Redo %1").arg(m_session->redoText()) : tr("&Redo"));

	a.setEnabled(u"edit.delete"_s, alive && kind != EntityKind::Project);
	a.setEnabled(u"edit.rename"_s, alive && kind != EntityKind::Project && kind != EntityKind::Link);

	a.setEnabled(u"building.addBuilding"_s, true);
	a.setEnabled(u"building.addFloor"_s, kind == EntityKind::BuildingPart);
	a.setEnabled(u"building.addRoom"_s, kind == EntityKind::BuildingPart);
	a.setEnabled(u"catalog.import"_s, true);
	a.setEnabled(u"catalog.addToLine"_s, m_catalogPanel->canAdd());

	a.setEnabled(u"building.unplaceDevice"_s,
		kind == EntityKind::Device && project.devices().at(ref->as<model::Device>()).place.has_value());

	a.setEnabled(u"topology.addArea"_s, project.areas().size() < model::kMaxAreaNumber);
	a.setEnabled(u"topology.addLine"_s, alive && m_topology->areaFor(*ref).has_value());
	a.setEnabled(u"topology.addDevice"_s, alive && m_topology->lineFor(*ref).has_value());

	a.setEnabled(u"ga.addMainGroup"_s, true);
	a.setEnabled(u"ga.addMiddleGroup"_s,
		kind == EntityKind::GroupRange && project.settings().gaStyle == knx::GroupAddressStyle::ThreeLevel
			&& !project.groupRanges().at(ref->as<model::GroupRange>()).parent.has_value());
	a.setEnabled(u"ga.addAddress"_s, true);

	a.setEnabled(u"device.open"_s, deviceOf(ref).has_value());

	// Commissioning: the selected device (or the device of a selected group object), connected, one at a time.
	const auto target = m_commissioning->targetDevice();
	const bool addressedDevice = target && project.individualAddress(*target).has_value();
	const bool idle = m_bus->isConnected() && !m_commissioning->isOperationActive();
	a.setEnabled(u"bus.connect"_s, true);
	a.setEnabled(u"bus.discover"_s, !m_busConnection->isDiscovering());
	a.setEnabled(u"bus.disconnect"_s, m_bus->state() != knx::LinkState::Disconnected);
	a.setEnabled(u"bus.groupMonitor"_s, true);
	a.setEnabled(u"bus.programmingMode"_s, idle);
	a.setEnabled(u"device.programAddress"_s, addressedDevice && idle);
	a.setEnabled(u"device.readInfo"_s, addressedDevice && idle);
	a.setEnabled(u"device.download"_s,
		addressedDevice && idle && !project.devices().at(*target).appProgramRef.isEmpty());
	a.setEnabled(u"device.cancelOperation"_s, m_commissioning->isCancellable());
}

} // namespace kestrel::viewmodels
