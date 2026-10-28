#include "../model/SampleProject.hpp"

#include "kestrel/persist/Persist.hpp"
#include "kestrel/validate/Validate.hpp"

#include <QTemporaryDir>

#include <Printers.hpp>
#include <QtPrinters.hpp>
#include <UnitTests.hpp>

#include <chrono>
#include <iostream>
#include <set>

using namespace kestrel;
using namespace kestrel::model;
using namespace kestrel::validate;
using namespace Qt::StringLiterals;

namespace
{

/// Builds projects with raw transactions, so that states operations refuse (duplicates…) can be created.
struct Builder
{
	Project project{IdGenerator::sequential()};
	Id<BuildingPart> room;
	Id<Area> area;
	Id<Line> line;

	Builder()
	{
		room = addBuildingPart(project, std::nullopt, BuildingPartKind::Room, u"Room"_s).value();
		area = addArea(project, 1, u"Area"_s).value();
		line = addLine(project, area, 1, u"Line"_s).value();
	}

	template <class T>
	Id<T> put(T entity)
	{
		Id<T> id;
		project
			.edit(u"put"_s,
				[&](Tx& tx)
				{
					id = tx.insert(std::move(entity));
					return ok();
				})
			.value();
		return id;
	}

	Id<Device> device(std::optional<unsigned> number, std::optional<Id<Line>> onLine = {}, bool placed = true)
	{
		Device d;
		d.line = onLine.value_or(line);
		d.number = number;
		if (placed)
			d.place = room;
		return put(d);
	}

	Id<ComObject> object(Id<Device> owner, unsigned sizeInBit, const QString& flags)
	{
		ComObject o;
		o.device = owner;
		o.sizeInBit = sizeInBit;
		o.flags = *ComObjectFlags::parse(flags);
		return put(o);
	}

	Id<GroupAddress> address(std::uint16_t raw, const QString& dpt)
	{
		GroupAddress g;
		g.raw = raw;
		g.dpt = dpt;
		return put(g);
	}

	Id<GroupRange> range(std::optional<Id<GroupRange>> parent, std::uint16_t start, std::uint16_t end)
	{
		GroupRange r;
		r.parent = parent;
		r.start = start;
		r.end = end;
		return put(r);
	}

	Id<Link> connect(Id<ComObject> object, Id<GroupAddress> address, bool sending)
	{
		Link l;
		l.comObject = object;
		l.groupAddress = address;
		l.sending = sending;
		return put(l);
	}
};

std::vector<Diagnostic> runRule(Rule rule, const Project& project)
{
	DiagnosticSink sink;
	rule(project, sink);
	return sink.take();
}

Diagnostic diagnostic(const char* code, Severity severity, const QString& message, std::vector<EntityRef> subjects)
{
	return Diagnostic{QString::fromLatin1(code), severity, message, std::move(subjects)};
}

} // namespace

UNIT_TEST(FR_PRJ_1__sampleProjectHasNoDiagnostics, SampleProject)
{
	ASSERT_EQ(validate::run(project), std::vector<Diagnostic>{});
}

UNIT_TEST(FR_TOP_2__TOP001_duplicateIndividualAddress)
{
	Builder b;
	const auto first = b.device(5u);
	const auto second = b.device(5u);
	b.device(6u);
	const auto otherLine = addLine(b.project, b.area, 2, u"Line 2"_s).value();
	b.device(5u, otherLine); // 1.2.5: different address
	b.device(std::nullopt);
	b.device(std::nullopt); // unaddressed devices never collide
	ASSERT_EQ(runRule(&checkDuplicateIndividualAddresses, b.project),
		std::vector<Diagnostic>{
			diagnostic("TOP-001", Severity::Error, u"Duplicate individual address"_s, {first, second})});
}

UNIT_TEST(FR_TOP_2__TOP002_deviceNumberZero)
{
	Builder b;
	const auto coupler = b.device(0u);
	b.device(1u);
	b.device(std::nullopt);
	ASSERT_EQ(runRule(&checkDeviceNumberZero, b.project),
		std::vector<Diagnostic>{
			diagnostic("TOP-002", Severity::Error, u"Device number 0 is reserved for couplers"_s, {coupler})});
}

