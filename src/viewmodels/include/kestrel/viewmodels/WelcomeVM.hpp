#pragma once

// Welcome tab (shown when no editor is open): new / open project and the recent projects list.

#include "kestrel/viewmodels/PanelVM.hpp"

#include <QStringList>

namespace kestrel::viewmodels
{

class WelcomeVM : public PanelVM
{
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("View models are created by the application")
	Q_PROPERTY(QStringList recentProjects READ recentProjects NOTIFY recentProjectsChanged)

public:
	static constexpr int kMaxRecent = 8;

	explicit WelcomeVM(const Services& services, QObject* parent = nullptr);

	QStringList recentProjects() const { return m_recent; }
	/// Moves `dir` to the top of the recent list (persisted in QSettings).
	void addRecent(const QString& dir);

	/// Asks the shell to open a recent project (it may ask to save the current one first).
	Q_INVOKABLE void openRecent(const QString& dir);
	Q_INVOKABLE void removeRecent(const QString& dir);

Q_SIGNALS:
	void recentProjectsChanged();
	void openRequested(const QString& dir);

private:
	void store();

	QStringList m_recent;
};

} // namespace kestrel::viewmodels
