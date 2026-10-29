pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kestrel.Controls
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

		PanelToolBar {
			Layout.fillWidth: true
			spacing: 6

			Label {
				objectName: "problems.summary"
				Layout.leftMargin: 4
				text: root.vm.summary
				color: Theme.textSecondary
			}
		}

		HorizontalHeaderView {
			id: header
			objectName: "problems.header"
			Layout.fillWidth: true
			syncView: table
			clip: true
			delegate: HeaderCell {}
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

			delegate: TableCell {
				id: cell

				required property string display
				required property string severity
				required property int row
				required property int column
				required property bool current

				selected: cell.row === table.currentRow
				firstColumn: cell.column === 0
				mono: cell.column === 1 || cell.column === 3
				tone: cell.column === 0 ? (cell.severity === "error" ? "error" : cell.severity === "warning" ? "warning" : "info") : ""
				text: cell.display
				label.leftPadding: cell.column === 0 ? 14 : 0
				label.font.weight: cell.column === 0 ? Font.Medium : Font.Normal
				Accessible.name: cell.display

				// Severity dot in the first column.
				Rectangle {
					visible: cell.column === 0
					x: 10
					anchors.verticalCenter: parent.verticalCenter
					width: 7
					height: 7
					radius: 3.5
					color: Theme.toneColor(cell.tone)
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
