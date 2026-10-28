// ProblemsModel: validate::run() debounced on ChangeSets, keyed diff (no reset), counts, navigation.

#include "SampleProject.hpp"

#include "kestrel/model/Operations.hpp"
#include "kestrel/viewmodels/ProblemsModel.hpp"
#include "kestrel/viewmodels/ProblemsPanelVM.hpp"
#include "kestrel/viewmodels/Workspace.hpp"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wconversion" // QTRY_* narrowing inside Qt 6.8's macro
#endif

using namespace Qt::StringLiterals;
using namespace kestrel;

class ProblemsModelTest : public QObject
{
	Q_OBJECT

private Q_SLOTS:
	void fr_top_2__validationIsDebouncedAndIncremental()
	{
		viewmodels::Workspace ws;
		model::Project p(model::IdGenerator::sequential());
		const auto ids = testing::buildSampleProject(p);
		ws.session()->replaceProject(std::move(p));
		viewmodels::ProblemsModel* model = ws.problems()->problemsModel();
		QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
		QSignalSpy resets(model, &QAbstractItemModel::modelAboutToBeReset);
		QSignalSpy refreshed(model, &viewmodels::ProblemsModel::refreshed);

		QCOMPARE(model->errorCount(), 0);
		const int warnings = model->warningCount();
		QVERIFY(warnings > 0);
		QCOMPARE(ws.problems()->summary(), u"%1 warnings, 1 message"_s.arg(warnings));

		// Duplicate individual address: TOP-001 (error), after the debounce only.
		QVERIFY(model::setDeviceNumber(ws.session()->project(), ids.weather, std::nullopt).has_value());
		QVERIFY(model::moveDevice(ws.session()->project(), ids.weather, ids.line11).has_value());
		QVERIFY(model::setDeviceNumber(ws.session()->project(), ids.weather, 4).has_value());
		QVERIFY(model->isPending());
		QTRY_COMPARE(refreshed.count(), 1); // three edits, one validation run
		QCOMPARE(model->errorCount(), 0);

		QVERIFY(ws.session()->project().edit(u"Duplicate"_s,
			[&](model::Tx& tx)
			{
				tx.set(ids.weather, &model::Device::number, std::optional<unsigned>(1));
				return ok();
			}).has_value());
		QTRY_COMPARE(model->errorCount(), 1);
		QCOMPARE(model->index(0, 1).data().toString(), u"TOP-001"_s);
		QCOMPARE(model->index(0, 0).data(viewmodels::ProblemsModel::SeverityRole).toString(), u"error"_s);
		QVERIFY(model->index(0, 3).data().toString().contains(u"1.1.1 Dimmer"_s));

		// Navigation selects the first subject.
		ws.problems()->navigate(0);
		QVERIFY(ws.selection()->current().has_value());

		QVERIFY(ws.session()->undo());
		QTRY_COMPARE(model->errorCount(), 0);
		QCOMPARE(resets.count(), 0);
	}
};

QTEST_MAIN(ProblemsModelTest)
#include "tst_ProblemsModel.moc"
