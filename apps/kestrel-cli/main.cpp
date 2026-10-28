// kestrel-cli: scriptable access to Kestrel's bus and project services (links T0/T1 only, no QtGui).
//
//   kestrel-cli bus discover [--host H --port P] [--timeout s]
//   kestrel-cli group write <ga> <dpt> <value> --interface host:port
//   kestrel-cli group read <ga> <dpt> --interface host:port [--timeout s]
//   kestrel-cli monitor --interface host:port [--count N] [--timeout s] [--dpt ga=dpt ...]
//   kestrel-cli device program-ia <ia> --interface host:port [--timeout s] [--overwrite]
//   kestrel-cli device info <ia> --interface host:port
//   kestrel-cli project new <dir> --name N [--ga-style S]
//   kestrel-cli project info <dir>
//   kestrel-cli building add <dir> <kind> <name> [--parent id]
//   kestrel-cli topology add-area <dir> <n> <name> | add-line <dir> <areaId> <n> <name>
//                        | add-device <dir> <lineId> <name> [--number d] [--place bpId]
//   kestrel-cli co add <dir> <devId> <number> <name> [--size bits] [--flags CWTU] [--dpt X]
//   kestrel-cli ga add <dir> <address> <name> [--dpt X]
//   kestrel-cli link <dir> <coId> <gaId>
//   kestrel-cli validate <dir>
//
// Every command accepts --json (one JSON object on stdout; "ok" tells success). Commands that create
// an entity print its id (text mode: the id alone, so scripts can chain).
// Exit codes: 0 ok, 1 validation errors or an edit rejected by the model (duplicate, out of range,
// unknown id), 2 usage, 3 file I/O, 4 bus error (connection, timeout, device answer).

#include "Cli.hpp"

#include <QCoreApplication>
#include <QLoggingCategory>

int main(int argc, char* argv[])
{
	QCoreApplication app(argc, argv);
	QCoreApplication::setApplicationName(QStringLiteral("kestrel-cli"));
	QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
	// Library chatter stays off stdout/stderr unless asked for (QT_LOGGING_RULES takes precedence).
	QLoggingCategory::setFilterRules(QStringLiteral("kestrel.*.debug=false\nkestrel.*.info=false"));
	return kestrel::cli::dispatch(QCoreApplication::arguments().mid(1));
}
