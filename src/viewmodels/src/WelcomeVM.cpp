#include "kestrel/viewmodels/WelcomeVM.hpp"

#include "kestrel/viewmodels/ProjectSession.hpp"

#include <QSettings>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

namespace
{
const QString kRecentKey = u"recentProjects"_s;
}

WelcomeVM::WelcomeVM(const Services& services, QObject* parent) : PanelVM(services, u"welcome"_s, parent)
{
	m_recent = QSettings().value(kRecentKey).toStringList();
	auto remember = [this]
	{
		if (!session()->path().isEmpty())
			addRecent(session()->path());
	};
	connect(session(), &ProjectSession::pathChanged, this, remember);
	connect(session(), &ProjectSession::saved, this, remember);
}

void WelcomeVM::addRecent(const QString& dir)
{
	QStringList next = m_recent;
	next.removeAll(dir);
	next.prepend(dir);
	while (next.size() > kMaxRecent)
		next.removeLast();
	if (next != m_recent)
	{
		m_recent = next;
		store();
	}
}

void WelcomeVM::openRecent(const QString& dir)
{
	Q_EMIT openRequested(dir);
}

void WelcomeVM::removeRecent(const QString& dir)
{
	if (m_recent.removeAll(dir) > 0)
		store();
}

void WelcomeVM::store()
{
	QSettings().setValue(kRecentKey, m_recent);
	Q_EMIT recentProjectsChanged();
}

} // namespace kestrel::viewmodels
