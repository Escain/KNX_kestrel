#include "../model/SampleProject.hpp"

#include "kestrel/persist/Persist.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <Printers.hpp>
#include <QtPrinters.hpp>
#include <UnitTests.hpp>

using namespace kestrel;
using namespace kestrel::model;
using namespace kestrel::persist;
using namespace Qt::StringLiterals;

namespace
{

QByteArray readFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return file.readAll();
}

void writeFile(const QString& path, const QByteArray& data)
{
	QFile file(path);
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		file.write(data);
}

QJsonObject readJson(const QString& path)
{
	return QJsonDocument::fromJson(readFile(path)).object();
}

void writeJson(const QString& path, const QJsonObject& object)
{
	writeFile(path, QJsonDocument(object).toJson());
}

/// Relative path -> content of every file below `dir`.
std::map<QString, QByteArray> snapshotFolder(const QString& dir)
{
	std::map<QString, QByteArray> files;
	const QDir root(dir);
	for (const QString& name : root.entryList(QDir::Files))
		files[name] = readFile(root.filePath(name));
	const QDir devices(root.filePath(u"devices"_s));
	for (const QString& name : devices.entryList(QDir::Files))
		files[u"devices/"_s + name] = readFile(devices.filePath(name));
	return files;
}

/// Same entities (by id) and states; table order may differ for lines, devices and links.
template <class T>
bool sameRows(const Table<T>& a, const Table<T>& b)
{
	if (a.size() != b.size())
		return false;
	for (const T& row : a)
	{
		const T* other = b.find(row.id);
		if (!other || !(*other == row))
			return false;
	}
	return true;
}

bool sameContent(const Project& a, const Project& b)
{
	bool states = true;
	for (const Device& device : a.devices())
		states = states && a.deviceState(device.id) == b.deviceState(device.id);
	return states && a.settings() == b.settings() && sameRows(a.buildingParts(), b.buildingParts())
		&& sameRows(a.areas(), b.areas()) && sameRows(a.lines(), b.lines()) && sameRows(a.devices(), b.devices())
		&& sameRows(a.comObjects(), b.comObjects()) && sameRows(a.groupRanges(), b.groupRanges())
		&& sameRows(a.groupAddresses(), b.groupAddresses()) && sameRows(a.links(), b.links());
}

/// Sample project with every field populated, saved into a temporary folder.
struct SavedSample : SampleProject
{
	QTemporaryDir temp;
	QString dir;

	SavedSample()
	{
		setDeviceParam(project, actuator, u"M-FFFF_A-1_P-2"_s, u"3"_s).value();
		setDeviceParam(project, actuator, u"M-FFFF_A-1_P-1"_s, u"line1\nline2 \"quoted\" é中"_s).value();
		project.edit(u"Details"_s,
				   [&](Tx& tx)
				   {
					   tx.set(actuator, &Device::description, u"Cabinet, rail 2"_s);
					   tx.set(lightSwitch, &GroupAddress::description, u"Tab\there"_s);
					   tx.set(room, &BuildingPart::description, u"South"_s);
					   tx.set(actuatorStatus, &ComObject::catalogRef, u"M-FFFF_A-1_O-1"_s);
					   tx.set(line, &Line::medium, Medium::IP);
					   return ok();
				   })
			.value();
		addDevice(project, line, std::nullopt, u"Unaddressed"_s).value();
		project.recordState(actuator, DeviceState{true, u"00FA:00000001"_s, u"sha256:01"_s});
		dir = temp.path() + u"/house.kestrel.d"_s;
	}
};

} // namespace

UNIT_TEST(FR_PRJ_1__saveLoadRoundTrip, SavedSample)
{
	ASSERT(save(project, dir).has_value());
	auto loaded = load(dir);
	ASSERT(loaded.has_value());
	ASSERT(sameContent(project, *loaded));
	ASSERT_EQ(loaded->deviceState(actuator).serial, u"00FA:00000001"_s);
	ASSERT_EQ(loaded->devices().at(actuator).params.size(), std::size_t{2});
	ASSERT_FALSE(loaded->isDirty());
	ASSERT_FALSE(loaded->history().canUndo());
	ASSERT_EQ(loaded->devicesOf(line).size(), std::size_t{3});
	ASSERT(loaded->links().at(buttonSwitchLink).sending);
	// The loaded project is fully editable.
	ASSERT(rename(*loaded, actuator, u"Edited"_s).has_value());
	ASSERT(loaded->history().undo());
	ASSERT(sameContent(project, *loaded));
}

