#include "kestrel/viewmodels/ShadowTreeModel.hpp"

#include "kestrel/model/Operations.hpp"
#include "kestrel/viewmodels/Format.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace kestrel::viewmodels
{

ShadowTreeModel::ShadowTreeModel(ProjectSession* session, QStringList columns, QObject* parent)
	: QAbstractItemModel(parent), m_session(session), m_columns(std::move(columns)), m_root(std::make_unique<Node>())
{
	assert(session);
	connect(session, &ProjectSession::aboutToReplaceProject, this,
		[this]
		{
			beginResetModel();
			m_resetting = true;
		});
	connect(session, &ProjectSession::projectReplaced, this,
		[this]
		{
			m_index.clear();
			m_root->children.clear();
			m_root->ref = rootRef();
			m_index.emplace(m_root->ref, m_root.get());
			buildChildren(m_root.get(), nullptr);
			m_resetting = false;
			endResetModel();
		});
	connect(session, &ProjectSession::changed, this, &ShadowTreeModel::applyChangeSet);
}

ShadowTreeModel::~ShadowTreeModel() = default;

const model::Project& ShadowTreeModel::project() const
{
	return m_session->project();
}

void ShadowTreeModel::rebuild()
{
	beginResetModel();
	m_index.clear();
	m_root->children.clear();
	m_root->ref = rootRef();
	m_index.emplace(m_root->ref, m_root.get());
	buildChildren(m_root.get(), nullptr);
	endResetModel();
}

void ShadowTreeModel::refreshEntity(model::EntityRef ref)
{
	if (const Node* node = find(ref); node && node != m_root.get())
	{
		emitRowChanged(node);
	}
}

// --- QAbstractItemModel ------------------------------------------------------------------------------------

ShadowTreeModel::Node* ShadowTreeModel::nodeOf(const QModelIndex& index) const
{
	return index.isValid() ? static_cast<Node*>(index.internalPointer()) : m_root.get();
}

ShadowTreeModel::Node* ShadowTreeModel::find(model::EntityRef ref) const
{
	const auto it = m_index.find(ref);
	return it == m_index.end() ? nullptr : it->second;
}

QModelIndex ShadowTreeModel::indexOfNode(const Node* node) const
{
	if (!node || node == m_root.get())
	{
		return {};
	}
	return createIndex(node->row, 0, const_cast<Node*>(node));
}

QModelIndex ShadowTreeModel::index(int row, int column, const QModelIndex& parent) const
{
	if (row < 0 || column < 0 || column >= m_columns.size() || (parent.isValid() && parent.column() != 0))
	{
		return {};
	}
	const Node* node = nodeOf(parent);
	if (static_cast<std::size_t>(row) >= node->children.size())
	{
		return {};
	}
	return createIndex(row, column, node->children[static_cast<std::size_t>(row)].get());
}

QModelIndex ShadowTreeModel::parent(const QModelIndex& child) const
{
	if (!child.isValid())
	{
		return {};
	}
	return indexOfNode(nodeOf(child)->parent);
}

int ShadowTreeModel::rowCount(const QModelIndex& parent) const
{
	if (parent.isValid() && parent.column() != 0)
	{
		return 0;
	}
	return static_cast<int>(nodeOf(parent)->children.size());
}

int ShadowTreeModel::columnCount(const QModelIndex&) const
{
	return static_cast<int>(m_columns.size());
}

QVariant ShadowTreeModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid())
	{
		return {};
	}
	const model::EntityRef ref = nodeOf(index)->ref;
	if (!project().contains(ref))
	{
		return {};
	}
	switch (role)
	{
		case RefRole: return ref.toString();
		case KindRole: return QString::fromLatin1(model::toString(ref.kind));
		case NameRole: return entityName(project(), ref);
		default: return cellData(ref, index.column(), role);
	}
}

bool ShadowTreeModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
	if (!index.isValid() || (role != Qt::EditRole && role != Qt::DisplayRole && role != NameRole)
		|| !isEditable(nodeOf(index)->ref, index.column()))
	{
		return false;
	}
	const Status status = model::rename(m_session->project(), nodeOf(index)->ref, value.toString().trimmed());
	if (!status)
	{
		m_session->reportError(errorText(status.error()));
	}
	return static_cast<bool>(status);
}

QVariant ShadowTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section >= 0 && section < m_columns.size())
	{
		return m_columns.at(section);
	}
	return {};
}

Qt::ItemFlags ShadowTreeModel::flags(const QModelIndex& index) const
{
	if (!index.isValid())
	{
		return Qt::NoItemFlags;
	}
	Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
	if (isEditable(nodeOf(index)->ref, index.column()))
	{
		result |= Qt::ItemIsEditable;
	}
	return result;
}

