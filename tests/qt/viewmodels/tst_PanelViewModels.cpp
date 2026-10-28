// Panel view models: Q_INVOKABLE intents, selection, action enablement, properties and device editor.

#include "SampleProject.hpp"

#include "kestrel/model/Operations.hpp"
#include "kestrel/viewmodels/ActionRegistry.hpp"
#include "kestrel/viewmodels/BuildingTreeModel.hpp"
#include "kestrel/viewmodels/BuildingsPanelVM.hpp"
#include "kestrel/viewmodels/DeviceEditorVM.hpp"
#include "kestrel/viewmodels/GroupAddressPanelVM.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"
#include "kestrel/viewmodels/PropertiesPanelVM.hpp"
#include "kestrel/viewmodels/SelectionService.hpp"
#include "kestrel/viewmodels/TopologyPanelVM.hpp"
#include "kestrel/viewmodels/Workspace.hpp"

#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace kestrel;
using model::EntityRef;

namespace
{
QString ref(EntityRef r)
{
	return r.toString();
}
} // namespace

class PanelViewModelsTest : public QObject
{
	Q_OBJECT

private:
	std::unique_ptr<viewmodels::Workspace> m_ws;
	testing::SampleIds m_ids;

	model::Project& project() { return m_ws->session()->project(); }

private Q_SLOTS:
	void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

	void init()
	{
		m_ws = std::make_unique<viewmodels::Workspace>();
		model::Project p(model::IdGenerator::sequential());
		m_ids = testing::buildSampleProject(p);
		m_ws->session()->replaceProject(std::move(p));
	}

	void cleanup() { m_ws.reset(); }

	void fr_top_1__topologyIntentsPickFreeNumbers()
	{
		auto* vm = m_ws->topology();
		const QString area = vm->addArea();
		QVERIFY(!area.isEmpty());
		QCOMPARE(project().areas().at(EntityRef::parse(area)->as<model::Area>()).number, 2u);
		const QString line = vm->addLine(area);
		QCOMPARE(project().lines().at(EntityRef::parse(line)->as<model::Line>()).number, 1u);
		// A device ref resolves to its line; the lowest free number is used.
		const QString device = vm->addDevice(ref(m_ids.dimmer), u"Blind actuator"_s);
		QCOMPARE(project().devices().at(EntityRef::parse(device)->as<model::Device>()).number, 4u);
		QCOMPARE(project().devices().at(EntityRef::parse(device)->as<model::Device>()).name, u"Blind actuator"_s);

		QVERIFY(vm->setDeviceNumber(device, u"9"_s));
		QVERIFY(!vm->setDeviceNumber(device, u"1"_s)); // taken
		QVERIFY(!vm->lastError().isEmpty());
		QVERIFY(!vm->setDeviceNumber(device, u"300"_s));
		QVERIFY(vm->setDeviceNumber(device, u""_s));
		QVERIFY(!project().devices().at(EntityRef::parse(device)->as<model::Device>()).number);
		QVERIFY(vm->addLine(ref(m_ids.house)).isEmpty()); // not a topology entity
		QVERIFY(vm->addDevice(u"dev-bogus"_s).isEmpty());

		QVERIFY(vm->rename(ref(m_ids.line12), u"Upstairs"_s));
		QCOMPARE(project().lines().at(m_ids.line12).name, u"Upstairs"_s);
		QVERIFY(vm->remove(ref(m_ids.line12)));
		QVERIFY(!project().lines().contains(m_ids.line12));
		QVERIFY(m_ws->session()->undo());
		QVERIFY(project().lines().contains(m_ids.line12));
		QVERIFY(vm->lastError().isEmpty());
	}

