import QtQuick
import QtQuick.Controls

/// A group or individual address in a small monospaced chip; `emphasis` marks the sending address.
Rectangle {
	id: chip

	property alias text: label.text
	property bool emphasis: false

	implicitWidth: label.implicitWidth + 12
	implicitHeight: label.implicitHeight + 4
	radius: 4
	color: chip.emphasis ? Theme.selection : Theme.panelAlt
	border.color: chip.emphasis ? Theme.selectionBorder : Theme.border
	Accessible.name: label.text

	Label {
		id: label
		anchors.centerIn: parent
		font.family: Theme.monoFamily
		font.pixelSize: 12
		color: chip.emphasis ? Theme.selectionText : Theme.text
	}
}
