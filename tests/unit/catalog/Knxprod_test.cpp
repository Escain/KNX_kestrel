// Reading product files: the archive layout, hardware/products/catalog, listing, tolerance and error paths.

#include "CatalogFixture.hpp"

#include "kestrel/archive/ZipReader.hpp"
#include "kestrel/catalog/Knxprod.hpp"

#include <Printers.hpp>
#include <QtPrinters.hpp>
#include <UnitTests.hpp>

using namespace kestrel;
using namespace kestrel::catalog;
using namespace Qt::StringLiterals;

namespace
{

bool hasWarning(const std::vector<Diagnostic>& warnings, const QString& fragment)
{
	for (const Diagnostic& warning : warnings)
	{
		if (warning.message.contains(fragment))
		{
			return true;
		}
	}
	return false;
}

std::string describe(const std::vector<Diagnostic>& warnings)
{
	std::ostringstream os;
	for (const Diagnostic& warning : warnings)
	{
		os << warning << '\n';
	}
	return os.str();
}

/// Replaces `from` by `to` in one file of the package.
PackageFiles patched(const QString& file, const QByteArray& from, const QByteArray& to)
{
	PackageFiles files = fixture::folderFiles();
	files[file].replace(from, to);
	return files;
}

} // namespace

UNIT_TEST(FR_CAT_1__knxprodLoads)
{
	auto loaded = loadKnxprodFile(fixture::knxprodPath());
	ASSERT(loaded.has_value());
	ASSERT_EQ(describe(loaded->warnings), std::string());
	const ProductDatabase& db = loaded->database;
	ASSERT_EQ(db.manufacturerId, u"M-FFFF"_s);
	ASSERT_EQ(db.schemaNamespace, u"http://knx.org/xml/project/20"_s);
	ASSERT(db.hasMasterData);
	ASSERT_EQ(db.hardware.size(), std::size_t{2});
	ASSERT_EQ(db.catalogItems.size(), std::size_t{2});
	ASSERT_EQ(db.applicationPrograms.size(), std::size_t{2});
	ASSERT_EQ(db.files, (QStringList{u"M-FFFF/Hardware.xml"_s, u"M-FFFF/Catalog.xml"_s,
							u"M-FFFF/M-FFFF_A-0001-10-0001.xml"_s, u"M-FFFF/M-FFFF_A-0002-21-0001.xml"_s}));
}

// The committed .knxprod must be regenerated (make_fixtures.py) whenever the XML changes.
UNIT_TEST(FR_CAT_1__knxprodFixtureIsUpToDate)
{
	QFile file(fixture::knxprodPath());
	ASSERT(file.open(QIODevice::ReadOnly));
	const QByteArray bytes = file.readAll();
	auto zip = archive::ZipReader::open(Bytes(bytes.begin(), bytes.end()));
	ASSERT(zip.has_value());
	const PackageFiles folder = fixture::folderFiles();
	ASSERT_EQ(folder.size(), std::size_t{4});
	for (const auto& [path, content] : folder)
	{
		auto entry = zip->read(path.toStdString());
		ASSERT(entry.has_value());
		ASSERT(QByteArray(reinterpret_cast<const char*>(entry->data()), static_cast<qsizetype>(entry->size())) == content);
	}
	// The zip also carries the synthetic knx_master.xml (tests/data/catalog/knx_master.xml).
	QFile masterFile(QStringLiteral(KESTREL_TEST_DATA_DIR "/catalog/knx_master.xml"));
	ASSERT(masterFile.open(QIODevice::ReadOnly));
	PackageFiles withMaster = folder;
	withMaster[u"knx_master.xml"_s] = masterFile.readAll();
	auto master = zip->read("knx_master.xml");
	ASSERT(master.has_value());
	ASSERT(QByteArray(reinterpret_cast<const char*>(master->data()), static_cast<qsizetype>(master->size()))
		== withMaster[u"knx_master.xml"_s]);
	auto fromFolder = parsePackage(withMaster);
	auto fromZip = loadKnxprodFile(fixture::knxprodPath());
	ASSERT(fromFolder.has_value() && fromZip.has_value());
	ASSERT(fromFolder->database == fromZip->database);

	// Mask procedures sit below HawkConfigurationData.
	const MaskVersionInfo* systemB = fromZip->database.findMaskVersion(u"MV-07B0");
	ASSERT(systemB != nullptr);
	const MaskProcedure* ap1 = systemB->procedure(u"Load", u"ap1");
	ASSERT(ap1 != nullptr);
	ASSERT_EQ(ap1->controls.front().element, u"LdCtrlConnect"_s);
	ASSERT_EQ(ap1->controls.back().element, u"LdCtrlRestart"_s);
}

