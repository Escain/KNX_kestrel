#include "Cli.hpp"

#include <QCoreApplication>
#include <QJsonDocument>

#include <cstdio>

namespace kestrel::cli
{

using namespace Qt::StringLiterals;

namespace
{

const QCommandLineOption kJsonOption(u"json"_s, u"Print machine-readable JSON."_s);
const QCommandLineOption kHelpOption(QStringList{u"h"_s, u"help"_s}, u"Show help for the command."_s);

QString synopsis(const Command& command)
{
	QString text = command.words.join(u' ');
	for (const QString& argument : command.arguments)
	{
		text += u" <"_s + argument + u'>';
	}
	return text;
}

void printUsage(const std::vector<Command>& commands, FILE* stream)
{
	QString text = u"Usage: kestrel-cli <command> [arguments] [--json]\n\nCommands:\n"_s;
	for (const Command& command : commands)
	{
		text += u"  %1\n      %2\n"_s.arg(synopsis(command), command.description);
	}
	text += u"\nExit codes: 0 ok, 1 validation findings / rejected edit, 2 usage, 3 file I/O, 4 bus error.\n"_s
		u"Run 'kestrel-cli <command> --help' for the options of a command.\n"_s;
	const QByteArray bytes = text.toUtf8();
	std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stream);
}

} // namespace

void Output::writeStdout(const QByteArray& line)
{
	std::fwrite(line.constData(), 1, static_cast<std::size_t>(line.size()), stdout);
	std::fputc('\n', stdout);
	std::fflush(stdout);
}

int Output::success(QJsonObject object, const QString& text)
{
	if (m_json)
	{
		object.insert(u"ok"_s, true);
		writeStdout(QJsonDocument(object).toJson(QJsonDocument::Compact));
	}
	else if (!text.isEmpty())
	{
		writeStdout(text.toUtf8());
	}
	return kExitOk;
}

int Output::failure(int exitCode, const QString& message, const QString& code)
{
	if (m_json)
	{
		QJsonObject error{{u"message"_s, message}};
		if (!code.isEmpty())
		{
			error.insert(u"code"_s, code);
		}
		writeStdout(QJsonDocument(QJsonObject{{u"ok"_s, false}, {u"error"_s, error}, {u"exitCode"_s, exitCode}})
						.toJson(QJsonDocument::Compact));
	}
	else
	{
		std::fprintf(stderr, "kestrel-cli: %s\n", message.toUtf8().constData());
	}
	return exitCode;
}

int Output::failure(int exitCode, const Error& error)
{
	return failure(exitCode, QString::fromStdString(error.message), QString::fromLatin1(toString(error.code)));
}

void Output::stream(const QJsonObject& object, const QString& text)
{
	writeStdout(m_json ? QJsonDocument(object).toJson(QJsonDocument::Compact) : text.toUtf8());
}

void Output::note(const QString& text)
{
	if (!m_json)
	{
		std::fprintf(stderr, "%s\n", text.toUtf8().constData());
		std::fflush(stderr);
	}
}

int busExitCode(const Error& error)
{
	return error.code == ErrorCode::InvalidArgument ? kExitUsage : kExitBus;
}

int dispatch(const QStringList& arguments)
{
	std::vector<Command> commands = busCommands();
	for (Command& command : projectCommands())
	{
		commands.push_back(std::move(command));
	}
	for (Command& command : catalogCommands())
	{
		commands.push_back(std::move(command));
	}

	// Longest matching command word sequence.
	const Command* match = nullptr;
	for (const Command& command : commands)
	{
		if (command.words.size() <= arguments.size() && arguments.mid(0, command.words.size()) == command.words
			&& (!match || command.words.size() > match->words.size()))
		{
			match = &command;
		}
	}
	const bool wantsJson = arguments.contains(u"--json"_s);
	if (!match)
	{
		if (arguments.isEmpty() || arguments.first() == u"--help"_s || arguments.first() == u"-h"_s
			|| arguments.first() == u"help"_s)
		{
			printUsage(commands, stdout);
			return arguments.isEmpty() ? kExitUsage : kExitOk;
		}
		Output out(wantsJson);
		if (!wantsJson)
		{
			printUsage(commands, stderr);
		}
		return out.failure(kExitUsage, u"unknown command '%1'"_s.arg(arguments.join(u' ')), u"Usage"_s);
	}

	QCommandLineParser parser;
	parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
	parser.setApplicationDescription(match->description);
	parser.addOption(kJsonOption);
	parser.addOption(kHelpOption);
	parser.addOptions(match->options);
	for (const QString& argument : match->arguments)
	{
		parser.addPositionalArgument(argument, QString());
	}
	const QStringList rest = QStringList{u"kestrel-cli "_s + match->words.join(u' ')} + arguments.mid(match->words.size());
	Output out(wantsJson);
	if (!parser.parse(rest))
	{
		return out.failure(kExitUsage, parser.errorText(), u"Usage"_s);
	}
	if (parser.isSet(kHelpOption))
	{
		std::fputs(qPrintable(parser.helpText()), stdout);
		return kExitOk;
	}
	const QStringList positional = parser.positionalArguments();
	if (positional.size() != match->arguments.size())
	{
		return out.failure(kExitUsage,
			u"usage: kestrel-cli %1 (expected %2 argument(s), got %3)"_s.arg(synopsis(*match))
				.arg(match->arguments.size())
				.arg(positional.size()),
			u"Usage"_s);
	}
	Invocation invocation{parser, positional, out};
	return match->run(invocation);
}

} // namespace kestrel::cli
