#include "SimController.hpp"

#include "kestrel/base/Bytes.hpp"
#include "kestrel/knx/Apdu.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <cstdio>
#include <sstream>
#include <stdexcept>

namespace kestrel::simapp
{

using namespace Qt::StringLiterals;

namespace
{

/// Thrown inside execute() only, turned into {"ok":false,...}: keeps the command handlers linear.
struct CommandError : std::runtime_error
{
	using std::runtime_error::runtime_error;
};

template <class T>
QString streamed(const T& value)
{
	std::ostringstream os;
	os << value;
	return QString::fromStdString(os.str());
}

QString hex(ByteView bytes)
{
	return QString::fromStdString(toHex(bytes));
}

QString requireString(const QJsonObject& request, const QString& key)
{
	const QJsonValue value = request.value(key);
	if (!value.isString() || value.toString().isEmpty())
	{
		throw CommandError("missing string field '" + key.toStdString() + "'");
	}
	return value.toString();
}

std::uint16_t requireAddress16(const QJsonObject& request, const QString& key)
{
	const QJsonValue value = request.value(key);
	if (value.isDouble())
	{
		const double number = value.toDouble();
		if (number >= 0 && number <= 0xFFFF)
		{
			return static_cast<std::uint16_t>(number);
		}
	}
	else if (value.isString())
	{
		const QString text = value.toString();
		bool ok = false;
		const uint number = text.startsWith(u"0x"_s, Qt::CaseInsensitive) ? text.mid(2).toUInt(&ok, 16) : text.toUInt(&ok);
		if (ok && number <= 0xFFFF)
		{
			return static_cast<std::uint16_t>(number);
		}
	}
	throw CommandError("field '" + key.toStdString() + "' must be a 16-bit number (or \"0x…\")");
}

Bytes requireHex(const QJsonObject& request, const QString& key)
{
	auto bytes = fromHex(requireString(request, key).toStdString());
	if (!bytes)
	{
		throw CommandError("field '" + key.toStdString() + "': " + bytes.error().message);
	}
	return *bytes;
}

} // namespace

SimController::SimController(simnet::Simulation& simulation, QObject* parent)
	: QObject(parent), m_simulation(simulation)
{
}

void SimController::writeLine(const QJsonObject& object)
{
	const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact);
	std::fwrite(line.constData(), 1, static_cast<std::size_t>(line.size()), stdout);
	std::fputc('\n', stdout);
	std::fflush(stdout);
}

QJsonObject SimController::toJson(const sim::Telegram& telegram)
{
	const knx::cemi::LData& frame = telegram.frame;
	QJsonObject object{
		{u"event"_s, u"telegram"_s},
		{u"index"_s, static_cast<qint64>(telegram.index)},
		{u"source"_s, QString::fromStdString(frame.sourceAddress().toString())},
		{u"destination"_s, QString::fromStdString(frame.groupDestination ? frame.groupAddress().toString()
		                                                                  : frame.individualDestination().toString())},
		{u"group"_s, frame.groupDestination},
		{u"priority"_s, streamed(frame.priority)},
		{u"cemi"_s, hex(knx::cemi::encode(frame))},
	};
	auto tpdu = knx::decodeTpdu(frame);
	if (tpdu)
	{
		object.insert(u"tpdu"_s, streamed(tpdu->type));
		if (tpdu->isData())
		{
			const knx::Apci apci = tpdu->apdu.apci;
			object.insert(u"apci"_s, streamed(apci));
			const bool groupValue = apci == knx::Apci::GroupValueWrite || apci == knx::Apci::GroupValueResponse;
			object.insert(u"data"_s, hex(groupValue ? tpdu->apdu.groupValue() : tpdu->apdu.data));
		}
		if (tpdu->type == knx::TpduType::DataConnected || tpdu->type == knx::TpduType::Ack
			|| tpdu->type == knx::TpduType::Nak)
		{
			object.insert(u"seq"_s, tpdu->seq);
		}
	}
	return object;
}

void SimController::handleLine(const QString& line)
{
	if (line.trimmed().isEmpty())
	{
		return;
	}
	const QJsonObject response = execute(line);
	writeLine(response);
	if (response.value(u"ok"_s).toBool() && response.value(u"cmd"_s).toString() == u"quit"_s)
	{
		Q_EMIT quitRequested();
	}
}