UNIT_TEST(FR_CAT_1__hardwareAndProducts)
{
	auto loaded = parsePackage(fixture::folderFiles());
	ASSERT(loaded.has_value());
	const ProductDatabase& db = loaded->database;
	const Hardware* button = db.findHardware(u"M-FFFF_H-KTB2-1");
	ASSERT(button != nullptr);
	ASSERT_EQ(button->name, u"Kestrel Testtaster 2-fach"_s);
	ASSERT_EQ(button->serialNumber, u"KTB2"_s);
	ASSERT_EQ(button->versionNumber, u"1"_s);
	ASSERT_EQ(button->attributes.at(u"BusCurrent"_s), u"10"_s);
	ASSERT_EQ(button->products.size(), std::size_t{1});
	ASSERT_EQ(button->products[0].id, u"M-FFFF_H-KTB2-1_P-KTB.2D2"_s);
	ASSERT_EQ(button->products[0].orderNumber, u"KTB-2"_s);
	ASSERT_EQ(button->hardware2Programs.size(), std::size_t{1});
	ASSERT_EQ(button->hardware2Programs[0].id, u"M-FFFF_H-KTB2-1_HP-0001-10-0001"_s);
	ASSERT_EQ(button->hardware2Programs[0].applicationProgramRefs, QStringList{u"M-FFFF_A-0001-10-0001"_s});
	ASSERT(db.findProduct(u"M-FFFF_H-KSA8-1_P-KSA.2D8") != nullptr);
	ASSERT(db.findProduct(u"nope") == nullptr);
	ASSERT_EQ(db.text(u"M-FFFF_H-KTB2-1_P-KTB.2D2"_s, u"Text"_s, u"en-US"_s), u"Kestrel Test Button 2-fold"_s);
	ASSERT_EQ(db.text(u"M-FFFF_H-KSA8-1_P-KSA.2D8"_s, u"Text"_s, u"de-DE"_s), u"Kestrel Testschaltaktor 8-fach"_s);
	// Package text() also reaches application-program texts.
	ASSERT_EQ(db.text(u"M-FFFF_A-0002-21-0001_CH-6"_s, u"Text"_s, u"de-DE"_s), u"Kanal F"_s);
}

UNIT_TEST(FR_CAT_2__catalogItems)
{
	auto loaded = parsePackage(fixture::folderFiles());
	ASSERT(loaded.has_value());
	const std::vector<CatalogItem>& items = loaded->database.catalogItems;
	ASSERT_EQ(items.size(), std::size_t{2});
	ASSERT_EQ(items[0].name, u"Kestrel Testtaster 2-fach"_s);
	ASSERT_EQ(items[0].sectionPath, QStringList{u"Taster"_s});
	ASSERT_EQ(items[0].productRefId, u"M-FFFF_H-KTB2-1_P-KTB.2D2"_s);
	ASSERT_EQ(items[0].hardware2ProgramRefId, u"M-FFFF_H-KTB2-1_HP-0001-10-0001"_s);
	ASSERT_EQ(items[1].sectionPath, (QStringList{u"Actuators"_s, u"Switch actuators"_s}));
	ASSERT_EQ(loaded->database.text(u"M-FFFF_CS-1"_s, u"Name"_s, u"en-US"_s), u"Push buttons"_s);
	ASSERT_EQ(loaded->database.text(items[0].id, u"Name"_s, u"en-US"_s), u"Kestrel Test Button 2-fold"_s);
}