UNIT_TEST(FR_TOP_1__TOP003_deviceWithoutAddress)
{
	Builder b;
	b.device(1u);
	const auto fresh = b.device(std::nullopt);
	ASSERT_EQ(runRule(&checkDevicesWithoutAddress, b.project),
		std::vector<Diagnostic>{
			diagnostic("TOP-003", Severity::Info, u"Device has no individual address yet"_s, {fresh})});
}

UNIT_TEST(FR_GA_1__GA001_duplicateGroupAddress)
{
	Builder b;
	const auto a = b.address(0x0901, u"DPST-1-1"_s);
	b.address(0x0902, u"DPST-1-1"_s);
	const auto c = b.address(0x0901, u""_s);
	const auto d = b.address(0x0901, u"DPT-5"_s);
	ASSERT_EQ(runRule(&checkDuplicateGroupAddresses, b.project),
		std::vector<Diagnostic>{diagnostic("GA-001", Severity::Error, u"Duplicate group address"_s, {a, c, d})});
}

UNIT_TEST(FR_GA_4__GA002_groupAddressWithoutDpt)
{
	Builder b;
	b.address(1, u"DPST-1-1"_s);
	const auto missing = b.address(2, u""_s);
	ASSERT_EQ(runRule(&checkGroupAddressesWithoutDpt, b.project),
		std::vector<Diagnostic>{
			diagnostic("GA-002", Severity::Warning, u"Group address has no datapoint type"_s, {missing})});
}

UNIT_TEST(FR_GA_4__GA003_linkedObjectSizeDiffersFromDpt)
{
	Builder b;
	const auto dev = b.device(1u);
	const auto temperature = b.address(1, u"DPST-9-1"_s);
	const auto noDpt = b.address(2, u""_s);
	const auto unknownDpt = b.address(3, u"DPT-999"_s);
	const auto oneBit = b.object(dev, 1, u"CWT"_s);
	const auto twoBytes = b.object(dev, 16, u"CRT"_s);
	b.connect(oneBit, temperature, true);
	b.connect(twoBytes, temperature, true);
	b.connect(oneBit, noDpt, false);
	b.connect(oneBit, unknownDpt, false);
	ASSERT_EQ(runRule(&checkDptSizeMismatch, b.project),
		std::vector<Diagnostic>{diagnostic("GA-003", Severity::Error,
			u"Linked communication object size differs from the group address datapoint type"_s,
			{temperature, oneBit})});
}

UNIT_TEST(FR_GA_4__dptSizeFromAllNotations)
{
	ASSERT_EQ(dptSizeInBits(u"DPST-9-1"_s), std::optional<unsigned>(16));
	ASSERT_EQ(dptSizeInBits(u"DPT-9"_s), std::optional<unsigned>(16));
	ASSERT_EQ(dptSizeInBits(u"9.001"_s), std::optional<unsigned>(16));
	ASSERT_EQ(dptSizeInBits(u"9"_s), std::optional<unsigned>(16));
	ASSERT_EQ(dptSizeInBits(u"DPST-1-1"_s), std::optional<unsigned>(1));
	ASSERT_EQ(dptSizeInBits(u"DPST-3-7"_s), std::optional<unsigned>(4));
	ASSERT_EQ(dptSizeInBits(u"DPT-16"_s), std::optional<unsigned>(112));
	ASSERT_EQ(dptSizeInBits(u"DPST-232-600"_s), std::optional<unsigned>(24));
	ASSERT_EQ(dptSizeInBits(u"14.068"_s), std::optional<unsigned>(32));
	ASSERT_EQ(dptSizeInBits(u""_s), std::optional<unsigned>());
	ASSERT_EQ(dptSizeInBits(u"DPST-99-1"_s), std::optional<unsigned>());
	ASSERT_EQ(dptSizeInBits(u"DPST--1"_s), std::optional<unsigned>());
	ASSERT_EQ(dptSizeInBits(u"DPT-"_s), std::optional<unsigned>());
	ASSERT_EQ(dptSizeInBits(u"abc"_s), std::optional<unsigned>());
}

