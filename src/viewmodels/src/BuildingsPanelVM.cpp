#include "kestrel/viewmodels/BuildingsPanelVM.hpp"

#include "kestrel/model/Operations.hpp"
#include "kestrel/viewmodels/ActionRegistry.hpp"
#include "kestrel/viewmodels/BuildingTreeModel.hpp"
#include "kestrel/viewmodels/Format.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

BuildingsPanelVM::BuildingsPanelVM(const Services& services, QObject* parent)
	: TreePanelVM(services, u"buildings"_s, parent)
{
	setModel(new BuildingTreeModel(services.session));
}

BuildingTreeModel* BuildingsPanelVM::buildingModel() const
{
	return static_cast<BuildingTreeModel*>(treeModel());
}

QString BuildingsPanelVM::addBuildingPart(const QString& parentRef, const QString& kind, const QString& name)
{
	std::optional<model::Id<model::BuildingPart>> parent;
	if (!parentRef.isEmpty())
	{
		const auto ref = parseRef(parentRef);
		if (!ref)
			return {};
		if (!ref->is<model::BuildingPart>())
		{
			setError(tr("Building parts can only be added to building parts."));
			return {};
		}
		parent = ref->as<model::BuildingPart>();
	}
	const auto partKind = model::parseBuildingPartKind(kind);
	if (!partKind)
	{
		setError(tr("Unknown building part kind: %1").arg(kind));
		return {};
	}
	QString text = name.trimmed();
	if (text.isEmpty())
	{
		const auto siblings = project().buildingPartsIn(parent);
		text = u"%1 %2"_s.arg(buildingPartKindName(*partKind)).arg(siblings.size() + 1);
	}
	const auto id = check(model::addBuildingPart(project(), parent, *partKind, text));
	return id ? id->toString() : QString();
}

bool BuildingsPanelVM::placeDevice(const QString& deviceRef, const QString& placeRef)
{
	const auto device = parseRef(deviceRef);
	const auto place = device ? parseRef(placeRef) : std::nullopt;
	if (!device || !place)
		return false;
	if (!device->is<model::Device>() || !place->is<model::BuildingPart>())
	{
		setError(tr("Only devices can be placed, and only in building parts."));
		return false;
	}
	return check(model::placeDevice(project(), device->as<model::Device>(), place->as<model::BuildingPart>()));
}

bool BuildingsPanelVM::unplaceDevice(const QString& deviceRef)
{
	const auto device = parseRef(deviceRef);
	if (!device || !device->is<model::Device>())
	{
		setError(tr("Not a device: %1").arg(deviceRef));
		return false;
	}
	return check(model::placeDevice(project(), device->as<model::Device>(), std::nullopt));
}

QStringList BuildingsPanelVM::contextActions(model::EntityRef ref) const
{
	const QString sep = ActionRegistry::kSeparator;
	switch (ref.kind)
	{
		case model::EntityKind::BuildingPart:
			return {u"building.addFloor"_s, u"building.addRoom"_s, sep, u"edit.rename"_s, u"edit.delete"_s};
		case model::EntityKind::Device:
			return {u"device.open"_s, u"building.unplaceDevice"_s, sep, u"device.programAddress"_s, u"device.download"_s,
				u"device.readInfo"_s, sep, u"edit.rename"_s, u"edit.delete"_s};
		default: return {u"building.addBuilding"_s};
	}
}

} // namespace kestrel::viewmodels
