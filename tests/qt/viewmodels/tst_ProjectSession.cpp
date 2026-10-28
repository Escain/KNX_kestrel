// ProjectSession: new / open / save / save as, dirty state from model::History, undo/redo (FR-PRJ-1/2).

#include "SampleProject.hpp"

#include "kestrel/model/Operations.hpp"
#include "kestrel/persist/Persist.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace kestrel;
using viewmodels::ProjectSession;

class ProjectSessionTest : public QObject
{
	Q_OBJECT

private Q_SLOTS:
	void fr_prj_1__newProjectIsCleanAndUntitled()
	{
		ProjectSession session;
		QSignalSpy replaced(&session, &ProjectSession::projectReplaced);
		session.newProject();
		QCOMPARE(replaced.count(), 1);
		QVERIFY(!session.isDirty());
		QVERIFY(!session.canUndo());
		QCOMPARE(session.title(), u"Untitled"_s);
		session.newProject(u"Flat"_s);
		QCOMPARE(session.title(), u"Flat"_s);
		QVERIFY(!session.isDirty());
		QVERIFY(!session.canUndo());
	}

	void fr_prj_1__saveAsOpenRoundTrip()
	{
		QTemporaryDir temp;
		ProjectSession session;
		testing::buildSampleProject(session.project());
		QVERIFY(model::addArea(session.project(), 3, u"Extra"_s).has_value());
		QVERIFY(session.isDirty());
		QVERIFY(!session.save()); // no folder yet
		QVERIFY(session.lastError().contains(u"Save As"_s));

		const QString dir = temp.filePath(u"house"_s);
		QSignalSpy dirty(&session, &ProjectSession::dirtyChanged);
		QVERIFY(session.saveAs(dir));
		QVERIFY(!session.isDirty());
		QCOMPARE(dirty.count(), 1);
		QCOMPARE(session.path(), QDir::cleanPath(dir));
		QVERIFY(session.lastError().isEmpty());

		ProjectSession other;
		QVERIFY(other.open(dir));
		QCOMPARE(other.project().devices().size(), session.project().devices().size());
		QCOMPARE(other.project().areas().size(), 2u);
		QVERIFY(!other.isDirty());
		QCOMPARE(other.title(), u"Sample house"_s);

		// Edits after saving dirty the project; save() writes to the same folder.
		QVERIFY(model::rename(other.project(), model::EntityRef::project(), u"Renamed"_s).has_value());
		QVERIFY(other.isDirty());
		QCOMPARE(other.title(), u"Renamed"_s);
		QVERIFY(other.save());
		QVERIFY(!other.isDirty());
		QCOMPARE(persist::load(dir)->settings().name, u"Renamed"_s);
	}

	void fr_prj_1__openFailureKeepsTheCurrentProject()
	{
		QTemporaryDir temp;
		ProjectSession session;
		testing::buildSampleProject(session.project());
		const auto devices = session.project().devices().size();
		QSignalSpy errors(&session, &ProjectSession::errorReported);
		QVERIFY(!session.open(temp.filePath(u"nothing-here"_s)));
		QCOMPARE(errors.count(), 1);
		QVERIFY(!session.lastError().isEmpty());
		QCOMPARE(session.project().devices().size(), devices);
	}

	void fr_prj_2__undoRedoFollowHistoryAndCleanState()
	{
		QTemporaryDir temp;
		ProjectSession session;
		testing::buildSampleProject(session.project());
		QVERIFY(session.saveAs(temp.filePath(u"p"_s)));
		QSignalSpy changed(&session, &ProjectSession::changed);
		QSignalSpy undoState(&session, &ProjectSession::undoStateChanged);

		QVERIFY(model::addArea(session.project(), 5, u"Five"_s).has_value());
		QCOMPARE(changed.count(), 1);
		QVERIFY(session.isDirty());
		QVERIFY(session.canUndo());
		QCOMPARE(session.undoText(), u"Add area"_s);

		QVERIFY(session.undo());
		QCOMPARE(changed.count(), 2);
		QVERIFY(changed.last().at(0).value<model::ChangeSet>().fromUndo);
		QVERIFY(!session.isDirty()); // back at the saved state
		QVERIFY(session.canRedo());
		QVERIFY(!session.canUndo());

		QVERIFY(session.redo());
		QVERIFY(session.isDirty());
		QVERIFY(session.canUndo()); // refreshed after History::redo() returned
		QVERIFY(!session.canRedo());
		QVERIFY(undoState.count() >= 3);
	}

	void fr_prj_2__commissioningFactsDirtyWithoutUndo()
	{
		ProjectSession session;
		const auto ids = testing::buildSampleProject(session.project());
		model::DeviceState state;
		state.iaProgrammed = true;
		session.project().recordState(ids.dimmer, state);
		QVERIFY(session.isDirty());
		QVERIFY(!session.canUndo());
	}
};

QTEST_GUILESS_MAIN(ProjectSessionTest)
#include "tst_ProjectSession.moc"
