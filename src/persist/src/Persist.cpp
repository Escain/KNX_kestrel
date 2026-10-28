#include "kestrel/persist/Persist.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>

using namespace Qt::StringLiterals;
using namespace kestrel::model;

namespace kestrel::persist
{

namespace
{

const QString kHeaderFile = u"kestrel.json"_s;
const QString kProjectFile = u"project.json"_s;
const QString kBuildingsFile = u"buildings.json"_s;
const QString kTopologyFile = u"topology.json"_s;
const QString kGroupAddressesFile = u"groupaddresses.json"_s;
const QString kDevicesDir = u"devices"_s;
const QString kFormatName = u"kestrel-project"_s;
constexpr qint64 kMaxFileSize = 64 * 1024 * 1024;

QString deviceFile(Id<Device> id)
{
	return kDevicesDir + u'/' + id.toString() + u".json"_s;
}

Unexpected fileError(ErrorCode code, const QString& file, const QString& message)
{
	return makeError(code, (file + u": "_s + message).toStdString());
}

// --- Writing -------------------------------------------------------------------------------------------------

void writeIndent(QByteArray& out, int depth)
{
	out.append(2 * depth, ' ');
}

void writeString(QByteArray& out, const QString& text)
{
	static constexpr char kHex[] = "0123456789abcdef";
	out += '"';
	for (const char c : text.toUtf8())
	{
		const auto byte = static_cast<unsigned char>(c);
		switch (c)
		{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			default:
				if (byte < 0x20)
				{
					out += "\\u00";
					out += kHex[byte >> 4];
					out += kHex[byte & 0x0F];
				}
				else
				{
					out += c;
				}
		}
	}
	out += '"';
}

void writeValue(QByteArray& out, const QJsonValue& value, int depth)
{
	switch (value.type())
	{
		case QJsonValue::Null:
		case QJsonValue::Undefined: out += "null"; break;
		case QJsonValue::Bool: out += value.toBool() ? "true" : "false"; break;
		case QJsonValue::Double:
		{
			const double number = value.toDouble();
			if (std::trunc(number) == number && std::fabs(number) < 9007199254740992.0)
			{
				out += QByteArray::number(static_cast<qint64>(number));
			}
			else
			{
				out += QByteArray::number(number, 'g', QLocale::FloatingPointShortest);
			}
			break;
	}
	case QJsonValue::String: writeString(out, value.toString()); break;
	case QJsonValue::Array:
	{
		const QJsonArray array = value.toArray();
		if (array.isEmpty())
		{
			out += "[]";
			break;
		}
		out += "[\n";
		for (qsizetype i = 0; i < array.size(); ++i)
		{
			writeIndent(out, depth + 1);
			writeValue(out, array[i], depth + 1);
			out += i + 1 < array.size() ? ",\n" : "\n";
		}
		writeIndent(out, depth);
		out += ']';
		break;
	}
	case QJsonValue::Object:
	{
		const QJsonObject object = value.toObject();
		if (object.isEmpty())
		{
			out += "{}";
			break;
		}
		QStringList keys = object.keys();
		std::sort(keys.begin(), keys.end());
		out += "{\n";
		for (qsizetype i = 0; i < keys.size(); ++i)
		{
			writeIndent(out, depth + 1);
			writeString(out, keys[i]);
			out += ": ";
			writeValue(out, object.value(keys[i]), depth + 1);
			out += i + 1 < keys.size() ? ",\n" : "\n";
		}
		writeIndent(out, depth);
		out += '}';
		break;
	}
	}
}

template <class T>
QJsonValue idValue(const std::optional<Id<T>>& id)
{
	return id ? QJsonValue(id->toString()) : QJsonValue(QJsonValue::Null);
}

QJsonValue number(unsigned value)
{
	return QJsonValue(static_cast<qint64>(value));
}

QJsonObject toJson(const ComObject& object, const Project& project)
{
	// Links of this object, sending first, then table order.
	std::vector<const Link*> links;
	for (const Id<Link> linkId : project.linksOfComObject(object.id))
		links.push_back(&project.links().at(linkId));
	std::stable_partition(links.begin(), links.end(), [](const Link* l) { return l->sending; });
	QJsonArray linkArray;
	for (const Link* l : links)
	{
		linkArray.append(QJsonObject{
			{u"id"_s, l->id.toString()},
			{u"groupAddress"_s, l->groupAddress.toString()},
			{u"sending"_s, l->sending},
		});
	}
	return QJsonObject{
		{u"id"_s, object.id.toString()},
		{u"number"_s, number(object.number)},
		{u"name"_s, object.name},
		{u"function"_s, object.function},
		{u"sizeInBit"_s, number(object.sizeInBit)},
		{u"flags"_s, object.flags.toString()},
		{u"dpt"_s, object.dpt},
		{u"catalogRef"_s, object.catalogRef},
		{u"links"_s, linkArray},
	};
}

QJsonObject toJson(const Device& device, const Project& project)
{
	QJsonObject params;
	for (const auto& [ref, value] : device.params)
		params.insert(ref, value);
	QJsonArray objects;
	for (const Id<ComObject> objectId : project.comObjectsOf(device.id))
		objects.append(toJson(project.comObjects().at(objectId), project));
	const DeviceState& state = project.deviceState(device.id);
	return QJsonObject{
		{u"id"_s, device.id.toString()},
		{u"number"_s, device.number ? number(*device.number) : QJsonValue(QJsonValue::Null)},
		{u"name"_s, device.name},
		{u"description"_s, device.description},
		{u"place"_s, idValue(device.place)},
		{u"productRef"_s, device.productRef},
		{u"appProgramRef"_s, device.appProgramRef},
		{u"params"_s, params},
		{u"comObjects"_s, objects},
		{u"state"_s,
			QJsonObject{
				{u"iaProgrammed"_s, state.iaProgrammed},
				{u"serial"_s, state.serial},
				{u"downloadedHash"_s, state.downloadedHash},
			}},
	};
}

// --- Reading -------------------------------------------------------------------------------------------------

/// Typed accessors over one file. The first failure is kept (file + location + message); later calls return
/// defaults, so parsing code reads straight through and checks ok() at the end.
class Reader
{
public:
	explicit Reader(QString file) : m_file(std::move(file)) {}

