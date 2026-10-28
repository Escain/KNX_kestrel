#pragma once

// Topology tree (FR-TOP-1): areas -> lines -> devices, each level sorted by number (devices without a number
// last). Columns: Name | Address | Medium.

#include "kestrel/viewmodels/ShadowTreeModel.hpp"

namespace kestrel::viewmodels
{

class TopologyTreeModel : public ShadowTreeModel
{
	Q_OBJECT

public:
	explicit TopologyTreeModel(ProjectSession* session, QObject* parent = nullptr);

protected:
	bool isShown(model::EntityKind kind) const override;
	std::vector<model::EntityRef> childrenOf(model::EntityRef parent) const override;
	std::optional<model::EntityRef> parentOf(model::EntityRef ref) const override;
	QVariant cellData(model::EntityRef ref, int column, int role) const override;
	bool sortedByContent() const override { return true; }
	void collectRelated(const model::ChangeSet& changeSet, std::set<model::EntityRef>& refresh, bool& all) const override;
};

} // namespace kestrel::viewmodels
