#pragma once

// Group address tree (FR-GA-1/2): group ranges (main / middle groups) sorted by start, then the group
// addresses of each range sorted by address; addresses rendered in the project's gaStyle.
// Columns: Name | Address | DPT.

#include "kestrel/viewmodels/ShadowTreeModel.hpp"

namespace kestrel::viewmodels
{

class GroupAddressTreeModel : public ShadowTreeModel
{
	Q_OBJECT

public:
	explicit GroupAddressTreeModel(ProjectSession* session, QObject* parent = nullptr);

protected:
	bool isShown(model::EntityKind kind) const override;
	std::vector<model::EntityRef> childrenOf(model::EntityRef parent) const override;
	std::optional<model::EntityRef> parentOf(model::EntityRef ref) const override;
	QVariant cellData(model::EntityRef ref, int column, int role) const override;
	bool sortedByContent() const override { return true; }
	void collectRelated(const model::ChangeSet& changeSet, std::set<model::EntityRef>& refresh, bool& all) const override;
};

} // namespace kestrel::viewmodels
