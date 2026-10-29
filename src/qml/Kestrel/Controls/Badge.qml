import QtQuick
import QtQuick.Controls

/// Rounded status label ("Needs download", "Warning", a count).
Rectangle {
	id: badge

	property alias text: label.text
	/// "neutral", "info", "success", "warning" or "error"
	property string tone: "neutral"

	implicitWidth: label.implicitWidth + 16
	implicitHeight: label.implicitHeight + 4
	radius: height / 2
	color: Theme.toneBackground(badge.tone)
	Accessible.name: label.text

	Label {
		id: label
		anchors.centerIn: parent
		font.pixelSize: 12
		font.weight: Font.Medium
		color: Theme.toneColor(badge.tone)
	}
}