	bool ok() const { return !m_error; }
	Unexpected error() const { return Unexpected{*m_error}; }
	const QString& file() const { return m_file; }

	void fail(ErrorCode code, const QString& where, const QString& message)
	{
		if (!m_error)
		{
			const QString location = where.isEmpty() ? QString() : where + u": "_s;
			m_error = Error{code, (m_file + u": "_s + location + message).toStdString()};
		}
	}

	QJsonValue value(const QJsonObject& object, const QString& key, const QString& where)
	{
		if (!object.contains(key))
		{
			fail(ErrorCode::ParseError, where, u"missing '%1'"_s.arg(key));
			return {};
		}
		return object.value(key);
	}

	QString string(const QJsonObject& object, const QString& key, const QString& where)
	{
		const QJsonValue v = value(object, key, where);
		if (ok() && !v.isString())
			fail(ErrorCode::ParseError, where, u"'%1' must be a string"_s.arg(key));
		return v.toString();
	}

	bool boolean(const QJsonObject& object, const QString& key, const QString& where)
	{
		const QJsonValue v = value(object, key, where);
		if (ok() && !v.isBool())
			fail(ErrorCode::ParseError, where, u"'%1' must be a boolean"_s.arg(key));
		return v.toBool();
	}

	std::optional<std::int64_t> optionalInteger(
		const QJsonObject& object, const QString& key, const QString& where, std::int64_t min, std::int64_t max)
	{
		const QJsonValue v = value(object, key, where);
		if (!ok() || v.isNull())
			return std::nullopt;
		const double d = v.toDouble();
		if (!v.isDouble() || std::trunc(d) != d || d < static_cast<double>(min) || d > static_cast<double>(max))
		{
			fail(ErrorCode::ParseError, where, u"'%1' must be an integer in %2..%3"_s.arg(key).arg(min).arg(max));
			return std::nullopt;
		}
		return static_cast<std::int64_t>(d);
	}

	std::int64_t integer(
		const QJsonObject& object, const QString& key, const QString& where, std::int64_t min, std::int64_t max)
	{
		const auto v = optionalInteger(object, key, where, min, max);
		if (ok() && !v)
			fail(ErrorCode::ParseError, where, u"'%1' must not be null"_s.arg(key));
		return v.value_or(min);
	}

	QJsonArray array(const QJsonObject& object, const QString& key, const QString& where)
	{
		const QJsonValue v = value(object, key, where);
		if (ok() && !v.isArray())
			fail(ErrorCode::ParseError, where, u"'%1' must be an array"_s.arg(key));
		return v.toArray();
	}

