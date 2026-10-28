#pragma once

// Group monitor rows (FR-BUS-5, specs/brainstorm/round1-ui.md §4).
//
// A fixed-capacity ring of TelegramRecords plus an index of the rows that pass the filter. Records are
// appended in batches (one per drain tick): at most one beginRemoveRows (records pushed out of the ring)
// and one beginInsertRows per batch, never one signal per telegram. Names (GA names) and texts are
// resolved lazily in data(). Changing the filter rebuilds the index (a reset of this view-only model).
// Columns: Time | Source | Destination | Name | Service | Data | Value.

#include "kestrel/app/TelegramRecord.hpp"
#include "kestrel/model/ChangeSet.hpp"

#include <QAbstractTableModel>
#include <QHash>
#include <QList>

#include <cstddef>
#include <cstdint>
#include <deque>

namespace kestrel::viewmodels
{

class ProjectSession;

class TelegramModel : public QAbstractTableModel
{
	Q_OBJECT

public:
	enum Column
	{
		TimeColumn,
		SourceColumn,
		DestinationColumn,
		NameColumn,
		ServiceColumn,
		DataColumn,
		ValueColumn,
		ColumnCount,
	};
	enum Role
	{
		OutgoingRole = Qt::UserRole + 1,
		ServiceRole,
	};

	static constexpr std::size_t kDefaultCapacity = 20000;

	explicit TelegramModel(ProjectSession* session, std::size_t capacity = kDefaultCapacity, QObject* parent = nullptr);

	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

	/// Appends one batch (oldest first).
	void append(const QList<app::TelegramRecord>& records);
	void clear();
	/// Case-insensitive text matched against source, destination and GA name; empty = everything.
	void setFilter(const QString& text);
	QString filter() const { return m_filter; }

	std::size_t capacity() const { return m_capacity; }
	/// Records kept (filtered or not).
	std::size_t storedCount() const { return m_all.size(); }
	/// Record of a visible row.
	const app::TelegramRecord& record(int row) const;

private:
	bool matches(const app::TelegramRecord& record) const;
	QString destinationText(const app::TelegramRecord& record) const;
	QString groupAddressName(const app::TelegramRecord& record) const;
	const app::TelegramRecord& bySequence(std::uint64_t sequence) const;

	ProjectSession* m_session;
	std::size_t m_capacity;
	std::deque<app::TelegramRecord> m_all; ///< m_all[i] has sequence m_firstSequence + i
	std::uint64_t m_firstSequence = 0;
	std::deque<std::uint64_t> m_visible; ///< sequences of the rows that pass the filter
	QString m_filter;
	mutable QHash<std::uint16_t, QString> m_names; ///< GA raw -> name, rebuilt lazily
	mutable bool m_namesValid = false;
};

} // namespace kestrel::viewmodels