	void fr_bld_1__buildingIntentsAddAndPlace()
	{
		auto* vm = m_ws->buildings();
		const QString garage = vm->addBuildingPart(ref(m_ids.groundFloor), u"Room"_s, u"Garage"_s);
		QVERIFY(!garage.isEmpty());
		const QString building = vm->addBuildingPart({}, u"Building"_s);
		QCOMPARE(project().buildingParts().at(EntityRef::parse(building)->as<model::BuildingPart>()).name, u"Building 2"_s);
		QVERIFY(vm->addBuildingPart(ref(m_ids.dimmer), u"Room"_s).isEmpty());
		QVERIFY(vm->addBuildingPart({}, u"Spaceship"_s).isEmpty());

		// FR-BLD-2
		QVERIFY(vm->placeDevice(ref(m_ids.weather), garage));
		QCOMPARE(project().devices().at(m_ids.weather).place, EntityRef::parse(garage)->as<model::BuildingPart>());
		QVERIFY(vm->unplaceDevice(ref(m_ids.weather)));
		QVERIFY(!project().devices().at(m_ids.weather).place);
		QVERIFY(!vm->placeDevice(ref(m_ids.house), garage));
	}

	void fr_ga_1__groupAddressIntents()
	{
		auto* vm = m_ws->groupAddresses();
		const QString main = vm->addMainGroup();
		const auto& range = project().groupRanges().at(EntityRef::parse(main)->as<model::GroupRange>());
		QCOMPARE(range.start, 0x1000); // main groups 0 and 1 are taken
		QCOMPARE(range.end, 0x17FF);
		const QString middle = vm->addMiddleGroup(main);
		QCOMPARE(project().groupRanges().at(EntityRef::parse(middle)->as<model::GroupRange>()).start, 0x1000);
		QVERIFY(vm->addMiddleGroup(middle).isEmpty()); // only below main groups

		const QString address = vm->addGroupAddress(middle);
		QCOMPARE(project().groupAddresses().at(EntityRef::parse(address)->as<model::GroupAddress>()).raw, 0x1000);
		const QString next = vm->addGroupAddress(address); // same range as the selected address
		QCOMPARE(project().groupAddresses().at(EntityRef::parse(next)->as<model::GroupAddress>()).raw, 0x1001);

		// Explicit address: goes into the innermost range, DPT normalised.
		const QString explicitRef = vm->addGroupAddressAt(u"0/1/7"_s, u"Dim kitchen"_s, u"3.007"_s);
		const auto& ga = project().groupAddresses().at(EntityRef::parse(explicitRef)->as<model::GroupAddress>());
		QCOMPARE(ga.range, m_ids.dimming);
		QCOMPARE(ga.dpt, u"DPST-3-7"_s);
		QVERIFY(vm->addGroupAddressAt(u"0/1/7"_s, u"Again"_s).isEmpty()); // duplicate
		QVERIFY(vm->addGroupAddressAt(u"nonsense"_s, u"x"_s).isEmpty());

		QVERIFY(vm->setDpt(ref(m_ids.unlinked), u"9.001"_s));
		QCOMPARE(project().groupAddresses().at(m_ids.unlinked).dpt, u"DPST-9-1"_s);
		QVERIFY(!vm->setDpt(ref(m_ids.unlinked), u"not a dpt"_s));
		QVERIFY(vm->setDpt(ref(m_ids.unlinked), u""_s));
		QVERIFY(project().groupAddresses().at(m_ids.unlinked).dpt.isEmpty());
		QCOMPARE(viewmodels::normalizeDpt(u"DPT-5"_s), std::optional<QString>(u"DPT-5"_s));
	}

