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
	padding: Theme.pad

	// Two columns in a wide (bottom) dock, one column in a narrow (side) dock.
	GridLayout {
		anchors.fill: parent
		columns: root.width > 560 ? 2 : 1
		columnSpacing: 24
		rowSpacing: 8

		ColumnLayout {
			Layout.fillWidth: true
			Layout.alignment: Qt.AlignTop
			Layout.preferredWidth: 1
			spacing: 8

			SectionLabel {
				text: qsTr("Interface")
			}
			Rectangle {
				readonly property string tone: root.vm.connected ? "success" : root.vm.connecting ? "warning" : "neutral"

				id: statePill
				Layout.fillWidth: true
				implicitHeight: 32
				radius: 16
				color: Theme.toneBackground(statePill.tone)

				RowLayout {
					anchors.fill: parent
					anchors.leftMargin: 12
					anchors.rightMargin: 12
					spacing: 8

					Rectangle {
						implicitWidth: 8
						implicitHeight: 8
						radius: 4
						color: root.vm.connected || root.vm.connecting ? Theme.toneColor(statePill.tone) : Theme.textDisabled
					}
					Label {
						objectName: "bus.state"
						Layout.fillWidth: true
						text: root.vm.connected ? qsTr("%1 to %2:%3 as %4").arg(root.vm.state).arg(root.vm.host).arg(root.vm.port).arg(root.vm.localAddress)
							: root.vm.state
						font.weight: Font.Medium
						color: Theme.toneColor(statePill.tone)
						elide: Text.ElideRight
					}
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
					highlighted: !root.vm.connected && !root.vm.connecting
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
			spacing: 8

			RowLayout {
				Layout.fillWidth: true

				SectionLabel {
					Layout.fillWidth: true
					text: qsTr("Interfaces on the network")
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
				topMargin: 1
				bottomMargin: 1

				Rectangle {
					parent: interfaces
					anchors.fill: parent
					z: -1
					radius: 8
					color: Theme.panel
					border.color: Theme.border
				}

				delegate: ItemDelegate {
					id: entry

					required property var modelData
					required property int index

					objectName: "bus.interface"
					Accessible.name: text
					width: interfaces.width
					leftPadding: 12
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
					color: Theme.textSecondary
				}
			}
		}
	}
}
