#include "kestrel/appconfig/Images.hpp"

#include <Printers.hpp>
#include <QtPrinters.hpp>
#include <UnitTests.hpp>

using namespace kestrel;
using namespace kestrel::appconfig;
using namespace Qt::StringLiterals;
using catalog::DynamicKind;
using catalog::DynamicNode;

namespace
{

Bytes hex(std::string_view text)
{
	return fromHex(text).value();
}

DynamicNode node(DynamicKind kind, QString element, catalog::AttributeMap attributes = {},
	std::vector<DynamicNode> children = {})
{
	DynamicNode n;
	n.kind = kind;
	n.element = std::move(element);
	n.attributes = std::move(attributes);
	n.children = std::move(children);
	return n;
}
DynamicNode paramRef(const QString& id)
{
	return node(DynamicKind::ParameterRefRef, u"ParameterRefRef"_s, {{u"RefId"_s, id}});
}
DynamicNode objectRef(const QString& id)
{
	return node(DynamicKind::ComObjectRefRef, u"ComObjectRefRef"_s, {{u"RefId"_s, id}});
}
DynamicNode choose(const QString& refId, std::vector<DynamicNode> whens)
{
	return node(DynamicKind::Choose, u"choose"_s, {{u"ParamRefId"_s, refId}}, std::move(whens));
}
DynamicNode when(const QString& test, std::vector<DynamicNode> children)
{
	return node(DynamicKind::When, u"when"_s, {{u"test"_s, test}}, std::move(children));
}
DynamicNode otherwise(std::vector<DynamicNode> children)
{
	return node(DynamicKind::When, u"when"_s, {{u"default"_s, u"true"_s}}, std::move(children));
}

/// A two-channel switch actuator in the shape of a real System B application (synthetic, M-FFFF):
/// "Mode" selects a pair function, each channel has a function parameter, and a top-level choose on a
/// parameter that is only shown in "blinds" mode.
catalog::ApplicationProgram testProgram()
{
	catalog::ApplicationProgram app;
	app.id = u"M-FFFF_A-0001-10-0001"_s;
	app.applicationNumber = 1;
	app.applicationVersion = 0x10;
	catalog::RelativeSegment segment;
	segment.id = u"RS-4"_s;
	segment.size = 8;
	segment.loadStateMachine = 4;
	segment.data = QByteArray::fromHex("aaaaaaaaaaaaaaaa");
	app.relativeSegments.push_back(segment);

	auto type = [&](QString id, catalog::ParameterTypeKind kind, unsigned bits, QString encoding = {})
	{
		catalog::ParameterType t;
		t.id = std::move(id);
		t.kind = kind;
		t.sizeInBit = bits;
		t.encoding = std::move(encoding);
		app.parameterTypes.push_back(t);
	};
	type(u"T-8"_s, catalog::ParameterTypeKind::Restriction, 8);
	type(u"T-1"_s, catalog::ParameterTypeKind::Number, 1);
	type(u"T-F"_s, catalog::ParameterTypeKind::Float, 0, u"DPT 9"_s);
	auto parameter = [&](QString id, QString type_, QString value, unsigned offset, unsigned bit)
	{
		catalog::Parameter p;
		p.id = std::move(id);
		p.parameterType = std::move(type_);
		p.value = std::move(value);
		p.memory = catalog::MemoryLocation{u"RS-4"_s, offset, bit};
		app.parameters.push_back(p);
		catalog::ParameterRef r;
		r.id = p.id + u"_R"_s;
		r.refId = p.id;
		app.parameterRefs.push_back(r);
	};
	parameter(u"P-Mode"_s, u"T-8"_s, u"0"_s, 0, 0);      // 0 off, 1 switch, 2 blinds
	parameter(u"P-FuncA"_s, u"T-8"_s, u"0"_s, 1, 0);     // 0 inactive, 1 switching
	parameter(u"P-Blinds"_s, u"T-8"_s, u"0"_s, 2, 0);    // only shown in blinds mode
	parameter(u"P-Invert"_s, u"T-1"_s, u"0"_s, 3, 7);    // one bit, the last of octet 3
	parameter(u"P-Lux"_s, u"T-F"_s, u"60000"_s, 4, 0);   // DPT 9 threshold
	// A second ref with another default for the blinds parameter, as real files have.
	catalog::ParameterRef second;
	second.id = u"P-Blinds_R2"_s;
	second.refId = u"P-Blinds"_s;
	second.value = u"1"_s;
	app.parameterRefs.push_back(second);

	auto object = [&](QString id, unsigned number, unsigned bits, catalog::ComObjectFlags flags)
	{
		catalog::ComObject o;
		o.id = id;
		o.number = number;
		o.sizeInBit = bits;
		o.flags = flags;
		o.attributes[u"Priority"_s] = u"Low"_s;
		app.comObjects.push_back(o);
		catalog::ComObjectRef r;
		r.id = id + u"_R"_s;
		r.refId = id;
		app.comObjectRefs.push_back(r);
	};
	const catalog::ComObjectFlags cw{true, false, true, false, false, false};
	const catalog::ComObjectFlags crt{true, true, false, true, false, false};
	object(u"O-1"_s, 1, 1, cw);  // switch A
	object(u"O-2"_s, 2, 1, crt); // status A
	object(u"O-3"_s, 3, 8, cw);  // blinds position
	object(u"O-4"_s, 4, 1, cw);  // never active

	app.dynamic = node(DynamicKind::Root, u"Dynamic"_s, {},
		{node(DynamicKind::ChannelIndependentBlock, u"ChannelIndependentBlock"_s, {},
			{paramRef(u"P-Mode_R"_s), paramRef(u"P-Lux_R"_s),
				choose(u"P-Mode_R"_s,
					{when(u"1"_s, {paramRef(u"P-FuncA_R"_s), paramRef(u"P-Invert_R"_s)}),
						when(u"2"_s, {paramRef(u"P-Blinds_R2"_s)}), otherwise({})}),
				choose(u"P-FuncA_R"_s, {when(u"1"_s, {objectRef(u"O-1_R"_s), objectRef(u"O-2_R"_s)})}),
				// Top level, but P-Blinds_R2 is only shown in blinds mode: inactive otherwise.
				choose(u"P-Blinds_R2"_s, {when(u"1"_s, {objectRef(u"O-3_R"_s)})})})});
	return app;
}

} // namespace

