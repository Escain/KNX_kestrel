#include "kestrel/shell/QmlHost.hpp"

#include <QLoggingCategory>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QtQml/qqmlextensionplugin.h>

// The QML modules are static libraries: import their plugins once, here (this file is always linked).
Q_IMPORT_QML_PLUGIN(Kestrel_ViewModelsPlugin)
Q_IMPORT_QML_PLUGIN(Kestrel_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Kestrel_ViewsPlugin)

Q_LOGGING_CATEGORY(lcQmlHost, "kestrel.shell.qmlhost")

namespace kestrel::shell
{

QmlHost::QmlHost(QQmlEngine* engine, const QString& module, const QString& type, const QVariantMap& initialProperties,
	QWidget* parent)
	: QQuickWidget(engine, parent), m_module(module), m_type(type), m_initialProperties(initialProperties)
{
	setResizeMode(QQuickWidget::SizeRootObjectToView);
	setObjectName(QStringLiteral("host.") + type);
}

QmlHost::~QmlHost() = default;

bool QmlHost::load()
{
	if (m_attempted)
	{
		return isLoaded();
	}
	m_attempted = true;
	m_component = new QQmlComponent(engine(), this);
	m_component->loadFromModule(m_module, m_type);
	QObject* object = m_component->isReady() ? m_component->createWithInitialProperties(m_initialProperties) : nullptr;
	auto* item = qobject_cast<QQuickItem*>(object);
	if (!item)
	{
		for (const QQmlError& error : m_component->errors())
		{
			m_errors.append(error.toString());
		}
		if (m_errors.isEmpty())
		{
			m_errors.append(QStringLiteral("%1.%2 did not create an Item").arg(m_module, m_type));
		}
		qCWarning(lcQmlHost).noquote() << "Cannot load" << m_module << m_type << m_errors.join(u'\n');
		delete object;
		return false;
	}
	// The widget takes ownership of the item (spike, ADR-006).
	setContent(m_component->url(), m_component, item);
	Q_EMIT loaded();
	return true;
}

void QmlHost::showEvent(QShowEvent* event)
{
	load();
	QQuickWidget::showEvent(event);
}

} // namespace kestrel::shell
