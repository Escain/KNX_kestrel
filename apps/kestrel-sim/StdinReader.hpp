#pragma once

// Reads stdin line by line on the main thread: a QSocketNotifier on POSIX, a detached reader thread
// posting queued calls on Windows (where QSocketNotifier cannot watch a console/pipe).
// End of input is reported once with endOfInput().

#include <QObject>
#include <QString>

namespace kestrel::simapp
{

class StdinReader : public QObject
{
	Q_OBJECT

public:
	explicit StdinReader(QObject* parent = nullptr);

	/// Starts watching stdin.
	void start();

Q_SIGNALS:
	void lineRead(const QString& line);
	void endOfInput();
};

} // namespace kestrel::simapp
