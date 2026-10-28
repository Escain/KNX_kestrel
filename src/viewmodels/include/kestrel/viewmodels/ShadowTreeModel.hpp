#pragma once

// Base of the project item models (specs/brainstorm/round2-ui.md §2c).
//
// The model keeps a *shadow tree*: lightweight nodes {EntityRef, parent, children}. data() reads the live
// Project by ref. Each ChangeSet is applied incrementally:
//   - the parents whose children may have changed (added / removed / moved / re-sorted entities) are
//     re-synchronised against the live project with a keyed diff that emits beginRemoveRows /
//     beginInsertRows / beginMoveRows (moves keep the views' expansion and selection);
//   - other modified entities get dataChanged for their row.
// A model reset happens only when the project is replaced, or for bulk changes (> kBulkThreshold structural
// changes in one ChangeSet).
//
// Subclasses describe the tree: which kinds they show, the ordered live children of a node, the live parent of
// an entity, and the cell data.

#include "kestrel/model/ChangeSet.hpp"
#include "kestrel/model/Id.hpp"
#include "kestrel/model/Project.hpp"

#include <QAbstractItemModel>
#include <QStringList>

#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace kestrel::viewmodels
{

class ProjectSession;

class ShadowTreeModel : public QAbstractItemModel
{
	Q_OBJECT

public:
	enum Role
	{
		RefRole = Qt::UserRole + 1, ///< entity ref text ("dev-…")
		KindRole,                   ///< "Device", "Area", … (model::toString(EntityKind))
		NameRole,
		AddressRole,
		DptRole,
		DescriptionRole,
		IconRole, ///< icon theme name
		ExtraRole, ///< first role free for subclasses
	};
	Q_ENUM(Role)

	static constexpr std::size_t kBulkThreshold = 500;

	~ShadowTreeModel() override;

	QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
	QModelIndex parent(const QModelIndex& child) const override;
	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
	Qt::ItemFlags flags(const QModelIndex& index) const override;
	QHash<int, QByteArray> roleNames() const override;

	model::EntityRef refAt(const QModelIndex& index) const;
	/// Index (column 0) of the entity's row; invalid if the entity is not shown by this model.
	QModelIndex indexOf(model::EntityRef ref, int column = 0) const;
	Q_INVOKABLE QString refString(const QModelIndex& index) const;
	Q_INVOKABLE QModelIndex indexOfRef(const QString& ref) const;

	/// Rebuilds the shadow tree from the project (model reset).
	void rebuild();

protected:
	ShadowTreeModel(ProjectSession* session, QStringList columns, QObject* parent);

	ProjectSession* session() const { return m_session; }
	const model::Project& project() const;

	/// Ref of the invisible root (EntityRef::project() for project trees).
	virtual model::EntityRef rootRef() const { return model::EntityRef::project(); }
	/// True for the entity kinds that appear as rows.
	virtual bool isShown(model::EntityKind kind) const = 0;
	/// Ordered live children of `parent` (root or a shown entity). Empty for unknown refs.
	virtual std::vector<model::EntityRef> childrenOf(model::EntityRef parent) const = 0;
	/// Live parent of an existing shown entity (rootRef() for top-level rows); nullopt if not shown.
	virtual std::optional<model::EntityRef> parentOf(model::EntityRef ref) const = 0;
	/// Cell data of an existing entity.
	virtual QVariant cellData(model::EntityRef ref, int column, int role) const = 0;
	/// True if a non-structural modification can change the sibling order (sorted models).
	virtual bool sortedByContent() const { return false; }
	/// Adds rows to refresh because of changes to other entity kinds; sets `all` to refresh everything.
	virtual void collectRelated(const model::ChangeSet& changeSet, std::set<model::EntityRef>& refresh, bool& all) const;
	virtual QHash<int, QByteArray> extraRoleNames() const { return {}; }
	virtual bool isEditable(model::EntityRef ref, int column) const;

private:
	struct Node
	{
		model::EntityRef ref;
		Node* parent = nullptr;
		int row = 0;
		std::vector<std::unique_ptr<Node>> children;
	};

	Node* nodeOf(const QModelIndex& index) const;
	Node* find(model::EntityRef ref) const;
	QModelIndex indexOfNode(const Node* node) const;
	void applyChangeSet(const model::ChangeSet& changeSet);
	void buildChildren(Node* node, std::vector<Node*>* conflicts);
	void syncChildren(Node* parent);
	void insertNode(Node* parent, int row, model::EntityRef ref);
	void removeNode(Node* node);
	void moveNode(Node* node, Node* target, int row);
	void unindex(Node* node);
	void enqueue(model::EntityRef ref);
	void emitRowChanged(const Node* node);
	void emitSubtreeChanged(const Node* node);
	static void renumber(Node* parent, std::size_t from);
	static bool isWithin(const Node* node, const Node* ancestor);

	ProjectSession* m_session;
	QStringList m_columns;
	std::unique_ptr<Node> m_root;
	std::map<model::EntityRef, Node*> m_index;
	std::deque<model::EntityRef> m_queue;
	std::set<model::EntityRef> m_queued;
	bool m_resetting = false;
};

} // namespace kestrel::viewmodels