	QJsonObject object(const QJsonObject& parent, const QString& key, const QString& where)
	{
		const QJsonValue v = value(parent, key, where);
		if (ok() && !v.isObject())
			fail(ErrorCode::ParseError, where, u"'%1' must be an object"_s.arg(key));
		return v.toObject();
	}

	QJsonObject element(const QJsonArray& array, qsizetype i, const QString& where)
	{
		if (ok() && !array[i].isObject())
			fail(ErrorCode::ParseError, where, u"must be an object"_s);
		return array[i].toObject();
	}

	template <class T>
	Id<T> idText(const QJsonValue& v, const QString& where)
	{
		if (!ok())
			return {};
		const auto id = Id<T>::parse(v.toString());
		if (!v.isString() || !id)
		{
			fail(ErrorCode::ParseError, where,
				u"invalid %1 id '%2'"_s.arg(QLatin1StringView(toString(EntityTraits<T>::kKind)), v.toString()));
			return {};
		}
		return *id;
	}

	template <class T>
	Id<T> id(const QJsonObject& object, const QString& key, const QString& where)
	{
		const QJsonValue v = value(object, key, where);
		return idText<T>(v, where.isEmpty() ? key : where + u'.' + key);
	}

	template <class T>
	std::optional<Id<T>> optionalId(const QJsonObject& object, const QString& key, const QString& where)
	{
		const QJsonValue v = value(object, key, where);
		if (!ok() || v.isNull())
			return std::nullopt;
		return idText<T>(v, where.isEmpty() ? key : where + u'.' + key);
	}

	template <class E>
	E enumeration(const QJsonObject& object, const QString& key, const QString& where,
		std::optional<E> (*parse)(QStringView), E fallback)
	{
		const QString text = string(object, key, where);
		if (!ok())
			return fallback;
		const auto parsed = parse(text);
		if (!parsed)
		{
			fail(ErrorCode::ParseError, where, u"invalid %1 '%2'"_s.arg(key, text));
			return fallback;
		}
		return *parsed;
	}

	QString dpt(const QJsonObject& object, const QString& where)
	{
		const QString text = string(object, u"dpt"_s, where);
		if (ok() && !isValidDptRef(text))
			fail(ErrorCode::ParseError, where, u"invalid dpt '%1'"_s.arg(text));
		return text;
	}

	/// Registers an id; a second occurrence anywhere in the project is an error.
	template <class T>
	void unique(std::set<Id<T>>& seen, Id<T> id, const QString& where)
	{
		if (ok() && !seen.insert(id).second)
			fail(ErrorCode::ParseError, where, u"duplicate id %1"_s.arg(id.toString()));
	}

