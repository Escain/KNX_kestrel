// Item models over the project (ADR-006): every model runs under QAbstractItemModelTester while operations,
// undo and redo are applied. After each step the incrementally updated model must equal a model freshly built
// from the project, and no model reset may have happened.

#include "SampleProject.hpp"

#include "kestrel/model/Operations.hpp"
#include "kestrel/viewmodels/BuildingTreeModel.hpp"
#include "kestrel/viewmodels/ComObjectTableModel.hpp"
#include "kestrel/viewmodels/GroupAddressTreeModel.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"
#include "kestrel/viewmodels/TopologyTreeModel.hpp"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

#include <functional>
#include <memory>

using namespace Qt::StringLiterals;
using namespace kestrel;
using namespace kestrel::model;
using kestrel::testing::must;
using viewmodels::ProjectSession;

namespace
{

/// Text dump of the whole tree: one line per row with every column and the ref.
QString dump(const QAbstractItemModel& model, const QModelIndex& parent = {}, int depth = 0)
{
	QString out;
	for (int row = 0; row < model.rowCount(parent); ++row)
	{
		QStringList cells;
		for (int column = 0; column < model.columnCount(parent); ++column)
			cells.append(model.index(row, column, parent).data().toString());
		const QModelIndex index = model.index(row, 0, parent);
		out += QString(depth * 2, u' ') + cells.join(u" | "_s) + u"  <"_s + index.data(viewmodels::ShadowTreeModel::RefRole).toString() + u">\n"_s;
		out += dump(model, index, depth + 1);
	}
	return out;
}

/// Watches one model: tester + reset spy + comparison against a freshly built model.
class Watch
{
public:
	Watch(QAbstractItemModel* model, std::function<std::unique_ptr<QAbstractItemModel>()> fresh)
		: m_model(model)
		, m_fresh(std::move(fresh))
		, m_tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest)
		, m_resets(model, &QAbstractItemModel::modelAboutToBeReset)
		, m_moves(model, &QAbstractItemModel::rowsMoved)
	{
	}

	/// True if the live model equals a fresh one; prints both otherwise.
	bool consistent() const
	{
		const auto fresh = m_fresh();
		const QString expected = dump(*fresh);
		const QString actual = dump(*m_model);
		if (expected != actual)
		{
			qWarning().noquote() << "expected:\n" << expected << "actual:\n" << actual;
		}
		return expected == actual;
	}
	qsizetype resets() const { return m_resets.count(); }
	qsizetype moves() const { return m_moves.count(); }

private:
	QAbstractItemModel* m_model;
	std::function<std::unique_ptr<QAbstractItemModel>()> m_fresh;
	QAbstractItemModelTester m_tester;
	QSignalSpy m_resets;
	QSignalSpy m_moves;
};

template <class M>
std::function<std::unique_ptr<QAbstractItemModel>()> freshOf(ProjectSession* session)
{
	return [session] { return std::make_unique<M>(session); };
}

} // namespace

class TreeModelsTest : public QObject
{
	Q_OBJECT

private:
	std::unique_ptr<ProjectSession> m_session;
	testing::SampleIds m_ids;

	Project& project() { return m_session->project(); }

	/// Undoes everything, checking after each step, then redoes everything.
	void undoRedoAll(const Watch& watch)
	{
		int steps = 0;
		while (m_session->canUndo())
		{
			QVERIFY(m_session->undo());
			QVERIFY(watch.consistent());
			++steps;
		}
		for (int i = 0; i < steps; ++i)
		{
			QVERIFY(m_session->redo());
			QVERIFY(watch.consistent());
		}
		QCOMPARE(watch.resets(), 0);
	}

private Q_SLOTS:
	void init()
	{
		m_session = std::make_unique<ProjectSession>();
		Project p(IdGenerator::sequential());
		m_ids = testing::buildSampleProject(p);
		m_session->replaceProject(std::move(p));
	}

	void cleanup() { m_session.reset(); }