QHash<int, QByteArray> ShadowTreeModel::roleNames() const
{
	QHash<int, QByteArray> names = QAbstractItemModel::roleNames();
	names.insert(RefRole, "ref");
	names.insert(KindRole, "kind");
	names.insert(NameRole, "name");
	names.insert(AddressRole, "address");
	names.insert(DptRole, "dpt");
	names.insert(DescriptionRole, "description");
	names.insert(IconRole, "iconName");
	names.insert(extraRoleNames());
	return names;
}

bool ShadowTreeModel::isEditable(model::EntityRef ref, int column) const
{
	return column == 0 && ref.kind != model::EntityKind::Link;
}

void ShadowTreeModel::collectRelated(const model::ChangeSet&, std::set<model::EntityRef>&, bool&) const {}

model::EntityRef ShadowTreeModel::refAt(const QModelIndex& index) const
{
	return index.isValid() ? nodeOf(index)->ref : m_root->ref;
}

QModelIndex ShadowTreeModel::indexOf(model::EntityRef ref, int column) const
{
	const Node* node = find(ref);
	if (!node || node == m_root.get())
	{
		return {};
	}
	return createIndex(node->row, column, const_cast<Node*>(node));
}

QString ShadowTreeModel::refString(const QModelIndex& index) const
{
	return index.isValid() ? nodeOf(index)->ref.toString() : QString();
}

QModelIndex ShadowTreeModel::indexOfRef(const QString& ref) const
{
	const auto parsed = model::EntityRef::parse(ref);
	return parsed ? indexOf(*parsed) : QModelIndex();
}

// --- Shadow tree ---------------------------------------------------------------------------------------------

void ShadowTreeModel::buildChildren(Node* node, std::vector<Node*>* conflicts)
{
	const std::vector<model::EntityRef> refs = childrenOf(node->ref);
	node->children.reserve(refs.size());
	for (const model::EntityRef& ref : refs)
	{
		if (m_index.contains(ref))
		{
			// Still shown elsewhere (moved in the same ChangeSet): the follow-up sync moves it here.
			if (conflicts)
			{
				conflicts->push_back(node);
			}
			continue;
		}
		auto child = std::make_unique<Node>();
		child->ref = ref;
		child->parent = node;
		child->row = static_cast<int>(node->children.size());
		Node* raw = child.get();
		m_index.emplace(ref, raw);
		node->children.push_back(std::move(child));
		buildChildren(raw, conflicts);
	}
}

void ShadowTreeModel::renumber(Node* parent, std::size_t from)
{
	for (std::size_t i = from; i < parent->children.size(); ++i)
	{
		parent->children[i]->row = static_cast<int>(i);
	}
}

bool ShadowTreeModel::isWithin(const Node* node, const Node* ancestor)
{
	for (const Node* n = node; n; n = n->parent)
	{
		if (n == ancestor)
		{
			return true;
		}
	}
	return false;
}

void ShadowTreeModel::unindex(Node* node)
{
	m_index.erase(node->ref);
	for (const auto& child : node->children)
	{
		unindex(child.get());
	}
}

void ShadowTreeModel::enqueue(model::EntityRef ref)
{
	if (m_queued.insert(ref).second)
	{
		m_queue.push_back(ref);
	}
}

void ShadowTreeModel::insertNode(Node* parent, int row, model::EntityRef ref)
{
	std::vector<Node*> conflicts;
	beginInsertRows(indexOfNode(parent), row, row);
	auto node = std::make_unique<Node>();
	node->ref = ref;
	node->parent = parent;
	Node* raw = node.get();
	m_index.emplace(ref, raw);
	buildChildren(raw, &conflicts);
	parent->children.insert(parent->children.begin() + row, std::move(node));
	renumber(parent, static_cast<std::size_t>(row));
	endInsertRows();
	for (const Node* conflict : conflicts)
	{
		enqueue(conflict->ref);
	}
}

void ShadowTreeModel::removeNode(Node* node)
{
	Node* parent = node->parent;
	const int row = node->row;
	beginRemoveRows(indexOfNode(parent), row, row);
	unindex(node);
	parent->children.erase(parent->children.begin() + row);
	renumber(parent, static_cast<std::size_t>(row));
	endRemoveRows();
}

void ShadowTreeModel::moveNode(Node* node, Node* target, int row)
{
	Node* source = node->parent;
	const int sourceRow = node->row;
	if (source == target && (row == sourceRow || row == sourceRow + 1))
	{
		return;
	}
	if (!beginMoveRows(indexOfNode(source), sourceRow, sourceRow, indexOfNode(target), row))
	{
		const model::EntityRef ref = node->ref;
		removeNode(node);
		insertNode(target, std::min(row, static_cast<int>(target->children.size())), ref);
		return;
	}
	std::unique_ptr<Node> owned = std::move(source->children[static_cast<std::size_t>(sourceRow)]);
	source->children.erase(source->children.begin() + sourceRow);
	int insertAt = row;
	if (source == target && row > sourceRow)
	{
		--insertAt;
	}
	owned->parent = target;
	target->children.insert(target->children.begin() + insertAt, std::move(owned));
	if (source == target)
	{
		renumber(source, static_cast<std::size_t>(std::min(sourceRow, insertAt)));
	}
	else
	{
		renumber(source, static_cast<std::size_t>(sourceRow));
		renumber(target, static_cast<std::size_t>(insertAt));
	}
	endMoveRows();
}

