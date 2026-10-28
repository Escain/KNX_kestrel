#pragma once

// The open project of the GUI (specs/02-architecture.md §3.2, ADR-003).
// Owns the model::Project, its folder and the dirty state derived from model::History; re-emits every
// ChangeSet as a Qt signal (synchronously, on the GUI thread) so that item models can update incrementally.
// Undo/redo go straight to model::History: there is no QUndoStack.

#include "kestrel/model/ChangeSet.hpp"
#include "kestrel/model/Project.hpp"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>
#include <memory>
#include <optional>

namespace kestrel::viewmodels
{

class ProjectSession : public QObject
{
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("ProjectSession is owned by the application")
	Q_PROPERTY(QString path READ path NOTIFY pathChanged)
	Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)
	Q_PROPERTY(QString projectName READ projectName NOTIFY projectNameChanged)
	Q_PROPERTY(QString title READ title NOTIFY projectNameChanged)
	Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
	Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
	Q_PROPERTY(QString undoText READ undoText NOTIFY undoStateChanged)
	Q_PROPERTY(QString redoText READ redoText NOTIFY undoStateChanged)
	Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
	Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
	explicit ProjectSession(QObject* parent = nullptr);
	~ProjectSession() override;

	model::Project& project() { return *m_project; }
	const model::Project& project() const { return *m_project; }

	QString path() const { return m_path; }
	bool isDirty() const { return m_dirty; }
	/// Project name from the settings, else the folder name, else "Untitled".
	QString projectName() const;
	/// projectName() for window titles ("Untitled" for a new project).
	QString title() const { return projectName(); }
	bool canUndo() const;
	bool canRedo() const;
	QString undoText() const;
	QString redoText() const;
	QString lastError() const { return m_lastError; }
	/// Incremented on every ChangeSet and project replacement; QML bindings may depend on it.
	int revision() const { return m_revision; }

	/// Replaces the project (models reset). `path` may be empty (never saved).
	void replaceProject(model::Project project, const QString& path = {});

	/// Optional id generator for new projects (tests use sequential ids).
	void setIdGeneratorFactory(std::function<model::IdGenerator()> factory);

	Q_INVOKABLE void newProject(const QString& name = {});
	/// Loads a project folder. On failure keeps the current project and sets lastError.
	Q_INVOKABLE bool open(const QString& dir);
	/// Saves to path(); fails (lastError) if the project has no folder yet.
	Q_INVOKABLE bool save();
	/// Saves into `dir` and makes it the project's folder.
	Q_INVOKABLE bool saveAs(const QString& dir);
	Q_INVOKABLE bool undo();
	Q_INVOKABLE bool redo();
	Q_INVOKABLE void clearError();

	/// Reports an error raised by a view model so the shell can show it (status bar).
	void reportError(const QString& message);

Q_SIGNALS:
	void aboutToReplaceProject();
	void projectReplaced();
	/// Emitted synchronously for every commit / undo / redo / recordState.
	void changed(const kestrel::model::ChangeSet& changeSet);
	void pathChanged();
	void dirtyChanged();
	void projectNameChanged();
	void undoStateChanged();
	void lastErrorChanged();
	void revisionChanged();
	void saved();
	void errorReported(const QString& message);

private:
	void subscribe();
	void onProjectChanged(const model::ChangeSet& changeSet);
	void updateDirty();
	void refreshHistoryState();
	void setPath(const QString& path);
	void setLastError(const QString& message);

	std::unique_ptr<model::Project> m_project;
	model::Subscription m_subscription;
	std::function<model::IdGenerator()> m_idGeneratorFactory;
	QString m_path;
	QString m_lastError;
	bool m_dirty = false;
	int m_revision = 0;
};

} // namespace kestrel::viewmodels