UNIT_TEST(FR_PRJ_1__saveIsDeterministic, SavedSample)
{
	const QString second = temp.path() + u"/second"_s;
	const QString third = temp.path() + u"/third"_s;
	ASSERT(save(project, dir).has_value());
	ASSERT(save(project, second).has_value());
	const auto files = snapshotFolder(dir);
	ASSERT_EQ(files.size(), std::size_t{5 + 3}); // 5 fixed files + 3 devices
	ASSERT(files == snapshotFolder(second));

	auto loaded = load(dir);
	ASSERT(loaded.has_value());
	ASSERT(save(*loaded, third).has_value());
	ASSERT(files == snapshotFolder(third));

	ASSERT_EQ(files.at(u"kestrel.json"_s),
		QByteArray("{\n  \"format\": \"kestrel-project\",\n  \"formatVersion\": 1,\n  \"generator\": \"Kestrel 0.1.0\"\n}\n"));
	for (const auto& [name, content] : files)
	{
		ASSERT(content.endsWith("}\n"));
		ASSERT_FALSE(content.contains('\t')); // tabs are escaped, indentation is spaces
	}
	ASSERT(files.contains(u"devices/"_s + actuator.toString() + u".json"_s));
}

UNIT_TEST(FR_PRJ_1__toJsonFormat)
{
	const QJsonObject object{
		{u"b"_s, QJsonArray{1, true, QJsonValue()}},
		{u"a"_s, u"x\"\\\n\u0001é"_s},
		{u"c"_s, QJsonObject{}},
		{u"d"_s, QJsonArray{}},
		{u"e"_s, QJsonObject{{u"z"_s, -5}, {u"y"_s, 0.5}}},
	};
	const QByteArray expected = "{\n"
								"  \"a\": \"x\\\"\\\\\\n\\u0001\xc3\xa9\",\n"
								"  \"b\": [\n"
								"    1,\n"
								"    true,\n"
								"    null\n"
								"  ],\n"
								"  \"c\": {},\n"
								"  \"d\": [],\n"
								"  \"e\": {\n"
								"    \"y\": 0.5,\n"
								"    \"z\": -5\n"
								"  }\n"
								"}\n";
	ASSERT_EQ(toJson(object), expected);
	// Our output is valid JSON.
	ASSERT(QJsonDocument::fromJson(expected).object() == object);
}

UNIT_TEST(FR_PRJ_1__staleDeviceFilesAreRemoved, SavedSample)
{
	ASSERT(save(project, dir).has_value());
	const QString actuatorFile = dir + u"/devices/"_s + actuator.toString() + u".json"_s;
	ASSERT(QFile::exists(actuatorFile));
	ASSERT(removeDevice(project, actuator).has_value());
	ASSERT(save(project, dir).has_value());
	ASSERT_FALSE(QFile::exists(actuatorFile));
	auto loaded = load(dir);
	ASSERT(loaded.has_value());
	ASSERT(sameContent(project, *loaded));
}

UNIT_TEST(FR_PRJ_1__emptyProjectRoundTrip)
{
	QTemporaryDir temp;
	Project project(IdGenerator::sequential());
	ASSERT(save(project, temp.path()).has_value());
	auto loaded = load(temp.path());
	ASSERT(loaded.has_value());
	ASSERT_EQ(*loaded, project);
}

UNIT_TEST(FR_PRJ_1__loadUsesInjectedIdGenerator, SavedSample)
{
	ASSERT(save(project, dir).has_value());
	LoadOptions options;
	options.idGenerator = IdGenerator::sequential(1); // collides with nothing: ids start at 1000
	auto loaded = load(dir, options);
	ASSERT(loaded.has_value());
	const auto area2 = addArea(*loaded, 2, u"Two"_s);
	ASSERT_EQ(*area2, Id<Area>(1));
}

// --- Versioning ----------------------------------------------------------------------------------------------