UNIT_TEST(FR_GA_3__GA004_groupAddressWithoutLinks)
{
	Builder b;
	const auto dev = b.device(1u);
	const auto linked = b.address(1, u"DPST-1-1"_s);
	const auto lonely = b.address(2, u"DPST-1-1"_s);
	b.connect(b.object(dev, 1, u"CWT"_s), linked, true);
	ASSERT_EQ(runRule(&checkGroupAddressesWithoutLinks, b.project),
		std::vector<Diagnostic>{diagnostic("GA-004", Severity::Warning, u"Group address has no links"_s, {lonely})});
}

UNIT_TEST(FR_GA_3__GA005_groupAddressWithoutSender)
{
	Builder b;
	const auto dev = b.device(1u);
	const auto listenersOnly = b.address(1, u"DPST-1-1"_s);
	const auto withSender = b.address(2, u"DPST-1-1"_s);
	b.address(3, u"DPST-1-1"_s); // no links at all: GA-004's business
	const auto actuator = b.object(dev, 1, u"CW"_s);
	const auto sensor = b.object(dev, 1, u"CT"_s);
	b.connect(actuator, listenersOnly, true);
	b.connect(actuator, withSender, false);
	b.connect(sensor, withSender, true);
	ASSERT_EQ(runRule(&checkGroupAddressesWithoutSender, b.project),
		std::vector<Diagnostic>{diagnostic("GA-005", Severity::Warning,
			u"Group address has links but no sender (no linked communication object with the T flag)"_s,
			{listenersOnly})});
}

UNIT_TEST(FR_GA_3__CO001_multipleSendingLinks)
{
	Builder b;
	const auto dev = b.device(1u);
	const auto ga1 = b.address(1, u"DPST-1-1"_s);
	const auto ga2 = b.address(2, u"DPST-1-1"_s);
	const auto broken = b.object(dev, 1, u"CWT"_s);
	const auto fine = b.object(dev, 1, u"CWT"_s);
	b.connect(broken, ga1, true);
	b.connect(broken, ga2, true);
	b.connect(fine, ga1, true);
	b.connect(fine, ga2, false);
	ASSERT_EQ(runRule(&checkMultipleSendingLinks, b.project),
		std::vector<Diagnostic>{diagnostic("CO-001", Severity::Warning,
			u"Communication object has more than one sending group address"_s, {broken})});
}

UNIT_TEST(FR_BLD_2__BLD001_deviceNotPlaced)
{
	Builder b;
	b.device(1u);
	const auto unplaced = b.device(2u, std::nullopt, false);
	ASSERT_EQ(runRule(&checkDevicesNotPlaced, b.project),
		std::vector<Diagnostic>{
			diagnostic("BLD-001", Severity::Info, u"Device is not placed in a building part"_s, {unplaced})});
}

UNIT_TEST(FR_PRJ_1__runSortsBySeverityThenCodeThenSubject)
{
	Builder b;
	const auto unplaced = b.device(std::nullopt, std::nullopt, false); // TOP-003 + BLD-001
	const auto zero = b.device(0u);                                     // TOP-002
	const auto ga = b.address(1, u""_s);                                // GA-002 + GA-004
	const auto dupA = b.address(2, u"DPST-1-1"_s);                      // GA-001 + GA-004
	const auto dupB = b.address(2, u"DPST-1-1"_s);                      // GA-001 + GA-004

	const std::vector<Diagnostic> result = validate::run(b.project);
	std::vector<std::string> summary;
	for (const Diagnostic& d : result)
	{
		std::string line = d.code.toStdString();
		for (const EntityRef& subject : d.subjects)
			line += ' ' + subject.toString().toStdString();
		summary.push_back(line);
	}
	const auto entry = [](const char* code, std::vector<EntityRef> subjects)
	{
		std::string line = code;
		for (const EntityRef& subject : subjects)
			line += ' ' + subject.toString().toStdString();
		return line;
	};
	const std::vector<std::string> expected{
		entry("GA-001", {dupA, dupB}),
		entry("TOP-002", {zero}),
		entry("GA-002", {ga}),
		entry("GA-004", {ga}),
		entry("GA-004", {dupA}),
		entry("GA-004", {dupB}),
		entry("BLD-001", {unplaced}),
		entry("TOP-003", {unplaced}),
	};
	ASSERT_EQ(summary, expected);
	ASSERT(result.front().severity == Severity::Error);
	ASSERT(result.back().severity == Severity::Info);
}

