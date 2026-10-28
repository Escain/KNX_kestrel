#pragma once

// Buildings tree (FR-BLD-1/2): building parts in sibling order, then the devices placed in each part.
// Columns: Name | Address (individual address of placed devices).

#include "kestrel/viewmodels/ShadowTreeModel.hpp"

namespace kestrel::viewmodels
{

class BuildingTreeModel : public ShadowTreeModel
{
	Q_OBJECT

public:
	explicit BuildingTreeModel(ProjectSession* session, QObject* parent = nullptr);

protected:
	bool isShown(model::EntityKind kind) const override;
	std::vector<model::EntityRef> childrenOf(model::EntityRef parent) const override;
	std::optional<model::EntityRef> parentOf(model::EntityRef ref) const override;
	QVariant cellData(model::EntityRef ref, int column, int role) const override;
	void collectRelated(const model::ChangeSet& changeSet, std::set<model::EntityRef>& refresh, bool& all) const override;
};

} // namespace kestrel::viewmodels
