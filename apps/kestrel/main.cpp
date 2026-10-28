// Kestrel desktop application (tier T2).
//
//   kestrel [project-dir]
//
// Test-only options: --screenshot <file.png> grabs the main window after --quit-after-ms (default 1500)
// and quits; --quit-after-ms <N> alone quits after N ms.

#include "kestrel/shell/MainWindow.hpp"
#include "kestrel/shell/Style.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

#include <cstdio>

using namespace Qt::StringLiterals;

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);
	QApplication::setOrganizationName(u"Kestrel"_s);
	QApplication::setApplicationName(u"Kestrel"_s);
	QApplication::setApplicationVersion(QStringLiteral(KESTREL_VERSION));
	kestrel::shell::initializeStyle();

	QCommandLineParser parser;
	parser.setApplicationDescription(u"Open configuration tool for KNX installations"_s);
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addPositionalArgument(u"project"_s, u"Project folder to open."_s, u"[project]"_s);
	const QCommandLineOption screenshot(u"screenshot"_s, u"Save a screenshot of the main window and quit (testing)."_s, u"file"_s);
	const QCommandLineOption quitAfter(u"quit-after-ms"_s, u"Quit after this many milliseconds (testing)."_s, u"ms"_s);
	parser.addOption(screenshot);
	parser.addOption(quitAfter);
	parser.process(app);

	const bool testRun = parser.isSet(screenshot) || parser.isSet(quitAfter);
	kestrel::shell::MainWindowOptions options;
	options.persistLayout = !testRun;
	options.confirmDiscard = !testRun;
	kestrel::shell::MainWindow window(options);
	window.show();

	if (!parser.positionalArguments().isEmpty() && !window.openProject(parser.positionalArguments().constFirst()))
	{
		std::fprintf(stderr, "kestrel: cannot open %s\n", qPrintable(parser.positionalArguments().constFirst()));
	}

	if (testRun)
	{
		const int delay = parser.isSet(quitAfter) ? parser.value(quitAfter).toInt() : 1500;
		QTimer::singleShot(delay, &app,
			[&]
			{
				if (parser.isSet(screenshot) && !window.grab().save(parser.value(screenshot)))
					std::fprintf(stderr, "kestrel: cannot write %s\n", qPrintable(parser.value(screenshot)));
				window.close();
				QApplication::quit();
			});
	}
	return QApplication::exec();
}