UNIT_TEST(FR_PRJ_1__ruleTableIsComplete)
{
	std::set<std::string> codes;
	for (const RuleInfo& info : rules())
		codes.insert(info.code);
	const std::set<std::string> expected{"TOP-001", "TOP-002", "TOP-003", "TOP-004", "GA-001", "GA-002", "GA-003",
		"GA-004", "GA-005", "GA-006", "GA-007", "CO-001", "CO-002", "CO-003", "BLD-001"};
	ASSERT(codes == expected);
	ASSERT_EQ(rules().size(), expected.size());
}

UNIT_TEST(FR_GA_4__validationPerformance1000Devices10000GroupAddresses)
{
	Project project(IdGenerator::sequential());
	const auto area = addArea(project, 1, u"Area"_s).value();
	ASSERT(project
			   .edit(u"Bulk"_s,
				   [&](Tx& tx)
				   {
					   std::uint16_t raw = 1;
					   for (unsigned lineNumber = 0; lineNumber < 10; ++lineNumber)
					   {
						   Line line;
						   line.area = area;
						   line.number = lineNumber;
						   const Id<Line> lineId = tx.insert(line);
						   for (unsigned d = 1; d <= 100; ++d)
						   {
							   Device device;
							   device.line = lineId;
							   device.number = d;
							   const Id<Device> deviceId = tx.insert(device);
							   for (unsigned c = 0; c < 10; ++c)
							   {
								   ComObject object;
								   object.device = deviceId;
								   object.number = c;
								   object.flags = *ComObjectFlags::parse(u"CWT"_s);
								   GroupAddress address;
								   address.raw = raw++;
								   address.dpt = u"DPST-1-1"_s;
								   Link l;
								   l.comObject = tx.insert(object);
								   l.groupAddress = tx.insert(address);
								   l.sending = true;
								   tx.insert(l);
							   }
						   }
					   }
					   return ok();
				   })
			   .has_value());
	ASSERT_EQ(project.devices().size(), std::size_t{1000});
	ASSERT_EQ(project.groupAddresses().size(), std::size_t{10000});
	ASSERT_EQ(project.links().size(), std::size_t{10000});

	const auto start = std::chrono::steady_clock::now();
	const std::vector<Diagnostic> result = validate::run(project);
	const auto elapsed = std::chrono::steady_clock::now() - start;
	std::cout << "    validate::run on 1000 devices / 10000 group addresses: "
			  << static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()) / 1000.0 << " ms"
			  << std::endl;
	ASSERT_EQ(result.size(), std::size_t{1000}); // BLD-001 only: no device is placed
}

// --- Rules guarding states that operations refuse (hand-edited / imported files) -----------------------------

UNIT_TEST(FR_TOP_2__TOP004_duplicateAreaAndLineNumbers)
{
	Builder b;
	Area area;
	area.number = 1;
	const auto twin = b.put(area); // area 1 again
	Line line;
	line.area = b.area;
	line.number = 1;
	const auto twinLine = b.put(line); // line 1.1 again
	line.area = twin;
	b.put(line); // 1.1 in the other area 1: only the area duplicate is reported
	ASSERT_EQ(runRule(&checkDuplicateAreaAndLineNumbers, b.project),
		(std::vector<Diagnostic>{
			diagnostic("TOP-004", Severity::Error, u"Duplicate area number"_s, {b.area, twin}),
			diagnostic("TOP-004", Severity::Error, u"Duplicate line number in area"_s, {b.line, twinLine})}));
}

