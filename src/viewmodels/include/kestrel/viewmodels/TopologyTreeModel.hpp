#pragma once

// Topology tree (FR-TOP-1): areas -> lines -> devices, each level sorted by number (devices without a number
// last). Columns: Name | Address | Medium | Download (catalog devices: DeviceStatusService::shortText).

#include "kestrel/viewmodels/ShadowTreeModel.hpp"

namespace kestrel::viewmodels
{

class DeviceStatusService;

class TopologyTreeModel : public ShadowTreeModel
{
	Q_OBJECT

public:
	enum Column
	{
		NameColumn,
		AddressColumn,
		MediumColumn,
		DownloadColumn,
	};

	explicit TopologyTreeModel(ProjectSession* session, QObject* parent = nullptr);

	/// Source of the Download column (optional).
	void setDeviceStatus(DeviceStatusService* status);

protected:
	bool isShown(model::EntityKind kind) const override;
	std::vector<model::EntityRef> childrenOf(model::EntityRef parent) const override;
	std::optional<model::EntityRef> parentOf(model::EntityRef ref) const override;
	QVariant cellData(model::EntityRef ref, int column, int role) const override;
	bool sortedByContent() const override { return true; }
	void collectRelated(const model::ChangeSet& changeSet, std::set<model::EntityRef>& refresh, bool& all) const override;

private:
	DeviceStatusService* m_status = nullptr;
};

} // namespace kestrel::viewmodels
