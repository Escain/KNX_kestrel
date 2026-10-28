#include "kestrel/shell/QmlDock.hpp"

#include "kestrel/shell/QmlHost.hpp"

namespace kestrel::shell
{

QmlDock::QmlDock(const QString& id, const QString& title, QWidget* body, QWidget* parent) : QDockWidget(title, parent)
{
	setObjectName(id);
	setWidget(body);
	setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
	if (QmlHost* qmlHost = host())
	{
		qmlHost->setObjectName(id + QStringLiteral(".host"));
	}
}

QmlHost* QmlDock::host() const
{
	return qobject_cast<QmlHost*>(widget());
}

void QmlDock::releaseBody()
{
	delete widget();
}

} // namespace kestrel::shell
