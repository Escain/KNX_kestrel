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
	padding: 8

	GridLayout {
		anchors.fill: parent
		columns: root.width > 720 ? 2 : 1
		columnSpacing: 16
		rowSpacing: 6

		ColumnLayout {
			Layout.fillWidth: true
			Layout.fillHeight: true
			Layout.preferredWidth: 3
			Layout.alignment: Qt.AlignTop
			spacing: 6

			Label {
				objectName: "commissioning.device"
				Layout.fillWidth: true
				font.bold: true
				elide: Text.ElideRight
				text: root.vm.deviceLabel !== "" ? root.vm.deviceLabel : qsTr("Select a device in Topology or Buildings")
			}
			Label {
				objectName: "commissioning.deviceStatus"
				Layout.fillWidth: true
				visible: text !== ""
				text: root.vm.deviceStatus
				opacity: 0.8
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
				spacing: 6

				Label {
					Layout.fillWidth: true
					text: qsTr("Not connected to the bus.")
					opacity: 0.8
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

				ColumnLayout {
					anchors.left: parent.left
					anchors.right: parent.right
					spacing: 6

					Label {
						objectName: "commissioning.button"
						Layout.fillWidth: true
						visible: root.vm.waitingForButton
						text: qsTr("Press the programming button of the device now.")
						font.bold: true
						font.pixelSize: 16
						color: "#ef6c00"
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
				color: root.vm.lastSucceeded ? "#2e7d32" : "#c62828"
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

				Label {
					Layout.fillWidth: true
					text: qsTr("Log")
					font.bold: true
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
				delegate: Label {
					required property string modelData
					width: logView.width
					text: modelData
					elide: Text.ElideRight
					font.family: "monospace"
				}
				ScrollBar.vertical: ScrollBar {}

				Label {
					anchors.centerIn: parent
					visible: logView.count === 0
					text: qsTr("No bus operation yet")
					opacity: 0.6
				}
			}
		}
	}
}
