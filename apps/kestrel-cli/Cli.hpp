#pragma once

// kestrel-cli plumbing: a table of sub-commands ("group write", "topology add-area"...), each parsed
// with its own QCommandLineParser, and an Output that prints either human text or one JSON object
// (--json). Exit codes are stable and documented in main.cpp.

#include "kestrel/base/Result.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

namespace kestrel::cli
{

enum ExitCode : int
{
	kExitOk = 0,
	kExitFindings = 1, ///< validation errors found, or an edit rejected by the model's rules
	kExitUsage = 2,    ///< bad command line / malformed argument
	kExitIo = 3,       ///< file / project folder / scenario I/O
	kExitBus = 4,      ///< connection, timeout or device error on the bus
};

/// Prints results; in JSON mode exactly one object per command (monitor: one per line).
class Output
{
public:
	explicit Output(bool json) : m_json(json) {}

	bool json() const { return m_json; }
	/// Success: JSON mode prints `object` + "ok":true; text mode prints `text` (if not empty).
	int success(QJsonObject object, const QString& text);
	/// Failure: JSON mode prints {"ok":false,"error":{"code","message"},"exitCode"}; text mode writes
	/// "kestrel-cli: <message>" to stderr. Returns exitCode.
	int failure(int exitCode, const QString& message, const QString& code = {});
	int failure(int exitCode, const Error& error);
	/// One JSON line (JSON mode) or text line (text mode), flushed: streaming commands (monitor).
	void stream(const QJsonObject& object, const QString& text);
	/// Informational text on stderr (text mode only).
	void note(const QString& text);

	static void writeStdout(const QByteArray& line);

private:
	bool m_json;
};

/// A parsed invocation of one command.
struct Invocation
{
	QCommandLineParser& parser;
	QStringList args; ///< positional arguments
	Output& out;

	QString value(const QString& option) const { return parser.value(option); }
	bool isSet(const QString& option) const { return parser.isSet(option); }
};

struct Command
{
	QStringList words;     ///< {"topology", "add-area"}
	QStringList arguments; ///< positional argument names (all required): {"dir", "number", "name"}
	QString description;
	QList<QCommandLineOption> options;
	std::function<int(Invocation&)> run;
};

std::vector<Command> busCommands();
std::vector<Command> projectCommands();

/// Dispatches argv (without the program name) to a command. Returns the exit code.
int dispatch(const QStringList& arguments);

/// Maps a Result error from the bus layer to an exit code (usage errors stay 2).
int busExitCode(const Error& error);

} // namespace kestrel::cli