	void fr_top_1__topologyTreeAppliesEditsIncrementally()
	{
		viewmodels::TopologyTreeModel model(m_session.get());
		Watch watch(&model, freshOf<viewmodels::TopologyTreeModel>(m_session.get()));
		QVERIFY(watch.consistent());
		QCOMPARE(model.rowCount(), 1);
		QCOMPARE(model.indexOf(m_ids.dimmer, 1).data().toString(), u"1.1.1"_s);

		const auto area2 = must(addArea(project(), 2, u"Second area"_s));
		const auto area0 = must(addArea(project(), 0, u"Backbone"_s));
		QVERIFY(watch.consistent());
		QCOMPARE(model.index(0, 0).data(viewmodels::ShadowTreeModel::RefRole).toString(), EntityRef(area0).toString());
		const auto line21 = must(addLine(project(), area2, 1, u"Line 2.1"_s));
		const auto device = must(addDevice(project(), line21, 7, u"Sensor"_s));
		QVERIFY(watch.consistent());

		// Renumbering re-sorts the line: a move, not a remove + insert.
		must(setDeviceNumber(project(), m_ids.button, 0));
		QVERIFY(watch.consistent());
		QVERIFY(watch.moves() > 0);
		must(setDeviceNumber(project(), m_ids.button, std::nullopt));
		QVERIFY(watch.consistent());
		QCOMPARE(model.indexOf(m_ids.button, 1).data().toString(), u"1.1.–"_s);

		// Moving to another line keeps the row (cross-parent move).
		const auto movesBefore = watch.moves();
		must(moveDevice(project(), device, m_ids.line12));
		QVERIFY(watch.consistent());
		QVERIFY(watch.moves() > movesBefore);

		QSignalSpy dataChanged(&model, &QAbstractItemModel::dataChanged);
		must(rename(project(), m_ids.dimmer, u"Living room dimmer"_s));
		QVERIFY(watch.consistent());
		QVERIFY(dataChanged.count() >= 1);

		must(removeLine(project(), m_ids.line11)); // cascades devices, com objects, links
		QVERIFY(watch.consistent());
		must(removeArea(project(), m_ids.area1));
		QVERIFY(watch.consistent());
		QCOMPARE(watch.resets(), 0);
	}

	void fr_prj_2__topologyTreeFollowsUndoRedo()
	{
		viewmodels::TopologyTreeModel model(m_session.get());
		Watch watch(&model, freshOf<viewmodels::TopologyTreeModel>(m_session.get()));
		const auto area2 = must(addArea(project(), 2, u"Second area"_s));
		const auto line = must(addLine(project(), area2, 1, u"Line"_s));
		must(addDevice(project(), line, 1, u"A"_s));
		must(setDeviceNumber(project(), m_ids.dimmer, 9));
		must(moveDevice(project(), m_ids.actuator, line));
		must(removeArea(project(), m_ids.area1));
		QVERIFY(watch.consistent());
		undoRedoAll(watch);
	}

	void fr_bld_1__buildingTreeTracksPartsAndPlacement()
	{
		viewmodels::BuildingTreeModel model(m_session.get());
		Watch watch(&model, freshOf<viewmodels::BuildingTreeModel>(m_session.get()));
		QVERIFY(watch.consistent());
		// House > Ground floor > Living room > (Dimmer, Push button)
		const QModelIndex living = model.indexOf(m_ids.livingRoom);
		QCOMPARE(model.rowCount(living), 2);
		QCOMPARE(model.indexOf(m_ids.dimmer, 1).data().toString(), u"1.1.1"_s);

		const auto garage = must(addBuildingPart(project(), m_ids.groundFloor, BuildingPartKind::Room, u"Garage"_s));
		QVERIFY(watch.consistent());
		must(placeDevice(project(), m_ids.weather, garage));
		QVERIFY(watch.consistent());

		// FR-BLD-2: re-placing a device moves its row.
		const auto movesBefore = watch.moves();
		must(placeDevice(project(), m_ids.dimmer, m_ids.bedroom));
		QVERIFY(watch.consistent());
		QVERIFY(watch.moves() > movesBefore);
		must(placeDevice(project(), m_ids.button, std::nullopt));
		QVERIFY(watch.consistent());

		// Renumbering the line changes the address column of placed devices.
		must(project().edit(u"Renumber line"_s,
			[&](Tx& tx)
			{
				tx.set(m_ids.line11, &Line::number, 5u);
				return ok();
			}));
		QVERIFY(watch.consistent());

		must(removeBuildingPart(project(), m_ids.groundFloor)); // cascades rooms, unplaces devices
		QVERIFY(watch.consistent());
		undoRedoAll(watch);
	}