	template <class T>
	void reference(const std::set<Id<T>>& known, const std::optional<Id<T>>& id, const QString& where)
	{
		if (ok() && id && !known.contains(*id))
			fail(ErrorCode::NotFound, where, u"unknown %1 %2"_s.arg(QLatin1StringView(toString(EntityTraits<T>::kKind)), id->toString()));
	}

private:
	QString m_file;
	std::optional<Error> m_error;
};

QString at(const QString& key, qsizetype index)
{
	return u"%1[%2]"_s.arg(key).arg(index);
}

/// Detects parent cycles: following parents from any node must end within `count` steps.
template <class T>
bool hasCycle(const std::vector<T>& nodes)
{
	std::map<Id<T>, std::optional<Id<T>>> parentOf;
	for (const T& node : nodes)
		parentOf[node.id] = node.parent;
	for (const T& node : nodes)
	{
		std::optional<Id<T>> current = node.parent;
		for (std::size_t steps = 0; current; ++steps)
		{
			if (steps > nodes.size())
				return true;
			const auto it = parentOf.find(*current);
			current = it == parentOf.end() ? std::nullopt : it->second;
		}
	}
	return false;
}

struct Loaded
{
	ProjectSettings settings;
	std::vector<BuildingPart> buildingParts;
	std::vector<Area> areas;
	std::vector<Line> lines;
	std::vector<Device> devices;
	std::vector<ComObject> comObjects;
	std::vector<GroupRange> groupRanges;
	std::vector<GroupAddress> groupAddresses;
	std::vector<Link> links;
	std::vector<std::pair<Id<Device>, DeviceState>> states;
};

const QJsonObject* findFile(const ProjectTree& tree, const QString& path)
{
	const auto it = tree.files.find(path);
	return it == tree.files.end() ? nullptr : &it->second;
}

Status parseSettings(const QJsonObject& root, Loaded& out)
{
	Reader r(kProjectFile);
	out.settings.name = r.string(root, u"name"_s, {});
	out.settings.gaStyle = r.enumeration(
		root, u"gaStyle"_s, {}, &parseGroupAddressStyle, knx::GroupAddressStyle::ThreeLevel);
	out.settings.contentLanguage = r.string(root, u"contentLanguage"_s, {});
	return r.ok() ? ok() : Status(r.error());
}

Status parseBuildings(const QJsonObject& root, Loaded& out, std::set<Id<BuildingPart>>& ids)
{
	Reader r(kBuildingsFile);
	const QJsonArray parts = r.array(root, u"buildingParts"_s, {});
	for (qsizetype i = 0; i < parts.size() && r.ok(); ++i)
	{
		const QString where = at(u"buildingParts"_s, i);
		const QJsonObject o = r.element(parts, i, where);
		BuildingPart part;
		part.id = r.id<BuildingPart>(o, u"id"_s, where);
		part.parent = r.optionalId<BuildingPart>(o, u"parent"_s, where);
		part.kind = r.enumeration(o, u"kind"_s, where, &parseBuildingPartKind, BuildingPartKind::Building);
		part.name = r.string(o, u"name"_s, where);
		part.description = r.string(o, u"description"_s, where);
		r.unique(ids, part.id, where);
		out.buildingParts.push_back(std::move(part));
	}
	for (std::size_t i = 0; i < out.buildingParts.size() && r.ok(); ++i)
		r.reference(ids, out.buildingParts[i].parent, at(u"buildingParts"_s, static_cast<qsizetype>(i)));
	if (r.ok() && hasCycle(out.buildingParts))
		r.fail(ErrorCode::ParseError, {}, u"building parts form a cycle"_s);
	return r.ok() ? ok() : Status(r.error());
}

Status parseGroupAddresses(const QJsonObject& root, Loaded& out, std::set<Id<GroupAddress>>& ids)
{
	Reader r(kGroupAddressesFile);
	std::set<Id<GroupRange>> rangeIds;
	const QJsonArray ranges = r.array(root, u"groupRanges"_s, {});
	for (qsizetype i = 0; i < ranges.size() && r.ok(); ++i)
	{
		const QString where = at(u"groupRanges"_s, i);
		const QJsonObject o = r.element(ranges, i, where);
		GroupRange range;
		range.id = r.id<GroupRange>(o, u"id"_s, where);
		range.parent = r.optionalId<GroupRange>(o, u"parent"_s, where);
		range.name = r.string(o, u"name"_s, where);
		range.start = static_cast<std::uint16_t>(r.integer(o, u"start"_s, where, 0, 0xFFFF));
		range.end = static_cast<std::uint16_t>(r.integer(o, u"end"_s, where, 0, 0xFFFF));
		if (r.ok() && range.start > range.end)
			r.fail(ErrorCode::ParseError, where, u"start is after end"_s);
		r.unique(rangeIds, range.id, where);
		out.groupRanges.push_back(std::move(range));
	}
	for (std::size_t i = 0; i < out.groupRanges.size() && r.ok(); ++i)
		r.reference(rangeIds, out.groupRanges[i].parent, at(u"groupRanges"_s, static_cast<qsizetype>(i)));
	if (r.ok() && hasCycle(out.groupRanges))
		r.fail(ErrorCode::ParseError, {}, u"group ranges form a cycle"_s);

	const QJsonArray addresses = r.array(root, u"groupAddresses"_s, {});
	for (qsizetype i = 0; i < addresses.size() && r.ok(); ++i)
	{
		const QString where = at(u"groupAddresses"_s, i);
		const QJsonObject o = r.element(addresses, i, where);
		GroupAddress address;
		address.id = r.id<GroupAddress>(o, u"id"_s, where);
		address.range = r.optionalId<GroupRange>(o, u"range"_s, where);
		address.raw = static_cast<std::uint16_t>(r.integer(o, u"raw"_s, where, 0, 0xFFFF));
		address.name = r.string(o, u"name"_s, where);
		address.dpt = r.dpt(o, where);
		address.description = r.string(o, u"description"_s, where);
		r.unique(ids, address.id, where);
		r.reference(rangeIds, address.range, where);
		out.groupAddresses.push_back(std::move(address));
	}
	return r.ok() ? ok() : Status(r.error());
}

/// Returns the listed devices in order, with their line.
Status parseTopology(const QJsonObject& root, Loaded& out, std::vector<std::pair<Id<Device>, Id<Line>>>& listed)
{
	Reader r(kTopologyFile);
	std::set<Id<Area>> areaIds;
	std::set<Id<Line>> lineIds;
	std::set<Id<Device>> deviceIds;
	const QJsonArray areas = r.array(root, u"areas"_s, {});
	for (qsizetype i = 0; i < areas.size() && r.ok(); ++i)
	{
		const QString where = at(u"areas"_s, i);
		const QJsonObject o = r.element(areas, i, where);
		Area area;
		area.id = r.id<Area>(o, u"id"_s, where);
		area.number = static_cast<unsigned>(r.integer(o, u"number"_s, where, 0, kMaxAreaNumber));
		area.name = r.string(o, u"name"_s, where);
		area.medium = r.enumeration(o, u"medium"_s, where, &parseMedium, Medium::TP);
		r.unique(areaIds, area.id, where);

		const QJsonArray lines = r.array(o, u"lines"_s, where);
		for (qsizetype j = 0; j < lines.size() && r.ok(); ++j)
		{
			const QString lineWhere = where + u'.' + at(u"lines"_s, j);
			const QJsonObject lo = r.element(lines, j, lineWhere);
			Line line;
			line.id = r.id<Line>(lo, u"id"_s, lineWhere);
			line.area = area.id;
			line.number = static_cast<unsigned>(r.integer(lo, u"number"_s, lineWhere, 0, kMaxLineNumber));
			line.name = r.string(lo, u"name"_s, lineWhere);
			line.medium = r.enumeration(lo, u"medium"_s, lineWhere, &parseMedium, Medium::TP);
			r.unique(lineIds, line.id, lineWhere);

			const QJsonArray devices = r.array(lo, u"devices"_s, lineWhere);
			for (qsizetype k = 0; k < devices.size() && r.ok(); ++k)
			{
				const QString deviceWhere = lineWhere + u'.' + at(u"devices"_s, k);
				const Id<Device> device = r.idText<Device>(devices[k], deviceWhere);
				r.unique(deviceIds, device, deviceWhere);
				listed.emplace_back(device, line.id);
			}
			out.lines.push_back(std::move(line));
		}
		out.areas.push_back(std::move(area));
	}
	return r.ok() ? ok() : Status(r.error());
}

Status parseDevice(const QString& file, const QJsonObject& root, Id<Device> expectedId, Id<Line> line,
	const std::set<Id<BuildingPart>>& partIds, const std::set<Id<GroupAddress>>& addressIds,
	std::set<Id<ComObject>>& objectIds, std::set<Id<Link>>& linkIds, Loaded& out)
{
	Reader r(file);
	Device device;
	device.id = r.id<Device>(root, u"id"_s, {});
	if (r.ok() && device.id != expectedId)
		r.fail(ErrorCode::ParseError, {}, u"id %1 does not match the file name"_s.arg(device.id.toString()));
	device.line = line;
	const auto deviceNumber = r.optionalInteger(root, u"number"_s, {}, 0, kMaxDeviceNumber);
	if (deviceNumber)
		device.number = static_cast<unsigned>(*deviceNumber);
	device.name = r.string(root, u"name"_s, {});
	device.description = r.string(root, u"description"_s, {});
	device.place = r.optionalId<BuildingPart>(root, u"place"_s, {});
	r.reference(partIds, device.place, u"place"_s);
	device.productRef = r.string(root, u"productRef"_s, {});
	device.appProgramRef = r.string(root, u"appProgramRef"_s, {});
	const QJsonObject params = r.object(root, u"params"_s, {});
	for (auto it = params.begin(); it != params.end() && r.ok(); ++it)
	{
		if (!it.value().isString())
			r.fail(ErrorCode::ParseError, u"params"_s, u"value of '%1' must be a string"_s.arg(it.key()));
		device.params.emplace(it.key(), it.value().toString());
	}

	const QJsonArray objects = r.array(root, u"comObjects"_s, {});
	for (qsizetype i = 0; i < objects.size() && r.ok(); ++i)
	{
		const QString where = at(u"comObjects"_s, i);
		const QJsonObject o = r.element(objects, i, where);
		ComObject object;
		object.id = r.id<ComObject>(o, u"id"_s, where);
		object.device = device.id;
		object.number = static_cast<unsigned>(r.integer(o, u"number"_s, where, 0, 0xFFFF));
		object.name = r.string(o, u"name"_s, where);
		object.function = r.string(o, u"function"_s, where);
		object.sizeInBit = static_cast<unsigned>(r.integer(o, u"sizeInBit"_s, where, 1, kMaxComObjectSizeInBit));
		const QString flags = r.string(o, u"flags"_s, where);
		const auto parsedFlags = ComObjectFlags::parse(flags);
		if (r.ok() && !parsedFlags)
			r.fail(ErrorCode::ParseError, where, u"invalid flags '%1'"_s.arg(flags));
		object.flags = parsedFlags.value_or(ComObjectFlags{});
		object.dpt = r.dpt(o, where);
		object.catalogRef = r.string(o, u"catalogRef"_s, where);
		r.unique(objectIds, object.id, where);

		const QJsonArray links = r.array(o, u"links"_s, where);
		for (qsizetype j = 0; j < links.size() && r.ok(); ++j)
		{
			const QString linkWhere = where + u'.' + at(u"links"_s, j);
			const QJsonObject lo = r.element(links, j, linkWhere);
			Link l;
			l.id = r.id<Link>(lo, u"id"_s, linkWhere);
			l.comObject = object.id;
			l.groupAddress = r.id<GroupAddress>(lo, u"groupAddress"_s, linkWhere);
			l.sending = r.boolean(lo, u"sending"_s, linkWhere);
			r.unique(linkIds, l.id, linkWhere);
			r.reference(addressIds, std::optional(l.groupAddress), linkWhere);
			out.links.push_back(l);
		}
		out.comObjects.push_back(std::move(object));
	}

	const QJsonObject stateObject = r.object(root, u"state"_s, {});
	DeviceState state;
	state.iaProgrammed = r.boolean(stateObject, u"iaProgrammed"_s, u"state"_s);
	state.serial = r.string(stateObject, u"serial"_s, u"state"_s);
	state.downloadedHash = r.string(stateObject, u"downloadedHash"_s, u"state"_s);
	if (!r.ok())
		return r.error();
	if (state != DeviceState{})
		out.states.emplace_back(device.id, std::move(state));
	out.devices.push_back(std::move(device));
	return ok();
}

Result<QJsonObject> readJsonFile(const QString& dir, const QString& relative)
{
	QFile file(QDir(dir).filePath(relative));
	if (!file.exists())
		return fileError(ErrorCode::NotFound, relative, u"file is missing"_s);
	if (!file.open(QIODevice::ReadOnly))
		return fileError(ErrorCode::IoError, relative, file.errorString());
	if (file.size() > kMaxFileSize)
		return fileError(ErrorCode::ParseError, relative, u"file is too large"_s);
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError)
		return fileError(
			ErrorCode::ParseError, relative, u"%1 at offset %2"_s.arg(parseError.errorString()).arg(parseError.offset));
	if (!document.isObject())
		return fileError(ErrorCode::ParseError, relative, u"root is not a JSON object"_s);
	return document.object();
}

} // namespace

