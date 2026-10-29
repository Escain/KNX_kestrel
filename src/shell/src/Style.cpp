#include "kestrel/shell/Style.hpp"

#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QQuickStyle>
#include <QStyleHints>
#include <QSvgRenderer>

using namespace Qt::StringLiterals;

namespace kestrel::shell
{

namespace
{

Theme g_theme = Theme::light();

bool wantsDark()
{
	// KESTREL_COLOR_SCHEME=dark|light overrides the system colour scheme.
	const QString forced = qEnvironmentVariable("KESTREL_COLOR_SCHEME").toLower();
	if (forced == u"dark" || forced == u"light")
		return forced == u"dark";
	return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

QPalette paletteFor(const Theme& t)
{
	QPalette p;
	p.setColor(QPalette::Window, t.panel);
	p.setColor(QPalette::WindowText, t.text);
	p.setColor(QPalette::Base, t.panel);
	p.setColor(QPalette::AlternateBase, t.panelAlt);
	p.setColor(QPalette::Text, t.text);
	p.setColor(QPalette::BrightText, t.onAccent);
	p.setColor(QPalette::Button, t.panel);
	p.setColor(QPalette::ButtonText, t.text);
	p.setColor(QPalette::Highlight, t.accent);
	p.setColor(QPalette::HighlightedText, t.onAccent);
	p.setColor(QPalette::Accent, t.accent);
	p.setColor(QPalette::Link, t.accent);
	p.setColor(QPalette::LinkVisited, t.accent);
	p.setColor(QPalette::PlaceholderText, t.textSecondary);
	p.setColor(QPalette::ToolTipBase, t.dark ? t.borderStrong : t.text);
	p.setColor(QPalette::ToolTipText, t.dark ? t.text : t.panel);
	// Fusion draws frames and bevels from these.
	p.setColor(QPalette::Light, t.dark ? t.panelAlt : QColor(Qt::white));
	p.setColor(QPalette::Midlight, t.panelAlt);
	p.setColor(QPalette::Mid, t.borderStrong);
	p.setColor(QPalette::Dark, t.dark ? t.border.darker(140) : t.textDisabled);
	p.setColor(QPalette::Shadow, t.dark ? QColor(Qt::black) : t.textSecondary);
	for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
		p.setColor(QPalette::Disabled, role, t.textDisabled);
	p.setColor(QPalette::Disabled, QPalette::Highlight, t.border);
	p.setColor(QPalette::Disabled, QPalette::HighlightedText, t.textDisabled);
	return p;
}

void apply(const Theme& theme)
{
	g_theme = theme;
	QApplication::setPalette(paletteFor(theme));
	qApp->setStyleSheet(styleSheet(theme));
}

} // namespace

Theme Theme::light()
{
	Theme t;
	t.gutter = QColor(0xEE, 0xF0, 0xF3);
	t.panel = QColor(0xFF, 0xFF, 0xFF);
	t.panelAlt = QColor(0xF6, 0xF7, 0xF9);
	t.border = QColor(0xE1, 0xE4, 0xE9);
	t.borderStrong = QColor(0xD5, 0xD9, 0xE0);
	t.text = QColor(0x1B, 0x22, 0x30);
	t.textSecondary = QColor(0x5B, 0x64, 0x72);
	t.textDisabled = QColor(0xA3, 0xAA, 0xB5);
	t.accent = QColor(0x0B, 0x6E, 0x99);
	t.onAccent = QColor(0xFF, 0xFF, 0xFF);
	t.selection = QColor(0xE6, 0xF1, 0xF6);
	t.selectionText = QColor(0x08, 0x50, 0x6F);
	t.hover = QColor(0xEE, 0xF0, 0xF3);
	t.pressed = QColor(0xE1, 0xE4, 0xE9);
	t.warning = QColor(0xB4, 0x53, 0x09);
	t.success = QColor(0x1E, 0x7B, 0x3E);
	t.error = QColor(0xB4, 0x23, 0x18);
	return t;
}

Theme Theme::darkTheme()
{
	Theme t;
	t.dark = true;
	t.gutter = QColor(0x0F, 0x12, 0x16);
	t.panel = QColor(0x17, 0x1B, 0x21);
	t.panelAlt = QColor(0x1D, 0x22, 0x2A);
	t.border = QColor(0x2A, 0x30, 0x39);
	t.borderStrong = QColor(0x3A, 0x42, 0x4D);
	t.text = QColor(0xE4, 0xE7, 0xEC);
	t.textSecondary = QColor(0x9A, 0xA3, 0xB0);
	t.textDisabled = QColor(0x5E, 0x67, 0x74);
	t.accent = QColor(0x3F, 0xA4, 0xCF);
	t.onAccent = QColor(0x07, 0x1A, 0x23);
	t.selection = QColor(0x17, 0x32, 0x3F);
	t.selectionText = QColor(0xCD, 0xE8, 0xF4);
	t.hover = QColor(0x22, 0x28, 0x31);
	t.pressed = QColor(0x2A, 0x31, 0x3B);
	t.warning = QColor(0xF5, 0x9E, 0x0B);
	t.success = QColor(0x34, 0xC2, 0x6B);
	t.error = QColor(0xF8, 0x71, 0x71);
	return t;
}

const Theme& theme()
{
	return g_theme;
}

void initializeStyle()
{
	QApplication::setStyle(u"Fusion"_s);
	QQuickStyle::setStyle(u"Fusion"_s);
	apply(wantsDark() ? Theme::darkTheme() : Theme::light());
	static QMetaObject::Connection follow;
	QObject::disconnect(follow);
	follow = QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp,
		[] { apply(wantsDark() ? Theme::darkTheme() : Theme::light()); });
}

QString styleSheet(const Theme& t)
{
	// Scoped to the main-window chrome; dialogs keep plain Fusion with the palette.
	QString css = uR"(
QMainWindow { background: @gutter; }
QMainWindow::separator { background: @gutter; width: 6px; height: 6px; }

QMenuBar { background: @panel; border-bottom: 1px solid @border; padding: 3px 6px; }
QMenuBar::item { padding: 4px 9px; border-radius: 5px; background: transparent; color: @text; }
QMenuBar::item:selected { background: @hover; }
QMenuBar::item:pressed { background: @selection; color: @selectionText; }

QMenu { background: @panel; border: 1px solid @borderStrong; border-radius: 8px; padding: 4px; }
QMenu::item { padding: 6px 28px 6px 10px; border-radius: 5px; color: @text; }
QMenu::item:selected { background: @selection; color: @selectionText; }
QMenu::item:disabled { color: @textDisabled; }
QMenu::icon { padding-left: 8px; }
QMenu::separator { height: 1px; background: @border; margin: 4px 6px; }

QToolBar { background: @panel; border: 0; border-bottom: 1px solid @border; padding: 6px 8px; spacing: 2px; }
QToolBar::separator { width: 1px; background: @border; margin: 6px 8px; }
QToolBar QToolButton { padding: 5px 9px; border: 0; border-radius: 6px; color: @text; font-weight: 500; }
QToolBar QToolButton:hover { background: @hover; }
QToolBar QToolButton:pressed { background: @pressed; }
QToolBar QToolButton:disabled { color: @textDisabled; }
QToolBar QToolButton#primaryAction { background: @accent; color: @onAccent; font-weight: 600; padding: 5px 12px; }
QToolBar QToolButton#primaryAction:hover { background: @accentHover; }
QToolBar QToolButton#primaryAction:disabled { background: @hover; color: @textDisabled; }

QDockWidget { color: @text; font-weight: 600; }
QDockWidget::title { background: @panel; padding: 8px 10px; border-bottom: 1px solid @border; text-align: left; }
QDockWidget::close-button, QDockWidget::float-button { border: 0; border-radius: 4px; padding: 2px; background: transparent; }
QDockWidget::close-button:hover, QDockWidget::float-button:hover { background: @hover; }

QMainWindow > QTabBar { background: @panel; }
QMainWindow > QTabBar::tab { background: @panel; color: @textSecondary; padding: 7px 14px; border: 0; border-bottom: 1px solid @border; font-weight: 500; }
QMainWindow > QTabBar::tab:hover { color: @text; }
QMainWindow > QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent; font-weight: 600; }

QTabWidget#documentArea::pane { border: 0; border-top: 1px solid @border; background: @panel; }
QTabWidget#documentArea > QTabBar { background: @panelAlt; }
QTabWidget#documentArea > QTabBar::tab { background: @panelAlt; color: @textSecondary; padding: 7px 10px 7px 14px; border: 0; border-right: 1px solid @border; }
QTabWidget#documentArea > QTabBar::tab:hover { color: @text; }
QTabWidget#documentArea > QTabBar::close-button { image: url(:/kestrel/icons/close.svg); subcontrol-position: right; border-radius: 4px; }
QTabWidget#documentArea > QTabBar::close-button:hover { background: @pressed; }
QTabWidget#documentArea > QTabBar::tab:selected { background: @panel; color: @text; border-top: 2px solid @accent; font-weight: 500; }

QStatusBar { background: @panel; border-top: 1px solid @border; color: @textSecondary; }
QStatusBar::item { border: 0; }
QStatusBar QLabel, QStatusBar QToolButton { color: @textSecondary; padding: 0 6px; border: 0; background: transparent; }
QStatusBar QToolButton:hover { color: @text; }
QStatusBar QToolButton#busStatus { border-radius: 9px; padding: 1px 10px; margin: 2px 4px; }

QToolTip { background: @tooltip; color: @tooltipText; border: 0; padding: 4px 8px; }
)"_s;
	const std::pair<QString, QColor> tokens[] = {
		{u"@gutter"_s, t.gutter}, {u"@panelAlt"_s, t.panelAlt}, {u"@panel"_s, t.panel},
		{u"@borderStrong"_s, t.borderStrong}, {u"@border"_s, t.border},
		{u"@textSecondary"_s, t.textSecondary}, {u"@textDisabled"_s, t.textDisabled},
		{u"@selectionText"_s, t.selectionText}, {u"@selection"_s, t.selection},
		{u"@accentHover"_s, t.dark ? t.accent.lighter(115) : t.accent.darker(115)}, {u"@accent"_s, t.accent},
		{u"@onAccent"_s, t.onAccent}, {u"@hover"_s, t.hover}, {u"@pressed"_s, t.pressed},
		{u"@tooltipText"_s, t.dark ? t.text : t.panel}, {u"@tooltip"_s, t.dark ? t.borderStrong : t.text},
		{u"@text"_s, t.text}};
	// A name that extends another comes first, so "@textSecondary" is not taken for "@text".
	for (const auto& [name, color] : tokens)
		css.replace(name, color.name());
	return css;
}

QIcon themedIcon(const QString& name, const QColor& color)
{
	QFile file(u":/kestrel/icons/"_s + name + u".svg"_s);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	const QByteArray source = file.readAll();
	const auto render = [&source](const QColor& c, int size)
	{
		QByteArray svg = source;
		svg.replace("currentColor", c.name().toLatin1());
		QSvgRenderer renderer(svg);
		QPixmap pixmap(size, size);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		renderer.render(&painter);
		return pixmap;
	};
	const QColor normal = color.isValid() ? color : g_theme.text;
	QIcon icon;
	for (int size : {16, 24, 32, 48})
	{
		icon.addPixmap(render(normal, size), QIcon::Normal);
		icon.addPixmap(render(g_theme.textDisabled, size), QIcon::Disabled);
	}
	return icon;
}

} // namespace kestrel::shell