	void fr_bld_2__newPartAndPlacementInOneTransaction()
	{
		viewmodels::BuildingTreeModel model(m_session.get());
		Watch watch(&model, freshOf<viewmodels::BuildingTreeModel>(m_session.get()));
		// The device is still shown under the living room when its new parent is built: exercises the
		// "conflict" path (the subtree build skips it, the follow-up sync moves it).
		must(project().edit(u"New room with device"_s,
			[&](Tx& tx)
			{
				BuildingPart room;
				room.parent = m_ids.firstFloor;
				room.kind = BuildingPartKind::Room;
				room.name = u"Office"_s;
				const auto id = tx.insert(room);
				tx.set(m_ids.dimmer, &Device::place, std::optional<Id<BuildingPart>>(id));
				return ok();
			}));
		QVERIFY(watch.consistent());
		undoRedoAll(watch);
	}

	void fr_ga_1__groupAddressTreeFollowsRangesAndStyle()
	{
		viewmodels::GroupAddressTreeModel model(m_session.get());
		Watch watch(&model, freshOf<viewmodels::GroupAddressTreeModel>(m_session.get()));
		QVERIFY(watch.consistent());
		QCOMPARE(model.indexOf(m_ids.lighting, 1).data().toString(), u"0"_s);
		QCOMPARE(model.indexOf(m_ids.dimming, 1).data().toString(), u"0/1"_s);
		QCOMPARE(model.indexOf(m_ids.livingDim, 1).data().toString(), u"0/1/1"_s);
		QCOMPARE(model.indexOf(m_ids.livingDim, 2).data().toString(), u"DPST-3-7"_s);

		// Sorted by address: 0/1/0 goes before 0/1/1.
		const auto first = must(addGroupAddress(project(), knx::GroupAddress(0x0100), u"Central"_s, {}, m_ids.dimming));
		QVERIFY(watch.consistent());
		QCOMPARE(model.indexOf(first).row(), 0);
		must(addGroupAddress(project(), knx::GroupAddress(0x1001), u"Loose"_s));
		const auto middle = must(addGroupRange(project(), m_ids.heating, u"Valves"_s, 0x0900, 0x09FF));
		must(addGroupAddress(project(), knx::GroupAddress(0x0905), u"Valve 1"_s, u"DPST-5-1"_s, middle));
		QVERIFY(watch.consistent());

		must(setGroupAddressDpt(project(), m_ids.unlinked, u"DPST-1-8"_s));
		QVERIFY(watch.consistent());

		ProjectSettings settings = project().settings();
		settings.gaStyle = knx::GroupAddressStyle::TwoLevel;
		must(updateSettings(project(), settings));
		QVERIFY(watch.consistent());
		QCOMPARE(model.indexOf(m_ids.livingDim, 1).data().toString(), u"0/257"_s);
		settings.gaStyle = knx::GroupAddressStyle::Free;
		must(updateSettings(project(), settings));
		QVERIFY(watch.consistent());

		must(removeGroupRange(project(), m_ids.lighting));
		QVERIFY(watch.consistent());
		undoRedoAll(watch);
	}