// --- Public API ----------------------------------------------------------------------------------------------

const MigrationChain& builtinMigrations()
{
	static const MigrationChain kChain;
	return kChain;
}

QByteArray toJson(const QJsonObject& object)
{
	QByteArray out;
	writeValue(out, object, 0);
	out += '\n';
	return out;
}

ProjectTree toTree(const Project& project)
{
	ProjectTree tree;
	tree.files[kHeaderFile] = QJsonObject{
		{u"format"_s, kFormatName},
		{u"formatVersion"_s, kFormatVersion},
		{u"generator"_s, QString::fromLatin1("Kestrel " KESTREL_VERSION_STRING)},
	};

	const ProjectSettings& settings = project.settings();
	tree.files[kProjectFile] = QJsonObject{
		{u"name"_s, settings.name},
		{u"gaStyle"_s, model::toString(settings.gaStyle)},
		{u"contentLanguage"_s, settings.contentLanguage},
	};

	QJsonArray parts;
	for (const BuildingPart& part : project.buildingParts())
	{
		parts.append(QJsonObject{
			{u"id"_s, part.id.toString()},
			{u"parent"_s, idValue(part.parent)},
			{u"kind"_s, toString(part.kind)},
			{u"name"_s, part.name},
			{u"description"_s, part.description},
		});
	}
	tree.files[kBuildingsFile] = QJsonObject{{u"buildingParts"_s, parts}};

	QJsonArray areas;
	for (const Area& area : project.areas())
	{
		QJsonArray lines;
		for (const Id<Line> lineId : project.linesOf(area.id))
		{
			const Line& line = project.lines().at(lineId);
			QJsonArray devices;
			for (const Id<Device> deviceId : project.devicesOf(line.id))
			{
				devices.append(deviceId.toString());
				tree.files[deviceFile(deviceId)] = toJson(project.devices().at(deviceId), project);
			}
			lines.append(QJsonObject{
				{u"id"_s, line.id.toString()},
				{u"number"_s, number(line.number)},
				{u"name"_s, line.name},
				{u"medium"_s, toString(line.medium)},
				{u"devices"_s, devices},
			});
		}
		areas.append(QJsonObject{
			{u"id"_s, area.id.toString()},
			{u"number"_s, number(area.number)},
			{u"name"_s, area.name},
			{u"medium"_s, toString(area.medium)},
			{u"lines"_s, lines},
		});
	}
	tree.files[kTopologyFile] = QJsonObject{{u"areas"_s, areas}};

	QJsonArray ranges;
	for (const GroupRange& range : project.groupRanges())
	{
		ranges.append(QJsonObject{
			{u"id"_s, range.id.toString()},
			{u"parent"_s, idValue(range.parent)},
			{u"name"_s, range.name},
			{u"start"_s, number(range.start)},
			{u"end"_s, number(range.end)},
		});
	}
	QJsonArray addresses;
	for (const GroupAddress& address : project.groupAddresses())
	{
		addresses.append(QJsonObject{
			{u"id"_s, address.id.toString()},
			{u"range"_s, idValue(address.range)},
			{u"raw"_s, number(address.raw)},
			{u"name"_s, address.name},
			{u"dpt"_s, address.dpt},
			{u"description"_s, address.description},
		});
	}
	tree.files[kGroupAddressesFile] = QJsonObject{{u"groupRanges"_s, ranges}, {u"groupAddresses"_s, addresses}};
	return tree;
}