UNIT_TEST(FR_CAT_2__listProducts)
{
	auto loaded = parsePackage(fixture::folderFiles());
	ASSERT(loaded.has_value());
	const std::vector<ProductSummary> english = listProducts(loaded->database, u"en-US"_s);
	ASSERT_EQ(english.size(), std::size_t{2});
	ASSERT_EQ(english[0].productId, u"M-FFFF_H-KTB2-1_P-KTB.2D2"_s);
	ASSERT_EQ(english[0].text, u"Kestrel Test Button 2-fold"_s);
	ASSERT_EQ(english[0].orderNumber, u"KTB-2"_s);
	ASSERT_EQ(english[0].hardwareName, u"Kestrel Test Button 2-fold"_s);
	ASSERT_EQ(english[0].programs.size(), std::size_t{1});
	const ProgramSummary& program = english[0].programs[0];
	ASSERT(program.available);
	ASSERT_EQ(program.id, u"M-FFFF_A-0001-10-0001"_s);
	ASSERT_EQ(program.name, u"Kestrel Test Button 2-fold"_s);
	ASSERT_EQ(program.maskVersion, u"MV-07B0"_s);
	ASSERT_EQ(program.applicationNumber, 1u);
	ASSERT_EQ(program.applicationVersion, 16u);
	ASSERT_EQ(english[1].programs.at(0).maskVersion, u"MV-0705"_s);

	const std::vector<ProductSummary> german = listProducts(loaded->database, u"de-DE"_s);
	ASSERT_EQ(german[0].text, u"Kestrel Testtaster 2-fach"_s);
	ASSERT_EQ(german[1].text, u"Kestrel Testschaltaktor 8-fach"_s);
	ASSERT_EQ(german[1].programs.at(0).name, u"Kestrel Testschaltaktor 8-fach"_s);
}

// --- tolerance -------------------------------------------------------------------------------------------------

UNIT_TEST(FR_CAT_1__toleratesOtherNamespaceVersions)
{
	auto reference = parsePackage(fixture::folderFiles());
	ASSERT(reference.has_value());
	for (const QByteArray& version : {QByteArray("11"), QByteArray("14"), QByteArray("23")})
	{
		PackageFiles files = fixture::folderFiles();
		for (auto& [path, content] : files)
		{
			content.replace("http://knx.org/xml/project/20", "http://knx.org/xml/project/" + version);
		}
		auto loaded = parsePackage(files);
		ASSERT(loaded.has_value());
		ASSERT(loaded->warnings.empty());
		ASSERT_EQ(loaded->database.schemaNamespace, u"http://knx.org/xml/project/"_s + QString::fromLatin1(version));
		loaded->database.schemaNamespace = reference->database.schemaNamespace;
		ASSERT(loaded->database == reference->database);
	}
}

