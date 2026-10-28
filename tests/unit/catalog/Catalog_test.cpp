// Application-program model: parameters, com objects, the merged com-object view, channels, translations.

#include "CatalogFixture.hpp"

#include "kestrel/catalog/Knxprod.hpp"
#include "kestrel/catalog/Model.hpp"

#include <Printers.hpp>
#include <QtPrinters.hpp>
#include <UnitTests.hpp>

using namespace kestrel;
using namespace kestrel::catalog;
using namespace Qt::StringLiterals;

namespace
{

/// Parsed once per call from the fixture folder; tests assert on copies.
ProductDatabase database()
{
	auto loaded = parsePackage(fixture::folderFiles());
	return loaded ? loaded->database : ProductDatabase{};
}

ApplicationProgram program(const char16_t* id)
{
	const ProductDatabase db = database();
	const ApplicationProgram* app = db.findApplicationProgram(QStringView(id));
	return app ? *app : ApplicationProgram{};
}

QString button(const QString& suffix)
{
	return QString::fromUtf16(fixture::kButton) + suffix;
}

QString actuator(const QString& suffix)
{
	return QString::fromUtf16(fixture::kActuator) + suffix;
}

} // namespace

UNIT_TEST(FR_CAT_1__applicationProgramAttributes)
{
	const ApplicationProgram app = program(fixture::kButton);
	ASSERT_EQ(app.id, button(u""_s));
	ASSERT_EQ(app.name, u"Kestrel Testtaster 2fach"_s);
	ASSERT_EQ(app.applicationNumber, 1u);
	ASSERT_EQ(app.applicationVersion, 16u);
	ASSERT_EQ(app.programType, u"ApplicationProgram"_s);
	ASSERT_EQ(app.maskVersion, u"MV-07B0"_s);
	ASSERT_EQ(app.loadProcedureStyle, u"MergedProcedure"_s);
	ASSERT_EQ(app.peiType, 0u);
	ASSERT_FALSE(app.dynamicTableManagement);
	ASSERT_EQ(app.defaultLanguage, u"de-DE"_s);
	ASSERT_EQ(app.attributes.at(u"MinEtsVersion"_s), u"5.0"_s); // anything else stays available raw
	ASSERT_EQ(app.attributes.at(u"ReplacesVersions"_s), u"1 2"_s);
	ASSERT_FALSE(app.hasModuleDefs);

	const ApplicationProgram act = program(fixture::kActuator);
	ASSERT_EQ(act.maskVersion, u"MV-0705"_s);
	ASSERT_EQ(act.applicationNumber, 2u);
	ASSERT_EQ(act.applicationVersion, 33u);
	ASSERT_EQ(act.loadProcedureStyle, u"ProductProcedure"_s);
}

UNIT_TEST(FR_CAT_1__codeSegments)
{
	const ApplicationProgram app = program(fixture::kButton);
	ASSERT_EQ(app.relativeSegments.size(), std::size_t{3});
	ASSERT(app.absoluteSegments.empty());
	const RelativeSegment* parameters = app.findRelativeSegment(button(u"_RS-04-00000"_s));
	ASSERT(parameters != nullptr);
	ASSERT_EQ(parameters->name, u"Parameters"_s);
	ASSERT_EQ(parameters->offset, 0u);
	ASSERT_EQ(parameters->size, 48u);
	ASSERT_EQ(parameters->loadStateMachine, 4u);

	const ApplicationProgram act = program(fixture::kActuator);
	ASSERT_EQ(act.absoluteSegments.size(), std::size_t{4});
	ASSERT(act.relativeSegments.empty());
	const AbsoluteSegment* table = act.findAbsoluteSegment(actuator(u"_AS-0116"_s));
	ASSERT(table != nullptr);
	ASSERT_EQ(table->address, 278u);
	ASSERT_EQ(table->size, 130u);
	ASSERT_FALSE(table->data.has_value());
	const AbsoluteSegment* params = act.findAbsoluteSegment(actuator(u"_AS-4000"_s));
	ASSERT(params != nullptr && params->data.has_value());
	ASSERT_EQ(params->address, 16384u);
	ASSERT_EQ(*params->data, QByteArray::fromHex("000102030405060708090a0b0c0d0e0f"));
}

