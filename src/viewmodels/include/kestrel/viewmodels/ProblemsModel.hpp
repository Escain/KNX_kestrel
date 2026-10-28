#pragma once

// Validation results (specs/03-domain-model.md §5). validate::run() is re-run 200 ms after the last ChangeSet
// (debounced); the rows are updated with a keyed diff (no reset). Messages are the fixed English rule texts
// (translated); the subjects are rendered with their current names and addresses.
// Columns: Severity | Code | Message | Objects.

#include "kestrel/validate/Validate.hpp"

#include <QAbstractTableModel>
#include <QStringList>
#include <QTimer>

#include <vector>

namespace kestrel::viewmodels
{

class ProjectSession;

class ProblemsModel : public QAbstractTableModel
{
	Q_OBJECT
	Q_PROPERTY(int errorCount READ errorCount NOTIFY countsChanged)
	Q_PROPERTY(int warningCount READ warningCount NOTIFY countsChanged)
	Q_PROPERTY(int infoCount READ infoCount NOTIFY countsChanged)

public:
	enum Role
	{
		SeverityRole = Qt::UserRole + 1, ///< "error" | "warning" | "info"
		CodeRole,
		MessageRole,
		SubjectsRole, ///< rendered subjects
		RefRole,      ///< first subject ref text
	};

	static constexpr int kDebounceMs = 200;

	explicit ProblemsModel(ProjectSession* session, QObject* parent = nullptr);

	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

	const std::vector<validate::Diagnostic>& diagnostics() const { return m_rows; }
	int errorCount() const { return m_counts[0]; }
	int warningCount() const { return m_counts[1]; }
	int infoCount() const { return m_counts[2]; }
	bool isPending() const { return m_timer.isActive(); }

	/// Runs the validation now (cancels the pending debounce).
	Q_INVOKABLE void refresh();

Q_SIGNALS:
	void countsChanged();
	void refreshed();

private:
	static QString keyOf(const validate::Diagnostic& diagnostic);
	QString subjectsText(const validate::Diagnostic& diagnostic) const;

	ProjectSession* m_session;
	std::vector<validate::Diagnostic> m_rows;
	QStringList m_keys;
	int m_counts[3] = {0, 0, 0};
	QTimer m_timer;
};

} // namespace kestrel::viewmodels