Status writeTree(const ProjectTree& tree, const QString& dir)
{
	QDir root(dir);
	if (!root.mkpath(kDevicesDir))
		return fileError(ErrorCode::IoError, dir, u"cannot create the project folder"_s);

	for (const auto& [path, object] : tree.files)
	{
		QSaveFile file(root.filePath(path));
		if (!file.open(QIODevice::WriteOnly))
			return fileError(ErrorCode::IoError, path, file.errorString());
		const QByteArray data = toJson(object);
		if (file.write(data) != data.size() || !file.commit())
			return fileError(ErrorCode::IoError, path, file.errorString());
	}

	const QDir devices(root.filePath(kDevicesDir));
	for (const QString& name : devices.entryList({u"*.json"_s}, QDir::Files, QDir::Name))
	{
		if (!tree.files.contains(kDevicesDir + u'/' + name) && !QFile::remove(devices.filePath(name)))
			return fileError(ErrorCode::IoError, kDevicesDir + u'/' + name, u"cannot remove stale file"_s);
	}
	return ok();
}

Status save(const Project& project, const QString& dir)
{
	return writeTree(toTree(project), dir);
}

Result<ProjectTree> readTree(const QString& dir)
{
	const QDir root(dir);
	if (!root.exists())
		return fileError(ErrorCode::NotFound, dir, u"project folder does not exist"_s);

	ProjectTree tree;
	for (const QString& name : {kHeaderFile, kProjectFile, kBuildingsFile, kTopologyFile, kGroupAddressesFile})
	{
		KESTREL_TRY(tree.files[name], readJsonFile(dir, name));
	}
	const QDir devices(root.filePath(kDevicesDir));
	if (devices.exists())
	{
		for (const QString& name : devices.entryList({u"*.json"_s}, QDir::Files, QDir::Name))
		{
			const QString relative = kDevicesDir + u'/' + name;
			KESTREL_TRY(tree.files[relative], readJsonFile(dir, relative));
		}
	}
	return tree;
}