UNIT_TEST(FR_DEV_1__parameterTypes)
{
	const ApplicationProgram app = program(fixture::kButton);
	ASSERT_EQ(app.parameterTypes.size(), std::size_t{9});

	const ParameterType* debounce = app.findParameterType(button(u"_PT-Debounce"_s));
	ASSERT(debounce != nullptr);
	ASSERT_EQ(debounce->kind, ParameterTypeKind::Number);
	ASSERT_EQ(debounce->sizeInBit, 8u);
	ASSERT_EQ(debounce->numberType, u"unsignedInt"_s);
	ASSERT_EQ(debounce->minimum, std::optional<qint64>(10));
	ASSERT_EQ(debounce->maximum, std::optional<qint64>(250));

	const ParameterType* function = app.findParameterType(button(u"_PT-ButtonFunction"_s));
	ASSERT(function != nullptr);
	ASSERT_EQ(function->kind, ParameterTypeKind::Restriction);
	ASSERT_EQ(function->base, u"Value"_s);
	ASSERT_EQ(function->enumeration.size(), std::size_t{3});
	ASSERT_EQ(function->enumeration[1].value, u"1"_s);
	ASSERT_EQ(function->enumeration[1].text, u"Umschalten"_s);
	ASSERT_EQ(app.text(function->enumeration[1].id, u"Text"_s, u"en-US"_s), u"Toggle"_s);

	const ParameterType* name = app.findParameterType(button(u"_PT-DeviceName"_s));
	ASSERT(name != nullptr);
	ASSERT_EQ(name->kind, ParameterTypeKind::Text);
	ASSERT_EQ(name->sizeInBit, 80u);

	const ParameterType* longPress = app.findParameterType(button(u"_PT-LongPress"_s));
	ASSERT(longPress != nullptr);
	ASSERT_EQ(longPress->kind, ParameterTypeKind::Float);
	ASSERT_EQ(longPress->encoding, u"IEEE-754 Single"_s);
	ASSERT_EQ(longPress->minimumFloat, std::optional<double>(0.5));
	ASSERT_EQ(longPress->maximumFloat, std::optional<double>(10.0));

	ASSERT_EQ(app.findParameterType(button(u"_PT-Info"_s))->kind, ParameterTypeKind::None);
	const ParameterType* logo = app.findParameterType(button(u"_PT-Logo"_s));
	ASSERT_EQ(logo->kind, ParameterTypeKind::Unsupported);
	ASSERT_EQ(logo->element, u"TypePicture"_s);
	ASSERT_EQ(logo->attributes.at(u"RefId"_s), u"M-FFFF_BG--logo.2Epng"_s);
}

UNIT_TEST(FR_DEV_1__parametersMemoryAndUnion)
{
	const ApplicationProgram app = program(fixture::kButton);
	ASSERT_EQ(app.parameters.size(), std::size_t{10});

	const Parameter* debounce = app.findParameter(button(u"_P-1"_s));
	ASSERT(debounce != nullptr);
	ASSERT_EQ(debounce->name, u"Debounce"_s);
	ASSERT_EQ(debounce->parameterType, button(u"_PT-Debounce"_s));
	ASSERT_EQ(debounce->value, u"50"_s);
	ASSERT_EQ(debounce->access, u"ReadWrite"_s);
	ASSERT_EQ(debounce->memory, std::optional<MemoryLocation>(MemoryLocation{button(u"_RS-04-00000"_s), 0, 0}));
	ASSERT_FALSE(debounce->unionMember.has_value());

	ASSERT_EQ(app.unions.size(), std::size_t{1});
	const Union& group = app.unions[0];
	ASSERT_EQ(group.sizeInBit, 8u);
	ASSERT_EQ(group.memory, std::optional<MemoryLocation>(MemoryLocation{button(u"_RS-04-00000"_s), 1, 0}));
	ASSERT_EQ(group.parameters, (QStringList{button(u"_UP-2"_s), button(u"_UP-3"_s)}));

	const Parameter* mode = app.findParameter(button(u"_UP-2"_s));
	ASSERT(mode != nullptr && mode->unionMember.has_value());
	ASSERT(mode->unionMember->isDefault);
	ASSERT_FALSE(mode->memory.has_value());
	const Parameter* brightness = app.findParameter(button(u"_UP-3"_s));
	ASSERT(brightness != nullptr && brightness->unionMember.has_value());
	ASSERT_EQ(*brightness->unionMember, (UnionMember{0, 0, 4, false}));
	ASSERT_EQ(app.memoryOf(*brightness), std::optional<MemoryLocation>(MemoryLocation{button(u"_RS-04-00000"_s), 1, 4}));
	ASSERT_EQ(app.memoryOf(*debounce), debounce->memory);

	const Parameter* info = app.findParameter(button(u"_P-8"_s));
	ASSERT(info != nullptr);
	ASSERT_FALSE(app.memoryOf(*info).has_value());

	const Parameter* blink = app.findParameter(button(u"_P-10"_s));
	ASSERT(blink != nullptr && blink->property.has_value());
	ASSERT_EQ(*blink->property, (PropertyLocation{0u, std::nullopt, 54, 0, 7}));
	ASSERT_FALSE(blink->memory.has_value());
}

