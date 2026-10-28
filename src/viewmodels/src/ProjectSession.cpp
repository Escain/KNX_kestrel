#include "kestrel/viewmodels/ProjectSession.hpp"

#include "kestrel/persist/Persist.hpp"
#include "kestrel/viewmodels/Format.hpp"

#include <QDir>
#include <QFileInfo>

#include <utility>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

ProjectSession::ProjectSession(QObject* parent)
	: QObject(parent), m_project(std::make_unique<model::Project>())
{
	subscribe();
}

ProjectSession::~ProjectSession() = default;

QString ProjectSession::projectName() const
{
	if (!m_project->settings().name.isEmpty())
	{
		return m_project->settings().name;
	}
	if (!m_path.isEmpty())
	{
		return QFileInfo(m_path).fileName();
	}
	return tr("Untitled");
}

bool ProjectSession::canUndo() const
{
	return m_project->history().canUndo();
}

bool ProjectSession::canRedo() const
{
	return m_project->history().canRedo();
}

QString ProjectSession::undoText() const
{
	return m_project->history().undoText();
}

QString ProjectSession::redoText() const
{
	return m_project->history().redoText();
}

void ProjectSession::setIdGeneratorFactory(std::function<model::IdGenerator()> factory)
{
	m_idGeneratorFactory = std::move(factory);
}

void ProjectSession::replaceProject(model::Project project, const QString& path)
{
	Q_EMIT aboutToReplaceProject();
	m_subscription.reset();
	m_project = std::make_unique<model::Project>(std::move(project));
	subscribe();
	setPath(path);
	++m_revision;
	Q_EMIT projectReplaced();
	Q_EMIT revisionChanged();
	Q_EMIT projectNameChanged();
	Q_EMIT undoStateChanged();
	updateDirty();
}

void ProjectSession::newProject(const QString& name)
{
	model::Project project(m_idGeneratorFactory ? m_idGeneratorFactory() : model::IdGenerator::random());
	if (!name.isEmpty())
	{
		model::ProjectSettings settings = project.settings();
		settings.name = name;
		(void)project.edit(u"New project"_s,
			[&](model::Tx& tx)
			{
				tx.setSettings(settings);
				return ok();
			});
		project.history().clear();
		project.history().setClean();
	}
	clearError();
	replaceProject(std::move(project), {});
}

bool ProjectSession::open(const QString& dir)
{
	persist::LoadOptions options;
	if (m_idGeneratorFactory)
	{
		options.idGenerator = m_idGeneratorFactory();
	}
	auto loaded = persist::load(dir, options);
	if (!loaded)
	{
		setLastError(tr("Cannot open %1: %2").arg(QDir::toNativeSeparators(dir), errorText(loaded.error())));
		return false;
	}
	clearError();
	replaceProject(std::move(loaded).value(), QDir::cleanPath(dir));
	return true;
}

bool ProjectSession::save()
{
	if (m_path.isEmpty())
	{
		setLastError(tr("The project has no folder yet: use Save As."));
		return false;
	}
	const Status status = persist::save(*m_project, m_path);
	if (!status)
	{
		setLastError(tr("Cannot save %1: %2").arg(QDir::toNativeSeparators(m_path), errorText(status.error())));
		return false;
	}
	m_project->history().setClean();
	clearError();
	updateDirty();
	Q_EMIT saved();
	return true;
}

bool ProjectSession::saveAs(const QString& dir)
{
	const QString previous = m_path;
	setPath(QDir::cleanPath(dir));
	if (!save())
	{
		setPath(previous);
		return false;
	}
	Q_EMIT projectNameChanged();
	return true;
}

bool ProjectSession::undo()
{
	const bool done = m_project->history().undo();
	refreshHistoryState();
	return done;
}

bool ProjectSession::redo()
{
	// model::History::redo() notifies before advancing its index: refresh once it returned.
	const bool done = m_project->history().redo();
	refreshHistoryState();
	return done;
}

void ProjectSession::refreshHistoryState()
{
	Q_EMIT undoStateChanged();
	updateDirty();
}

void ProjectSession::clearError()
{
	setLastError({});
}

void ProjectSession::reportError(const QString& message)
{
	setLastError(message);
}

void ProjectSession::subscribe()
{
	m_subscription = m_project->onChanged([this](const model::ChangeSet& changeSet) { onProjectChanged(changeSet); });
}

void ProjectSession::onProjectChanged(const model::ChangeSet& changeSet)
{
	++m_revision;
	Q_EMIT changed(changeSet);
	Q_EMIT revisionChanged();
	if (changeSet.contains(model::EntityRef::project()))
	{
		Q_EMIT projectNameChanged();
	}
	Q_EMIT undoStateChanged();
	updateDirty();
	if (changeSet.fromUndo)
	{
		// Undo/redo started elsewhere than undo()/redo(): the history settles after this callback.
		QMetaObject::invokeMethod(this, &ProjectSession::refreshHistoryState, Qt::QueuedConnection);
	}
}

void ProjectSession::updateDirty()
{
	const bool dirty = m_project->isDirty();
	if (dirty != m_dirty)
	{
		m_dirty = dirty;
		Q_EMIT dirtyChanged();
	}
}

void ProjectSession::setPath(const QString& path)
{
	if (path != m_path)
	{
		m_path = path;
		Q_EMIT pathChanged();
		Q_EMIT projectNameChanged();
	}
}

void ProjectSession::setLastError(const QString& message)
{
	if (message != m_lastError)
	{
		m_lastError = message;
		Q_EMIT lastErrorChanged();
	}
	if (!message.isEmpty())
	{
		Q_EMIT errorReported(message);
	}
}

} // namespace kestrel::viewmodels