Status migrate(ProjectTree& tree, const MigrationChain& chain)
{
	const auto it = tree.files.find(kHeaderFile);
	if (it == tree.files.end())
		return fileError(ErrorCode::NotFound, kHeaderFile, u"file is missing"_s);

	Reader r(kHeaderFile);
	const QString format = r.string(it->second, u"format"_s, {});
	if (r.ok() && format != kFormatName)
		r.fail(ErrorCode::ParseError, {}, u"not a Kestrel project (format '%1')"_s.arg(format));
	int version = static_cast<int>(r.integer(it->second, u"formatVersion"_s, {}, 0, 1'000'000));
	if (!r.ok())
		return r.error();
	if (version > kFormatVersion)
	{
		return fileError(ErrorCode::Unsupported, kHeaderFile,
			u"formatVersion %1 is newer than the supported version %2; please update Kestrel"_s.arg(version).arg(
				kFormatVersion));
	}

	while (version < kFormatVersion)
	{
		const auto step = std::find_if(
			chain.begin(), chain.end(), [&](const Migration& m) { return m.fromVersion == version && m.apply; });
		if (step == chain.end())
		{
			return fileError(ErrorCode::Unsupported, kHeaderFile,
				u"formatVersion %1 is not supported (no migration to version %2)"_s.arg(version).arg(version + 1));
		}
		if (Status status = step->apply(tree); !status)
		{
			return fileError(status.error().code, kHeaderFile,
				u"migration from version %1 failed: %2"_s.arg(version).arg(
					QString::fromStdString(status.error().message)));
		}
		++version;
		tree.files[kHeaderFile].insert(u"formatVersion"_s, version);
	}
	return ok();
}

