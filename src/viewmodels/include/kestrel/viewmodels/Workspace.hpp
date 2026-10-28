#pragma once

// Composition root of the view-model layer: owns the services (ProjectSession, SelectionService,
// ActionRegistry) and the dock view models, registers the model-level actions and computes their enablement
// from the selection and the project. The shell adds its own actions (file, view, help) to the same registry.
//
// Destruction order: panel view models first, then the services (members are declared in that order).

#include "kestrel/model/Id.hpp"
#include "kestrel/viewmodels/ConfigurationHooks.hpp"
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
class CatalogPanelVM;
class CatalogService;
class CommissioningVM;
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
	/// `catalogDirectory` empty = CatalogService::defaultDirectory().
	explicit Workspace(QObject* parent = nullptr, const QString& catalogDirectory = {});
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
	CatalogService* catalog() const { return m_catalog.get(); }
	CatalogPanelVM* catalogPanel() const { return m_catalogPanel.get(); }
	CommissioningVM* commissioning() const { return m_commissioning.get(); }

	// --- Hooks for the configuration engine (kestrel::appconfig) -------------------------------------------
	/// Chooses the active com objects shown by device editors opened from now on (default: activeComObjects,
	/// i.e. appconfig::evaluate; an empty function restores it).
	void setActiveComObjectFilter(ActiveComObjectFilter filter);
	const ActiveComObjectFilter& activeComObjectFilter() const { return m_comObjectFilter; }
	/// Builds download plans (default: catalogDownloadPlanner(catalog()); an empty function removes the planner and
	/// "Download" then reports CommissioningVM::noPlannerText()).
	void setDownloadPlanner(DownloadPlanner planner);

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
	/// "catalog.import": the shell asks for files and calls catalogPanel()->importFiles().
	void importCatalogRequested();
	/// "bus.connect": the shell shows the bus connection dock.
	void showBusConnectionRequested();

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
	std::unique_ptr<CatalogService> m_catalog;
	ActiveComObjectFilter m_comObjectFilter;
	std::unique_ptr<BuildingsPanelVM> m_buildings;
	std::unique_ptr<TopologyPanelVM> m_topology;
	std::unique_ptr<GroupAddressPanelVM> m_groupAddresses;
	std::unique_ptr<PropertiesPanelVM> m_properties;
	std::unique_ptr<ProblemsPanelVM> m_problems;
	std::unique_ptr<WelcomeVM> m_welcome;
	std::unique_ptr<BusConnectionVM> m_busConnection;
	std::unique_ptr<CatalogPanelVM> m_catalogPanel;
	std::unique_ptr<CommissioningVM> m_commissioning;
};

} // namespace kestrel::viewmodels
