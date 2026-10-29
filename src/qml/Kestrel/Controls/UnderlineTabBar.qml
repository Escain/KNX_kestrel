import QtQuick
import QtQuick.Controls

/// Tab bar drawn as labels with an accent underline (use UnderlineTabButton inside).
TabBar {
	id: bar

	spacing: 18
	leftPadding: 16
	background: Rectangle {
		color: Theme.panel
		Rectangle {
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.bottom: parent.bottom
			height: 1
			color: Theme.border
		}
	}
}
