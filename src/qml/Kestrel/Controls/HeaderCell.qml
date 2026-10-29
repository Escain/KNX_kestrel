import QtQuick
import QtQuick.Controls

/// Delegate of a HorizontalHeaderView: small semibold column title on the header background.
Rectangle {
	id: cell

	required property var model
	property string textRole: "display"
	property int horizontalAlignment: Text.AlignLeft

	implicitWidth: label.implicitWidth + 20
	implicitHeight: Theme.headerHeight
	color: Theme.panelAlt

	Label {
		id: label
		anchors.fill: parent
		anchors.leftMargin: 10
		anchors.rightMargin: 6
		verticalAlignment: Text.AlignVCenter
		horizontalAlignment: cell.horizontalAlignment
		elide: Text.ElideRight
		text: String(cell.model[cell.textRole] ?? "")
		font.pixelSize: 12
		font.weight: Font.DemiBold
		color: Theme.textSecondary
	}

	Rectangle {
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.bottom: parent.bottom
		height: 1
		color: Theme.border
	}
}
