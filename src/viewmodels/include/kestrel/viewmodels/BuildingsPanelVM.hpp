#pragma once

// Buildings dock (FR-BLD-1/2).

#include "kestrel/viewmodels/PanelVM.hpp"

namespace kestrel::viewmodels
{

class BuildingTreeModel;

class BuildingsPanelVM : public TreePanelVM
{
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("View models are created by the application")

public:
	explicit BuildingsPanelVM(const Services& services, QObject* parent = nullptr);

	BuildingTreeModel* buildingModel() const;

	/// Adds a building part below `parentRef` (empty = top level). `kind`: "Building", "Floor", "Room",
	/// "Corridor", "Stairway", "DistributionBoard". An empty name picks a default. Returns the new ref or "".
	Q_INVOKABLE QString addBuildingPart(const QString& parentRef, const QString& kind, const QString& name = {});
	Q_INVOKABLE bool placeDevice(const QString& deviceRef, const QString& placeRef);
	Q_INVOKABLE bool unplaceDevice(const QString& deviceRef);

	QStringList contextActions(model::EntityRef ref) const override;
};

} // namespace kestrel::viewmodels
