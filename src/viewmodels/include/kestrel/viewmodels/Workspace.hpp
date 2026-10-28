#pragma once

// Composition root of the view-model layer: owns the services (ProjectSession, SelectionService,
// ActionRegistry) and the dock view models, registers the model-level actions and computes their enablement
// from the selection and the project. The shell adds its own actions (file, view, help) to the same registry.
//
// Destruction order: panel view models first, then the services (members are declared in that order).

#include "kestrel/model/Id.hpp"
#include "kestrel/viewmodels/PanelVM.hpp"

#include <QObject>

#include <memory>

namespace kestrel::app
{
class BusSession;
}

namespace kestrel::viewmodels
{

class ActionRegistry;
class BusConnectionVM;
class GroupMonitorVM;
class BuildingsPanelVM;
class DeviceEditorVM;
class GroupAddressPanelVM;
class ProblemsPanelVM;
class ProjectSession;
class PropertiesPanelVM;
class SelectionService;
class TopologyPanelVM;
class WelcomeVM;

class Workspace : public QObject
{
	Q_OBJECT

public:
	explicit Workspace(QObject* parent = nullptr);
	~Workspace() override;

	Services services() const;
	ProjectSession* session() const { return m_session.get(); }
	SelectionService* selection() const { return m_selection.get(); }
	ActionRegistry* actions() const { return m_actions.get(); }
	BuildingsPanelVM* buildings() const { return m_buildings.get(); }
	TopologyPanelVM* topology() const { return m_topology.get(); }
	GroupAddressPanelVM* groupAddresses() const { return m_groupAddresses.get(); }
	PropertiesPanelVM* properties() const { return m_properties.get(); }
	ProblemsPanelVM* problems() const { return m_problems.get(); }
	WelcomeVM* welcome() const { return m_welcome.get(); }
	app::BusSession* bus() const { return m_bus.get(); }
	BusConnectionVM* busConnection() const { return m_busConnection.get(); }

	/// A new editor for one device; the caller owns it.
	std::unique_ptr<DeviceEditorVM> createDeviceEditor(model::Id<model::Device> device);
	/// A new group monitor over the bus session; the caller owns it.
	std::unique_ptr<GroupMonitorVM> createGroupMonitor();

	/// Recomputes text and enablement of the model-level actions.
	void updateActions();

Q_SIGNALS:
	/// "device.open" or activation (double-click / Enter) of a device or one of its group objects.
	void openDeviceRequested(kestrel::model::Id<kestrel::model::Device> device);
	/// "bus.groupMonitor"
	void openGroupMonitorRequested();

private:
	void registerActions();
	void selectCreated(const QString& ref);
	std::optional<model::Id<model::Device>> deviceOf(std::optional<model::EntityRef> ref) const;
	/// Tells the bus session the DPT of every group address so that monitored telegrams decode.
	void syncGroupAddressDpts();

	// Services first: they are destroyed last.
	std::unique_ptr<ProjectSession> m_session;
	std::unique_ptr<SelectionService> m_selection;
	std::unique_ptr<ActionRegistry> m_actions;
	std::unique_ptr<app::BusSession> m_bus;
	std::unique_ptr<BuildingsPanelVM> m_buildings;
	std::unique_ptr<TopologyPanelVM> m_topology;
	std::unique_ptr<GroupAddressPanelVM> m_groupAddresses;
	std::unique_ptr<PropertiesPanelVM> m_properties;
	std::unique_ptr<ProblemsPanelVM> m_problems;
	std::unique_ptr<WelcomeVM> m_welcome;
	std::unique_ptr<BusConnectionVM> m_busConnection;
};

} // namespace kestrel::viewmodels