UNIT_TEST(FR_DEV_1__parameterRefsOverride)
{
	const ApplicationProgram app = program(fixture::kButton);
	ASSERT_EQ(app.parameterRefs.size(), std::size_t{10});
	const ParameterRef* plain = app.findParameterRef(button(u"_P-4_R-1"_s));
	ASSERT(plain != nullptr);
	ASSERT_EQ(plain->refId, button(u"_P-4"_s));
	ASSERT_FALSE(plain->text.has_value());
	ASSERT_FALSE(plain->value.has_value());
	const ParameterRef* overriding = app.findParameterRef(button(u"_P-5_R-1"_s));
	ASSERT(overriding != nullptr);
	ASSERT_EQ(overriding->text, std::optional<QString>(u"Funktion zweiter Taster"_s));
	ASSERT_EQ(overriding->value, std::optional<QString>(u"0"_s));
	ASSERT_EQ(overriding->attributes.at(u"Tag"_s), u"b"_s);
	ASSERT_EQ(app.text(overriding->id, u"Text"_s, u"en-US"_s), u"Function of the second button"_s);
	ASSERT_EQ(app.findParameterRef(button(u"_P-10_R-1"_s))->access, std::optional<QString>(u"None"_s));
}

UNIT_TEST(FR_GA_3__comObjectsAndRefs)
{
	const ApplicationProgram app = program(fixture::kButton);
	ASSERT_EQ(app.comObjects.size(), std::size_t{4});
	ASSERT_EQ(app.comObjectRefs.size(), std::size_t{6});
	const ComObject* sw = app.findComObject(button(u"_O-0"_s));
	ASSERT(sw != nullptr);
	ASSERT_EQ(sw->name, u"ButtonA_Switch"_s);
	ASSERT_EQ(sw->number, 0u);
	ASSERT_EQ(sw->objectSize, u"1 Bit"_s);
	ASSERT_EQ(sw->sizeInBit, 1u);
	ASSERT_EQ(sw->flags.toString(), u"CWTU"_s);
	ASSERT_EQ(sw->datapointType, u"DPST-1-1"_s);
	ASSERT_EQ(sw->attributes.at(u"Priority"_s), u"Low"_s);

	const ComObjectRef* toggle = app.findComObjectRef(button(u"_O-0_R-2"_s));
	ASSERT(toggle != nullptr);
	ASSERT_EQ(toggle->refId, button(u"_O-0"_s));
	ASSERT_EQ(toggle->functionText, std::optional<QString>(u"Umschalten"_s));
	ASSERT_EQ(toggle->read, std::optional<bool>(true));
	ASSERT_FALSE(toggle->write.has_value());
	ASSERT_FALSE(toggle->text.has_value());

	const ComObjectRef* led = app.findComObjectRef(button(u"_O-3_R-1"_s));
	ASSERT(led != nullptr);
	ASSERT_EQ(led->sizeInBit, std::optional<unsigned>(8));
	ASSERT_EQ(led->datapointType, std::optional<QString>(u"DPST-5-1"_s));

	ASSERT(app.comObjectTable.has_value());
	ASSERT_EQ(app.comObjectTable->codeSegment, button(u"_RS-04-00000"_s));
	ASSERT_EQ(app.comObjectTable->offset, 32u);
}