QJsonObject SimController::execute(const QString& line)
{
	QJsonParseError error{};
	const QJsonDocument document = QJsonDocument::fromJson(line.toUtf8(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject())
	{
		return {{u"ok"_s, false}, {u"error"_s, u"invalid JSON command: "_s + error.errorString()}};
	}
	const QJsonObject request = document.object();
	const QString cmd = request.value(u"cmd"_s).toString();
	QJsonObject response;
	try
	{
		if (cmd == u"press-prog-button"_s)
		{
			response = pressProgButton(request);
		}
		else if (cmd == u"device"_s)
		{
			response = deviceState(request);
		}
		else if (cmd == u"memory"_s)
		{
			response = memory(request);
		}
		else if (cmd == u"group-value"_s)
		{
			response = groupValue(request);
		}
		else if (cmd == u"telegrams"_s)
		{
			response = telegrams(request);
		}
		else if (cmd == u"status"_s)
		{
			response = status();
		}
		else if (cmd == u"quit"_s)
		{
			response = QJsonObject{};
		}
		else
		{
			throw CommandError("unknown command '" + cmd.toStdString() + "'");
		}
	}
	catch (const CommandError& e)
	{
		return {{u"ok"_s, false}, {u"cmd"_s, cmd}, {u"error"_s, QString::fromStdString(e.what())}};
	}
	response.insert(u"ok"_s, true);
	response.insert(u"cmd"_s, cmd);
	return response;
}

namespace
{

sim::VirtualDevice& requireDevice(simnet::Simulation& simulation, const QJsonObject& request)
{
	const QString name = requireString(request, u"device"_s);
	sim::VirtualDevice* device = simulation.device(name.toStdString());
	if (!device)
	{
		throw CommandError("unknown device '" + name.toStdString() + "'");
	}
	return *device;
}

} // namespace

QJsonObject SimController::pressProgButton(const QJsonObject& request)
{
	sim::VirtualDevice& device = requireDevice(m_simulation, request);
	device.pressProgButton();
	return {{u"device"_s, QString::fromStdString(device.name())}, {u"progMode"_s, device.progMode()}};
}

QJsonObject SimController::deviceState(const QJsonObject& request)
{
	sim::VirtualDevice& device = requireDevice(m_simulation, request);
	QJsonArray groupObjects;
	for (const sim::GroupObjectConfig& object : device.groupObjects())
	{
		groupObjects.append(QJsonObject{
			{u"ga"_s, QString::fromStdString(object.address.toString())},
			{u"flags"_s, QString::fromStdString(object.flags.toString())},
			{u"dpt"_s, object.dpt.main != 0 ? QString::fromStdString(object.dpt.toString()) : QString()},
			{u"value"_s, hex(object.value)},
		});
	}
	return {
		{u"device"_s, QString::fromStdString(device.name())},
		{u"address"_s, QString::fromStdString(device.address().toString())},
		{u"progMode"_s, device.progMode()},
		{u"serial"_s, QString::fromStdString(device.serial().toString())},
		{u"maskVersion"_s, u"%1"_s.arg(device.maskVersion(), 4, 16, QLatin1Char('0'))},
		{u"restartCount"_s, static_cast<qint64>(device.restartCount())},
		{u"groupObjects"_s, groupObjects},
	};
}

QJsonObject SimController::memory(const QJsonObject& request)
{
	sim::VirtualDevice& device = requireDevice(m_simulation, request);
	const std::uint16_t address = requireAddress16(request, u"address"_s);
	if (request.contains(u"data"_s))
	{
		const Bytes data = requireHex(request, u"data"_s);
		device.writeMemory(address, data);
		return {{u"device"_s, QString::fromStdString(device.name())}, {u"address"_s, address},
			{u"length"_s, static_cast<qint64>(data.size())}};
	}
	const int length = request.value(u"length"_s).toInt(1);
	if (length < 1 || length > 0x10000 - address)
	{
		throw CommandError("field 'length' out of range");
	}
	return {{u"device"_s, QString::fromStdString(device.name())}, {u"address"_s, address},
		{u"data"_s, hex(device.readMemory(address, static_cast<std::size_t>(length)))}};
}

QJsonObject SimController::groupValue(const QJsonObject& request)
{
	sim::VirtualDevice& device = requireDevice(m_simulation, request);
	const QString gaText = requireString(request, u"ga"_s);
	auto ga = knx::GroupAddress::parse(gaText.toStdString());
	if (!ga)
	{
		throw CommandError("field 'ga': " + ga.error().message);
	}
	if (request.contains(u"value"_s))
	{
		const Bytes value = requireHex(request, u"value"_s);
		const Status status = device.setGroupValue(*ga, value);
		if (!status)
		{
			throw CommandError(status.error().message);
		}
	}
	const auto value = device.groupValue(*ga);
	if (!value)
	{
		throw CommandError("device '" + device.name() + "' has no group object on " + gaText.toStdString());
	}
	return {{u"device"_s, QString::fromStdString(device.name())}, {u"ga"_s, QString::fromStdString(ga->toString())},
		{u"value"_s, hex(*value)}};
}

QJsonObject SimController::telegrams(const QJsonObject& request)
{
	const qint64 since = request.value(u"since"_s).toInteger(0);
	QJsonArray list;
	qint64 next = since;
	for (const sim::Telegram& telegram : m_simulation.bus().telegrams())
	{
		if (static_cast<qint64>(telegram.index) >= since)
		{
			QJsonObject entry = toJson(telegram);
			entry.remove(u"event"_s);
			list.append(entry);
			next = static_cast<qint64>(telegram.index) + 1;
		}
	}
	return {{u"telegrams"_s, list}, {u"next"_s, next}};
}

QJsonObject SimController::status()
{
	return {
		{u"port"_s, m_simulation.port()},
		{u"clients"_s, static_cast<qint64>(m_simulation.server().connectionCount())},
		{u"telegramCount"_s, static_cast<qint64>(m_simulation.bus().telegrams().size())},
		{u"devices"_s, static_cast<qint64>(m_simulation.devices().size())},
	};
}

} // namespace kestrel::simapp
