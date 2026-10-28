#pragma once

// Topology dock (FR-TOP-1): areas, lines, devices.

#include "kestrel/viewmodels/PanelVM.hpp"

#include <functional>

namespace kestrel::viewmodels
{

class TopologyTreeModel;

class TopologyPanelVM : public TreePanelVM
{
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("View models are created by the application")

public:
	explicit TopologyPanelVM(const Services& services, QObject* parent = nullptr);

	TopologyTreeModel* topologyModel() const;

	/// Adds an area with the lowest free number >= 1. Returns the new ref or "".
	Q_INVOKABLE QString addArea(const QString& name = {});
	/// Adds a line to the area of `ref` (an area, line or device) with the lowest free number >= 1.
	Q_INVOKABLE QString addLine(const QString& ref, const QString& name = {});
	/// Adds a device to the line of `ref` (a line or device) with the lowest free number >= 1.
	Q_INVOKABLE QString addDevice(const QString& ref, const QString& name = {});
	/// Empty text removes the number (device not addressed yet).
	Q_INVOKABLE bool setDeviceNumber(const QString& deviceRef, const QString& number);

	/// Area of an area / line / device ref.
	std::optional<model::Id<model::Area>> areaFor(model::EntityRef ref) const;
	/// Line of a line / device ref.
	std::optional<model::Id<model::Line>> lineFor(model::EntityRef ref) const;

	QStringList contextActions(model::EntityRef ref) const override;

	/// Handler of catalog products dropped on a line or device (format CatalogPanelVM::kMimeType, data = key).
	using CatalogDropHandler = std::function<bool(const QString& key, model::Id<model::Line> line)>;
	void setCatalogDropHandler(CatalogDropHandler handler) { m_catalogDrop = std::move(handler); }
	QStringList dropFormats() const override;
	bool acceptsDrop(const QModelIndex& index, const QString& format) const override;
	bool dropOn(const QModelIndex& index, const QString& format, const QString& data) override;

private:
	CatalogDropHandler m_catalogDrop;
};

} // namespace kestrel::viewmodels