UNIT_TEST(FR_GA_3__effectiveComObjectsMergeRefs)
{
	const ApplicationProgram app = program(fixture::kButton);
	const std::vector<EffectiveComObject> objects = app.effectiveComObjects();
	ASSERT_EQ(objects.size(), std::size_t{6});
	// Ordered by number, then ref id.
	ASSERT_EQ(objects[0].refId, button(u"_O-0_R-1"_s));
	ASSERT_EQ(objects[1].refId, button(u"_O-0_R-2"_s));
	ASSERT_EQ(objects[5].refId, button(u"_O-3_R-1"_s));

	const EffectiveComObject& toggle = objects[1];
	ASSERT_EQ(toggle.comObjectId, button(u"_O-0"_s));
	ASSERT_EQ(toggle.number, 0u);
	ASSERT_EQ(toggle.name, u"ButtonA_Switch"_s);
	ASSERT_EQ(toggle.text, u"Taster A"_s);          // inherited
	ASSERT_EQ(toggle.functionText, u"Umschalten"_s); // overridden
	ASSERT_EQ(toggle.flags.toString(), u"CRWTU"_s);  // R overridden, the rest inherited
	ASSERT_EQ(toggle.sizeInBit, 1u);
	ASSERT_EQ(toggle.datapointType, u"DPST-1-1"_s);
	ASSERT_EQ(toggle.channelId, button(u"_CH-1"_s));
	ASSERT(toggle.referenced);

	const EffectiveComObject& led = objects[5];
	ASSERT_EQ(led.text, u"Taster B LED-Helligkeit"_s);
	ASSERT_EQ(led.functionText, u"LED"_s);
	ASSERT_EQ(led.objectSize, u"1 Byte"_s);
	ASSERT_EQ(led.sizeInBit, 8u);
	ASSERT_EQ(led.datapointType, u"DPST-5-1"_s);
	ASSERT_EQ(led.flags.toString(), u"CW"_s);

	// Translated view: ref translations win over object translations, both fall back to the default.
	const std::vector<EffectiveComObject> english = app.effectiveComObjects(u"en-US"_s);
	ASSERT_EQ(english[0].text, u"Button A"_s);
	ASSERT_EQ(english[0].functionText, u"Switch"_s);
	ASSERT_EQ(english[1].functionText, u"Toggle"_s);
	ASSERT_EQ(english[5].text, u"Button B LED brightness"_s);
	ASSERT_EQ(english[5].functionText, u"LED"_s);
}

UNIT_TEST(FR_GA_3__channelIndexButton)
{
	const ApplicationProgram app = program(fixture::kButton);
	ASSERT_EQ(app.channels.size(), std::size_t{3});
	const Channel& general = app.channels[0];
	ASSERT(general.independent);
	ASSERT(general.id.isEmpty());
	ASSERT_EQ(general.parameterBlocks.size(), std::size_t{1});
	ASSERT_EQ(general.parameterBlocks[0].id, button(u"_PB-1"_s));
	ASSERT(general.comObjectRefs.isEmpty());
	// Conditions are ignored: UP-3 (inside a when) is listed too.
	ASSERT_EQ(general.parameterRefs.size(), qsizetype{8});
	ASSERT(general.parameterRefs.contains(button(u"_UP-3_R-1"_s)));

	const Channel& a = app.channels[1];
	ASSERT_FALSE(a.independent);
	ASSERT_EQ(a.id, button(u"_CH-1"_s));
	ASSERT_EQ(a.name, u"ButtonA"_s);
	ASSERT_EQ(a.text, u"Taster A"_s);
	ASSERT_EQ(a.number, u"1"_s);
	// Both when branches, document order, O-1_R-1 only once.
	ASSERT_EQ(a.comObjectRefs, (QStringList{button(u"_O-0_R-1"_s), button(u"_O-1_R-1"_s), button(u"_O-0_R-2"_s)}));
	ASSERT_EQ(a.parameterRefs, QStringList{button(u"_P-4_R-1"_s)});

	const Channel& b = app.channels[2];
	ASSERT_EQ(b.comObjectRefs, (QStringList{button(u"_O-2_R-1"_s), button(u"_O-2_R-2"_s), button(u"_O-3_R-1"_s)}));
	ASSERT(app.findChannel(button(u"_CH-2"_s)) == &app.channels[2]);
	ASSERT(app.channelOfComObjectRef(button(u"_O-3_R-1"_s)) == &app.channels[2]);
	ASSERT(app.channelOfComObjectRef(u"nope") == nullptr);
	ASSERT_EQ(app.text(b.id, u"Text"_s, u"en-US"_s), u"Button B"_s);
}

