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

		PanelToolBar {
			Layout.fillWidth: true
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
				placeholderText: qsTr("Search: brand, product, order number…")
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
				Layout.rightMargin: 6
				horizontalAlignment: Text.AlignRight
				elide: Text.ElideLeft
				color: Theme.textSecondary
				text: root.vm.target !== "" ? qsTr("Adds to line %1").arg(root.vm.target)
					: qsTr("Select a line in Topology to add products")
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
			visible: productsArea.visible
			syncView: table
			clip: true
			delegate: HeaderCell {}
		}

		Item {
			id: productsArea
			Layout.fillWidth: true
			Layout.fillHeight: true
			Layout.minimumHeight: 60
			// Nothing imported matches but the index does: the space goes to the products to download.
			visible: !(table.rows === 0 && indexSection.visible && !root.vm.busy)

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

				delegate: Item {
					id: cell

					required property string display
					required property string key
					required property int row
					required property int column
					required property bool selected

					implicitHeight: Theme.rowHeight
					implicitWidth: cellView.implicitWidth
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

					TableCell {
						id: cellView
						anchors.fill: parent
						text: cell.display
						selected: cell.selected
						firstColumn: cell.column === 0
						// Order number and version.
						mono: cell.column === 1 || cell.column === 3
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
				color: Theme.textSecondary
				text: root.vm.busy ? qsTr("Reading product files…")
					: root.vm.productCount === 0 ? qsTr("No products yet. Download one below, or use “Import Product File…” for a .knxprod you have.")
					: qsTr("No product matches the filter.")
			}
		}

		// Products of the product index matching the filter: their files are downloaded from the manufacturer.
		ColumnLayout {
			id: indexSection
			objectName: "catalog.index"
			Layout.fillWidth: true
			Layout.fillHeight: !productsArea.visible
			// Rows are 48 px; at most about half the panel.
			Layout.preferredHeight: Math.min(root.vm.indexCount * 48 + indexHeading.implicitHeight + 13,
				Math.max(120, root.height * 0.5))
			visible: root.vm.indexCount > 0 && (root.vm.filter !== "" || root.vm.productCount === 0)
			spacing: 0

			Rectangle {
				Layout.fillWidth: true
				implicitHeight: 1
				color: Theme.border
			}
			SectionLabel {
				id: indexHeading
				Layout.leftMargin: 10
				Layout.topMargin: 8
				Layout.bottomMargin: 4
				text: qsTr("From manufacturers (%n)", "", root.vm.indexCount)
			}
			ListView {
				id: indexList
				objectName: "catalog.indexList"
				Accessible.name: qsTr("Products to download")
				Accessible.role: Accessible.List
				Layout.fillWidth: true
				Layout.fillHeight: true
				Layout.minimumHeight: 48
				clip: true
				boundsBehavior: Flickable.StopAtBounds
				model: root.vm.indexModel
				ScrollBar.vertical: ScrollBar {}

				delegate: Rectangle {
					id: entry

					required property int index
					required property string product
					required property string manufacturer
					required property string orderNumber
					required property string category
					required property string mask
					required property bool deviceDownload
					required property string state
					required property real progress

					width: indexList.width
					implicitHeight: 48
					color: Theme.panel
					Accessible.name: entry.manufacturer + " " + entry.product

					Rectangle {
						anchors.left: parent.left
						anchors.right: parent.right
						anchors.bottom: parent.bottom
						height: 1
						color: Theme.border
						opacity: 0.6
					}

					RowLayout {
						anchors.fill: parent
						anchors.leftMargin: 10
						anchors.rightMargin: 10
						spacing: 10

						ColumnLayout {
							Layout.fillWidth: true
							spacing: 1

							Label {
								Layout.fillWidth: true
								text: entry.product
								font.weight: Font.Medium
								elide: Text.ElideRight
							}
							Label {
								Layout.fillWidth: true
								text: [entry.manufacturer, entry.orderNumber, entry.category].filter(t => t !== "").join("  ·  ")
								font.pixelSize: 12
								color: Theme.textSecondary
								elide: Text.ElideRight
							}
						}
						Badge {
							// Device family of the mask version.
							readonly property var families: ({ "MV-0010": "System 1", "MV-0011": "System 1", "MV-0012": "System 1",
									"MV-0020": "System 2", "MV-0021": "System 2", "MV-0025": "System 2",
									"MV-0701": "System 7", "MV-0705": "System 7", "MV-07B0": "System B",
									"MV-091A": "Coupler", "MV-57B0": "KNX IP" })
							visible: entry.mask !== ""
							text: families[entry.mask] ?? entry.mask.replace("MV-", "")
							tone: entry.deviceDownload ? "info" : "neutral"
							ToolTip.visible: maskHover.hovered
							ToolTip.text: entry.deviceDownload
								? qsTr("Kestrel can configure and download this device (mask %1).").arg(entry.mask)
								: qsTr("Kestrel can configure this device; downloading to mask %1 is not supported yet.").arg(entry.mask)

							HoverHandler {
								id: maskHover
							}
						}
						Badge {
							visible: entry.state === "imported"
							text: qsTr("In catalog")
							tone: "success"
						}
						ProgressBar {
							visible: entry.state === "downloading"
							Layout.preferredWidth: 80
							from: 0
							to: 1
							value: entry.progress
							indeterminate: entry.progress <= 0
						}
						Button {
							objectName: "catalog.index.cancel"
							Accessible.name: qsTr("Cancel the download")
							visible: entry.state === "downloading"
							text: qsTr("Cancel")
							onClicked: root.vm.cancelDownload()
						}
						Button {
							objectName: "catalog.index.download"
							Accessible.name: qsTr("Download %1").arg(entry.orderNumber)
							visible: entry.state === "available" || entry.state === "failed"
							enabled: !root.vm.downloading
							highlighted: entry.state === "available"
							text: entry.state === "failed" ? qsTr("Retry") : qsTr("Download")
							onClicked: root.vm.downloadIndexed(entry.index)
						}
					}
				}
			}
		}

		Rectangle {
			Layout.fillWidth: true
			implicitHeight: statusLabel.implicitHeight + 12
			color: Theme.panelAlt

			Rectangle {
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.top: parent.top
				height: 1
				color: Theme.border
			}

			Label {
				id: statusLabel
				objectName: "catalog.status"
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.verticalCenter: parent.verticalCenter
				anchors.leftMargin: 10
				anchors.rightMargin: 10
				text: root.vm.status
				elide: Text.ElideRight
				font.pixelSize: 12
				color: Theme.textSecondary
				ToolTip.visible: statusHover.hovered && root.vm.problems !== ""
				ToolTip.text: root.vm.problems

				HoverHandler {
					id: statusHover
				}
			}
		}
	}
}