UNIT_TEST(FR_GA_1__GA006_groupAddressZero)
{
	Builder b;
	const auto zero = b.address(0, u"DPST-1-1"_s);
	b.address(1, u"DPST-1-1"_s);
	ASSERT_EQ(runRule(&checkGroupAddressZero, b.project),
		std::vector<Diagnostic>{diagnostic("GA-006", Severity::Error, u"Group address 0/0/0 is reserved"_s, {zero})});
}

UNIT_TEST(FR_GA_2__GA007_groupRangeConsistency)
{
	Builder b;
	const auto main = b.range(std::nullopt, 0x0800, 0x0FFF);
	const auto middle = b.range(main, 0x0900, 0x09FF);
	const auto escaped = b.range(main, 0x0F00, 0x10FF);       // outside its parent
	const auto overlapping = b.range(std::nullopt, 0x0F80, 0x17FF); // overlaps main
	GroupAddress inside;
	inside.raw = 0x0901;
	inside.range = middle;
	b.put(inside);
	GroupAddress outside;
	outside.raw = 0x0A01;
	outside.range = middle;
	const auto stray = b.put(outside);
	ASSERT_EQ(runRule(&checkGroupRangeConsistency, b.project),
		(std::vector<Diagnostic>{
			diagnostic("GA-007", Severity::Error, u"Group address is outside its group range"_s, {stray, middle}),
			diagnostic("GA-007", Severity::Error, u"Group range is outside its parent range"_s, {escaped, main}),
			diagnostic("GA-007", Severity::Error, u"Sibling group ranges overlap"_s, {main, overlapping})}));
}

UNIT_TEST(FR_GA_3__CO002_duplicateLink)
{
	Builder b;
	const auto dev = b.device(1u);
	const auto object = b.object(dev, 1, u"CWT"_s);
	const auto ga = b.address(1, u"DPST-1-1"_s);
	const auto first = b.connect(object, ga, true);
	const auto again = b.connect(object, ga, false);
	b.connect(object, b.address(2, u"DPST-1-1"_s), false);
	ASSERT_EQ(runRule(&checkDuplicateLinks, b.project),
		std::vector<Diagnostic>{diagnostic("CO-002", Severity::Error,
			u"Communication object is linked twice to the same group address"_s, {first, again})});
}

UNIT_TEST(FR_TOP_1__CO003_duplicateComObjectNumbers)
{
	Builder b;
	const auto dev = b.device(1u);
	const auto other = b.device(2u);
	const auto a = b.object(dev, 1, u"CW"_s); // number 0
	const auto c = b.object(dev, 1, u"CW"_s); // number 0 again
	b.object(other, 1, u"CW"_s);             // number 0 on another device: fine
	ASSERT_EQ(runRule(&checkDuplicateComObjectNumbers, b.project),
		std::vector<Diagnostic>{diagnostic("CO-003", Severity::Error,
			u"Duplicate communication object number on device"_s, {a, c})});
}

UNIT_TEST(FR_PRJ_1__loadIsLenientAndValidationReportsForbiddenStates)
{
	Builder b;
	Area area;
	area.number = 1;
	b.put(area);
	const auto dev = b.device(1u);
	const auto object = b.object(dev, 1, u"CWT"_s);
	b.object(dev, 1, u"CWT"_s);
	const auto zero = b.address(0, u"DPST-1-1"_s);
	b.connect(object, zero, true);
	b.connect(object, zero, false);
	const auto main = b.range(std::nullopt, 0x0800, 0x0FFF);
	GroupAddress outside;
	outside.raw = 0x0001;
	outside.range = main;
	outside.dpt = u"DPST-1-1"_s;
	b.connect(object, b.put(outside), false);

	QTemporaryDir temp;
	ASSERT(persist::save(b.project, temp.path()).has_value());
	const auto loaded = persist::load(temp.path());
	ASSERT(loaded.has_value());
	std::set<std::string> codes;
	for (const Diagnostic& d : validate::run(*loaded))
		codes.insert(d.code.toStdString());
	for (const char* code : {"TOP-004", "GA-006", "GA-007", "CO-002", "CO-003"})
		ASSERT(codes.contains(code));
}