UNIT_TEST(FR_DEV_1__whenTestsMatchListsAndComparisons)
{
	ASSERT(matchesTest(u"2"_s, u"1 2 3"_s));
	ASSERT(!matchesTest(u"4"_s, u"1 2 3"_s));
	ASSERT(matchesTest(u"1.0E+001"_s, u"10"_s));
	ASSERT(matchesTest(u"5"_s, u"<6"_s));
	ASSERT(!matchesTest(u"6"_s, u"<6"_s));
	ASSERT(matchesTest(u"6"_s, u">=6"_s));
	ASSERT(matchesTest(u"0"_s, u"!=1"_s));
	ASSERT(matchesTest(u"abc"_s, u"abc"_s));
}

UNIT_TEST(FR_DEV_1__chooseOnHiddenParameterSelectsNothing)
{
	const auto app = testProgram();
	// Defaults: mode off → no channel parameters, no objects; the blinds choose is top level but its
	// parameter is hidden, so O-3 stays inactive although P-Blinds_R2 defaults to 1.
	auto evaluation = evaluate(app, {});
	ASSERT(evaluation.has_value());
	ASSERT_EQ(evaluation->parameterRefs, (QStringList{u"P-Mode_R"_s, u"P-Lux_R"_s}));
	ASSERT(evaluation->comObjectRefs.isEmpty());

	evaluation = evaluate(app, {{u"P-Mode"_s, u"2"_s}});
	ASSERT(evaluation->isParameterRefActive(u"P-Blinds_R2"_s));
	ASSERT_EQ(evaluation->comObjectRefs, QStringList{u"O-3_R"_s});

	evaluation = evaluate(app, {{u"P-Mode"_s, u"1"_s}, {u"P-FuncA"_s, u"1"_s}});
	ASSERT_EQ(evaluation->comObjectRefs, (QStringList{u"O-1_R"_s, u"O-2_R"_s}));
	ASSERT_EQ(valueOf(app, {}, u"P-Blinds_R2"_s), u"1"_s); // the ref's own default
	ASSERT_EQ(valueOf(app, {{u"P-Blinds"_s, u"0"_s}}, u"P-Blinds_R2"_s), u"0"_s); // values belong to the parameter
}