UNIT_TEST(FR_CAT_1__ignoresUnknownElementsAndAttributes)
{
	auto reference = parsePackage(fixture::folderFiles());
	ASSERT(reference.has_value());
	PackageFiles files = fixture::folderFiles();
	QByteArray& app = files[fixture::buttonFile()];
	app.replace("<Static>", "<FutureTopLevel x=\"1\"><Static><Nested/></Static></FutureTopLevel><Static>");
	app.replace("<Code>", "<Code><FutureSegment Id=\"x\" />");
	app.replace("<ParameterTypes>", "<Script>function f() { return 1 &lt; 2; }</Script><ParameterTypes>");
	app.replace("<ComObjectRefs>", "<ComObjectRefs><FutureRef><ComObjectRef Id=\"bogus\"/></FutureRef>");
	app.replace("<Channel Id=", "<Channel FutureAttribute=\"yes\" Id=");
	app.replace("</Dynamic>", "<FutureDynamic><Channel Id=\"inside-unknown\"/></FutureDynamic></Dynamic>");
	files[u"M-FFFF/Hardware.xml"_s].replace("<Products>", "<Products><Accessory Id=\"x\"/>");
	files[u"M-FFFF/Hardware.xml"_s].replace("<ManufacturerData>", "<FutureData><Manufacturer RefId=\"M-0000\"/></FutureData><ManufacturerData>");
	auto loaded = parsePackage(files);
	ASSERT(loaded.has_value());
	ASSERT_EQ(describe(loaded->warnings), std::string());

	const ApplicationProgram* expected = reference->database.findApplicationProgram(QStringView(fixture::kButton));
	const ApplicationProgram* actual = loaded->database.findApplicationProgram(QStringView(fixture::kButton));
	ASSERT(expected != nullptr && actual != nullptr);
	ASSERT(actual->parameters == expected->parameters);
	ASSERT(actual->parameterTypes == expected->parameterTypes);
	ASSERT(actual->comObjectRefs == expected->comObjectRefs);
	ASSERT(actual->relativeSegments == expected->relativeSegments);
	ASSERT(actual->effectiveComObjects() == expected->effectiveComObjects());
	// Unknown Dynamic elements are kept in the tree (as Other) and channels below them still count.
	ASSERT_EQ(actual->channels.size(), expected->channels.size() + 1);
	ASSERT_EQ(actual->channels[1].comObjectRefs, expected->channels[1].comObjectRefs);
	ASSERT_EQ(actual->dynamic.children.back().kind, DynamicKind::Other);
	ASSERT(loaded->database.hardware == reference->database.hardware);
}

UNIT_TEST(FR_CAT_1__toleratesMissingOptionalParts)
{
	// No Catalog.xml: still usable, one warning.
	PackageFiles files = fixture::folderFiles();
	files.erase(u"M-FFFF/Catalog.xml"_s);
	auto loaded = parsePackage(files);
	ASSERT(loaded.has_value());
	ASSERT(loaded->database.catalogItems.empty());
	ASSERT(hasWarning(loaded->warnings, u"Catalog.xml"_s));
	ASSERT_EQ(listProducts(loaded->database).size(), std::size_t{2});

	// An application program referenced by the hardware is missing: listed as unavailable.
	files = fixture::folderFiles();
	files.erase(fixture::actuatorFile());
	loaded = parsePackage(files);
	ASSERT(loaded.has_value());
	ASSERT(hasWarning(loaded->warnings, u"M-FFFF_A-0002-21-0001 (of M-FFFF_H-KSA8-1_HP-0002-21-0001) is not in"_s));
	const std::vector<ProductSummary> products = listProducts(loaded->database);
	ASSERT_FALSE(products[1].programs.at(0).available);
	ASSERT(products[0].programs.at(0).available);

	// A minimal application program: no Static/Dynamic children at all.
	files = fixture::folderFiles();
	files[fixture::actuatorFile()] = "<KNX xmlns=\"http://knx.org/xml/project/12\"><ManufacturerData><Manufacturer RefId=\"M-FFFF\">"
									 "<ApplicationPrograms><ApplicationProgram Id=\"M-FFFF_A-0002-21-0001\" Name=\"x\"/>"
									 "</ApplicationPrograms></Manufacturer></ManufacturerData></KNX>";
	loaded = parsePackage(files);
	ASSERT(loaded.has_value());
	const ApplicationProgram* app = loaded->database.findApplicationProgram(u"M-FFFF_A-0002-21-0001");
	ASSERT(app != nullptr);
	ASSERT(app->channels.empty());
	ASSERT(app->effectiveComObjects().empty());
	ASSERT_FALSE(app->comObjectTable.has_value());
	ASSERT(hasWarning(loaded->warnings, u"namespace"_s)); // 12 differs from Hardware.xml's 20

	// Other files in the manufacturer folder: parsed leniently, foreign ones ignored with a warning.
	files = fixture::folderFiles();
	files[u"M-FFFF/Baggages.xml"_s] = "<this is not even xml";
	files[u"M-FFFF/Notes.xml"_s] = "<Notes/>";
	files[u"M-FFFF/Baggages/firmware.xml"_s] = "<x";
	files[u"README.txt"_s] = "hello";
	loaded = parsePackage(files);
	ASSERT(loaded.has_value());
	ASSERT(hasWarning(loaded->warnings, u"ignored: M-FFFF/Notes.xml: root element is <Notes>"_s));
	ASSERT_EQ(loaded->warnings.size(), std::size_t{1});
}