void ShadowTreeModel::syncChildren(Node* parent)
{
	const bool alive = parent == m_root.get() || project().contains(parent->ref);
	const std::vector<model::EntityRef> desired = alive ? childrenOf(parent->ref) : std::vector<model::EntityRef>{};
	const std::set<model::EntityRef> wanted(desired.begin(), desired.end());

	// 1. Children that no longer belong here: move them to their new parent if it is shown, else remove.
	for (std::size_t i = parent->children.size(); i-- > 0;)
	{
		Node* child = parent->children[i].get();
		if (wanted.contains(child->ref))
		{
			continue;
		}
		Node* target = nullptr;
		if (project().contains(child->ref))
		{
			if (const auto newParent = parentOf(child->ref))
			{
				target = find(*newParent);
			}
		}
		if (target && target != parent && !isWithin(target, child))
		{
			moveNode(child, target, static_cast<int>(target->children.size()));
			enqueue(target->ref);
		}
		else
		{
			removeNode(child);
		}
	}

	// 2. Bring the remaining children into the desired order, moving or inserting the missing ones.
	for (std::size_t i = 0; i < desired.size(); ++i)
	{
		const int row = static_cast<int>(i);
		if (i < parent->children.size() && parent->children[i]->ref == desired[i])
		{
			continue;
		}
		Node* existing = find(desired[i]);
		if (existing && !isWithin(parent, existing))
		{
			moveNode(existing, parent, row);
		}
		else
		{
			if (existing)
			{
				removeNode(existing);
			}
			insertNode(parent, row, desired[i]);
		}
	}
	assert(parent->children.size() == desired.size());
}

void ShadowTreeModel::applyChangeSet(const model::ChangeSet& changeSet)
{
	if (m_resetting)
	{
		return;
	}
	std::vector<model::EntityRef> destinations;
	std::vector<model::EntityRef> sources;
	std::set<model::EntityRef> refresh;
	bool refreshAll = false;
	std::size_t structural = 0;

	for (const model::Change& change : changeSet.changes)
	{
		if (!isShown(change.ref.kind))
		{
			continue;
		}
		const Node* node = find(change.ref);
		const bool alive = project().contains(change.ref);
		switch (change.kind)
		{
			case model::Change::Removed:
				if (node && node->parent)
				{
					sources.push_back(node->parent->ref);
					++structural;
				}
				break;
			case model::Change::Added:
				if (alive)
				{
					if (const auto parentRef = parentOf(change.ref))
					{
						destinations.push_back(*parentRef);
						++structural;
					}
				}
				break;
			case model::Change::Modified:
				if (change.structural || sortedByContent())
				{
					if (node && node->parent)
					{
						sources.push_back(node->parent->ref);
					}
					if (alive)
					{
						if (const auto parentRef = parentOf(change.ref))
						{
							destinations.push_back(*parentRef);
						}
					}
					structural += change.structural ? 1 : 0;
				}
				refresh.insert(change.ref);
				break;
		}
	}
	collectRelated(changeSet, refresh, refreshAll);

	if (structural > kBulkThreshold)
	{
		rebuild();
		return;
	}

	m_queue.clear();
	m_queued.clear();
	for (const model::EntityRef& ref : destinations)
	{
		enqueue(ref);
	}
	for (const model::EntityRef& ref : sources)
	{
		enqueue(ref);
	}
	while (!m_queue.empty())
	{
		const model::EntityRef ref = m_queue.front();
		m_queue.pop_front();
		m_queued.erase(ref);
		if (Node* node = find(ref))
		{
			syncChildren(node);
		}
	}

	if (refreshAll)
	{
		emitSubtreeChanged(m_root.get());
		return;
	}
	for (const model::EntityRef& ref : refresh)
	{
		if (const Node* node = find(ref); node && node != m_root.get())
		{
			emitRowChanged(node);
		}
	}
}

void ShadowTreeModel::emitRowChanged(const Node* node)
{
	const QModelIndex first = createIndex(node->row, 0, const_cast<Node*>(node));
	const QModelIndex last = createIndex(node->row, static_cast<int>(m_columns.size()) - 1, const_cast<Node*>(node));
	Q_EMIT dataChanged(first, last);
}

void ShadowTreeModel::emitSubtreeChanged(const Node* node)
{
	if (node->children.empty())
	{
		return;
	}
	const Node* first = node->children.front().get();
	const Node* last = node->children.back().get();
	Q_EMIT dataChanged(createIndex(first->row, 0, const_cast<Node*>(first)),
		createIndex(last->row, static_cast<int>(m_columns.size()) - 1, const_cast<Node*>(last)));
	for (const auto& child : node->children)
	{
		emitSubtreeChanged(child.get());
	}
}

} // namespace kestrel::viewmodels
