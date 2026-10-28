#include "kestrel/viewmodels/KeyedTableModel.hpp"

#include <QSet>

#include <cassert>
#include <utility>

namespace kestrel::viewmodels
{

KeyedTableModel::KeyedTableModel(QStringList columns, QObject* parent)
	: QAbstractTableModel(parent), m_columns(std::move(columns))
{
}

int KeyedTableModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(m_keys.size());
}

int KeyedTableModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(m_columns.size());
}

QVariant KeyedTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section >= 0 && section < m_columns.size())
		return m_columns.at(section);
	return {};
}

QHash<int, QByteArray> KeyedTableModel::roleNames() const
{
	QHash<int, QByteArray> names = QAbstractTableModel::roleNames();
	names.insert(KeyRole, "key");
	names.insert(extraRoleNames());
	return names;
}

QString KeyedTableModel::keyAt(int row) const
{
	return row >= 0 && row < m_keys.size() ? m_keys.at(row) : QString();
}

int KeyedTableModel::rowOf(const QString& key) const
{
	return static_cast<int>(m_keys.indexOf(key));
}

void KeyedTableModel::syncKeys(const QStringList& desired)
{
	const QSet<QString> wanted(desired.begin(), desired.end());
	assert(wanted.size() == desired.size());

	// 1. Remove rows whose key is gone, in contiguous runs from the bottom.
	for (qsizetype row = m_keys.size(); row-- > 0;)
	{
		if (wanted.contains(m_keys.at(row)))
			continue;
		qsizetype first = row;
		while (first > 0 && !wanted.contains(m_keys.at(first - 1)))
			--first;
		beginRemoveRows({}, static_cast<int>(first), static_cast<int>(row));
		m_keys.remove(first, row - first + 1);
		endRemoveRows();
		row = first;
	}

	// 2. Walk the desired order: keep matching rows, re-insert rows that moved, insert new keys.
	QSet<QString> present(m_keys.begin(), m_keys.end());
	for (qsizetype i = 0; i < desired.size(); ++i)
	{
		const QString& key = desired.at(i);
		if (i < m_keys.size() && m_keys.at(i) == key)
			continue;
		if (present.contains(key))
		{
			const qsizetype from = m_keys.indexOf(key, i);
			beginMoveRows({}, static_cast<int>(from), static_cast<int>(from), {}, static_cast<int>(i));
			m_keys.move(from, i);
			endMoveRows();
			continue;
		}
		beginInsertRows({}, static_cast<int>(i), static_cast<int>(i));
		m_keys.insert(i, key);
		present.insert(key);
		endInsertRows();
	}
	assert(m_keys == desired);
}

void KeyedTableModel::refreshAll(int column)
{
	if (m_keys.isEmpty() || m_columns.isEmpty())
		return;
	const int first = column < 0 ? 0 : column;
	const int last = column < 0 ? static_cast<int>(m_columns.size()) - 1 : column;
	Q_EMIT dataChanged(index(0, first), index(static_cast<int>(m_keys.size()) - 1, last));
}

} // namespace kestrel::viewmodels