Result<Project> fromTree(const ProjectTree& tree, std::optional<IdGenerator> idGenerator)
{
	for (const QString& name : {kHeaderFile, kProjectFile, kBuildingsFile, kTopologyFile, kGroupAddressesFile})
	{
		if (!findFile(tree, name))
			return fileError(ErrorCode::NotFound, name, u"file is missing"_s);
	}
	{
		Reader r(kHeaderFile);
		const auto version = r.integer(*findFile(tree, kHeaderFile), u"formatVersion"_s, {}, 0, 1'000'000);
		if (r.ok() && version != kFormatVersion)
			r.fail(ErrorCode::Unsupported, {}, u"formatVersion %1 must be migrated first"_s.arg(version));
		if (!r.ok())
			return r.error();
	}

	Loaded loaded;
	std::set<Id<BuildingPart>> partIds;
	std::set<Id<GroupAddress>> addressIds;
	std::set<Id<ComObject>> objectIds;
	std::set<Id<Link>> linkIds;
	std::vector<std::pair<Id<Device>, Id<Line>>> listed;
	KESTREL_CHECK(parseSettings(*findFile(tree, kProjectFile), loaded));
	KESTREL_CHECK(parseBuildings(*findFile(tree, kBuildingsFile), loaded, partIds));
	KESTREL_CHECK(parseGroupAddresses(*findFile(tree, kGroupAddressesFile), loaded, addressIds));
	KESTREL_CHECK(parseTopology(*findFile(tree, kTopologyFile), loaded, listed));

	std::set<QString> deviceFiles;
	for (const auto& [deviceId, lineId] : listed)
	{
		const QString file = deviceFile(deviceId);
		const QJsonObject* root = findFile(tree, file);
		if (!root)
		{
			return fileError(ErrorCode::NotFound, kTopologyFile,
				u"device %1 has no file %2"_s.arg(deviceId.toString(), file));
		}
		deviceFiles.insert(file);
		KESTREL_CHECK(parseDevice(file, *root, deviceId, lineId, partIds, addressIds, objectIds, linkIds, loaded));
	}
	for (const auto& [path, object] : tree.files)
	{
		if (path.startsWith(kDevicesDir + u'/') && !deviceFiles.contains(path))
			return fileError(ErrorCode::ParseError, path, u"device is not listed in topology.json"_s);
	}

	Project project(idGenerator ? *idGenerator : IdGenerator::random());
	KESTREL_CHECK(project.edit(u"Load"_s,
		[&](Tx& tx)
		{
			tx.setSettings(loaded.settings);
			for (auto& e : loaded.buildingParts)
				tx.insert(std::move(e));
			for (auto& e : loaded.areas)
				tx.insert(std::move(e));
			for (auto& e : loaded.lines)
				tx.insert(std::move(e));
			for (auto& e : loaded.devices)
				tx.insert(std::move(e));
			for (auto& e : loaded.comObjects)
				tx.insert(std::move(e));
			for (auto& e : loaded.groupRanges)
				tx.insert(std::move(e));
			for (auto& e : loaded.groupAddresses)
				tx.insert(std::move(e));
			for (auto& e : loaded.links)
				tx.insert(std::move(e));
			return ok();
		}));
	for (auto& [deviceId, state] : loaded.states)
		project.recordState(deviceId, std::move(state));
	project.history().clear();
	project.history().setClean();
	return project;
}

Result<Project> load(const QString& dir, const LoadOptions& options)
{
	KESTREL_TRY(ProjectTree tree, readTree(dir));
	KESTREL_CHECK(migrate(tree, options.migrations));
	return fromTree(tree, options.idGenerator);
}

} // namespace kestrel::persist