UNIT_TEST(FR_PRJ_1__loadRefusesNewerFormatVersion, SavedSample)
{
	ASSERT(save(project, dir).has_value());
	QJsonObject header = readJson(dir + u"/kestrel.json"_s);
	header[u"formatVersion"_s] = 2;
	writeJson(dir + u"/kestrel.json"_s, header);
	const auto loaded = load(dir);
	ASSERT_FALSE(loaded.has_value());
	ASSERT_EQ(loaded.error().code, ErrorCode::Unsupported);
	ASSERT(loaded.error().message.find("kestrel.json") != std::string::npos);
	ASSERT(loaded.error().message.find("newer") != std::string::npos);
}

UNIT_TEST(FR_PRJ_1__loadRefusesForeignFormat, SavedSample)
{
	ASSERT(save(project, dir).has_value());
	writeJson(dir + u"/kestrel.json"_s, QJsonObject{{u"format"_s, u"other"_s}, {u"formatVersion"_s, 1}});
	const auto loaded = load(dir);
	ASSERT_EQ(loaded.error().code, ErrorCode::ParseError);
	ASSERT(loaded.error().message.find("not a Kestrel project") != std::string::npos);
}

/// Turns a v1 folder into a fake "v0" one: version 0 and project.json "title" instead of "name".
static void downgradeToV0(const QString& dir)
{
	QJsonObject header = readJson(dir + u"/kestrel.json"_s);
	header[u"formatVersion"_s] = 0;
	writeJson(dir + u"/kestrel.json"_s, header);
	QJsonObject settings = readJson(dir + u"/project.json"_s);
	settings[u"title"_s] = settings.take(u"name"_s);
	writeJson(dir + u"/project.json"_s, settings);
}

UNIT_TEST(FR_PRJ_1__migrationChainUpgradesOldVersions, SavedSample)
{
	ASSERT(save(project, dir).has_value());
	downgradeToV0(dir);

	const auto withoutMigration = load(dir);
	ASSERT_FALSE(withoutMigration.has_value());
	ASSERT_EQ(withoutMigration.error().code, ErrorCode::Unsupported);
	ASSERT(withoutMigration.error().message.find("formatVersion 0") != std::string::npos);

	int calls = 0;
	LoadOptions options;
	options.migrations = {Migration{0,
		[&](ProjectTree& tree) -> Status
		{
			++calls;
			QJsonObject& settings = tree.files.at(u"project.json"_s);
			settings[u"name"_s] = settings.take(u"title"_s);
			return ok();
		}}};
	auto loaded = load(dir, options);
	ASSERT(loaded.has_value());
	ASSERT_EQ(calls, 1);
	ASSERT_EQ(loaded->settings().name, u"Sample house"_s);
	ASSERT(sameContent(project, *loaded));
}

UNIT_TEST(FR_PRJ_1__migrateUpdatesVersionAndPropagatesFailures, SavedSample)
{
	ASSERT(save(project, dir).has_value());
	downgradeToV0(dir);
	auto tree = readTree(dir);
	ASSERT(tree.has_value());
	ProjectTree copy = *tree;

	const MigrationChain failing{
		Migration{0, [](ProjectTree&) -> Status { return makeError(ErrorCode::ParseError, "bad input"); }}};
	const Status failed = migrate(copy, failing);
	ASSERT_FALSE(failed.has_value());
	ASSERT(failed.error().message.find("bad input") != std::string::npos);

	const MigrationChain identity{Migration{0, [](ProjectTree&) { return ok(); }}};
	ASSERT(migrate(*tree, identity).has_value());
	ASSERT_EQ(tree->files.at(u"kestrel.json"_s).value(u"formatVersion"_s).toInt(), kFormatVersion);
	ASSERT(builtinMigrations().empty());
}

// --- Robust loading ------------------------------------------------------------------------------------------

static std::string loadError(const QString& dir)
{
	const auto loaded = load(dir);
	return loaded.has_value() ? std::string("<loaded>") : loaded.error().message;
}

static bool mentions(const std::string& message, const QString& text)
{
	return message.find(text.toStdString()) != std::string::npos;
}

