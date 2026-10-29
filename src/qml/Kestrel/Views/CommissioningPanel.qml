pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kestrel.ViewModels
import Kestrel.Controls

/// Commissioning dock (FR-COM-1/2/4): bus operations on the selected device, progress, cancel, result and log.
Pane {
	id: root

	required property CommissioningVM vm

	objectName: "commissioningPanel"
	padding: Theme.pad

	GridLayout {
		anchors.fill: parent
		columns: root.width > 720 ? 2 : 1
		columnSpacing: 24
		rowSpacing: 8

		ColumnLayout {
			Layout.fillWidth: true
			Layout.fillHeight: true
			Layout.preferredWidth: 3
			Layout.alignment: Qt.AlignTop
			spacing: 8

			SectionLabel {
				text: qsTr("Device")
			}
			Label {
				objectName: "commissioning.device"
				Layout.fillWidth: true
				font.weight: Font.DemiBold
				font.pixelSize: 15
				elide: Text.ElideRight
				text: root.vm.deviceLabel !== "" ? root.vm.deviceLabel : qsTr("Select a device in Topology or Buildings")
			}
			Label {
				objectName: "commissioning.deviceStatus"
				Layout.fillWidth: true
				visible: text !== ""
				text: root.vm.deviceStatus
				color: Theme.textSecondary
				elide: Text.ElideRight
			}

			Flow {
				Layout.fillWidth: true
				spacing: 2

				Repeater {
					model: ["device.programAddress", "device.download", "device.readConfiguration", "device.readInfo",
						"bus.programmingMode"]
					delegate: ActionButton {
						required property string modelData
						actions: root.vm.actions
						actionId: modelData
					}
				}
			}

			RowLayout {
				Layout.fillWidth: true
				visible: !root.vm.connected
				spacing: 8

				Badge {
					tone: "neutral"
					text: qsTr("Not connected to the bus")
				}
				Item {
					Layout.fillWidth: true
				}
				ActionButton {
					actions: root.vm.actions
					actionId: "bus.connect"
				}
			}

			Frame {
				objectName: "commissioning.progress"
				Layout.fillWidth: true
				visible: root.vm.operationActive
				padding: Theme.pad
				background: Rectangle {
					radius: 8
					color: Theme.panelAlt
					border.color: Theme.border
				}

				ColumnLayout {
					anchors.left: parent.left
					anchors.right: parent.right
					spacing: 6

					Label {
						objectName: "commissioning.button"
						Layout.fillWidth: true
						visible: root.vm.waitingForButton
						text: qsTr("Press the programming button of the device now.")
						font.weight: Font.DemiBold
						font.pixelSize: 15
						color: Theme.warning
						wrapMode: Text.Wrap
					}
					Label {
						objectName: "commissioning.operation"
						Layout.fillWidth: true
						text: root.vm.operation
						wrapMode: Text.Wrap
					}
					RowLayout {
						Layout.fillWidth: true

						ProgressBar {
							objectName: "commissioning.progressBar"
							Layout.fillWidth: true
							indeterminate: root.vm.progressTotal === 0
							from: 0
							to: Math.max(1, root.vm.progressTotal)
							value: root.vm.progressValue
						}
						Label {
							visible: root.vm.progressTotal > 0
							text: qsTr("%1 / %2").arg(root.vm.progressValue).arg(root.vm.progressTotal)
						}
						ActionButton {
							actions: root.vm.actions
							actionId: "device.cancelOperation"
						}
					}
				}
			}

			Label {
				objectName: "commissioning.lastResult"
				Layout.fillWidth: true
				visible: !root.vm.operationActive && text !== ""
				text: root.vm.lastResult
				color: root.vm.lastSucceeded ? Theme.success : Theme.error
				font.weight: Font.Medium
				wrapMode: Text.Wrap
			}

			ErrorBanner {
				Layout.fillWidth: true
				vm: root.vm
			}

			Item {
				Layout.fillHeight: true
			}
		}

		ColumnLayout {
			Layout.fillWidth: true
			Layout.fillHeight: true
			Layout.preferredWidth: 2
			spacing: 4

			RowLayout {
				Layout.fillWidth: true

				SectionLabel {
					Layout.fillWidth: true
					text: qsTr("Log")
				}
				ToolButton {
					objectName: "commissioning.clearLog"
					text: qsTr("Clear")
					enabled: root.vm.log.length > 0
					focusPolicy: Qt.NoFocus
					Accessible.name: qsTr("Clear the log")
					onClicked: root.vm.clearLog()
				}
			}
			ListView {
				id: logView
				objectName: "commissioning.log"
				Accessible.name: qsTr("Commissioning log")
				Accessible.role: Accessible.List
				Layout.fillWidth: true
				Layout.fillHeight: true
				Layout.minimumHeight: 40
				clip: true
				model: root.vm.log
				topMargin: 6
				bottomMargin: 6
				leftMargin: 8
				delegate: Label {
					required property string modelData
					width: logView.width - 16
					text: modelData
					elide: Text.ElideRight
					font.family: Theme.monoFamily
					font.pixelSize: 12
					lineHeight: 1.25
				}
				Rectangle {
					parent: logView
					anchors.fill: parent
					z: -1
					radius: 8
					color: Theme.panelAlt
					border.color: Theme.border
				}
				ScrollBar.vertical: ScrollBar {}

				Label {
					anchors.centerIn: parent
					visible: logView.count === 0
					text: qsTr("No bus operation yet")
					color: Theme.textSecondary
				}
			}
		}
	}
}
