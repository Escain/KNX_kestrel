#include "kestrel/simnet/ScenarioLoader.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <set>

namespace kestrel::simnet
{

using namespace Qt::StringLiterals;

namespace
{

Unexpected fail(const std::string& path, const std::string& message)
{
	return makeError(ErrorCode::InvalidArgument, path + ": " + message);
}

Result<QString> optionalString(const QJsonObject& object, const QString& key, const std::string& path)
{
	const QJsonValue value = object.value(key);
	if (value.isUndefined() || value.isNull())
	{
		return QString();
	}
	if (!value.isString())
	{
		return fail(path + "." + key.toStdString(), "expected a string");
	}
	return value.toString();
}

Result<std::uint16_t> parseNumber16(const QString& text, const std::string& path)
{
	bool ok = false;
	const uint value = text.startsWith(u"0x"_s, Qt::CaseInsensitive) ? text.mid(2).toUInt(&ok, 16) : text.toUInt(&ok, 10);
	if (!ok || value > 0xFFFF)
	{
		return fail(path, "expected a 16-bit number (decimal or 0x hex)");
	}
	return static_cast<std::uint16_t>(value);
}

Result<sim::GroupObjectConfig> parseGroupObject(const QJsonValue& value, const std::string& path)
{
	if (!value.isObject())
	{
		return fail(path, "expected an object");
	}
	const QJsonObject object = value.toObject();
	sim::GroupObjectConfig config;

	KESTREL_TRY(const QString ga, optionalString(object, u"ga"_s, path));
	auto address = knx::GroupAddress::parse(ga.toStdString());
	if (!address)
	{
		return fail(path + ".ga", address.error().message);
	}
	config.address = *address;

	KESTREL_TRY(const QString flags, optionalString(object, u"flags"_s, path));
	if (!flags.isEmpty())
	{
		auto parsed = sim::GroupObjectFlags::parse(flags.toStdString());
		if (!parsed)
		{
			return fail(path + ".flags", parsed.error().message);
		}
		config.flags = *parsed;
	}

	KESTREL_TRY(const QString dpt, optionalString(object, u"dpt"_s, path));
	if (!dpt.isEmpty())
	{
		auto parsed = dpt::DptId::parse(dpt.toStdString());
		if (!parsed)
		{
			return fail(path + ".dpt", parsed.error().message);
		}
		config.dpt = *parsed;
	}

	KESTREL_TRY(const QString hexValue, optionalString(object, u"value"_s, path));
	if (!hexValue.isEmpty())
	{
		auto bytes = fromHex(hexValue.toStdString());
		if (!bytes)
		{
			return fail(path + ".value", bytes.error().message);
		}
		config.value = *bytes;
	}
	else if (config.dpt.main != 0)
	{
		const std::size_t bits = dpt::sizeInBit(config.dpt);
		config.value.assign(bits <= 8 ? 1 : (bits + 7) / 8, 0);
	}
	else
	{
		config.value = {0};
	}
	return config;
}

Result<ScenarioDevice> parseDevice(const QJsonValue& value, const std::string& path)
{
	if (!value.isObject())
	{
		return fail(path, "expected an object");
	}
	const QJsonObject object = value.toObject();
	ScenarioDevice device;
	sim::DeviceConfig& config = device.config;

	KESTREL_TRY(const QString name, optionalString(object, u"name"_s, path));
	if (name.isEmpty())
	{
		return fail(path + ".name", "a device needs a non-empty name");
	}
	config.name = name.toStdString();

	KESTREL_TRY(const QString address, optionalString(object, u"address"_s, path));
	if (!address.isEmpty())
	{
		auto parsed = knx::IndividualAddress::parse(address.toStdString());
		if (!parsed)
		{
			return fail(path + ".address", parsed.error().message);
		}
		config.address = *parsed;
	}

	KESTREL_TRY(const QString serial, optionalString(object, u"serial"_s, path));
	if (!serial.isEmpty())
	{
		auto parsed = knx::SerialNumber::parse(serial.toStdString());
		if (!parsed)
		{
			return fail(path + ".serial", parsed.error().message);
		}
		config.serial = *parsed;
	}

	KESTREL_TRY(const QString mask, optionalString(object, u"maskVersion"_s, path));
	if (!mask.isEmpty())
	{
		bool ok = false;
		const uint parsed = QString(mask).remove(u"0x"_s, Qt::CaseInsensitive).toUInt(&ok, 16);
		if (!ok || parsed > 0xFFFF)
		{
			return fail(path + ".maskVersion", "expected 4 hex digits");
		}
		config.maskVersion = static_cast<std::uint16_t>(parsed);
	}

	const QJsonValue progMode = object.value(u"progMode"_s);
	if (!progMode.isUndefined())
	{
		if (!progMode.isBool())
		{
			return fail(path + ".progMode", "expected a boolean");
		}
		config.progMode = progMode.toBool();
	}

	const QJsonArray groupObjects = object.value(u"groupObjects"_s).toArray();
	for (qsizetype i = 0; i < groupObjects.size(); ++i)
	{
		KESTREL_TRY(auto groupObject, parseGroupObject(groupObjects[i], path + ".groupObjects[" + std::to_string(i) + "]"));
		config.groupObjects.push_back(std::move(groupObject));
	}

	const QJsonArray memory = object.value(u"memory"_s).toArray();
	for (qsizetype i = 0; i < memory.size(); ++i)
	{
		const std::string blockPath = path + ".memory[" + std::to_string(i) + "]";
		const QJsonObject block = memory[i].toObject();
		KESTREL_TRY(const QString start, optionalString(block, u"address"_s, blockPath));
		KESTREL_TRY(const std::uint16_t startAddress, parseNumber16(start, blockPath + ".address"));
		KESTREL_TRY(const QString data, optionalString(block, u"data"_s, blockPath));
		auto bytes = fromHex(data.toStdString());
		if (!bytes)
		{
			return fail(blockPath + ".data", bytes.error().message);
		}
		device.memory.push_back(MemoryBlock{startAddress, *bytes});
	}
	return device;
}

} // namespace

sim::TunnelServerConfig Scenario::serverConfig() const
{
	sim::TunnelServerConfig config;
	config.deviceInfo.address = interfaceAddress;
	config.deviceInfo.friendlyName = friendlyName;
	config.deviceInfo.serial = serial;
	config.tunnelAddresses = tunnelAddresses.empty() ? std::vector<knx::IndividualAddress>{interfaceAddress} : tunnelAddresses;
	return config;
}

Result<Scenario> ScenarioLoader::fromJson(const QByteArray& json)
{
	QJsonParseError error{};
	const QJsonDocument document = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError)
	{
		return makeError(ErrorCode::ParseError,
			"scenario: " + error.errorString().toStdString() + " at offset " + std::to_string(error.offset));
	}
	if (!document.isObject())
	{
		return makeError(ErrorCode::ParseError, "scenario: the root must be an object");
	}
	const QJsonObject root = document.object();
	Scenario scenario;