	void fr_prj_2__selectionIsPrunedAndActionsFollowIt()
	{
		auto* actions = m_ws->actions();
		auto* selection = m_ws->selection();
		QSignalSpy changed(selection, &viewmodels::SelectionService::selectionChanged);
		QVERIFY(!actions->isEnabled(u"edit.delete"_s));
		QVERIFY(!actions->isEnabled(u"topology.addLine"_s));

		selection->select(m_ids.dimmer, u"test"_s);
		QCOMPARE(changed.count(), 1);
		selection->select(m_ids.dimmer, u"other"_s); // same ref: no signal
		QCOMPARE(changed.count(), 1);
		QVERIFY(actions->isEnabled(u"edit.delete"_s));
		QVERIFY(actions->isEnabled(u"topology.addLine"_s));
		QVERIFY(actions->isEnabled(u"topology.addDevice"_s));
		QVERIFY(actions->isEnabled(u"device.open"_s));
		QVERIFY(actions->isEnabled(u"building.unplaceDevice"_s));
		QVERIFY(!actions->isEnabled(u"building.addRoom"_s));
		QVERIFY(!actions->isEnabled(u"ga.addMiddleGroup"_s));

		QSignalSpy open(m_ws.get(), &viewmodels::Workspace::openDeviceRequested);
		QVERIFY(actions->trigger(u"device.open"_s));
		QCOMPARE(open.count(), 1);
		selection->activate(m_ids.dimmerSwitch, u"test"_s); // a group object opens its device
		QCOMPARE(open.count(), 2);

		selection->select(m_ids.dimmer, u"test"_s);
		QVERIFY(actions->trigger(u"edit.delete"_s));
		QVERIFY(!project().devices().contains(m_ids.dimmer));
		QVERIFY(!selection->current());
		QVERIFY(!actions->isEnabled(u"edit.delete"_s));
		QVERIFY(actions->trigger(u"edit.undo"_s));
		QVERIFY(project().devices().contains(m_ids.dimmer));

		selection->select(m_ids.lighting, u"test"_s);
		QVERIFY(actions->isEnabled(u"ga.addMiddleGroup"_s));
		QVERIFY(actions->trigger(u"ga.addMiddleGroup"_s));
		QCOMPARE(project().groupRangesIn(m_ids.lighting).size(), 3u);
		QVERIFY(selection->current()->is<model::GroupRange>()); // the new range is selected
	}

	void fr_bld_1__treePanelPublishesAndRevealsSelection()
	{
		auto* buildings = m_ws->buildings();
		auto* topology = m_ws->topology();
		QSignalSpy reveal(buildings, &viewmodels::TreePanelVM::revealRequested);
		QSignalSpy revealTopology(topology, &viewmodels::TreePanelVM::revealRequested);
		topology->selectIndex(topology->indexOfRef(ref(m_ids.button)));
		QCOMPARE(m_ws->selection()->origin(), u"topology"_s);
		QCOMPARE(reveal.count(), 1); // the building tree also shows the button
		QCOMPARE(revealTopology.count(), 0); // not back to the origin
		QCOMPARE(reveal.first().at(0).value<QModelIndex>(), buildings->indexOfRef(ref(m_ids.button)));
		QCOMPARE(buildings->contextActions(m_ids.livingRoom).first(), u"building.addFloor"_s);

		QSignalSpy context(m_ws->actions(), &viewmodels::ActionRegistry::contextMenuRequested);
		buildings->contextMenuAt({});
		QCOMPARE(context.count(), 1);
		QCOMPARE(context.first().at(0).toStringList(), QStringList{u"building.addBuilding"_s});
	}

	void fr_ga_4__propertiesPanelEditsTheSelection()
	{
		auto* vm = m_ws->properties();
		QCOMPARE(vm->kind(), u"Project"_s);
		QVERIFY(vm->gaStyleVisible());
		QVERIFY(vm->setGaStyle(1));
		QCOMPARE(project().settings().gaStyle, knx::GroupAddressStyle::TwoLevel);

		QSignalSpy changed(vm, &viewmodels::PropertiesPanelVM::subjectChanged);
		m_ws->selection()->select(m_ids.button, u"test"_s);
		QVERIFY(changed.count() >= 1);
		QCOMPARE(vm->kind(), u"Device"_s);
		QCOMPARE(vm->address(), u"1.1.3"_s);
		QVERIFY(vm->numberEditable());
		QVERIFY(vm->setNumber(u"12"_s));
		QCOMPARE(vm->address(), u"1.1.12"_s);
		QVERIFY(!vm->setNumber(u"1"_s));
		QVERIFY(vm->setName(u"Door button"_s));
		QVERIFY(vm->setDescription(u"Next to the door"_s));
		QCOMPARE(project().devices().at(m_ids.button).description, u"Next to the door"_s);
		// Location: "(not placed)", House, Ground floor, Living room, Kitchen, First floor, Bedroom
		QCOMPARE(vm->placeOptions().size(), 7);
		QCOMPARE(vm->placeIndex(), 3);
		QVERIFY(vm->setPlaceIndex(6));
		QCOMPARE(project().devices().at(m_ids.button).place, m_ids.bedroom);
		QVERIFY(vm->setPlaceIndex(0));
		QVERIFY(!project().devices().at(m_ids.button).place);

		m_ws->selection()->select(m_ids.unlinked, u"test"_s);
		QVERIFY(vm->dptVisible());
		QVERIFY(vm->setDpt(u"1.001"_s));
		QCOMPARE(vm->dpt(), u"DPST-1-1"_s);
		QVERIFY(!vm->setDpt(u"x"_s));
		QVERIFY(!vm->lastError().isEmpty());

		m_ws->selection()->select(m_ids.dimmerDim, u"test"_s);
		QCOMPARE(vm->descriptionLabel(), u"Function"_s);
		QVERIFY(vm->details().contains(u"4 bits"_s));
		QVERIFY(!vm->setNumber(u"5"_s)); // only device numbers are editable here

		// Removing the subject falls back to the project.
		QVERIFY(model::removeComObject(project(), m_ids.dimmerDim).has_value());
		QCOMPARE(vm->kind(), u"Project"_s);
	}

