pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kestrel.ViewModels

/// Welcome tab, shown when no editor is open.
Pane {
	id: root

	required property WelcomeVM vm

	objectName: "welcome"

	ColumnLayout {
		anchors.centerIn: parent
		width: Math.min(560, root.availableWidth - 48)
		spacing: 12

		Label {
			text: "Kestrel"
			font.pixelSize: 32
			font.bold: true
		}
		Label {
			Layout.fillWidth: true
			text: qsTr("Open configuration tool for KNX installations")
			opacity: 0.7
			wrapMode: Text.Wrap
		}
		Label {
			objectName: "welcome.project"
			Layout.fillWidth: true
			text: qsTr("Current project: %1").arg(root.vm.session.title)
			elide: Text.ElideMiddle
		}

		RowLayout {
			spacing: 8

			Button {
				objectName: "welcome.new"
				Accessible.name: text
				text: qsTr("New Project")
				enabled: root.vm.actions.revision >= 0 && root.vm.actions.isEnabled("file.new")
				onClicked: root.vm.actions.trigger("file.new")
			}
			Button {
				objectName: "welcome.open"
				Accessible.name: text
				text: qsTr("Open Project…")
				enabled: root.vm.actions.revision >= 0 && root.vm.actions.isEnabled("file.open")
				onClicked: root.vm.actions.trigger("file.open")
			}
		}

		Label {
			visible: root.vm.recentProjects.length > 0
			text: qsTr("Recent projects")
			font.bold: true
		}
		Repeater {
			model: root.vm.recentProjects
			delegate: ItemDelegate {
				required property string modelData
				objectName: "welcome.recent"
				Accessible.name: modelData
				Layout.fillWidth: true
				text: modelData
				onClicked: root.vm.openRecent(modelData)
			}
		}

		Label {
			Layout.fillWidth: true
			Layout.topMargin: 12
			text: qsTr("Start in the Topology panel: add an area and a line. Import the manufacturers' product files in the Catalog panel and add products to the selected line (or drag them onto it). Open a device (double-click) to set its parameters and link its group objects to group addresses (drag them from the Group Addresses panel). Then connect to the bus to program addresses and download.")
			opacity: 0.7
			wrapMode: Text.Wrap
		}
	}
}
