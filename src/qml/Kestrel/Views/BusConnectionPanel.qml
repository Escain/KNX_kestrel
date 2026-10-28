pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kestrel.ViewModels
import Kestrel.Controls

/// Bus connection dock (FR-BUS-1/2): manual endpoint, connect/disconnect and interface discovery.
/// Commissioning is in the Commissioning dock.
Pane {
	id: root

	required property BusConnectionVM vm

	objectName: "busConnectionPanel"
	padding: 8

	// Two columns in a wide (bottom) dock, one column in a narrow (side) dock.
	GridLayout {
		anchors.fill: parent
		columns: root.width > 560 ? 2 : 1
		columnSpacing: 16
		rowSpacing: 6

		ColumnLayout {
			Layout.fillWidth: true
			Layout.alignment: Qt.AlignTop
			Layout.preferredWidth: 1
			spacing: 6

			RowLayout {
				Layout.fillWidth: true
				spacing: 6

				Rectangle {
					implicitWidth: 10
					implicitHeight: 10
					radius: 5
					color: root.vm.connected ? "#2e7d32" : root.vm.connecting ? "#ef6c00" : "#9e9e9e"
				}
				Label {
					objectName: "bus.state"
					Layout.fillWidth: true
					text: root.vm.connected ? qsTr("%1 to %2:%3 as %4").arg(root.vm.state).arg(root.vm.host).arg(root.vm.port).arg(root.vm.localAddress)
						: root.vm.state
					elide: Text.ElideRight
				}
			}

			RowLayout {
				Layout.fillWidth: true
				spacing: 6

				TextField {
					id: hostField
					objectName: "bus.host"
					Accessible.name: qsTr("Interface host")
					Layout.fillWidth: true
					enabled: !root.vm.connected && !root.vm.connecting
					text: root.vm.host
					placeholderText: qsTr("Host")
					onEditingFinished: root.vm.host = text
				}
				TextField {
					id: portField
					objectName: "bus.port"
					Accessible.name: qsTr("Interface port")
					implicitWidth: 70
					enabled: !root.vm.connected && !root.vm.connecting
					text: root.vm.port
					validator: IntValidator {
						bottom: 1
						top: 65535
					}
					onEditingFinished: root.vm.port = parseInt(text)
				}
				Button {
					objectName: "bus.connect"
					Accessible.name: text
					text: root.vm.connected || root.vm.connecting ? qsTr("Disconnect") : qsTr("Connect")
					onClicked: {
						if (root.vm.connected || root.vm.connecting) {
							root.vm.disconnectFromBus()
						} else {
							root.vm.host = hostField.text
							root.vm.port = parseInt(portField.text)
							root.vm.connectToBus()
						}
					}
				}
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
			Layout.preferredWidth: 1
			spacing: 6

			RowLayout {
				Layout.fillWidth: true

				Label {
					Layout.fillWidth: true
					text: qsTr("Interfaces on the network")
					font.bold: true
				}
				Button {
					objectName: "bus.discover"
					Accessible.name: text
					text: root.vm.discovering ? qsTr("Searching…") : qsTr("Search")
					enabled: !root.vm.discovering
					onClicked: root.vm.discover()
				}
			}

			ListView {
				id: interfaces
				objectName: "bus.interfaces"
				Accessible.name: qsTr("Discovered interfaces")
				Accessible.role: Accessible.List
				Layout.fillWidth: true
				Layout.fillHeight: true
				Layout.minimumHeight: 40
				clip: true
				model: root.vm.interfaces
				delegate: ItemDelegate {
					id: entry

					required property var modelData
					required property int index

					objectName: "bus.interface"
					Accessible.name: text
					width: interfaces.width
					text: entry.modelData.name + "  —  " + entry.modelData.host + ":" + entry.modelData.port
						+ (entry.modelData.tunnelling ? "" : qsTr("  (no tunnelling)"))
					ToolTip.visible: hovered
					ToolTip.text: qsTr("Double-click to connect")
					onClicked: {
						root.vm.host = entry.modelData.host
						root.vm.port = entry.modelData.port
					}
					onDoubleClicked: root.vm.connectToInterface(entry.index)
				}

				Label {
					anchors.centerIn: parent
					width: parent.width - 16
					horizontalAlignment: Text.AlignHCenter
					wrapMode: Text.Wrap
					visible: interfaces.count === 0
					text: root.vm.discovering ? qsTr("Searching…")
						: root.vm.discovered ? qsTr("No KNXnet/IP interface answered. Enter its address on the left.")
						: qsTr("Search for KNXnet/IP interfaces on the local network")
					opacity: 0.6
				}
			}
		}
	}
}
