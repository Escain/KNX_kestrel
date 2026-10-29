#include "kestrel/shell/QmlHost.hpp"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPalette>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>

// The QML modules are static libraries: import their plugins once, here (this file is always linked).
Q_IMPORT_QML_PLUGIN(Kestrel_ViewModelsPlugin)
Q_IMPORT_QML_PLUGIN(Kestrel_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Kestrel_ViewsPlugin)

Q_LOGGING_CATEGORY(lcQmlHost, "kestrel.shell.qmlhost")

namespace kestrel::shell
{

namespace
{

/// Qt Quick Controls' Fusion takes its colours from the platform theme, not from QGuiApplication::palette();
/// the scene's root item inherits the palette of the window's content item, so the application palette goes
/// there (through the QQuickPalette properties, the public route).
void applyPalette(QQuickWindow* window, const QPalette& palette)
{
	auto* scenePalette = window->contentItem()->property("palette").value<QObject*>();
	if (!scenePalette)
		return;
	static const std::pair<const char*, QPalette::ColorRole> roles[] = {{"alternateBase", QPalette::AlternateBase},
		{"base", QPalette::Base}, {"brightText", QPalette::BrightText}, {"button", QPalette::Button},
		{"buttonText", QPalette::ButtonText}, {"dark", QPalette::Dark}, {"highlight", QPalette::Highlight},
		{"highlightedText", QPalette::HighlightedText}, {"light", QPalette::Light}, {"link", QPalette::Link},
		{"linkVisited", QPalette::LinkVisited}, {"mid", QPalette::Mid}, {"midlight", QPalette::Midlight},
		{"shadow", QPalette::Shadow}, {"text", QPalette::Text}, {"toolTipBase", QPalette::ToolTipBase},
		{"toolTipText", QPalette::ToolTipText}, {"window", QPalette::Window}, {"windowText", QPalette::WindowText},
		{"placeholderText", QPalette::PlaceholderText}, {"accent", QPalette::Accent}};
	static const std::pair<const char*, QPalette::ColorGroup> groups[] = {
		{"active", QPalette::Active}, {"inactive", QPalette::Inactive}, {"disabled", QPalette::Disabled}};
	for (const auto& [groupName, group] : groups)
	{
		auto* colors = scenePalette->property(groupName).value<QObject*>();
		if (!colors)
			continue;
		for (const auto& [roleName, role] : roles)
			colors->setProperty(roleName, palette.color(group, role));
	}
}

} // namespace

QmlHost::QmlHost(QQmlEngine* engine, const QString& module, const QString& type, const QVariantMap& initialProperties,
	QWidget* parent)
	: QQuickWidget(engine, parent), m_module(module), m_type(type), m_initialProperties(initialProperties)
{
	setResizeMode(QQuickWidget::SizeRootObjectToView);
	setObjectName(QStringLiteral("host.") + type);
	applyPalette(quickWindow(), QGuiApplication::palette());
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

bool QmlHost::event(QEvent* event)
{
	if (event->type() == QEvent::ApplicationPaletteChange)
		applyPalette(quickWindow(), QGuiApplication::palette());
	return QQuickWidget::event(event);
}

void QmlHost::showEvent(QShowEvent* event)
{
	load();
	QQuickWidget::showEvent(event);
}

} // namespace kestrel::shell