UNIT_TEST(FR_PRJ_1__loadReportsMissingFolderAndFiles)
{
	QTemporaryDir temp;
	const auto missing = load(temp.path() + u"/nothing"_s);
	ASSERT_EQ(missing.error().code, ErrorCode::NotFound);

	Project project(IdGenerator::sequential());
	ASSERT(save(project, temp.path()).has_value());
	QFile::remove(temp.path() + u"/topology.json"_s);
	const auto loaded = load(temp.path());
	ASSERT_EQ(loaded.error().code, ErrorCode::NotFound);
	ASSERT(mentions(loaded.error().message, u"topology.json"_s));
}

UNIT_TEST(FR_PRJ_1__loadReportsMalformedJson, SavedSample)
{
	ASSERT(save(project, dir).has_value());
	writeFile(dir + u"/groupaddresses.json"_s, "{ \"groupRanges\": [ ");
	auto loaded = load(dir);
	ASSERT_EQ(loaded.error().code, ErrorCode::ParseError);
	ASSERT(mentions(loaded.error().message, u"groupaddresses.json"_s));

	writeFile(dir + u"/groupaddresses.json"_s, "[1, 2]");
	loaded = load(dir);
	ASSERT(mentions(loaded.error().message, u"groupaddresses.json: root is not a JSON object"_s));
}

UNIT_TEST(FR_PRJ_1__loadReportsDanglingLink, SavedSample)
{
	ASSERT(save(project, dir).has_value());
	const QString file = u"devices/"_s + button.toString() + u".json"_s;
	QJsonObject device = readJson(dir + u'/' + file);
	QJsonArray objects = device[u"comObjects"_s].toArray();
	QJsonObject object = objects[0].toObject();
	QJsonArray links = object[u"links"_s].toArray();
	QJsonObject l = links[0].toObject();
	const Id<GroupAddress> ghost(0xABCDEF);
	l[u"groupAddress"_s] = ghost.toString();
	links[0] = l;
	object[u"links"_s] = links;
	objects[0] = object;
	device[u"comObjects"_s] = objects;
	writeJson(dir + u'/' + file, device);

	const auto loaded = load(dir);
	ASSERT_EQ(loaded.error().code, ErrorCode::NotFound);
	ASSERT(mentions(loaded.error().message, file));
	ASSERT(mentions(loaded.error().message, ghost.toString()));
}

UNIT_TEST(FR_PRJ_1__loadReportsDanglingPlaceAndRange, SavedSample)
{
	ASSERT(save(project, dir).has_value());
	const QString file = dir + u"/groupaddresses.json"_s;
	const QByteArray original = readFile(file);
	QJsonObject root = readJson(file);
	QJsonArray addresses = root[u"groupAddresses"_s].toArray();
	QJsonObject address = addresses[1].toObject();
	address[u"range"_s] = Id<GroupRange>(77).toString();
	addresses[1] = address;
	root[u"groupAddresses"_s] = addresses;
	writeJson(file, root);
	std::string message = loadError(dir);
	ASSERT(mentions(message, u"groupaddresses.json: groupAddresses[1]"_s));
	ASSERT(mentions(message, Id<GroupRange>(77).toString()));
	writeFile(file, original);

	const QString deviceFile = dir + u"/devices/"_s + actuator.toString() + u".json"_s;
	QJsonObject device = readJson(deviceFile);
	device[u"place"_s] = Id<BuildingPart>(5).toString();
	writeJson(deviceFile, device);
	message = loadError(dir);
	ASSERT(mentions(message, actuator.toString() + u".json: place"_s));
	ASSERT(mentions(message, Id<BuildingPart>(5).toString()));
}

UNIT_TEST(FR_PRJ_1__loadReportsMissingAndUnlistedDeviceFiles, SavedSample)
{
	ASSERT(save(project, dir).has_value());
	const QString actuatorFile = dir + u"/devices/"_s + actuator.toString() + u".json"_s;
	const QByteArray content = readFile(actuatorFile);
	QFile::remove(actuatorFile);
	std::string message = loadError(dir);
	ASSERT(mentions(message, u"topology.json"_s));
	ASSERT(mentions(message, actuator.toString()));

	writeFile(actuatorFile, content);
	writeFile(dir + u"/devices/"_s + Id<Device>(99).toString() + u".json"_s, content);
	message = loadError(dir);
	ASSERT(mentions(message, Id<Device>(99).toString() + u".json: device is not listed"_s));
}

