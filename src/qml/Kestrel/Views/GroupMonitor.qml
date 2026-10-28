pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kestrel.ViewModels
import Kestrel.Controls

/// Group monitor (central tab, FR-BUS-5/6). Plain Text delegates and fixed row height: the QQuickWidget
/// renders on the GUI thread, so cells stay cheap.
Pane {
	id: root

	required property GroupMonitorVM vm

	objectName: "groupMonitor"
	padding: 6

	ColumnLayout {
		anchors.fill: parent
		spacing: 6

		RowLayout {
			Layout.fillWidth: true
			spacing: 6

			Button {
				objectName: "monitor.pause"
				Accessible.name: text
				text: root.vm.paused ? qsTr("Resume") : qsTr("Pause")
				onClicked: root.vm.paused = !root.vm.paused
			}
			Button {
				objectName: "monitor.clear"
				Accessible.name: text
				text: qsTr("Clear")
				onClicked: root.vm.clear()
			}
			CheckBox {
				id: follow
				objectName: "monitor.follow"
				Accessible.name: text
				text: qsTr("Follow")
				checked: true
			}
			TextField {
				objectName: "monitor.filter"
				Accessible.name: qsTr("Filter telegrams")
				Layout.fillWidth: true
				placeholderText: qsTr("Filter by address, source or name")
				onTextChanged: root.vm.filter = text
			}
			Label {
				objectName: "monitor.count"
				text: root.vm.paused ? qsTr("%1 telegrams (%2 waiting)").arg(root.vm.count).arg(root.vm.pending)
					: qsTr("%1 telegrams").arg(root.vm.count)
				opacity: 0.8
			}
		}

		Label {
			visible: !root.vm.connected
			text: qsTr("Not connected: connect in the Bus Connection panel to see telegrams.")
			opacity: 0.7
		}

		HorizontalHeaderView {
			objectName: "monitor.header"
			Layout.fillWidth: true
			syncView: table
			clip: true
		}

		TableView {
			id: table
			objectName: "monitor.table"
			Accessible.name: qsTr("Telegrams")
			Accessible.role: Accessible.Table
			Layout.fillWidth: true
			Layout.fillHeight: true
			clip: true
			reuseItems: true
			boundsBehavior: Flickable.StopAtBounds
			model: root.vm.model
			// Fixed columns; the GA name takes the rest.
			columnWidthProvider: column => {
				const widths = [90, 60, 80, 0, 130, 60, 100]
				return column === 3 ? Math.max(80, table.width - 520) : widths[column]
			}
			onWidthChanged: forceLayout()
			onRowsChanged: {
				if (follow.checked && rows > 0)
					Qt.callLater(() => table.positionViewAtRow(table.rows - 1, TableView.Contain))
			}

			ScrollBar.vertical: ScrollBar {}

			delegate: Rectangle {
				id: cell

				required property string display
				required property bool outgoing
				required property int row

				implicitHeight: 22
				color: cell.row % 2 ? table.palette.alternateBase : table.palette.base

				Text {
					anchors.fill: parent
					anchors.leftMargin: 6
					anchors.rightMargin: 6
					verticalAlignment: Text.AlignVCenter
					elide: Text.ElideRight
					text: cell.display
					color: cell.outgoing ? "#1565c0" : table.palette.text
				}
			}
		}

		RowLayout {
			Layout.fillWidth: true
			spacing: 6
			enabled: root.vm.connected

			TextField {
				id: gaField
				objectName: "monitor.groupAddress"
				Accessible.name: qsTr("Group address")
				implicitWidth: 110
				placeholderText: qsTr("1/2/3")
				onEditingFinished: dptField.placeholderText = root.vm.dptOf(text) || qsTr("DPT")
			}
			TextField {
				id: dptField
				objectName: "monitor.dpt"
				Accessible.name: qsTr("Datapoint type")
				implicitWidth: 100
				placeholderText: qsTr("DPT")
			}
			TextField {
				id: valueField
				objectName: "monitor.value"
				Accessible.name: qsTr("Value")
				Layout.fillWidth: true
				placeholderText: qsTr("Value, e.g. on, 21.5, 50 %")
				onAccepted: root.vm.write(gaField.text, text, dptField.text)
			}
			Button {
				objectName: "monitor.write"
				Accessible.name: text
				text: qsTr("Write")
				onClicked: root.vm.write(gaField.text, valueField.text, dptField.text)
			}
			Button {
				objectName: "monitor.read"
				Accessible.name: text
				text: qsTr("Read")
				onClicked: root.vm.read(gaField.text, dptField.text)
			}
			Label {
				objectName: "monitor.lastRead"
				text: root.vm.lastRead
			}
		}

		ErrorBanner {
			Layout.fillWidth: true
			vm: root.vm
		}
	}
}