	void fr_ga_3__deviceEditorManagesGroupObjectsAndLinks()
	{
		auto editor = m_ws->createDeviceEditor(m_ids.dimmer);
		QVERIFY(editor->exists());
		QCOMPARE(editor->title(), u"1.1.1 Dimmer"_s);
		QCOMPARE(editor->comObjects()->rowCount(), 3);
		QCOMPARE(editor->nextNumber(), 3);

		const QString lock = editor->addComObject(u"Lock"_s, u"Disable"_s, 1, u"cw"_s, u"1.003"_s);
		QVERIFY(!lock.isEmpty());
		QCOMPARE(editor->current(), lock);
		QCOMPARE(editor->comObjects()->rowCount(), 4);
		const auto lockId = EntityRef::parse(lock)->as<model::ComObject>();
		QCOMPARE(project().comObjects().at(lockId).dpt, u"DPST-1-3"_s);
		QVERIFY(editor->addComObject(u"Bad"_s, {}, 1, u"XYZ"_s).isEmpty());

		QVERIFY(editor->linkGroupAddress(lock, u"0/0/3"_s));
		QVERIFY(editor->linkGroupAddress(lock, u"0/0/1"_s));
		QVERIFY(!editor->linkGroupAddress(lock, u"0/0/1"_s)); // already linked
		QVERIFY(!editor->linkGroupAddress(lock, u"9/9/9"_s));
		QVERIFY(!editor->linkGroupAddress(ref(m_ids.actuatorSwitch), u"0/0/3"_s)); // other device
		QVariantList links = editor->currentLinks();
		QCOMPARE(links.size(), 2);
		QCOMPARE(links[0].toMap()[u"address"_s].toString(), u"0/0/3"_s);
		QVERIFY(links[0].toMap()[u"sending"_s].toBool());
		QVERIFY(editor->setSending(links[1].toMap()[u"ref"_s].toString()));
		QCOMPARE(editor->currentLinks()[0].toMap()[u"address"_s].toString(), u"0/0/1"_s);
		QVERIFY(editor->unlink(editor->currentLinks()[1].toMap()[u"ref"_s].toString()));
		QCOMPARE(editor->currentLinks().size(), 1);

		QVERIFY(editor->setFlags(lock, u"CRT"_s));
		QCOMPARE(project().comObjects().at(lockId).flags.toString(), u"CRT"_s);
		QVERIFY(editor->setDpt(lock, u"5.001"_s));
		QVERIFY(editor->setSize(lock, 8));
		QVERIFY(!editor->setSize(lock, 0));
		QVERIFY(editor->removeComObject(lock));
		QVERIFY(editor->current().isEmpty());

		QSignalSpy removed(editor.get(), &viewmodels::DeviceEditorVM::deviceRemoved);
		QVERIFY(model::removeDevice(project(), m_ids.dimmer).has_value());
		QCOMPARE(removed.count(), 1);
		QVERIFY(!editor->exists());
	}
};

QTEST_MAIN(PanelViewModelsTest)
#include "tst_PanelViewModels.moc"