UNIT_TEST(FR_PRJ_1__loadReportsInvalidValues, SavedSample)
{
	ASSERT(save(project, dir).has_value());
	const QString topology = dir + u"/topology.json"_s;
	const QByteArray original = readFile(topology);

	const auto patchArea = [&](const QString& key, const QJsonValue& value)
	{
		QJsonObject root = QJsonDocument::fromJson(original).object();
		QJsonArray areas = root[u"areas"_s].toArray();
		QJsonObject first = areas[0].toObject();
		first[key] = value;
		areas[0] = first;
		root[u"areas"_s] = areas;
		writeJson(topology, root);
		return loadError(dir);
	};
	ASSERT(mentions(patchArea(u"number"_s, 16), u"topology.json: areas[0]: 'number' must be an integer in 0..15"_s));
	ASSERT(mentions(patchArea(u"number"_s, 1.5), u"'number' must be an integer"_s));
	ASSERT(mentions(patchArea(u"number"_s, u"1"_s), u"'number' must be an integer"_s));
	ASSERT(mentions(patchArea(u"medium"_s, u"Wifi"_s), u"invalid medium 'Wifi'"_s));
	ASSERT(mentions(patchArea(u"name"_s, 3), u"'name' must be a string"_s));
	ASSERT(mentions(patchArea(u"id"_s, u"dev-0000000000001"_s), u"invalid Area id"_s));
	ASSERT(mentions(patchArea(u"lines"_s, QJsonObject{}), u"'lines' must be an array"_s));
	QJsonObject root = QJsonDocument::fromJson(original).object();
	root.remove(u"areas"_s);
	writeJson(topology, root);
	ASSERT(mentions(loadError(dir), u"topology.json: missing 'areas'"_s));
}

UNIT_TEST(FR_PRJ_1__loadReportsDuplicateIdsAndCycles, SavedSample)
{
	ASSERT(save(project, dir).has_value());
	const QString buildings = dir + u"/buildings.json"_s;
	const QByteArray original = readFile(buildings);

	QJsonObject root = readJson(buildings);
	QJsonArray parts = root[u"buildingParts"_s].toArray();
	parts.append(parts[0]);
	root[u"buildingParts"_s] = parts;
	writeJson(buildings, root);
	std::string message = loadError(dir);
	ASSERT(mentions(message, u"buildings.json: buildingParts[3]: duplicate id "_s + building.toString()));

	root = QJsonDocument::fromJson(original).object();
	parts = root[u"buildingParts"_s].toArray();
	QJsonObject top = parts[0].toObject();
	top[u"parent"_s] = room.toString(); // house -> floor -> room -> house
	parts[0] = top;
	root[u"buildingParts"_s] = parts;
	writeJson(buildings, root);
	ASSERT(mentions(loadError(dir), u"buildings.json: building parts form a cycle"_s));
}

UNIT_TEST(FR_PRJ_1__loadReportsDeviceIdMismatchAndBadFlags, SavedSample)
{
	ASSERT(save(project, dir).has_value());
	const QString file = dir + u"/devices/"_s + button.toString() + u".json"_s;
	const QByteArray original = readFile(file);

	QJsonObject device = readJson(file);
	device[u"id"_s] = actuator.toString();
	writeJson(file, device);
	ASSERT(mentions(loadError(dir), u"does not match the file name"_s));

	device = QJsonDocument::fromJson(original).object();
	QJsonArray objects = device[u"comObjects"_s].toArray();
	QJsonObject object = objects[0].toObject();
	object[u"flags"_s] = u"CXZ"_s;
	objects[0] = object;
	device[u"comObjects"_s] = objects;
	writeJson(file, device);
	ASSERT(mentions(loadError(dir), u"comObjects[0]: invalid flags 'CXZ'"_s));

	object[u"flags"_s] = u"CWT"_s;
	object[u"dpt"_s] = u"1.001"_s;
	objects[0] = object;
	device[u"comObjects"_s] = objects;
	writeJson(file, device);
	ASSERT(mentions(loadError(dir), u"invalid dpt '1.001'"_s));
}
