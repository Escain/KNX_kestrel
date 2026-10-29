import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kestrel.ViewModels

/// Shows the panel's last error until dismissed.
Frame {
	id: banner

	required property PanelVM vm

	visible: vm.lastError !== ""
	padding: 6
	leftPadding: 12
	background: Rectangle {
		radius: Theme.radius
		color: Theme.errorBg
	}

	RowLayout {
		anchors.fill: parent
		spacing: 6

		Label {
			objectName: "errorBanner.text"
			Layout.fillWidth: true
			text: banner.vm.lastError
			color: Theme.error
			wrapMode: Text.Wrap
		}
		ToolButton {
			objectName: "errorBanner.dismiss"
			text: "✕"
			focusPolicy: Qt.NoFocus
			Accessible.name: qsTr("Dismiss error")
			onClicked: banner.vm.clearError()
		}
	}
}
