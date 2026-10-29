import QtQuick
import QtQuick.Controls

/// Tab of an UnderlineTabBar; `count` adds a small count badge.
TabButton {
	id: tab

	property string count: ""

	width: implicitWidth
	padding: 0
	implicitHeight: 36
	implicitWidth: row.implicitWidth
	Accessible.name: text

	contentItem: Row {
		id: row
		spacing: 6

		Label {
			anchors.verticalCenter: parent.verticalCenter
			text: tab.text
			font.weight: tab.checked ? Font.DemiBold : Font.Medium
			color: tab.checked ? Theme.text : Theme.textSecondary
		}
		Badge {
			anchors.verticalCenter: parent.verticalCenter
			visible: tab.count !== ""
			text: tab.count
			tone: tab.checked ? "info" : "neutral"
			implicitHeight: 18
		}
	}

	background: Item {
		Rectangle {
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.bottom: parent.bottom
			height: 2
			color: tab.checked ? Theme.accent : "transparent"
		}
	}
}