UNIT_TEST(FR_CAT_1__recoverableProblemsBecomeWarnings)
{
	PackageFiles files = patched(fixture::buttonFile(), "ObjectSize=\"1 Byte\"", "ObjectSize=\"1 Quux\"");
	files[fixture::buttonFile()].replace("ApplicationVersion=\"16\"", "ApplicationVersion=\"sixteen\"");
	files[fixture::buttonFile()].replace("RefId=\"M-FFFF_A-0001-10-0001_O-2\" FunctionText",
		"RefId=\"M-FFFF_A-0001-10-0001_O-99\" FunctionText");
	files[fixture::buttonFile()].replace("ReadFlag=\"Enabled\" />", "ReadFlag=\"Maybe\" />");
	auto loaded = parsePackage(files);
	ASSERT(loaded.has_value());
	ASSERT(hasWarning(loaded->warnings, u"unknown ObjectSize \"1 Quux\""_s));
	ASSERT(hasWarning(loaded->warnings, u"ApplicationVersion=\"sixteen\""_s));
	ASSERT(hasWarning(loaded->warnings, u"refers to unknown ComObject M-FFFF_A-0001-10-0001_O-99"_s));
	ASSERT(hasWarning(loaded->warnings, u"invalid flag ReadFlag=\"Maybe\""_s));
	const ApplicationProgram* app = loaded->database.findApplicationProgram(QStringView(fixture::kButton));
	ASSERT(app != nullptr);
	ASSERT_EQ(app->applicationVersion, 0u);
	ASSERT_EQ(app->effectiveComObjects().size(), std::size_t{5}); // the dangling ref is skipped
	// Warnings carry the file and line.
	for (const Diagnostic& warning : loaded->warnings)
	{
		if (warning.message.contains(u"Quux"_s))
		{
			ASSERT_EQ(warning.file, fixture::buttonFile());
			ASSERT(warning.line > 1);
		}
	}

	// ModuleDefs are reported, not silently dropped.
	files = patched(fixture::buttonFile(), "<Dynamic>", "<Dynamic><!-- modules below --></Dynamic><ModuleDefs><ModuleDef Id=\"m\"/></ModuleDefs><Dynamic>");
	loaded = parsePackage(files);
	ASSERT(loaded.has_value());
	ASSERT(hasWarning(loaded->warnings, u"ModuleDefs are not supported yet"_s));
	ASSERT(loaded->database.findApplicationProgram(QStringView(fixture::kButton))->hasModuleDefs);
}

// --- errors ----------------------------------------------------------------------------------------------------

UNIT_TEST(FR_CAT_1__missingHardwareIsAnError)
{
	PackageFiles files = fixture::folderFiles();
	files.erase(u"M-FFFF/Hardware.xml"_s);
	auto loaded = parsePackage(files);
	ASSERT_FALSE(loaded.has_value());
	ASSERT_EQ(loaded.error().code, ErrorCode::NotFound);
	ASSERT(loaded.error().message.find("Hardware.xml") != std::string::npos);

	// Hardware.xml outside a manufacturer folder does not count.
	files[u"Hardware.xml"_s] = fixture::folderFiles().at(u"M-FFFF/Hardware.xml"_s);
	files[u"X-FFFF/Hardware.xml"_s] = files[u"Hardware.xml"_s];
	ASSERT_FALSE(parsePackage(files).has_value());
}

