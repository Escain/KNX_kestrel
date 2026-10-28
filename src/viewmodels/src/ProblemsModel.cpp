#include "kestrel/viewmodels/ProblemsModel.hpp"

#include <algorithm>
#include <iterator>
#include <tuple>

#include "kestrel/viewmodels/Format.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

ProblemsModel::ProblemsModel(ProjectSession* session, QObject* parent) : QAbstractTableModel(parent), m_session(session)
{
	m_timer.setSingleShot(true);
	m_timer.setInterval(kDebounceMs);
	connect(&m_timer, &QTimer::timeout, this, &ProblemsModel::refresh);
	connect(session, &ProjectSession::changed, this, [this] { m_timer.start(); });
	connect(session, &ProjectSession::projectReplaced, this, &ProblemsModel::refresh);
	refresh();
}

int ProblemsModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int ProblemsModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : 4;
}

QString ProblemsModel::keyOf(const validate::Diagnostic& diagnostic)
{
	QString key = diagnostic.code + u'|' + diagnostic.message;
	for (const model::EntityRef& ref : diagnostic.subjects)
	{
		key += u'|' + ref.toString();
	}
	return key;
}

QString ProblemsModel::subjectsText(const validate::Diagnostic& diagnostic) const
{
	QStringList texts;
	for (const model::EntityRef& ref : diagnostic.subjects)
	{
		texts.append(entityLabel(m_session->project(), ref));
	}
	return texts.join(u"; "_s);
}

QVariant ProblemsModel::data(const QModelIndex& index, int role) const
{
	if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
	{
		return {};
	}
	const validate::Diagnostic& d = m_rows[static_cast<std::size_t>(index.row())];
	const QString severity = QString::fromLatin1(validate::toString(d.severity)).toLower();
	const QString message = QCoreApplication::translate("kestrel::validate", d.message.toUtf8().constData());
	switch (role)
	{
		case Qt::DisplayRole:
			switch (index.column())
			{
				case 0:
					switch (d.severity)
					{
						case validate::Severity::Error: return tr("Error");
						case validate::Severity::Warning: return tr("Warning");
						case validate::Severity::Info: return tr("Info");
					}
					return {};
				case 1: return d.code;
				case 2: return message;
				case 3: return subjectsText(d);
				default: return {};
			}
		case Qt::ToolTipRole: return message + u'\n' + subjectsText(d);
		case SeverityRole: return severity;
		case CodeRole: return d.code;
		case MessageRole: return message;
		case SubjectsRole: return subjectsText(d);
		case RefRole: return d.subjects.empty() ? QString() : d.subjects.front().toString();
		default: return {};
	}
}

QVariant ProblemsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
	{
		return {};
	}
	switch (section)
	{
		case 0: return tr("Severity");
		case 1: return tr("Code");
		case 2: return tr("Message");
		case 3: return tr("Objects");
		default: return {};
	}
}

QHash<int, QByteArray> ProblemsModel::roleNames() const
{
	QHash<int, QByteArray> names = QAbstractTableModel::roleNames();
	names.insert(SeverityRole, "severity");
	names.insert(CodeRole, "code");
	names.insert(MessageRole, "message");
	names.insert(SubjectsRole, "subjects");
	names.insert(RefRole, "ref");
	return names;
}

void ProblemsModel::setExtraSource(std::function<std::vector<validate::Diagnostic>()> source)
{
	m_extra = std::move(source);
	scheduleRefresh();
}

void ProblemsModel::refresh()
{
	m_timer.stop();
	std::vector<validate::Diagnostic> next = validate::run(m_session->project());
	if (m_extra)
	{
		std::vector<validate::Diagnostic> extra = m_extra();
		if (!extra.empty())
		{
			next.insert(next.end(), std::make_move_iterator(extra.begin()), std::make_move_iterator(extra.end()));
			validate::sortDiagnostics(next);
		}
	}

	// Keys, made unique by an occurrence counter (identical diagnostics are possible in imported data).
	QStringList nextKeys;
	QHash<QString, int> seen;
	for (const validate::Diagnostic& d : next)
	{
		const QString key = keyOf(d);
		nextKeys.append(key + u'#' + QString::number(seen[key]++));
	}
	const QSet<QString> wanted(nextKeys.begin(), nextKeys.end());

	// 1. Remove rows that disappeared (contiguous runs, bottom-up).
	for (qsizetype i = m_keys.size() - 1; i >= 0;)
	{
		if (wanted.contains(m_keys.at(i)))
		{
			--i;
			continue;
		}
		qsizetype first = i;
		while (first > 0 && !wanted.contains(m_keys.at(first - 1)))
		{
			--first;
		}
		beginRemoveRows({}, static_cast<int>(first), static_cast<int>(i));
		m_keys.remove(first, i - first + 1);
		m_rows.erase(m_rows.begin() + first, m_rows.begin() + i + 1);
		endRemoveRows();
		i = first - 1;
	}

	// 2. Insert the new rows. Both lists use the same total order, so the kept rows are already in place.
	qsizetype row = 0;
	for (qsizetype i = 0; i < nextKeys.size(); ++i)
	{
		if (row < m_keys.size() && m_keys.at(row) == nextKeys.at(i))
		{
			m_rows[static_cast<std::size_t>(row)] = next[static_cast<std::size_t>(i)];
			++row;
			continue;
		}
		beginInsertRows({}, static_cast<int>(row), static_cast<int>(row));
		m_keys.insert(row, nextKeys.at(i));
		m_rows.insert(m_rows.begin() + row, next[static_cast<std::size_t>(i)]);
		endInsertRows();
		++row;
	}
	if (m_keys != nextKeys)
	{
		// Unexpected ordering (should not happen): fall back to a reset.
		beginResetModel();
		m_keys = nextKeys;
		m_rows = std::move(next);
		endResetModel();
	}
	else if (!m_rows.empty())
	{
		// Subject labels may have changed (renamed / renumbered entities).
		Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
	}

	int counts[3] = {0, 0, 0};
	for (const validate::Diagnostic& d : m_rows)
	{
		++counts[static_cast<int>(d.severity)];
	}
	if (counts[0] != m_counts[0] || counts[1] != m_counts[1] || counts[2] != m_counts[2])
	{
		std::copy(std::begin(counts), std::end(counts), std::begin(m_counts));
		Q_EMIT countsChanged();
	}
	Q_EMIT refreshed();
}

} // namespace kestrel::viewmodels
