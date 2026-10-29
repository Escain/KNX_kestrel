#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace kestrel::shell
{

/// Colour tokens of the Kestrel look (specs/08-gui.md §6), light or dark. Widgets get them through the
/// application QPalette and style sheet; QML reads the palette (Kestrel.Controls Theme) and derives the rest.
struct Theme
{
	bool dark = false;
	QColor gutter;        ///< main window background between the panels
	QColor panel;         ///< panels, tool bar, menus
	QColor panelAlt;      ///< table headers, alternate rows, tab strips
	QColor border;
	QColor borderStrong;  ///< input and button outlines
	QColor text;
	QColor textSecondary;
	QColor textDisabled;
	QColor accent;
	QColor onAccent;
	QColor selection;     ///< selected row tint
	QColor selectionText;
	QColor hover;
	QColor pressed;
	QColor warning;
	QColor success;
	QColor error;

	static Theme light();
	static Theme darkTheme();
};

/// The theme in use (follows the system colour scheme).
const Theme& theme();

/// Fusion for widgets and Qt Quick Controls, so both draw from the same QPalette (ADR-006), with the Kestrel
/// palette and the main-window style sheet. Re-applied when the system colour scheme changes;
/// KESTREL_COLOR_SCHEME=dark|light overrides it.
/// Call after constructing QApplication and before creating any QML engine.
void initializeStyle();

/// The application style sheet for the main-window chrome (menus, tool bar, docks, tabs, status bar).
QString styleSheet(const Theme& theme);

/// One of the shell's stroke icons (`:/kestrel/icons/<name>.svg`) drawn in `color`, with a disabled variant.
/// Without a colour it takes the theme's text colour.
QIcon themedIcon(const QString& name, const QColor& color = {});

} // namespace kestrel::shell
