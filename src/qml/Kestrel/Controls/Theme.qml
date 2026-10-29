pragma Singleton

import QtQuick

/// Design tokens for the QML views (specs/08-gui.md §6). The shell sets the application palette from the same
/// tokens (shell/Style.cpp); colours the palette has no role for are repeated here, light and dark.
QtObject {
	id: theme

	readonly property SystemPalette active: SystemPalette { colorGroup: SystemPalette.Active }
	readonly property SystemPalette disabled: SystemPalette { colorGroup: SystemPalette.Disabled }
	readonly property bool dark: active.base.hslLightness < 0.5

	readonly property color panel: active.base
	readonly property color panelAlt: active.alternateBase
	readonly property color border: dark ? "#2A3039" : "#E1E4E9"
	readonly property color borderStrong: active.mid
	readonly property color text: active.text
	readonly property color textSecondary: active.placeholderText
	readonly property color textDisabled: disabled.text
	readonly property color accent: active.accent
	readonly property color onAccent: active.highlightedText
	readonly property color selection: dark ? "#17323F" : "#E6F1F6"
	readonly property color selectionText: dark ? "#CDE8F4" : "#08506F"
	readonly property color selectionBorder: dark ? "#25536A" : "#BFDCE8"
	readonly property color hover: dark ? "#222831" : "#F3F5F7"

	readonly property color warning: dark ? "#F59E0B" : "#B45309"
	readonly property color warningBg: dark ? "#3A2A0E" : "#FDF1E3"
	readonly property color success: dark ? "#34C26B" : "#1E7B3E"
	readonly property color successBg: dark ? "#123222" : "#E8F5EC"
	readonly property color error: dark ? "#F87171" : "#B42318"
	readonly property color errorBg: dark ? "#3B1717" : "#FDECEC"
	readonly property color info: accent
	readonly property color infoBg: selection

	readonly property int radius: 6
	readonly property int rowHeight: 30
	readonly property int headerHeight: 28
	readonly property int toolBarHeight: 38
	readonly property int gap: 8
	readonly property int pad: 12
	readonly property string monoFamily: "monospace"
	readonly property int smallSize: 11

	/// Foreground and background of a severity or status: "error", "warning", "success", "info", "neutral".
	function toneColor(tone: string): color {
		return tone === "error" ? error : tone === "warning" ? warning : tone === "success" ? success
			: tone === "info" ? selectionText : textSecondary
	}
	function toneBackground(tone: string): color {
		return tone === "error" ? errorBg : tone === "warning" ? warningBg : tone === "success" ? successBg
			: tone === "info" ? infoBg : (dark ? "#242A33" : "#EEF0F3")
	}
}