	void fr_ga_3__comObjectTableShowsLinks()
	{
		viewmodels::ComObjectTableModel model(m_session.get(), m_ids.dimmer);
		Watch watch(&model, [this] { return std::make_unique<viewmodels::ComObjectTableModel>(m_session.get(), m_ids.dimmer); });
		QVERIFY(watch.consistent());
		QCOMPARE(model.rowCount(), 3);
		using C = viewmodels::ComObjectTableModel;
		QCOMPARE(model.indexOf(m_ids.dimmerSwitch, C::NameColumn).data().toString(), u"Switch"_s);
		QCOMPARE(model.indexOf(m_ids.dimmerDim, C::SizeColumn).data().toString(), u"4 bits"_s);
		QCOMPARE(model.indexOf(m_ids.dimmerStatus, C::FlagsColumn).data().toString(), u"CRT"_s);
		QCOMPARE(model.indexOf(m_ids.dimmerSwitch, C::GroupAddressesColumn).data().toString(), u"0/0/1"_s);

		// Linking changes the group-address column (first link = sending, listed first).
		must(link(project(), m_ids.dimmerSwitch, m_ids.unlinked));
		QCOMPARE(model.indexOf(m_ids.dimmerSwitch, C::GroupAddressesColumn).data().toString(), u"0/0/1, 0/0/3"_s);
		const auto links = project().linksOfComObject(m_ids.dimmerSwitch);
		must(setSendingLink(project(), links.back()));
		QCOMPARE(model.indexOf(m_ids.dimmerSwitch, C::GroupAddressesColumn).data().toString(), u"0/0/3, 0/0/1"_s);
		must(unlink(project(), links.back()));
		QVERIFY(watch.consistent());

		const auto added = must(addComObject(project(), m_ids.dimmer, testing::comObject(10, u"Lock"_s, 1, u"CW"_s, {})));
		must(addComObject(project(), m_ids.actuator, testing::comObject(10, u"Other device"_s, 1, u"CW"_s, {})));
		QVERIFY(watch.consistent());
		QCOMPARE(model.rowCount(), 4);
		QCOMPARE(model.indexOf(added).row(), 3);
		must(removeComObject(project(), m_ids.dimmerDim));
		QVERIFY(watch.consistent());
		must(removeDevice(project(), m_ids.dimmer));
		QVERIFY(watch.consistent());
		QCOMPARE(model.rowCount(), 0);
		undoRedoAll(watch);
		QCOMPARE(model.rowCount(), 0);
		QVERIFY(m_session->undo()); // restores the device and its group objects
		QCOMPARE(model.rowCount(), 3);
		QVERIFY(watch.consistent());
	}

	void fr_prj_2__renameThroughSetDataIsUndoable()
	{
		viewmodels::TopologyTreeModel model(m_session.get());
		Watch watch(&model, freshOf<viewmodels::TopologyTreeModel>(m_session.get()));
		const QModelIndex index = model.indexOf(m_ids.weather);
		QVERIFY(model.flags(index).testFlag(Qt::ItemIsEditable));
		QVERIFY(model.setData(index, u"Roof sensor"_s));
		QCOMPARE(project().devices().at(m_ids.weather).name, u"Roof sensor"_s);
		QCOMPARE(model.indexOf(m_ids.weather).data().toString(), u"Roof sensor"_s);
		QVERIFY(m_session->undo());
		QCOMPARE(model.indexOf(m_ids.weather).data().toString(), u"Weather station"_s);
		QVERIFY(watch.consistent());
	}

	void nfr_8__bulkChangesResetOnce()
	{
		viewmodels::GroupAddressTreeModel model(m_session.get());
		Watch watch(&model, freshOf<viewmodels::GroupAddressTreeModel>(m_session.get()));
		must(project().edit(u"Import"_s,
			[&](Tx& tx)
			{
				for (unsigned i = 0; i < 1000; ++i)
				{
					GroupAddress address;
					address.raw = static_cast<std::uint16_t>(0x1000 + i);
					address.name = u"GA %1"_s.arg(i);
					tx.insert(address);
				}
				return ok();
			}));
		QCOMPARE(watch.resets(), 1);
		QVERIFY(watch.consistent());
		QCOMPARE(model.rowCount(), 2 + 1000);
	}

	void fr_prj_1__projectReplacementResetsModels()
	{
		viewmodels::TopologyTreeModel model(m_session.get());
		Watch watch(&model, freshOf<viewmodels::TopologyTreeModel>(m_session.get()));
		m_session->newProject(u"Empty"_s);
		QCOMPARE(watch.resets(), 1);
		QCOMPARE(model.rowCount(), 0);
		QVERIFY(watch.consistent());
	}
};

QTEST_GUILESS_MAIN(TreeModelsTest)
#include "tst_TreeModels.moc"