	KESTREL_TRY(const QString interfaceAddress, optionalString(root, u"interfaceAddress"_s, "scenario"));
	if (!interfaceAddress.isEmpty())
	{
		auto parsed = knx::IndividualAddress::parse(interfaceAddress.toStdString());
		if (!parsed)
		{
			return fail("interfaceAddress", parsed.error().message);
		}
		scenario.interfaceAddress = *parsed;
	}

	const QJsonArray tunnels = root.value(u"tunnelAddresses"_s).toArray();
	for (qsizetype i = 0; i < tunnels.size(); ++i)
	{
		auto parsed = knx::IndividualAddress::parse(tunnels[i].toString().toStdString());
		if (!parsed)
		{
			return fail("tunnelAddresses[" + std::to_string(i) + "]", parsed.error().message);
		}
		scenario.tunnelAddresses.push_back(*parsed);
	}

	KESTREL_TRY(const QString friendlyName, optionalString(root, u"friendlyName"_s, "scenario"));
	if (!friendlyName.isEmpty())
	{
		scenario.friendlyName = friendlyName.toStdString();
	}

	KESTREL_TRY(const QString serial, optionalString(root, u"serial"_s, "scenario"));
	if (!serial.isEmpty())
	{
		auto parsed = knx::SerialNumber::parse(serial.toStdString());
		if (!parsed)
		{
			return fail("serial", parsed.error().message);
		}
		scenario.serial = *parsed;
	}

	std::set<std::string> names;
	const QJsonArray devices = root.value(u"devices"_s).toArray();
	for (qsizetype i = 0; i < devices.size(); ++i)
	{
		const std::string path = "devices[" + std::to_string(i) + "]";
		KESTREL_TRY(auto device, parseDevice(devices[i], path));
		if (!names.insert(device.config.name).second)
		{
			return fail(path + ".name", "duplicate device name '" + device.config.name + "'");
		}
		scenario.devices.push_back(std::move(device));
	}
	return scenario;
}

Result<Scenario> ScenarioLoader::fromFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		return makeError(ErrorCode::IoError, "cannot read " + path.toStdString() + ": " + file.errorString().toStdString());
	}
	return fromJson(file.readAll());
}

} // namespace kestrel::simnet
