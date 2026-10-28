// kestrel-sim: a simulated KNX installation behind a KNXnet/IP tunnelling server on 127.0.0.1
// (Kestrel spec 04 §5). Controlled through JSON lines on stdin; see SimController.hpp.
//
//   kestrel-sim [--port N] [--scenario file.json]
//
// Exit codes: 0 after "quit" or end of stdin, 2 usage, 3 scenario file / socket error.

#include "SimController.hpp"
#include "StdinReader.hpp"

#include "kestrel/simnet/ScenarioLoader.hpp"
#include "kestrel/simnet/Simulation.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonObject>

#include <cstdio>

using namespace kestrel;
using namespace Qt::StringLiterals;

namespace
{

int fail(int code, const QString& message)
{
	simapp::SimController::writeLine({{u"event"_s, u"error"_s}, {u"message"_s, message}});
	std::fprintf(stderr, "kestrel-sim: %s\n", qPrintable(message));
	return code;
}

} // namespace

int main(int argc, char* argv[])
{
	QCoreApplication app(argc, argv);
	QCoreApplication::setApplicationName(u"kestrel-sim"_s);
	QCoreApplication::setApplicationVersion(u"0.1.0"_s);

	QCommandLineParser parser;
	parser.setApplicationDescription(u"Simulated KNX installation behind a KNXnet/IP tunnelling server."_s);
	parser.addHelpOption();
	parser.addVersionOption();
	const QCommandLineOption portOption(u"port"_s, u"UDP port on 127.0.0.1 (0 = ephemeral)."_s, u"port"_s, u"0"_s);
	const QCommandLineOption scenarioOption(u"scenario"_s, u"Scenario JSON file."_s, u"file"_s);
	const QCommandLineOption quietOption(u"no-telegram-events"_s, u"Do not print telegram events."_s);
	parser.addOptions({portOption, scenarioOption, quietOption});
	if (!parser.parse(QCoreApplication::arguments()))
	{
		return fail(2, parser.errorText());
	}
	if (parser.isSet(u"help"_s))
	{
		parser.showHelp(0);
	}
	if (parser.isSet(u"version"_s))
	{
		parser.showVersion();
	}

	bool portOk = false;
	const uint port = parser.value(portOption).toUInt(&portOk);
	if (!portOk || port > 0xFFFF)
	{
		return fail(2, u"invalid --port"_s);
	}

	simnet::Scenario scenario;
	if (parser.isSet(scenarioOption))
	{
		auto loaded = simnet::ScenarioLoader::fromFile(parser.value(scenarioOption));
		if (!loaded)
		{
			return fail(3, QString::fromStdString(loaded.error().message));
		}
		scenario = std::move(loaded).value();
	}

	simnet::Simulation simulation(scenario);
	simapp::SimController controller(simulation);
	if (!parser.isSet(quietOption))
	{
		QObject::connect(&simulation, &simnet::Simulation::telegram, &controller,
			[](const sim::Telegram& telegram) { simapp::SimController::writeLine(simapp::SimController::toJson(telegram)); });
	}

	if (const Status listening = simulation.listen(static_cast<quint16>(port)); !listening)
	{
		return fail(3, QString::fromStdString(listening.error().message));
	}

	simapp::StdinReader reader;
	QObject::connect(&reader, &simapp::StdinReader::lineRead, &controller, &simapp::SimController::handleLine);
	QObject::connect(&reader, &simapp::StdinReader::endOfInput, &app, &QCoreApplication::quit);
	QObject::connect(&controller, &simapp::SimController::quitRequested, &app, &QCoreApplication::quit,
		Qt::QueuedConnection);
	reader.start();

	simapp::SimController::writeLine({{u"event"_s, u"ready"_s}, {u"port"_s, simulation.port()}});
	return app.exec();
}
