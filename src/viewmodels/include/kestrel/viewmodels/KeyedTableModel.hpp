#pragma once

// Base of flat table models whose rows are identified by a string key (catalog products, catalog com objects).
// syncKeys() moves the rows to a new key list with a keyed diff: rows whose key disappeared are removed, new keys
// are inserted at their place, and surviving rows only get dataChanged. Models never reset on an update
// (ADR-006), so the views keep their current row and scroll position.

#include <QAbstractTableModel>
#include <QStringList>

namespace kestrel::viewmodels
{

class KeyedTableModel : public QAbstractTableModel
{
	Q_OBJECT

public:
	static constexpr int KeyRole = Qt::UserRole + 1;

	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

	const QStringList& keys() const { return m_keys; }
	/// Key of a row; empty when out of range.
	Q_INVOKABLE QString keyAt(int row) const;
	/// Row of a key; -1 if absent.
	Q_INVOKABLE int rowOf(const QString& key) const;

protected:
	KeyedTableModel(QStringList columns, QObject* parent);

	/// Brings the rows to `desired` (keys must be unique) with remove / insert / dataChanged.
	void syncKeys(const QStringList& desired);
	/// dataChanged for every row (optionally only one column).
	void refreshAll(int column = -1);
	virtual QHash<int, QByteArray> extraRoleNames() const { return {}; }

private:
	QStringList m_columns;
	QStringList m_keys;
};

} // namespace kestrel::viewmodels
