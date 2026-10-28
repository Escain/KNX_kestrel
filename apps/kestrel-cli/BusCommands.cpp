// Bus commands: each invocation runs an event loop around one app::BusSession (bus thread inside).

#include "Cli.hpp"

#include "kestrel/app/BusSession.hpp"
#include "kestrel/base/Bytes.hpp"
#include "kestrel/appconfig/DownloadPlan.hpp"
#include "kestrel/appconfig/Import.hpp"
#include "kestrel/catalog/Knxprod.hpp"
#include "kestrel/model/Operations.hpp"
#include "kestrel/persist/Persist.hpp"
#include "kestrel/download/Resolver.hpp"
#include "kestrel/dpt/Dpt.hpp"
#include "kestrel/knx/Address.hpp"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>

#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <sstream>

namespace kestrel::cli
{

using namespace Qt::StringLiterals;

namespace
{

const QCommandLineOption kInterfaceOption(u"interface"_s, u"KNXnet/IP interface host[:port] (port 3671 by default)."_s,
	u"host:port"_s);

struct Endpoint
{
	QString host;
	quint16 port = 3671;
};

std::optional<Endpoint> parseEndpoint(const QString& text)
{
	Endpoint endpoint;
	const qsizetype colon = text.lastIndexOf(u':');
	endpoint.host = colon < 0 ? text : text.left(colon);
	if (colon >= 0)
	{
		bool ok = false;
		const uint port = text.mid(colon + 1).toUInt(&ok);
		if (!ok || port == 0 || port > 0xFFFF)
		{
			return std::nullopt;
		}
		endpoint.port = static_cast<quint16>(port);
	}
	if (endpoint.host.isEmpty())
	{
		return std::nullopt;
	}
	return endpoint;
}

/// Seconds option → milliseconds; nullopt when malformed or negative.
std::optional<std::chrono::milliseconds> parseSeconds(const QString& text)
{
	bool ok = false;
	const double seconds = text.toDouble(&ok);
	if (!ok || seconds < 0 || !std::isfinite(seconds))
	{
		return std::nullopt;
	}
	return std::chrono::milliseconds(static_cast<std::int64_t>(std::llround(seconds * 1000.0)));
}

QString qs(const std::string& text)
{
	return QString::fromStdString(text);
}

QJsonValue toJsonValue(const dpt::DptValue& value)
{
	return std::visit(
		[](const auto& v) -> QJsonValue
		{
			using T = std::decay_t<decltype(v)>;
			if constexpr (std::is_same_v<T, bool>)
			{
				return v;
			}
			else if constexpr (std::is_same_v<T, std::int64_t>)
			{
				return static_cast<qint64>(v);
			}
			else if constexpr (std::is_same_v<T, std::uint64_t>)
			{
				return static_cast<qint64>(v);
			}
			else if constexpr (std::is_same_v<T, double>)
			{
				return v;
			}
			else
			{
				return QString::fromStdString(v);
			}
		},
		value);
}

QString hex(const QByteArray& bytes)
{
	return QString::fromLatin1(bytes.toHex(' '));
}

/// Runs `body` with a connected session; `body` calls finish(exitCode) exactly once.
int withSession(Invocation& inv, const std::function<void(app::BusSession&, const std::function<void(int)>&)>& body,
	std::optional<std::chrono::milliseconds> overallTimeout = std::nullopt)
{
	if (!inv.isSet(kInterfaceOption.names().first()))
	{
		return inv.out.failure(kExitUsage, u"--interface host:port is required"_s, u"Usage"_s);
	}
	const auto endpoint = parseEndpoint(inv.value(kInterfaceOption.names().first()));
	if (!endpoint)
	{
		return inv.out.failure(kExitUsage, u"invalid --interface (expected host[:port])"_s, u"Usage"_s);
	}

	QEventLoop loop;
	int exitCode = kExitOk;
	bool finished = false;
	const auto finish = [&](int code)
	{
		if (!finished)
		{
			finished = true;
			exitCode = code;
			loop.quit();
		}
	};

	app::BusSession session;
	QObject::connect(&session, &app::BusSession::connectionLost, &loop,
		[&](const QString& reason)
		{
			if (session.state() == knx::LinkState::Disconnected && !finished)
			{
				finish(inv.out.failure(kExitBus, u"connection lost: "_s + reason, u"IoError"_s));
			}
		});
	session.connectToInterface(endpoint->host, endpoint->port,
		[&](Status status)
		{
			if (!status)
			{
				finish(inv.out.failure(kExitBus, status.error()));
				return;
			}
			inv.out.note(u"connected to %1:%2 as %3"_s.arg(endpoint->host)
					.arg(endpoint->port)
					.arg(qs(session.localAddress().toString())));
			body(session, finish);
		});
	QTimer timeout;
	if (overallTimeout && overallTimeout->count() > 0)
	{
		timeout.setSingleShot(true);
		QObject::connect(&timeout, &QTimer::timeout, &loop,
			[&] { finish(inv.out.failure(kExitBus, u"timed out"_s, u"Timeout"_s)); });
		timeout.start(*overallTimeout);
	}
	if (!finished)
	{
		loop.exec();
	}
	session.disconnectFromInterface();
	return exitCode;
}

struct GroupTarget
{
	knx::GroupAddress address;
	dpt::DptId dpt;
};

std::optional<GroupTarget> parseGroupTarget(Invocation& inv, int& exitCode)
{
	auto ga = knx::GroupAddress::parse(inv.args[0].toStdString());
	if (!ga)
	{
		exitCode = inv.out.failure(kExitUsage, u"invalid group address '%1'"_s.arg(inv.args[0]), u"Usage"_s);
		return std::nullopt;
	}
	auto id = dpt::DptId::parse(inv.args[1].toStdString());
	if (!id || dpt::sizeInBit(*id) == 0)
	{
		exitCode = inv.out.failure(kExitUsage, u"unknown datapoint type '%1'"_s.arg(inv.args[1]), u"Usage"_s);
		return std::nullopt;
	}
	return GroupTarget{*ga, *id};
}

// --- bus discover ------------------------------------------------------------------------------------------

int busDiscover(Invocation& inv)
{
	app::BusSession::DiscoveryOptions options;
	const auto timeout = parseSeconds(inv.value(u"timeout"_s));
	if (!timeout)
	{
		return inv.out.failure(kExitUsage, u"invalid --timeout"_s, u"Usage"_s);
	}
	options.timeout = *timeout;
	options.unicastHost = inv.value(u"host"_s);
	bool portOk = false;
	const uint port = inv.value(u"port"_s).toUInt(&portOk);
	if (!portOk || port == 0 || port > 0xFFFF)
	{
		return inv.out.failure(kExitUsage, u"invalid --port"_s, u"Usage"_s);
	}
	options.unicastPort = static_cast<quint16>(port);
	// With an explicit host only that host is asked, unless --multicast is given as well.
	options.multicast = options.unicastHost.isEmpty() || inv.isSet(u"multicast"_s);

	QEventLoop loop;
	int exitCode = kExitOk;
	app::BusSession session;
	session.discover(options,
		[&](Result<QList<bus::DiscoveredInterface>> result)
		{
			if (!result)
			{
				exitCode = inv.out.failure(kExitBus, result.error());
				loop.quit();
				return;
			}
			QJsonArray list;
			QString text;
			for (const bus::DiscoveredInterface& found : *result)
			{
				const QString endpoint = u"%1:%2"_s.arg(found.host.toString()).arg(found.port);
				list.append(QJsonObject{
					{u"host"_s, found.host.toString()},
					{u"port"_s, found.port},
					{u"name"_s, found.friendlyName()},
					{u"address"_s, qs(found.info.address.toString())},
					{u"serial"_s, qs(found.info.serial.toString())},
					{u"programmingMode"_s, found.info.programmingMode},
					{u"tunnelling"_s, found.supportsTunnelling()},
					{u"routing"_s, found.supportsRouting()},
				});
				text += u"%1  %2  %3  %4%5\n"_s.arg(endpoint, qs(found.info.address.toString()), found.friendlyName(),
					found.supportsTunnelling() ? u"tunnelling"_s : u""_s,
					found.supportsRouting() ? u" routing"_s : u""_s);
			}
			if (result->isEmpty())
			{
				text = u"no KNXnet/IP interface found"_s;
			}
			exitCode = inv.out.success({{u"interfaces"_s, list}}, text.trimmed());
			loop.quit();
		});
	loop.exec();
	return exitCode;
}

// --- group write / read ------------------------------------------------------------------------------------

int groupWrite(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto target = parseGroupTarget(inv, exitCode);
	if (!target)
	{
		return exitCode;
	}
	auto value = dpt::parse(target->dpt, inv.args[2].toStdString());
	if (!value)
	{
		return inv.out.failure(kExitUsage, u"invalid value '%1' for DPT %2: %3"_s.arg(inv.args[2],
											   qs(target->dpt.toString()), qs(value.error().message)),
			u"Usage"_s);
	}
	auto encoded = dpt::encode(target->dpt, *value);
	if (!encoded)
	{
		return inv.out.failure(kExitUsage, encoded.error());
	}
	const QString formatted = qs(dpt::format(target->dpt, *value));
	return withSession(inv,
		[&](app::BusSession& session, const std::function<void(int)>& finish)
		{
			session.groupWrite(target->address, target->dpt, *value,
				[&, finish](Status status)
				{
					if (!status)
					{
						finish(inv.out.failure(busExitCode(status.error()), status.error()));
						return;
					}
					finish(inv.out.success(
						{
							{u"ga"_s, qs(target->address.toString())},
							{u"dpt"_s, qs(target->dpt.toString())},
							{u"value"_s, toJsonValue(*value)},
							{u"text"_s, formatted},
							{u"raw"_s, qs(toHex(*encoded))},
						},
						u"%1 <- %2"_s.arg(qs(target->address.toString()), formatted)));
				});
		});
}

int groupRead(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto target = parseGroupTarget(inv, exitCode);
	if (!target)
	{
		return exitCode;
	}
	const auto timeout = parseSeconds(inv.value(u"timeout"_s));
	if (!timeout)
	{
		return inv.out.failure(kExitUsage, u"invalid --timeout"_s, u"Usage"_s);
	}
	return withSession(inv,
		[&](app::BusSession& session, const std::function<void(int)>& finish)
		{
			session.groupRead(
				target->address, target->dpt,
				[&, finish](Result<app::GroupReadResult> result)
				{
					if (!result)
					{
						finish(inv.out.failure(busExitCode(result.error()), result.error()));
						return;
					}
					finish(inv.out.success(
						{
							{u"ga"_s, qs(target->address.toString())},
							{u"dpt"_s, qs(target->dpt.toString())},
							{u"source"_s, qs(result->responder.toString())},
							{u"value"_s, toJsonValue(result->value)},
							{u"text"_s, result->text},
							{u"raw"_s, hex(result->raw)},
						},
						u"%1 = %2 (from %3)"_s.arg(qs(target->address.toString()), result->text,
							qs(result->responder.toString()))));
				},
				*timeout);
		});
}

// --- monitor -----------------------------------------------------------------------------------------------

QJsonObject toJson(const app::TelegramRecord& record)
{
	QJsonObject object{
		{u"event"_s, u"telegram"_s},
		{u"time"_s, record.timestamp.toString(Qt::ISODateWithMs)},
		{u"source"_s, qs(record.source.toString())},
		{u"destination"_s, record.destinationText()},
		{u"group"_s, record.isGroup},
		{u"service"_s, record.serviceText()},
		{u"data"_s, hex(record.payload)},
	};
	if (!record.decoded.isEmpty())
	{
		object.insert(u"value"_s, record.decoded);
	}
	if (record.outgoing)
	{
		object.insert(u"outgoing"_s, true);
	}
	return object;
}

QString toText(const app::TelegramRecord& record)
{
	QString text = u"%1  %2 -> %3  %4  %5"_s.arg(record.timestamp.toLocalTime().toString(u"HH:mm:ss.zzz"_s),
		qs(record.source.toString()), record.destinationText(), record.serviceText(), hex(record.payload));
	if (!record.decoded.isEmpty())
	{
		text += u"  ("_s + record.decoded + u')';
	}
	return text;
}

int monitor(Invocation& inv)
{
	bool countOk = false;
	const int count = inv.value(u"count"_s).toInt(&countOk);
	if (!countOk || count < 0)
	{
		return inv.out.failure(kExitUsage, u"invalid --count"_s, u"Usage"_s);
	}
	const auto timeout = parseSeconds(inv.value(u"timeout"_s));
	if (!timeout)
	{
		return inv.out.failure(kExitUsage, u"invalid --timeout"_s, u"Usage"_s);
	}
	std::map<std::uint16_t, dpt::DptId> dpts;
	for (const QString& entry : inv.parser.values(u"dpt"_s))
	{
		const auto parts = entry.split(u'=');
		auto ga = parts.size() == 2 ? knx::GroupAddress::parse(parts[0].toStdString())
		                            : Result<knx::GroupAddress>(makeError(ErrorCode::InvalidArgument, ""));
		auto id = parts.size() == 2 ? dpt::DptId::parse(parts[1].toStdString())
		                            : Result<dpt::DptId>(makeError(ErrorCode::InvalidArgument, ""));
		if (!ga || !id)
		{
			return inv.out.failure(kExitUsage, u"invalid --dpt '%1' (expected ga=dpt, e.g. 1/1/2=9.001)"_s.arg(entry),
				u"Usage"_s);
		}
		dpts[ga->raw()] = *id;
	}

	int received = 0;
	return withSession(
		inv,
		[&](app::BusSession& session, const std::function<void(int)>& finish)
		{
			for (const auto& [raw, id] : dpts)
			{
				session.setGroupAddressDpt(knx::GroupAddress(raw), id);
			}
			inv.out.stream({{u"event"_s, u"connected"_s}, {u"address"_s, qs(session.localAddress().toString())}},
				u"monitoring (Ctrl+C to stop)"_s);
			QObject::connect(&session, &app::BusSession::telegramsAvailable, &session,
				[&, finish]
				{
					for (const app::TelegramRecord& record : session.takeTelegrams())
					{
						if (count > 0 && received >= count)
						{
							break;
						}
						inv.out.stream(toJson(record), toText(record));
						++received;
					}
					if (count > 0 && received >= count)
					{
						finish(kExitOk);
					}
				});
		},
		*timeout);
}

// --- device ------------------------------------------------------------------------------------------------

std::optional<knx::IndividualAddress> parseIa(Invocation& inv, const QString& text, int& exitCode)
{
	auto ia = knx::IndividualAddress::parse(text.toStdString());
	if (!ia)
	{
		exitCode = inv.out.failure(kExitUsage, u"invalid individual address '%1'"_s.arg(text), u"Usage"_s);
		return std::nullopt;
	}
	return *ia;
}

QString stateText(knx::IaProgrammerState state)
{
	std::ostringstream os;
	os << state;
	return qs(os.str());
}

int programIa(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto target = parseIa(inv, inv.args[0], exitCode);
	if (!target)
	{
		return exitCode;
	}
	const auto timeout = parseSeconds(inv.value(u"timeout"_s));
	if (!timeout)
	{
		return inv.out.failure(kExitUsage, u"invalid --timeout"_s, u"Usage"_s);
	}
	app::BusSession::ProgramOptions options;
	options.timeout = *timeout;
	options.allowOverwrite = inv.isSet(u"overwrite"_s);
	return withSession(inv,
		[&](app::BusSession& session, const std::function<void(int)>& finish)
		{
			inv.out.note(u"press the programming button of the device..."_s);
			session.programIndividualAddress(
				*target, options,
				[&, finish](Result<knx::IndividualAddress> result)
				{
					if (!result)
					{
						finish(inv.out.failure(busExitCode(result.error()), result.error()));
						return;
					}
					finish(inv.out.success(
						{{u"address"_s, qs(target->toString())}, {u"previousAddress"_s, qs(result->toString())}},
						u"programmed %1 (was %2)"_s.arg(qs(target->toString()), qs(result->toString()))));
				},
				[&](knx::IaProgrammerState state) { inv.out.note(u"  "_s + stateText(state)); });
		});
}

int deviceInfo(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto device = parseIa(inv, inv.args[0], exitCode);
	if (!device)
	{
		return exitCode;
	}
	return withSession(inv,
		[&](app::BusSession& session, const std::function<void(int)>& finish)
		{
			session.readDeviceInfo(*device,
				[&, finish](Result<app::DeviceInfo> result)
				{
					if (!result)
					{
						finish(inv.out.failure(busExitCode(result.error()), result.error()));
						return;
					}
					const QString mask = u"%1"_s.arg(result->maskVersion, 4, 16, QLatin1Char('0'));
					const QString manufacturer = u"M-%1"_s.arg(result->manufacturerId, 4, 16, QLatin1Char('0')).toUpper();
					finish(inv.out.success(
						{
							{u"address"_s, qs(result->address.toString())},
							{u"maskVersion"_s, mask},
							{u"serial"_s, qs(result->serial.toString())},
							{u"manufacturerId"_s, result->manufacturerId},
							{u"manufacturer"_s, manufacturer},
						},
						u"address       %1\nmask version  %2\nserial        %3\nmanufacturer  %4"_s.arg(
							qs(result->address.toString()), mask, qs(result->serial.toString()), manufacturer)));
				});
		});
}

int deviceProgMode(Invocation& inv)
{
	const auto window = parseSeconds(inv.value(u"timeout"_s));
	if (!window)
	{
		return inv.out.failure(kExitUsage, u"invalid --timeout"_s, u"Usage"_s);
	}
	return withSession(inv,
		[&](app::BusSession& session, const std::function<void(int)>& finish)
		{
			session.readProgrammingMode(
				[&, finish](Result<QList<knx::IndividualAddress>> result)
				{
					if (!result)
					{
						finish(inv.out.failure(busExitCode(result.error()), result.error()));
						return;
					}
					QJsonArray devices;
					QStringList lines;
					for (const knx::IndividualAddress& address : std::as_const(*result))
					{
						devices.append(qs(address.toString()));
						lines.append(qs(address.toString()));
					}
					finish(inv.out.success({{u"devices"_s, devices}},
						lines.isEmpty() ? u"no device in programming mode"_s : lines.join(u'\n')));
				},
				*window);
		});
}

/// Unsigned number in decimal or 0x-hex, at most `max`.
std::optional<uint> parseNumber(const QString& text, uint max)
{
	bool ok = false;
	const uint value = text.toUInt(&ok, 0);
	if (!ok || value > max)
	{
		return std::nullopt;
	}
	return value;
}

QString objectTypeName(uint type)
{
	switch (type)
	{
		case 0: return u"device"_s;
		case 1: return u"address table"_s;
		case 2: return u"association table"_s;
		case 3: return u"application program"_s;
		case 4: return u"interface program"_s;
		case 5: return u"object association table"_s;
		case 6: return u"router"_s;
		case 7: return u"LTE address routing table"_s;
		case 8: return u"cEMI server"_s;
		case 9: return u"group object table"_s;
		case 10: return u"polling master"_s;
		case 11: return u"KNXnet/IP parameter"_s;
		case 13: return u"file server"_s;
		case 17: return u"security"_s;
		case 19: return u"RF medium"_s;
		default: return u"type %1"_s.arg(type);
	}
}

QString loadStateName(uint state)
{
	switch (state)
	{
		case 0: return u"unloaded"_s;
		case 1: return u"loaded"_s;
		case 2: return u"loading"_s;
		case 3: return u"error"_s;
		case 4: return u"unloading"_s;
		case 5: return u"load completing"_s;
		default: return u"state %1"_s.arg(state);
	}
}

uint beUInt(const QByteArray& bytes)
{
	uint value = 0;
	for (const char byte : bytes)
	{
		value = (value << 8) | static_cast<uint>(static_cast<std::uint8_t>(byte));
	}
	return value;
}

int deviceProperty(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto device = parseIa(inv, inv.args[0], exitCode);
	if (!device)
	{
		return exitCode;
	}
	const auto object = parseNumber(inv.args[1], 255);
	const auto pid = parseNumber(inv.args[2], 255);
	const auto count = parseNumber(inv.value(u"count"_s), 15);
	const auto start = parseNumber(inv.value(u"start"_s), 0x0FFF);
	if (!object || !pid || !count || *count == 0 || !start)
	{
		return inv.out.failure(kExitUsage, u"invalid object index, property id, --count (1..15) or --start"_s,
			u"Usage"_s);
	}
	return withSession(inv,
		[&](app::BusSession& session, const std::function<void(int)>& finish)
		{
			session.readManagement(*device,
				{app::ManagementRead::property(static_cast<std::uint8_t>(*object), static_cast<std::uint8_t>(*pid),
					static_cast<std::uint8_t>(*count), static_cast<std::uint16_t>(*start))},
				[&, finish](Result<QList<Result<QByteArray>>> results)
				{
					if (!results)
					{
						finish(inv.out.failure(busExitCode(results.error()), results.error()));
						return;
					}
					const Result<QByteArray>& data = results->front();
					if (!data)
					{
						finish(inv.out.failure(busExitCode(data.error()), data.error()));
						return;
					}
					finish(inv.out.success({{u"object"_s, int(*object)}, {u"pid"_s, int(*pid)}, {u"data"_s, hex(*data)}},
						hex(*data)));
				});
		});
}

int deviceMemory(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto device = parseIa(inv, inv.args[0], exitCode);
	if (!device)
	{
		return exitCode;
	}
	const auto address = parseNumber(inv.args[1], 0xFFFF);
	const auto length = parseNumber(inv.args[2], 0x1000);
	if (!address || !length || *length == 0 || *address + *length > 0x10000)
	{
		return inv.out.failure(kExitUsage, u"invalid address or length (16-bit range, length 1..4096)"_s, u"Usage"_s);
	}
	return withSession(inv,
		[&](app::BusSession& session, const std::function<void(int)>& finish)
		{
			session.readManagement(*device,
				{app::ManagementRead::memory(static_cast<std::uint16_t>(*address), static_cast<std::uint16_t>(*length))},
				[&, finish](Result<QList<Result<QByteArray>>> results)
				{
					if (!results)
					{
						finish(inv.out.failure(busExitCode(results.error()), results.error()));
						return;
					}
					const Result<QByteArray>& data = results->front();
					if (!data)
					{
						finish(inv.out.failure(busExitCode(data.error()), data.error()));
						return;
					}
					QStringList lines;
					for (qsizetype offset = 0; offset < data->size(); offset += 16)
					{
						lines.append(u"%1  %2"_s.arg(*address + uint(offset), 4, 16, QLatin1Char('0'))
								.arg(hex(data->mid(offset, 16))));
					}
					finish(inv.out.success(
						{{u"address"_s, int(*address)}, {u"length"_s, int(*length)}, {u"data"_s, hex(*data)}},
						lines.join(u'\n')));
				});
		});
}

int deviceObjects(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto device = parseIa(inv, inv.args[0], exitCode);
	if (!device)
	{
		return exitCode;
	}
	return withSession(inv,
		[&](app::BusSession& session, const std::function<void(int)>& finish)
		{
			// One connection per object index; stop at the first index without PID_OBJECT_TYPE.
			auto objects = std::make_shared<QJsonArray>();
			auto lines = std::make_shared<QStringList>();
			auto next = std::make_shared<std::function<void(int)>>();
			*next = [&, objects, lines, next, finish](int index)
			{
				const auto i = static_cast<std::uint8_t>(index);
				session.readManagement(*device,
					{
						app::ManagementRead::property(i, 1),  // PID_OBJECT_TYPE
						app::ManagementRead::property(i, 5),  // PID_LOAD_STATE_CONTROL
						app::ManagementRead::property(i, 7),  // PID_TABLE_REFERENCE
						app::ManagementRead::property(i, 13), // PID_PROGRAM_VERSION
					},
					[&, objects, lines, next, finish, index](Result<QList<Result<QByteArray>>> results)
					{
						if (!results)
						{
							finish(inv.out.failure(busExitCode(results.error()), results.error()));
							return;
						}
						const auto& r = *results;
						if (!r[0] || index >= 32)
						{
							finish(inv.out.success({{u"address"_s, qs(device->toString())}, {u"objects"_s, *objects}},
								lines->isEmpty() ? u"no interface objects readable"_s : lines->join(u'\n')));
							next->operator=(nullptr); // break the self-reference
							return;
						}
						const uint type = beUInt(*r[0]);
						QJsonObject object{{u"index"_s, index}, {u"type"_s, int(type)},
							{u"typeName"_s, objectTypeName(type)}};
						QString line = u"%1  %2"_s.arg(index, 2).arg(objectTypeName(type), -26);
						if (r[1])
						{
							const uint state = r[1]->isEmpty() ? 0 : uint(static_cast<std::uint8_t>(r[1]->front()));
							object.insert(u"loadState"_s, loadStateName(state));
							line += u"  load: %1"_s.arg(loadStateName(state));
						}
						if (r[2])
						{
							object.insert(u"tableReference"_s, hex(*r[2]));
							line += u"  table: 0x%1"_s.arg(beUInt(*r[2]), 4, 16, QLatin1Char('0'));
						}
						if (r[3])
						{
							object.insert(u"programVersion"_s, hex(*r[3]));
							line += u"  version: %1"_s.arg(hex(*r[3]));
						}
						objects->append(object);
						lines->append(line);
						(*next)(index + 1);
					});
			};
			(*next)(0);
		});
}

/// Builds the download plan from the options; prints the reason and returns nullopt on failure.
std::optional<download::Plan> downloadPlan(Invocation& inv, int& exitCode)
{
	auto fail = [&](const QString& message, const QString& code, int exit)
	{
		exitCode = inv.out.failure(exit, message, code);
		return std::nullopt;
	};
	if (!inv.isSet(u"knxprod"_s) || !inv.isSet(u"images"_s))
	{
		return fail(u"--knxprod and --images are required"_s, u"Usage"_s, kExitUsage);
	}
	auto loaded = catalog::loadKnxprodFile(inv.value(u"knxprod"_s));
	if (!loaded)
	{
		return fail(qs(loaded.error().message), u"ParseError"_s, kExitFindings);
	}
	const catalog::ProductDatabase& database = loaded->database;
	const catalog::ApplicationProgram* program = nullptr;
	if (inv.isSet(u"application"_s))
	{
		program = database.findApplicationProgram(inv.value(u"application"_s));
	}
	else if (database.applicationPrograms.size() == 1)
	{
		program = &database.applicationPrograms.front();
	}
	if (!program)
	{
		return fail(u"choose the application program with --application"_s, u"Usage"_s, kExitUsage);
	}
	std::map<std::uint8_t, Bytes> images;
	// Images: files named obj<N>... in the --images directory (e.g. "obj4-application@2a18.bin").
	const QDir dir(inv.value(u"images"_s));
	const QStringList files = dir.entryList({u"obj*"_s}, QDir::Files, QDir::Name);
	for (const QString& name : files)
	{
		qsizetype digits = 3;
		while (digits < name.size() && name.at(digits).isDigit())
		{
			++digits;
		}
		bool ok = false;
		const uint object = name.mid(3, digits - 3).toUInt(&ok);
		if (!ok || object > 255)
		{
			continue;
		}
		QFile file(dir.filePath(name));
		if (!file.open(QIODevice::ReadOnly))
		{
			return fail(u"cannot read %1"_s.arg(file.fileName()), u"IoError"_s, kExitFindings);
		}
		const QByteArray content = file.readAll();
		images[static_cast<std::uint8_t>(object)] = Bytes(content.begin(), content.end());
	}
	if (images.empty())
	{
		return fail(u"no obj<N>* image files in %1"_s.arg(dir.path()), u"NotFound"_s, kExitFindings);
	}
	// PID_PROGRAM_VERSION: manufacturer, application number, application version.
	Bytes version;
	if (inv.isSet(u"program-version"_s))
	{
		auto parsed = fromHex(inv.value(u"program-version"_s).toStdString());
		if (!parsed || parsed->size() != 5)
		{
			return fail(u"--program-version needs 5 hex octets"_s, u"Usage"_s, kExitUsage);
		}
		version = *parsed;
	}
	else
	{
		version = appconfig::programVersion(database.manufacturerId, *program);
	}
	auto plan = appconfig::planDownload(database, *program, std::move(images), version, inv.value(u"procedure"_s));
	if (!plan)
	{
		return fail(qs(plan.error().message), QString::fromLatin1(toString(plan.error().code)), kExitFindings);
	}
	return std::move(*plan);
}

int deviceDownload(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto device = parseIa(inv, inv.args[0], exitCode);
	if (!device)
	{
		return exitCode;
	}
	auto plan = downloadPlan(inv, exitCode);
	if (!plan)
	{
		return exitCode;
	}
	QStringList lines;
	QJsonArray steps;
	for (std::size_t i = 0; i < plan->steps.size(); ++i)
	{
		std::ostringstream os;
		os << plan->steps[i];
		lines.append(u"%1  %2"_s.arg(i + 1, 2).arg(qs(os.str())));
		steps.append(qs(os.str()));
	}
	if (inv.isSet(u"dry-run"_s))
	{
		return inv.out.success({{u"steps"_s, steps}}, lines.join(u'\n'));
	}
	return withSession(inv,
		[&](app::BusSession& session, const std::function<void(int)>& finish)
		{
			session.download(*device, *plan,
				[&, finish](Status status)
				{
					if (!status)
					{
						finish(inv.out.failure(busExitCode(status.error()), status.error()));
						return;
					}
					finish(inv.out.success({{u"address"_s, qs(device->toString())}, {u"steps"_s, steps}},
						u"download to %1 completed (%2 steps)"_s.arg(qs(device->toString())).arg(steps.size())));
				},
				[&](std::size_t step, std::size_t total, const QString& text)
				{ inv.out.note(u"  [%1/%2] %3"_s.arg(step + 1).arg(total).arg(text)); });
		});
}

/// The one application program of the product file, or the one chosen with --application.
const catalog::ApplicationProgram* chooseProgram(Invocation& inv, const catalog::ProductDatabase& database,
	const QString& wanted = {})
{
	if (!wanted.isEmpty())
	{
		return database.findApplicationProgram(wanted);
	}
	if (inv.isSet(u"application"_s))
	{
		return database.findApplicationProgram(inv.value(u"application"_s));
	}
	return database.applicationPrograms.size() == 1 ? &database.applicationPrograms.front() : nullptr;
}

/// A product of `database` whose hardware lists `app`.
QString productFor(const catalog::ProductDatabase& database, const catalog::ApplicationProgram& app)
{
	for (const catalog::Hardware& hardware : database.hardware)
	{
		for (const catalog::Hardware2Program& h2p : hardware.hardware2Programs)
		{
			if (h2p.applicationProgramRefs.contains(app.id) && !hardware.products.empty())
			{
				return hardware.products.front().id;
			}
		}
	}
	return {};
}

std::uint32_t beValue(const QByteArray& bytes, qsizetype count)
{
	std::uint32_t value = 0;
	for (qsizetype i = 0; i < std::min(count, bytes.size()); ++i)
	{
		value = (value << 8) | static_cast<std::uint8_t>(bytes[i]);
	}
	return value;
}

int deviceImport(Invocation& inv)
{
	int exitCode = kExitOk;
	const auto address = parseIa(inv, inv.args[0], exitCode);
	if (!address)
	{
		return exitCode;
	}
	if (!inv.isSet(u"knxprod"_s) || !inv.isSet(u"project"_s))
	{
		return inv.out.failure(kExitUsage, u"--knxprod and --project are required"_s, u"Usage"_s);
	}
	auto loaded = catalog::loadKnxprodFile(inv.value(u"knxprod"_s));
	if (!loaded)
	{
		return inv.out.failure(kExitFindings, loaded.error());
	}
	const catalog::ProductDatabase& database = loaded->database;
	// Without --application, the device's program version (PID 13) selects the application below.
	const catalog::ApplicationProgram* app = chooseProgram(inv, database);
	if (inv.isSet(u"application"_s) && !app)
	{
		return inv.out.failure(kExitUsage, u"no application %1 in the product file"_s.arg(inv.value(u"application"_s)),
			u"Usage"_s);
	}
	const QString projectDir = inv.value(u"project"_s);
	auto project = persist::load(projectDir);
	if (!project)
	{
		return inv.out.failure(kExitIo, project.error());
	}

	std::map<std::uint8_t, Bytes> images;
	QStringList notes;
	const int busCode = withSession(inv,
		[&](app::BusSession& session, const std::function<void(int)>& finish)
		{
			// 1. Where and how large the segments are: PID_TABLE_REFERENCE and PID_MCB_TABLE of objects 1..4.
			QList<app::ManagementRead> reads;
			for (std::uint8_t object = 1; object <= 4; ++object)
			{
				reads.append(app::ManagementRead::property(object, 7));
				reads.append(app::ManagementRead::property(object, 27));
			}
			reads.append(app::ManagementRead::property(4, 13));
			session.readManagement(*address, reads,
				[&, finish](Result<QList<Result<QByteArray>>> results)
				{
					if (!results)
					{
						finish(inv.out.failure(busExitCode(results.error()), results.error()));
						return;
					}
					const auto& r = *results;
					if (!app && r[8] && r[8]->size() == 5)
					{
						// Same manufacturer and application number; the exact version if present, else the newest.
						const QByteArray version = *r[8];
						for (const catalog::ApplicationProgram& candidate : database.applicationPrograms)
						{
							const Bytes id = appconfig::programVersion(database.manufacturerId, candidate);
							if (static_cast<std::uint8_t>(version[0]) == id[0] && static_cast<std::uint8_t>(version[1]) == id[1]
								&& static_cast<std::uint8_t>(version[2]) == id[2] && static_cast<std::uint8_t>(version[3]) == id[3]
								&& (!app || id[4] == static_cast<std::uint8_t>(version[4])
									|| (app->applicationVersion != static_cast<std::uint8_t>(version[4])
										&& candidate.applicationVersion > app->applicationVersion)))
							{
								app = &candidate;
							}
						}
					}
					if (!app)
					{
						finish(inv.out.failure(kExitFindings,
							u"the device's application is not in this product file (use --application)"_s, u"NotFound"_s));
						return;
					}
					if (app->maskVersion != u"MV-07B0")
					{
						finish(inv.out.failure(kExitFindings, u"only System B (MV-07B0) devices can be imported yet"_s,
							u"Unsupported"_s));
						return;
					}
					if (r[8])
					{
						const QByteArray version = *r[8];
						const Bytes expected = appconfig::programVersion(database.manufacturerId, *app);
						if (version.size() == 5 && (static_cast<std::uint8_t>(version[2]) != expected[2]
								|| static_cast<std::uint8_t>(version[3]) != expected[3]))
						{
							finish(inv.out.failure(kExitFindings,
								u"the device runs application %1, not %2"_s.arg(QString::fromLatin1(version.toHex(' ')),
									app->id),
								u"Conflict"_s));
							return;
						}
						if (version.size() == 5 && static_cast<std::uint8_t>(version[4]) != expected[4])
						{
							notes.append(u"the device has application version %1, the product file %2"_s
									.arg(static_cast<std::uint8_t>(version[4]), 2, 16, QLatin1Char('0'))
									.arg(expected[4], 2, 16, QLatin1Char('0')));
						}
					}
					// Segment sizes: PID_MCB_TABLE when the load procedure wrote one; otherwise the table header
					// (u16 count) for objects 1..3 and the product file's segment size for the parameters.
					auto where = std::make_shared<std::array<std::uint32_t, 4>>();
					auto size = std::make_shared<std::array<std::uint32_t, 4>>();
					QList<app::ManagementRead> headers;
					QList<int> headerObjects;
					for (int object = 0; object < 4; ++object)
					{
						if (!r[2 * object])
						{
							finish(inv.out.failure(kExitBus,
								u"object %1: table reference not readable"_s.arg(object + 1), u"ProtocolError"_s));
							return;
						}
						(*where)[static_cast<std::size_t>(object)] = beValue(*r[2 * object], 4);
						(*size)[static_cast<std::size_t>(object)] = r[2 * object + 1] ? beValue(*r[2 * object + 1], 4) : 0;
						if ((*size)[static_cast<std::size_t>(object)] != 0)
						{
							continue;
						}
						if (object == 3)
						{
							for (const catalog::RelativeSegment& segment : app->relativeSegments)
							{
								if (segment.loadStateMachine == 4)
								{
									(*size)[3] = segment.size;
								}
							}
						}
						else
						{
							headers.append(app::ManagementRead::memory(
								static_cast<std::uint16_t>((*where)[static_cast<std::size_t>(object)]), 2));
							headerObjects.append(object);
						}
					}
					auto readSegments = [&, finish, where, size]()
					{
						QList<app::ManagementRead> memory;
						for (std::size_t object = 0; object < 4; ++object)
						{
							if ((*where)[object] + (*size)[object] > 0x10000 || (*size)[object] == 0)
							{
								finish(inv.out.failure(kExitBus, u"object %1: segment 0x%2 size %3 not readable"_s
																 .arg(object + 1)
																 .arg((*where)[object], 0, 16)
																 .arg((*size)[object]),
									u"Unsupported"_s));
								return;
							}
							memory.append(app::ManagementRead::memory(static_cast<std::uint16_t>((*where)[object]),
								static_cast<std::uint16_t>((*size)[object])));
						}
						// 2. The segments themselves.
						session.readManagement(*address, memory,
							[&, finish](Result<QList<Result<QByteArray>>> data)
							{
								if (!data)
								{
									finish(inv.out.failure(busExitCode(data.error()), data.error()));
									return;
								}
								for (int object = 0; object < 4; ++object)
								{
									if (!(*data)[object])
									{
										finish(inv.out.failure(busExitCode((*data)[object].error()), (*data)[object].error()));
										return;
									}
									const QByteArray& bytes = *(*data)[object];
									images[static_cast<std::uint8_t>(object + 1)] = Bytes(bytes.begin(), bytes.end());
								}
								finish(kExitOk);
							});
					};
					if (headers.isEmpty())
					{
						readSegments();
						return;
					}
					session.readManagement(*address, headers,
						[&, finish, size, headerObjects, readSegments](Result<QList<Result<QByteArray>>> counts)
						{
							if (!counts)
							{
								finish(inv.out.failure(busExitCode(counts.error()), counts.error()));
								return;
							}
							for (qsizetype i = 0; i < headerObjects.size(); ++i)
							{
								if (!(*counts)[i])
								{
									finish(inv.out.failure(busExitCode((*counts)[i].error()), (*counts)[i].error()));
									return;
								}
								const int object = headerObjects[i];
								const std::uint32_t entries = beValue(*(*counts)[i], 2);
								const std::uint32_t entrySize = object == 1 ? 4u : 2u; // association pairs: 4 octets
								(*size)[static_cast<std::size_t>(object)] = 2 + entries * entrySize;
							}
							readSegments();
						});
				});
		});
	if (busCode != kExitOk)
	{
		return busCode;
	}

	// 3. Into the project: area, line and device (created when missing), then the configuration.
	model::Project& p = *project;
	const unsigned areaNumber = address->area(), lineNumber = address->line(), deviceNumber = address->device();
	std::optional<model::Id<model::Device>> deviceId;
	const Status placed = p.edit(u"Import device %1"_s.arg(qs(address->toString())),
		[&](model::Tx&) -> Status
		{
			std::optional<model::Id<model::Area>> area;
			for (const model::Area& candidate : p.areas().rows())
			{
				if (candidate.number == areaNumber)
				{
					area = candidate.id;
				}
			}
			if (!area)
			{
				KESTREL_TRY(const auto created, model::addArea(p, areaNumber, u"Area %1"_s.arg(areaNumber)));
				area = created;
			}
			std::optional<model::Id<model::Line>> line;
			for (const model::Line& candidate : p.lines().rows())
			{
				if (candidate.area == *area && candidate.number == lineNumber)
				{
					line = candidate.id;
				}
			}
			if (!line)
			{
				KESTREL_TRY(const auto created,
					model::addLine(p, *area, lineNumber, u"Line %1.%2"_s.arg(areaNumber).arg(lineNumber)));
				line = created;
			}
			for (const model::Device& candidate : p.devices().rows())
			{
				if (candidate.line == *line && candidate.number == deviceNumber)
				{
					if (candidate.appProgramRef != app->id)
					{
						return makeError(ErrorCode::Conflict, "device " + address->toString()
								+ " exists in the project with another application");
					}
					deviceId = candidate.id;
				}
			}
			if (!deviceId)
			{
				const QString product = productFor(database, *app);
				const catalog::Product* info = database.findProduct(product);
				const QString name = inv.isSet(u"name"_s) ? inv.value(u"name"_s)
					: info ? database.text(info->id, u"Text"_s, {})
						   : app->name;
				KESTREL_TRY(const auto created, model::addDevice(p, *line, deviceNumber, name, product, app->id));
				deviceId = created;
			}
			return ok();
		});
	if (!placed)
	{
		return inv.out.failure(kExitFindings, placed.error());
	}
	auto warnings = appconfig::importSystemBImages(p, *deviceId, *app, images);
	if (!warnings)
	{
		return inv.out.failure(kExitFindings, warnings.error());
	}
	notes += *warnings;
	if (const int saved = [&]
		{
			const Status status = persist::save(p, projectDir);
			return status ? kExitOk : inv.out.failure(kExitIo, status.error());
		}();
		saved != kExitOk)
	{
		return saved;
	}
	const model::Device& device = p.devices().at(*deviceId);
	std::size_t objects = 0, links = 0;
	for (const model::ComObject& object : p.comObjects().rows())
	{
		if (object.device == *deviceId)
		{
			++objects;
			for (const model::Link& link : p.links().rows())
			{
				links += link.comObject == object.id ? 1u : 0u;
			}
		}
	}
	for (const QString& note : std::as_const(notes))
	{
		inv.out.note(u"note: "_s + note);
	}
	return inv.out.success(
		{{u"device"_s, device.id.toString()}, {u"address"_s, qs(address->toString())},
			{u"parameters"_s, static_cast<int>(device.params.size())}, {u"comObjects"_s, static_cast<int>(objects)},
			{u"links"_s, static_cast<int>(links)}},
		u"imported %1 into %2: %3 parameter values, %4 com objects, %5 links (device id %6)"_s.arg(
			qs(address->toString()), projectDir)
			.arg(device.params.size())
			.arg(objects)
			.arg(links)
			.arg(device.id.toString()));
}

int projectDownload(Invocation& inv)
{
	const QString projectDir = inv.args[0];
	auto project = persist::load(projectDir);
	if (!project)
	{
		return inv.out.failure(kExitIo, project.error());
	}
	const auto deviceId = model::Id<model::Device>::parse(inv.args[1]);
	const model::Device* device = deviceId ? project->devices().find(*deviceId) : nullptr;
	if (!device)
	{
		return inv.out.failure(kExitFindings, u"no device %1 in the project"_s.arg(inv.args[1]), u"NotFound"_s);
	}
	if (!device->number)
	{
		return inv.out.failure(kExitFindings, u"the device has no individual address yet"_s, u"InvalidArgument"_s);
	}
	const model::Line& line = project->lines().at(device->line);
	const model::Area& area = project->areas().at(line.area);
	const auto address = knx::IndividualAddress::fromParts(area.number, line.number, *device->number);
	if (!address)
	{
		return inv.out.failure(kExitFindings, address.error());
	}
	if (!inv.isSet(u"knxprod"_s))
	{
		return inv.out.failure(kExitUsage, u"--knxprod is required (the device's product file)"_s, u"Usage"_s);
	}
	auto loaded = catalog::loadKnxprodFile(inv.value(u"knxprod"_s));
	if (!loaded)
	{
		return inv.out.failure(kExitFindings, loaded.error());
	}
	const catalog::ApplicationProgram* app = loaded->database.findApplicationProgram(device->appProgramRef);
	if (!app)
	{
		return inv.out.failure(kExitFindings,
			u"the product file does not contain the device's application %1"_s.arg(device->appProgramRef), u"NotFound"_s);
	}
	auto planned = appconfig::planDeviceDownload(loaded->database, *app, *project, *deviceId, inv.value(u"procedure"_s));
	if (!planned)
	{
		return inv.out.failure(kExitFindings, planned.error());
	}
	for (const QString& warning : std::as_const(planned->warnings))
	{
		inv.out.note(u"warning: "_s + warning);
	}
	if (inv.isSet(u"save-images"_s))
	{
		const QDir dir(inv.value(u"save-images"_s));
		(void)QDir().mkpath(dir.path());
		for (const auto& [object, image] : planned->images)
		{
			QFile file(dir.filePath(u"obj%1.bin"_s.arg(object)));
			if (!file.open(QIODevice::WriteOnly)
				|| file.write(reinterpret_cast<const char*>(image.data()), static_cast<qint64>(image.size()))
					!= static_cast<qint64>(image.size()))
			{
				return inv.out.failure(kExitIo, u"cannot write %1"_s.arg(file.fileName()), u"IoError"_s);
			}
		}
	}
	QJsonArray steps;
	QStringList lines;
	for (std::size_t i = 0; i < planned->plan.steps.size(); ++i)
	{
		std::ostringstream os;
		os << planned->plan.steps[i];
		steps.append(qs(os.str()));
		lines.append(u"%1  %2"_s.arg(i + 1, 2).arg(qs(os.str())));
	}
	if (inv.isSet(u"dry-run"_s))
	{
		return inv.out.success({{u"address"_s, qs(address->toString())}, {u"steps"_s, steps}}, lines.join(u'\n'));
	}
	return withSession(inv,
		[&](app::BusSession& session, const std::function<void(int)>& finish)
		{
			session.download(*address, planned->plan,
				[&, finish](Status status)
				{
					if (!status)
					{
						finish(inv.out.failure(busExitCode(status.error()), status.error()));
						return;
					}
					finish(inv.out.success({{u"address"_s, qs(address->toString())}, {u"steps"_s, steps}},
						u"download to %1 completed (%2 steps)"_s.arg(qs(address->toString())).arg(steps.size())));
				},
				[&](std::size_t step, std::size_t total, const QString& text)
				{ inv.out.note(u"  [%1/%2] %3"_s.arg(step + 1).arg(total).arg(text)); });
		});
}

} // namespace

std::vector<Command> busCommands()
{
	const QCommandLineOption timeoutDiscover(u"timeout"_s, u"Seconds to collect answers (default 3)."_s, u"s"_s, u"3"_s);
	const QCommandLineOption host(u"host"_s, u"Ask this host by unicast (disables multicast unless --multicast)."_s,
		u"host"_s);
	const QCommandLineOption port(u"port"_s, u"Port of --host (default 3671)."_s, u"port"_s, u"3671"_s);
	const QCommandLineOption multicast(u"multicast"_s, u"Also search by multicast when --host is given."_s);
	const QCommandLineOption readTimeout(u"timeout"_s, u"Seconds to wait for the response (default 3)."_s, u"s"_s, u"3"_s);
	const QCommandLineOption count(u"count"_s, u"Stop after N telegrams (0 = never)."_s, u"n"_s, u"0"_s);
	const QCommandLineOption monitorTimeout(u"timeout"_s, u"Give up after s seconds (0 = never; exit 4)."_s, u"s"_s, u"0"_s);
	const QCommandLineOption monitorDpt(u"dpt"_s, u"Decode a group address with a DPT: ga=dpt (repeatable)."_s, u"ga=dpt"_s);
	const QCommandLineOption programTimeout(u"timeout"_s,
		u"Seconds to wait for a device in programming mode (default 60)."_s, u"s"_s, u"60"_s);
	const QCommandLineOption propCount(u"count"_s, u"Number of elements (default 1)."_s, u"n"_s, u"1"_s);
	const QCommandLineOption propStart(u"start"_s, u"First element index (default 1; 0 reads the element count)."_s,
		u"i"_s, u"1"_s);
	const QCommandLineOption knxprod(u"knxprod"_s, u"Product file (.knxprod) of the device."_s, u"file"_s);
	const QCommandLineOption images(u"images"_s, u"Directory with obj<N>*.bin segment images."_s, u"dir"_s);
	const QCommandLineOption application(u"application"_s, u"Application program id (when the file has several)."_s,
		u"id"_s);
	const QCommandLineOption procedure(u"procedure"_s, u"Load procedure subtype (default ap1)."_s, u"subtype"_s,
		u"ap1"_s);
	const QCommandLineOption programVersion(u"program-version"_s,
		u"PID_PROGRAM_VERSION to write (5 hex octets; default from the application)."_s, u"hex"_s);
	const QCommandLineOption dryRun(u"dry-run"_s, u"Print the resolved steps and stop (no bus access)."_s);
	const QCommandLineOption projectOption(u"project"_s, u"Project folder to add the device to."_s, u"dir"_s);
	const QCommandLineOption nameOption(u"name"_s, u"Device name (default: the product text)."_s, u"name"_s);
	const QCommandLineOption saveImages(u"save-images"_s, u"Also write the generated segment images (obj<N>.bin)."_s,
		u"dir"_s);
	const QCommandLineOption progModeTimeout(u"timeout"_s, u"Seconds to collect answers (default 3)."_s, u"s"_s, u"3"_s);
	const QCommandLineOption overwrite(u"overwrite"_s, u"Program even if another device already uses the address."_s);

	return {
		{{u"bus"_s, u"discover"_s}, {}, u"Find KNXnet/IP interfaces (FR-BUS-1)."_s, {timeoutDiscover, host, port, multicast},
			busDiscover},
		{{u"group"_s, u"write"_s}, {u"ga"_s, u"dpt"_s, u"value"_s}, u"Send A_GroupValue_Write (FR-BUS-6)."_s,
			{kInterfaceOption}, groupWrite},
		{{u"group"_s, u"read"_s}, {u"ga"_s, u"dpt"_s}, u"Send A_GroupValue_Read and print the response (FR-BUS-6)."_s,
			{kInterfaceOption, readTimeout}, groupRead},
		{{u"monitor"_s}, {}, u"Print bus telegrams (FR-BUS-5)."_s, {kInterfaceOption, count, monitorTimeout, monitorDpt},
			monitor},
		{{u"device"_s, u"program-ia"_s}, {u"ia"_s},
			u"Program the individual address of the device in programming mode (FR-COM-1)."_s,
			{kInterfaceOption, programTimeout, overwrite}, programIa},
		{{u"device"_s, u"info"_s}, {u"ia"_s}, u"Read mask version, serial number and manufacturer (FR-COM-4)."_s,
			{kInterfaceOption}, deviceInfo},
		{{u"device"_s, u"objects"_s}, {u"ia"_s},
			u"List the interface objects with type, load state, table reference and program version (read-only)."_s,
			{kInterfaceOption}, deviceObjects},
		{{u"device"_s, u"property"_s}, {u"ia"_s, u"object"_s, u"pid"_s},
			u"Read a property value as hex (read-only)."_s, {kInterfaceOption, propCount, propStart}, deviceProperty},
		{{u"device"_s, u"memory"_s}, {u"ia"_s, u"address"_s, u"length"_s},
			u"Read device memory as a hex dump (read-only; numbers may be 0x-hex)."_s, {kInterfaceOption}, deviceMemory},
		{{u"device"_s, u"download"_s}, {u"ia"_s},
			u"Download segment images with the product file's load procedure (FR-COM-2). Writes to the device!"_s,
			{kInterfaceOption, knxprod, images, application, procedure, programVersion, dryRun}, deviceDownload},
		{{u"device"_s, u"import"_s}, {u"ia"_s},
			u"Read a configured System B device (read-only) and add its configuration to a project (FR-DEV-3)."_s,
			{kInterfaceOption, knxprod, projectOption, application, nameOption}, deviceImport},
		{{u"project"_s, u"download"_s}, {u"dir"_s, u"device-id"_s},
			u"Generate a device's configuration from the project and download it (FR-COM-2). Writes to the device!"_s,
			{kInterfaceOption, knxprod, procedure, dryRun, saveImages}, projectDownload},
		{{u"device"_s, u"progmode"_s}, {},
			u"List the addresses of the devices in programming mode (read-only; FR-COM-1)."_s,
			{kInterfaceOption, progModeTimeout}, deviceProgMode},
	};
}

} // namespace kestrel::cli
