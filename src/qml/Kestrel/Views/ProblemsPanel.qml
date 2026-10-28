pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kestrel.ViewModels

/// Problems dock: validation results; click selects the subject, double-click opens it.
Pane {
	id: root

	required property ProblemsPanelVM vm

	objectName: "problemsPanel"
	padding: 0

	ColumnLayout {
		anchors.fill: parent
		spacing: 0

		ToolBar {
			Layout.fillWidth: true

			Label {
				objectName: "problems.summary"
				anchors.verticalCenter: parent.verticalCenter
				x: 8
				text: root.vm.summary
			}
		}

		HorizontalHeaderView {
			id: header
			objectName: "problems.header"
			Layout.fillWidth: true
			syncView: table
			clip: true
		}

		TableView {
			id: table
			objectName: "problems.table"
			Accessible.name: qsTr("Problems")
			Accessible.role: Accessible.Table
			Layout.fillWidth: true
			Layout.fillHeight: true
			clip: true
			boundsBehavior: Flickable.StopAtBounds
			model: root.vm.model
			selectionBehavior: TableView.SelectRows
			selectionMode: TableView.SingleSelection
			selectionModel: ItemSelectionModel {}
			columnWidthProvider: column => {
				const widths = [80, 70, 0, 280]
				return column === 2 ? Math.max(160, table.width - 430) : widths[column]
			}
			onWidthChanged: forceLayout()

			ScrollBar.vertical: ScrollBar {}

			delegate: Rectangle {
				id: cell

				required property string display
				required property string severity
				required property int row
				required property int column
				required property bool selected
				required property bool current

				implicitHeight: label.implicitHeight + 8
				color: cell.selected ? table.palette.highlight : (cell.row % 2 ? table.palette.alternateBase : table.palette.base)
				Accessible.name: cell.display

				Label {
					id: label
					anchors.fill: parent
					anchors.leftMargin: 6
					anchors.rightMargin: 6
					verticalAlignment: Text.AlignVCenter
					elide: Text.ElideRight
					text: cell.column === 0 ? "● " + cell.display : cell.display
					color: cell.selected ? table.palette.highlightedText
						: cell.column !== 0 ? table.palette.text
						: cell.severity === "error" ? "#d32f2f"
						: cell.severity === "warning" ? "#ef6c00"
						: "#1976d2"
				}

				TapHandler {
					onTapped: {
						table.selectionModel.setCurrentIndex(table.index(cell.row, 0),
							ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
						root.vm.navigate(cell.row)
					}
					onDoubleTapped: root.vm.activate(cell.row)
				}
			}
		}
	}
}
