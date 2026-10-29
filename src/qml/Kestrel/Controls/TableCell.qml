import QtQuick
import QtQuick.Controls

/// Text cell of a table row: selection tint, a hairline under the row and an optional tone for the text.
/// The accent bar marks the selected row in its first column.
Rectangle {
	id: cell

	property alias text: label.text
	property alias label: label
	property bool selected: false
	property bool firstColumn: false
	property bool dimmed: false
	property bool mono: false
	/// "" for normal text, else a Theme tone ("warning", "error", …).
	property string tone: ""

	implicitHeight: Theme.rowHeight
	implicitWidth: label.implicitWidth + 20
	color: cell.selected ? Theme.selection : Theme.panel

	Rectangle {
		visible: cell.selected && cell.firstColumn
		width: 2
		height: parent.height
		color: Theme.accent
	}

	Label {
		id: label
		anchors.fill: parent
		anchors.leftMargin: 10
		anchors.rightMargin: 6
		verticalAlignment: Text.AlignVCenter
		elide: Text.ElideRight
		font.family: cell.mono ? Theme.monoFamily : Qt.application.font.family
		font.pointSize: cell.mono ? Qt.application.font.pointSize - 0.5 : Qt.application.font.pointSize
		color: cell.dimmed ? Theme.textDisabled
			: cell.tone !== "" ? Theme.toneColor(cell.tone)
			: cell.selected ? Theme.selectionText : Theme.text
	}

	Rectangle {
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.bottom: parent.bottom
		height: 1
		color: Theme.border
		opacity: 0.6
	}
}
