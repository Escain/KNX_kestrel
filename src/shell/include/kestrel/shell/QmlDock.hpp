#pragma once

// A dock widget whose body is a QmlHost (or any placeholder widget). The objectName ("dock.topology") is
// stable: QMainWindow::saveState/restoreState key on it.

#include <QDockWidget>

namespace kestrel::shell
{

class QmlHost;

class QmlDock : public QDockWidget
{
	Q_OBJECT

public:
	QmlDock(const QString& id, const QString& title, QWidget* body, QWidget* parent = nullptr);

	/// The QML host, or nullptr for placeholder docks.
	QmlHost* host() const;
	/// Deletes the body (hosts must go before their view models).
	void releaseBody();
};

} // namespace kestrel::shell