UNIT_TEST(FR_GA_3__channelIndexFindsChannelFSwitch)
{
	const ApplicationProgram app = program(fixture::kActuator);
	ASSERT_EQ(app.channels.size(), std::size_t{9}); // independent + A..H
	const Channel* f = nullptr;
	for (const Channel& channel : app.channels)
	{
		if (channel.text == u"Channel F"_s)
		{
			f = &channel;
		}
	}
	ASSERT(f != nullptr);
	ASSERT_EQ(f->id, actuator(u"_CH-6"_s));
	ASSERT_EQ(f->comObjectRefs, (QStringList{actuator(u"_O-10_R-1"_s), actuator(u"_O-11_R-1"_s)}));

	QString switchRef;
	for (const EffectiveComObject& object : app.effectiveComObjects(u"en-US"_s))
	{
		if (object.channelId == f->id && object.functionText == u"Switch"_s)
		{
			switchRef = object.refId;
			ASSERT_EQ(object.number, 10u);
			ASSERT_EQ(object.flags.toString(), u"CW"_s);
			ASSERT_EQ(object.sizeInBit, 1u);
			ASSERT_EQ(object.datapointType, u"DPST-1-1"_s);
		}
	}
	ASSERT_EQ(switchRef, actuator(u"_O-10_R-1"_s));
	ASSERT_EQ(app.text(f->id, u"Text"_s, u"de-DE"_s), u"Kanal F"_s);
	ASSERT_EQ(app.effectiveComObjects(u"de-DE"_s)[10].functionText, u"Schalten"_s);
}

UNIT_TEST(FR_DEV_2__dynamicTreeKeepsConditions)
{
	const ApplicationProgram app = program(fixture::kButton);
	const DynamicNode& root = app.dynamic;
	ASSERT_EQ(root.kind, DynamicKind::Root);
	ASSERT_EQ(root.element, u"Dynamic"_s);
	ASSERT_EQ(root.children.size(), std::size_t{3});
	ASSERT_EQ(root.children[0].kind, DynamicKind::ChannelIndependentBlock);
	const DynamicNode& channel = root.children[1];
	ASSERT_EQ(channel.kind, DynamicKind::Channel);
	ASSERT_EQ(channel.attribute(u"Id"_s), button(u"_CH-1"_s));
	const DynamicNode& block = channel.children.at(0);
	ASSERT_EQ(block.kind, DynamicKind::ParameterBlock);
	const DynamicNode& choose = block.children.at(1);
	ASSERT_EQ(choose.kind, DynamicKind::Choose);
	ASSERT_EQ(choose.attribute(u"ParamRefId"_s), button(u"_P-4_R-1"_s));
	ASSERT_EQ(choose.children.size(), std::size_t{3});
	ASSERT_EQ(choose.children[0].kind, DynamicKind::When);
	ASSERT_EQ(choose.children[0].attribute(u"test"_s), u"0"_s);
	ASSERT_EQ(choose.children[0].children.at(0).kind, DynamicKind::ComObjectRefRef);
	ASSERT_EQ(choose.children[2].attribute(u"default"_s), u"true"_s);
	ASSERT(choose.children[2].children.empty());
	// Elements we do not interpret are kept as Other.
	const DynamicNode& separator = root.children[0].children.at(0).children.at(2);
	ASSERT_EQ(separator.kind, DynamicKind::Other);
	ASSERT_EQ(separator.element, u"ParameterSeparator"_s);
}

UNIT_TEST(FR_COM_2__tablesAndLoadProcedures)
{
	const ApplicationProgram app = program(fixture::kButton);
	ASSERT(app.addressTable.has_value());
	ASSERT_EQ(app.addressTable->codeSegment, button(u"_RS-01-00000"_s));
	ASSERT_EQ(app.addressTable->maxEntries, std::optional<unsigned>(32));
	ASSERT(app.associationTable.has_value());
	ASSERT_EQ(app.associationTable->codeSegment, button(u"_RS-02-00000"_s));
	ASSERT_EQ(app.loadProcedures.size(), std::size_t{2});
	ASSERT_EQ(app.loadProcedures[0].mergeId, std::optional<unsigned>(2));
	ASSERT_EQ(app.loadProcedures[0].controls.size(), std::size_t{1});
	ASSERT_EQ(app.loadProcedures[0].controls[0].element, u"LdCtrlRelSegment"_s);
	ASSERT_EQ(app.loadProcedures[0].controls[0].attributes.at(u"LsmIdx"_s), u"4"_s);
	ASSERT_EQ(app.loadProcedures[1].controls[0].element, u"LdCtrlWriteRelMem"_s);

	const ApplicationProgram act = program(fixture::kActuator);
	ASSERT_EQ(act.loadProcedures.size(), std::size_t{1});
	ASSERT_FALSE(act.loadProcedures[0].mergeId.has_value());
	const std::vector<LoadControl>& controls = act.loadProcedures[0].controls;
	ASSERT_EQ(controls.size(), std::size_t{20});
	ASSERT_EQ(controls.front().element, u"LdCtrlConnect"_s);
	ASSERT_EQ(controls[2].element, u"LdCtrlUnload"_s);
	ASSERT_EQ(controls[7].element, u"LdCtrlAbsSegment"_s);
	ASSERT_EQ(controls[7].attributes.at(u"Address"_s), u"278"_s);
	ASSERT_EQ(controls.back().element, u"LdCtrlDisconnect"_s);
	ASSERT_EQ(act.addressTable->codeSegment, actuator(u"_AS-0116"_s));
	ASSERT_EQ(act.comObjectTable->codeSegment, actuator(u"_AS-0300"_s));
}