UNIT_TEST(FR_CAT_1__brokenXmlIsAnError)
{
	for (const QString& file : {u"M-FFFF/Hardware.xml"_s, u"M-FFFF/Catalog.xml"_s, fixture::buttonFile()})
	{
		PackageFiles files = fixture::folderFiles();
		files[file].chop(40); // premature end of document
		auto loaded = parsePackage(files);
		ASSERT_FALSE(loaded.has_value());
		ASSERT_EQ(loaded.error().code, ErrorCode::ParseError);
		ASSERT(loaded.error().message.find(file.toStdString()) != std::string::npos);
	}
	// Mismatched tags.
	auto mismatched = parsePackage(patched(fixture::actuatorFile(), "</Dynamic>", "</Dynamik>"));
	ASSERT_FALSE(mismatched.has_value());
	ASSERT_EQ(mismatched.error().code, ErrorCode::ParseError);
	// Wrong root element and foreign namespace.
	auto root = parsePackage(patched(u"M-FFFF/Hardware.xml"_s, "<KNX xmlns", "<XNK xmlns"));
	ASSERT_FALSE(root.has_value());
	auto ns = parsePackage(patched(u"M-FFFF/Hardware.xml"_s, "http://knx.org/xml/project/20", "urn:something-else"));
	ASSERT_FALSE(ns.has_value());
	ASSERT(ns.error().message.find("unsupported namespace") != std::string::npos);
	// Empty file.
	PackageFiles empty = fixture::folderFiles();
	empty[fixture::buttonFile()].clear();
	ASSERT_FALSE(parsePackage(empty).has_value());
}

UNIT_TEST(FR_CAT_1__notAProductFile)
{
	auto missing = loadKnxprodFile(fixture::dataDir() + u"/catalog/does-not-exist.knxprod"_s);
	ASSERT_FALSE(missing.has_value());
	ASSERT_EQ(missing.error().code, ErrorCode::IoError);

	auto notZip = loadKnxprod(Bytes{'h', 'e', 'l', 'l', 'o'});
	ASSERT_FALSE(notZip.has_value());
	ASSERT_EQ(notZip.error().code, ErrorCode::ParseError);

	// A valid zip without the product layout.
	auto other = loadKnxprodFile(fixture::dataDir() + u"/archive/basic.zip"_s);
	ASSERT_FALSE(other.has_value());
	ASSERT_EQ(other.error().code, ErrorCode::NotFound);
}

UNIT_TEST(FR_CAT_1__hostileXmlFailsCleanly)
{
	// Deep nesting inside Dynamic is cut off with a warning instead of exhausting the stack.
	QByteArray deep;
	for (int i = 0; i < 2000; ++i)
	{
		deep += "<choose>";
	}
	for (int i = 0; i < 2000; ++i)
	{
		deep += "</choose>";
	}
	auto nested = parsePackage(patched(fixture::buttonFile(), "<Dynamic>", "<Dynamic>" + deep));
	ASSERT(nested.has_value());
	ASSERT(hasWarning(nested->warnings, u"nested too deeply"_s));

	// Entity expansion bombs are rejected by QXmlStreamReader's expansion limit.
	QByteArray bomb = "<?xml version=\"1.0\"?><!DOCTYPE KNX [<!ENTITY a \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\">";
	QByteArray previous = "a";
	for (char name = 'b'; name <= 'k'; ++name)
	{
		bomb += "<!ENTITY " + QByteArray(1, name) + " \"";
		for (int i = 0; i < 10; ++i)
		{
			bomb += "&" + previous + ";";
		}
		bomb += "\">";
		previous = QByteArray(1, name);
	}
	bomb += "]><KNX xmlns=\"http://knx.org/xml/project/20\"><ManufacturerData><Manufacturer RefId=\"&k;\"/>"
			"</ManufacturerData></KNX>";
	PackageFiles files = fixture::folderFiles();
	files[u"M-FFFF/Catalog.xml"_s] = bomb;
	auto loaded = parsePackage(files);
	ASSERT_FALSE(loaded.has_value());
	ASSERT_EQ(loaded.error().code, ErrorCode::ParseError);
}
