pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kestrel.ViewModels
import Kestrel.Controls

/// Catalog dock (FR-CAT-1/2/3): imported products, filter, import and "Add to line".
/// Rows drag onto a line or device of the Topology dock.
Pane {
	id: root

	required property CatalogPanelVM vm

	objectName: "catalogPanel"
	padding: 0

	// The dock is shown for the first time: read the catalog folder (lazy).
	Component.onCompleted: root.vm.activate()

	ColumnLayout {
		anchors.fill: parent
		spacing: 0

		ToolBar {
			Layout.fillWidth: true

			RowLayout {
				anchors.left: parent.left
				anchors.right: parent.right
				spacing: 4

				ActionButton {
					actions: root.vm.actions
					actionId: "catalog.import"
				}
				ActionButton {
					actions: root.vm.actions
					actionId: "catalog.addToLine"
				}
				TextField {
					id: filterField
					objectName: "catalog.filter"
					Accessible.name: qsTr("Filter products")
					Layout.fillWidth: true
					Layout.maximumWidth: 360
					placeholderText: qsTr("Filter: product, order number, application…")
					text: root.vm.filter
					onTextEdited: root.vm.filter = text
				}
				BusyIndicator {
					objectName: "catalog.busy"
					Layout.preferredWidth: 22
					Layout.preferredHeight: 22
					running: root.vm.busy
					visible: root.vm.busy
				}
				Label {
					objectName: "catalog.target"
					Layout.fillWidth: true
					horizontalAlignment: Text.AlignRight
					elide: Text.ElideLeft
					opacity: 0.8
					text: root.vm.target !== "" ? qsTr("Adds to line %1").arg(root.vm.target)
						: qsTr("Select a line in Topology to add products")
				}
			}
		}

		ErrorBanner {
			Layout.fillWidth: true
			vm: root.vm
		}

		HorizontalHeaderView {
			id: header
			objectName: "catalog.header"
			Layout.fillWidth: true
			syncView: table
			clip: true
		}

		Item {
			Layout.fillWidth: true
			Layout.fillHeight: true
			Layout.minimumHeight: 60

			TableView {
				id: table
				objectName: "catalog.products"
				Accessible.name: qsTr("Products")
				Accessible.role: Accessible.Table
				anchors.fill: parent
				clip: true
				focus: true
				boundsBehavior: Flickable.StopAtBounds
				model: root.vm.model
				selectionBehavior: TableView.SelectRows
				selectionMode: TableView.SingleSelection
				selectionModel: ItemSelectionModel {
					onCurrentChanged: (current, previous) => root.vm.setCurrentRow(current.row)
				}
				// Product and application share the width; order number, version and manufacturer are fixed.
				columnWidthProvider: column => {
					const fixed = [0, 110, 0, 60, 80]
					const share = [0.55, 0, 0.45, 0, 0]
					const rest = Math.max(240, table.width - 250)
					return fixed[column] > 0 ? fixed[column] : Math.floor(rest * share[column])
				}
				onWidthChanged: forceLayout()

				ScrollBar.vertical: ScrollBar {}

				Keys.onReturnPressed: root.vm.addToLine()
				Keys.onEnterPressed: root.vm.addToLine()

				delegate: Rectangle {
					id: cell

					required property string display
					required property string key
					required property int row
					required property bool selected

					implicitHeight: label.implicitHeight + 10
					color: cell.selected ? table.palette.highlight : (cell.row % 2 ? table.palette.alternateBase : table.palette.base)
					Accessible.name: cell.display

					// Drag.Automatic runs a platform drag, so the Topology dock (another QQuickWidget) receives it.
					Drag.dragType: Drag.Automatic
					Drag.supportedActions: Qt.CopyAction
					Drag.mimeData: ({ "application/x-kestrel-catalog-entry": cell.key, "text/plain": cell.display })
					Drag.active: dragHandler.active

					DragHandler {
						id: dragHandler
						target: null
						grabPermissions: PointerHandler.CanTakeOverFromAnything
					}

					Label {
						id: label
						anchors.fill: parent
						anchors.leftMargin: 6
						anchors.rightMargin: 6
						verticalAlignment: Text.AlignVCenter
						elide: Text.ElideRight
						text: cell.display
						color: cell.selected ? table.palette.highlightedText : table.palette.text
					}
				}

				TapHandler {
					acceptedButtons: Qt.LeftButton
					onDoubleTapped: eventPoint => {
						const cell = table.cellAtPosition(table.contentItem.mapFromItem(table, eventPoint.position), true)
						if (cell.y >= 0)
							root.vm.addToLine(root.vm.keyAt(cell.y))
					}
				}
			}

			Label {
				anchors.centerIn: table
				width: table.width - 32
				visible: table.rows === 0
				horizontalAlignment: Text.AlignHCenter
				wrapMode: Text.Wrap
				opacity: 0.6
				text: root.vm.busy ? qsTr("Reading product files…")
					: root.vm.productCount === 0 ? qsTr("No products yet. Use “Import Product File…” to add a manufacturer's .knxprod file.")
					: qsTr("No product matches the filter.")
			}
		}

		Label {
			objectName: "catalog.status"
			Layout.fillWidth: true
			Layout.margins: 4
			text: root.vm.status
			elide: Text.ElideRight
			opacity: 0.8
			ToolTip.visible: statusHover.hovered && root.vm.problems !== ""
			ToolTip.text: root.vm.problems

			HoverHandler {
				id: statusHover
			}
		}
	}
}