UNIT_TEST(FR_CAT_2__translationsWithFallback)
{
	const ApplicationProgram app = program(fixture::kButton);
	const QString id = app.id;
	ASSERT_EQ(app.text(id, u"Name"_s), u"Kestrel Testtaster 2fach"_s);                 // default
	ASSERT_EQ(app.text(id, u"Name"_s, u"de-DE"_s), u"Kestrel Testtaster 2-fach"_s);    // exact
	ASSERT_EQ(app.text(id, u"Name"_s, u"de-at"_s), u"Kestrel Testtaster 2-fach"_s);    // same language
	ASSERT_EQ(app.text(id, u"Name"_s, u"en-US"_s), u"Kestrel Test Button 2-fold"_s);
	ASSERT_EQ(app.text(id, u"Name"_s, u"en-GB"_s), u"Kestrel Test Button 2-fold"_s);
	ASSERT_EQ(app.text(id, u"Name"_s, u"fr-FR"_s), u"Kestrel Testtaster 2fach"_s);     // default
	ASSERT_EQ(app.text(button(u"_P-6"_s), u"Text"_s, u"en-US"_s), u"Gerätename"_s); // untranslated
	ASSERT_EQ(app.text(u"unknown"_s, u"Text"_s, u"en-US"_s), QString());
	ASSERT_EQ(app.translations.languages(), (QStringList{u"de-DE"_s, u"en-US"_s}));
	ASSERT_FALSE(app.translations.translation(id, u"Name"_s, u"fr-FR"_s).has_value());

	Translations t;
	t.setDefault(u"X"_s, u"Text"_s, u"default"_s);
	t.add(u"it-IT"_s, u"X"_s, u"Text"_s, u"italiano"_s);
	ASSERT_EQ(t.text(u"X"_s, u"Text"_s, u"IT-it"_s), u"italiano"_s);
	ASSERT_EQ(t.text(u"X"_s, u"Text"_s, u"it"_s), u"italiano"_s);
	ASSERT_EQ(t.text(u"X"_s, u"FunctionText"_s, u"it-IT"_s), QString());
	Translations u;
	u.merge(t);
	ASSERT(u == t);
}

UNIT_TEST(FR_GA_3__objectSizeParsing)
{
	ASSERT_EQ(parseObjectSize(u"1 Bit"), std::optional<unsigned>(1));
	ASSERT_EQ(parseObjectSize(u"4 Bit"), std::optional<unsigned>(4));
	ASSERT_EQ(parseObjectSize(u"1 Byte"), std::optional<unsigned>(8));
	ASSERT_EQ(parseObjectSize(u"2 Bytes"), std::optional<unsigned>(16));
	ASSERT_EQ(parseObjectSize(u" 14 Bytes "), std::optional<unsigned>(112));
	ASSERT_FALSE(parseObjectSize(u"LegacyVarData").has_value());
	ASSERT_FALSE(parseObjectSize(u"0 Bit").has_value());
	ASSERT_FALSE(parseObjectSize(u"2 Nibbles").has_value());
	ASSERT_FALSE(parseObjectSize(u"").has_value());
}

UNIT_TEST(FR_CAT_1__valueTypesCompareAndPrint)
{
	const ApplicationProgram a = program(fixture::kButton);
	ApplicationProgram b = a;
	ASSERT(a == b);
	b.comObjects[0].flags.read = true;
	ASSERT_FALSE(a == b);
	std::ostringstream os;
	os << a << a.comObjects[0] << a.channels[1] << a.parameters[0] << a.loadProcedures[0] << a.dynamic.children[0];
	ASSERT(os.str().find("MV-07B0") != std::string::npos);
	ASSERT(os.str().find("LdCtrlRelSegment") != std::string::npos);
}
