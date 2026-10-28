#pragma once

// Group objects of one device (FR-GA-3, FR-DEV-2), sorted by number.
// Columns: # | Name | Function | Size | Flags | DPT | Group addresses (sending address first).

#include "kestrel/viewmodels/ShadowTreeModel.hpp"

namespace kestrel::viewmodels
{

class ComObjectTableModel : public ShadowTreeModel
{
	Q_OBJECT

public:
	enum Column
	{
		NumberColumn,
		NameColumn,
		FunctionColumn,
		SizeColumn,
		FlagsColumn,
		DptColumn,
		GroupAddressesColumn,
	};

	ComObjectTableModel(ProjectSession* session, model::Id<model::Device> device, QObject* parent = nullptr);

	model::Id<model::Device> device() const { return m_device; }
	/// "1/2/3, 1/2/4": linked group addresses, sending first.
	QString groupAddressesText(model::Id<model::ComObject> object) const;

protected:
	model::EntityRef rootRef() const override { return m_device; }
	bool isShown(model::EntityKind kind) const override;
	std::vector<model::EntityRef> childrenOf(model::EntityRef parent) const override;
	std::optional<model::EntityRef> parentOf(model::EntityRef ref) const override;
	QVariant cellData(model::EntityRef ref, int column, int role) const override;
	bool sortedByContent() const override { return true; }
	void collectRelated(const model::ChangeSet& changeSet, std::set<model::EntityRef>& refresh, bool& all) const override;
	bool isEditable(model::EntityRef ref, int column) const override;
	/// "inactive" (always false: manual objects are always active), shared with CatalogComObjectModel.
	QHash<int, QByteArray> extraRoleNames() const override { return {{ExtraRole, "inactive"}}; }

private:
	model::Id<model::Device> m_device;
};

} // namespace kestrel::viewmodels
