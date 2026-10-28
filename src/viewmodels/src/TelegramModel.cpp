#include "kestrel/viewmodels/TelegramModel.hpp"

#include "kestrel/viewmodels/ProjectSession.hpp"

#include <QTimeZone>

#include <algorithm>
#include <cassert>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

TelegramModel::TelegramModel(ProjectSession* session, std::size_t capacity, QObject* parent)
	: QAbstractTableModel(parent), m_session(session), m_capacity(std::max<std::size_t>(capacity, 1))
{
	auto invalidate = [this]
	{
		m_namesValid = false;
		if (!m_visible.empty())
			Q_EMIT dataChanged(index(0, DestinationColumn), index(rowCount() - 1, NameColumn));
	};
	connect(session, &ProjectSession::changed, this,
		[invalidate](const model::ChangeSet& changeSet)
		{
			if (changeSet.touches(model::EntityKind::GroupAddress) || changeSet.touches(model::EntityKind::Project))
				invalidate();
		});
	connect(session, &ProjectSession::projectReplaced, this, invalidate);
}

int TelegramModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(m_visible.size());
}

int TelegramModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : ColumnCount;
}

const app::TelegramRecord& TelegramModel::bySequence(std::uint64_t sequence) const
{
	assert(sequence >= m_firstSequence && sequence - m_firstSequence < m_all.size());
	return m_all[static_cast<std::size_t>(sequence - m_firstSequence)];
}

const app::TelegramRecord& TelegramModel::record(int row) const
{
	return bySequence(m_visible[static_cast<std::size_t>(row)]);
}

QString TelegramModel::destinationText(const app::TelegramRecord& record) const
{
	return record.destinationText(m_session->project().settings().gaStyle);
}

QString TelegramModel::groupAddressName(const app::TelegramRecord& record) const
{
	if (!record.isGroup)
		return {};
	if (!m_namesValid)
	{
		m_names.clear();
		for (const model::GroupAddress& address : m_session->project().groupAddresses())
			m_names.insert(address.raw, address.name);
		m_namesValid = true;
	}
	return m_names.value(record.destination);
}

bool TelegramModel::matches(const app::TelegramRecord& record) const
{
	if (m_filter.isEmpty())
		return true;
	return QString::fromStdString(record.source.toString()).contains(m_filter, Qt::CaseInsensitive)
		|| destinationText(record).contains(m_filter, Qt::CaseInsensitive)
		|| groupAddressName(record).contains(m_filter, Qt::CaseInsensitive);
}

QVariant TelegramModel::data(const QModelIndex& index, int role) const
{
	if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
		return {};
	const app::TelegramRecord& r = record(index.row());
	switch (role)
	{
		case Qt::DisplayRole:
			switch (index.column())
			{
				case TimeColumn: return r.timestamp.toTimeZone(QTimeZone::LocalTime).toString(u"HH:mm:ss.zzz"_s);
				case SourceColumn: return QString::fromStdString(r.source.toString());
				case DestinationColumn: return destinationText(r);
				case NameColumn: return groupAddressName(r);
				case ServiceColumn: return r.serviceText();
				case DataColumn: return QString::fromLatin1(r.payload.toHex(' '));
				case ValueColumn: return r.decoded;
				default: return {};
			}
		case OutgoingRole: return r.outgoing;
		case ServiceRole: return r.serviceText();
		default: return {};
	}
}

QVariant TelegramModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return {};
	switch (section)
	{
		case TimeColumn: return tr("Time");
		case SourceColumn: return tr("Source");
		case DestinationColumn: return tr("Destination");
		case NameColumn: return tr("Name");
		case ServiceColumn: return tr("Service");
		case DataColumn: return tr("Data");
		case ValueColumn: return tr("Value");
		default: return {};
	}
}

QHash<int, QByteArray> TelegramModel::roleNames() const
{
	QHash<int, QByteArray> names = QAbstractTableModel::roleNames();
	names.insert(OutgoingRole, "outgoing");
	names.insert(ServiceRole, "service");
	return names;
}

void TelegramModel::append(const QList<app::TelegramRecord>& records)
{
	if (records.isEmpty())
		return;
	// Only the newest `capacity` records of an oversized batch can survive.
	const auto keep = std::min<std::size_t>(static_cast<std::size_t>(records.size()), m_capacity);
	const qsizetype skip = records.size() - static_cast<qsizetype>(keep);

	// 1. Push the oldest records out of the ring: one removal for the visible ones.
	const std::size_t overflow = m_all.size() + keep > m_capacity ? m_all.size() + keep - m_capacity : 0;
	if (overflow > 0)
	{
		const std::uint64_t newFirst = m_firstSequence + overflow;
		std::size_t gone = 0;
		while (gone < m_visible.size() && m_visible[gone] < newFirst)
			++gone;
		if (gone > 0)
		{
			beginRemoveRows({}, 0, static_cast<int>(gone) - 1);
			m_visible.erase(m_visible.begin(), m_visible.begin() + static_cast<std::ptrdiff_t>(gone));
		}
		const std::size_t dropped = std::min(overflow, m_all.size());
		m_all.erase(m_all.begin(), m_all.begin() + static_cast<std::ptrdiff_t>(dropped));
		m_firstSequence = newFirst;
		if (gone > 0)
			endRemoveRows();
	}

	// 2. Store the batch, then insert the rows that pass the filter in one go.
	std::vector<std::uint64_t> shown;
	for (qsizetype i = skip; i < records.size(); ++i)
	{
		const std::uint64_t sequence = m_firstSequence + m_all.size();
		m_all.push_back(records.at(i));
		if (matches(m_all.back()))
			shown.push_back(sequence);
	}
	if (!shown.empty())
	{
		const int first = rowCount();
		beginInsertRows({}, first, first + static_cast<int>(shown.size()) - 1);
		m_visible.insert(m_visible.end(), shown.begin(), shown.end());
		endInsertRows();
	}
}

void TelegramModel::clear()
{
	if (m_all.empty())
		return;
	if (!m_visible.empty())
	{
		beginRemoveRows({}, 0, rowCount() - 1);
		m_visible.clear();
		m_firstSequence += m_all.size();
		m_all.clear();
		endRemoveRows();
		return;
	}
	m_firstSequence += m_all.size();
	m_all.clear();
}

void TelegramModel::setFilter(const QString& text)
{
	const QString trimmed = text.trimmed();
	if (trimmed == m_filter)
		return;
	beginResetModel();
	m_filter = trimmed;
	m_visible.clear();
	for (std::size_t i = 0; i < m_all.size(); ++i)
	{
		if (matches(m_all[i]))
			m_visible.push_back(m_firstSequence + i);
	}
	endResetModel();
}

} // namespace kestrel::viewmodels
