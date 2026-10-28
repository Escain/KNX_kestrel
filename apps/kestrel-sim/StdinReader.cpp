#include "StdinReader.hpp"

#include <QByteArray>
#include <QtGlobal>

#include <memory>

#ifdef Q_OS_WIN
#include <QCoreApplication>
#include <iostream>
#include <string>
#include <thread>
#else
#include <QSocketNotifier>
#include <cerrno>
#include <unistd.h>
#endif

namespace kestrel::simapp
{

StdinReader::StdinReader(QObject* parent) : QObject(parent)
{
}

#ifdef Q_OS_WIN

// QSocketNotifier cannot watch a console/pipe handle on Windows: read on a detached thread and post
// each line to the main thread. The thread may outlive the application object only while blocked in a
// read, after which the process exits.
void StdinReader::start()
{
	std::thread(
		[this]
		{
			std::string line;
			while (std::getline(std::cin, line))
			{
				const QString text = QString::fromUtf8(line.data(), static_cast<qsizetype>(line.size()));
				QMetaObject::invokeMethod(this, [this, text] { Q_EMIT lineRead(text); }, Qt::QueuedConnection);
			}
			QMetaObject::invokeMethod(this, [this] { Q_EMIT endOfInput(); }, Qt::QueuedConnection);
		})
		.detach();
}

#else

void StdinReader::start()
{
	auto* notifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
	auto buffer = std::make_shared<QByteArray>();
	QObject::connect(notifier, &QSocketNotifier::activated, this,
		[this, notifier, buffer]
		{
			char chunk[4096];
			const ssize_t count = ::read(STDIN_FILENO, chunk, sizeof chunk);
			if (count < 0 && (errno == EINTR || errno == EAGAIN))
			{
				return;
			}
			if (count <= 0)
			{
				notifier->setEnabled(false);
				if (!buffer->isEmpty())
				{
					Q_EMIT lineRead(QString::fromUtf8(*buffer));
					buffer->clear();
				}
				Q_EMIT endOfInput();
				return;
			}
			buffer->append(chunk, static_cast<qsizetype>(count));
			qsizetype newline = buffer->indexOf('\n');
			while (newline >= 0)
			{
				QByteArray line = buffer->left(newline);
				buffer->remove(0, newline + 1);
				if (line.endsWith('\r'))
				{
					line.chop(1);
				}
				Q_EMIT lineRead(QString::fromUtf8(line));
				newline = buffer->indexOf('\n');
			}
		});
}

#endif

} // namespace kestrel::simapp