UNIT_TEST(FR_DEV_2__parameterCodecIsBitExact)
{
	ASSERT_EQ(encodeDpt9(60000.0), std::uint16_t{0x65B9}); // rounded mantissa (ETS6 image)
	ASSERT_EQ(encodeDpt9(21.5), std::uint16_t{0x0C33});
	ASSERT_EQ(encodeDpt9(-1.0), std::uint16_t{0x879C});
	ASSERT(std::abs(decodeDpt9(0x0C33) - 21.5) < 1e-9);

	catalog::ParameterType bit;
	bit.kind = catalog::ParameterTypeKind::Number;
	bit.sizeInBit = 3;
	Bytes segment(2, 0);
	ASSERT(writeParameter(segment, bit, {u"S"_s, 0, 6}, u"5"_s).has_value()); // 101 spans octets 0 and 1
	ASSERT_EQ(segment, hex("02 80"));
	ASSERT_EQ(*readParameter(segment, bit, {u"S"_s, 0, 6}), u"5"_s);

	catalog::ParameterType signedType;
	signedType.kind = catalog::ParameterTypeKind::Number;
	signedType.numberType = u"signedInt"_s;
	signedType.sizeInBit = 16;
	ASSERT(writeParameter(segment, signedType, {u"S"_s, 0, 0}, u"-2"_s).has_value());
	ASSERT_EQ(segment, hex("ff fe"));
	ASSERT_EQ(*readParameter(segment, signedType, {u"S"_s, 0, 0}), u"-2"_s);

	catalog::ParameterType text;
	text.kind = catalog::ParameterTypeKind::Text;
	text.sizeInBit = 32;
	Bytes four(4, 0xFF);
	ASSERT(writeParameter(four, text, {u"S"_s, 0, 0}, u"ab"_s).has_value());
	ASSERT_EQ(four, hex("61 62 00 00"));
	ASSERT(!writeParameter(four, text, {u"S"_s, 2, 0}, u"ab"_s).has_value()); // beyond the segment
}

UNIT_TEST(FR_DEV_2__parameterSegmentFollowsEtsRule)
{
	const auto app = testProgram();
	const ParameterValues values{{u"P-Mode"_s, u"1"_s}, {u"P-FuncA"_s, u"1"_s}, {u"P-Invert"_s, u"1"_s}};
	const auto evaluation = evaluate(app, values).value();
	QStringList warnings;
	const auto segments = buildParameterSegments(app, values, evaluation, warnings).value();
	// Data aa.., every parameter at its own default (P-Blinds 0 although its active-elsewhere ref says 1),
	// then the active refs at their values; untouched bits keep the data.
	ASSERT_EQ(segments.at(u"RS-4"_s), hex("01 01 00 ab 65 b9 aa aa"));
	ASSERT(warnings.isEmpty());

	QStringList decodeWarnings;
	const auto decoded = decodeParameterSegments(app, segments, decodeWarnings).value();
	ASSERT_EQ(decoded, values);
}

UNIT_TEST(FR_COM_2__groupObjectTableAndTables)
{
	const auto app = testProgram();
	const ParameterValues values{{u"P-Mode"_s, u"1"_s}, {u"P-FuncA"_s, u"1"_s}};
	// Object 1 linked (sending 1/0/6), object 2 linked to 2/0/6 and listening to 1/0/6, object 4 inactive.
	const Links links{{1, {0x0806}}, {2, {0x1006, 0x0806}}, {4, {0x0001}}};
	const auto images = buildSystemB(app, values, links).value();
	ASSERT_EQ(images.images.at(1), hex("00 02 08 06 10 06"));
	// Sending links first by object, then the listening one.
	ASSERT_EQ(images.images.at(2), hex("00 03 00 01 00 01 00 02 00 02 00 01 00 02"));
	// Count 4; object 1 CW+C low, object 2 CRT+C low, objects 3 and 4 inactive.
	ASSERT_EQ(images.images.at(3), hex("00 04 17 00 4f 00 00 00 00 00"));
	ASSERT_EQ(images.images.at(4).size(), std::size_t{8});
	ASSERT_EQ(images.warnings.size(), qsizetype{1}); // the link of inactive object 4

	// Without a link, C is cleared (as ETS6 does).
	const auto unlinked = buildSystemB(app, values, {}).value();
	ASSERT_EQ(unlinked.images.at(3), hex("00 04 13 00 4b 00 00 00 00 00"));
	ASSERT_EQ(unlinked.images.at(1), hex("00 00"));
	ASSERT_EQ(programVersion(u"M-0162"_s, app), hex("01 62 00 01 10"));
}
