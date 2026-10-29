import QtQuick
import QtQuick.Layouts

/// Flat strip at the top of a panel for its actions and filters; children go into a row.
Rectangle {
	id: bar

	default property alias content: row.data
	property alias spacing: row.spacing

	implicitHeight: Math.max(Theme.toolBarHeight, row.implicitHeight + 8)
	color: Theme.panel

	RowLayout {
		id: row
		anchors.fill: parent
		anchors.leftMargin: 6
		anchors.rightMargin: 6
		spacing: 2
	}

	Rectangle {
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.bottom: parent.bottom
		height: 1
		color: Theme.border
	}
}
