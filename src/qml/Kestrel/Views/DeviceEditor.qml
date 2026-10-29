pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kestrel.ViewModels
import Kestrel.Controls

/// Device editor (central tab): group objects and their group address associations (FR-GA-3, FR-DEV-2).
/// Catalog devices show the active com objects of their application program; manual devices (M1) define
/// group objects by hand. Group addresses can be dragged from the Group Addresses dock onto a row.
Pane {
	id: root

	required property DeviceEditorVM vm

	readonly property bool catalogTable: root.vm.catalogDevice && root.vm.catalogState === "ready"
	readonly property string currentSubject: root.catalogTable ? root.vm.currentObject : root.vm.current
	readonly property string refFormat: "application/x-kestrel-ref"

	objectName: "deviceEditor"
	padding: 0
	background: Rectangle {
		color: Theme.panel
	}

	function linkTyped() {
		const text = gaField.editText.trim()
		const ok = root.catalogTable ? root.vm.linkObject(root.vm.currentObject, text)
			: root.vm.linkGroupAddress(root.vm.current, text)
		if (ok)
			gaField.editText = ""
	}

	ColumnLayout {
		anchors.fill: parent
		spacing: 0

		// Header: name, address and download state; product and application below.
		ColumnLayout {
			Layout.fillWidth: true
			Layout.leftMargin: 20
			Layout.rightMargin: 20
			Layout.topMargin: 16
			Layout.bottomMargin: 12
			spacing: 4

			RowLayout {
				Layout.fillWidth: true
				spacing: 10

				Label {
					objectName: "deviceEditor.title"
					Layout.maximumWidth: Math.max(120, root.width - 360)
					text: root.vm.name !== "" ? root.vm.name : root.vm.title
					font.pixelSize: 18
					font.weight: Font.DemiBold
					elide: Text.ElideRight
				}
				AddressChip {
					visible: root.vm.address !== "" && root.vm.name !== ""
					text: root.vm.address
				}
				Badge {
					objectName: "deviceEditor.downloadState"
					visible: root.vm.downloadState !== ""
					text: root.vm.needsDownload ? qsTr("Needs download") : root.vm.downloadState
					tone: root.vm.needsDownload ? "warning" : "neutral"
					ToolTip.visible: badgeHover.hovered
					ToolTip.text: root.vm.downloadState

					HoverHandler {
						id: badgeHover
					}
				}
				Item {
					Layout.fillWidth: true
				}
			}

			RowLayout {
				objectName: "deviceEditor.product"
				Layout.fillWidth: true
				visible: root.vm.catalogDevice
				spacing: 6

				Label {
					objectName: "deviceEditor.productText"
					Layout.fillWidth: true
					Layout.maximumWidth: implicitWidth
					elide: Text.ElideRight
					color: Theme.textSecondary
					text: root.vm.orderNumber !== "" ? qsTr("%1 (%2)").arg(root.vm.productText).arg(root.vm.orderNumber)
						: root.vm.productText
				}
				Label {
					visible: root.vm.applicationText !== ""
					text: "·"
					color: Theme.textSecondary
				}
				Label {
					objectName: "deviceEditor.applicationText"
					Layout.fillWidth: true
					elide: Text.ElideRight
					color: Theme.textSecondary
					text: root.vm.applicationText
				}
			}
		}

		Rectangle {
			Layout.fillWidth: true
			Layout.leftMargin: 20
			Layout.rightMargin: 20
			Layout.bottomMargin: Theme.gap
			visible: root.vm.catalogDevice && root.vm.inactiveLinkedCount > 0
			implicitHeight: inactiveWarning.implicitHeight + 12
			radius: Theme.radius
			color: Theme.warningBg

			Label {
				id: inactiveWarning
				objectName: "deviceEditor.inactiveWarning"
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.verticalCenter: parent.verticalCenter
				anchors.leftMargin: 10
				anchors.rightMargin: 10
				wrapMode: Text.Wrap
				color: Theme.warning
				text: root.vm.inactiveLinkedCount === 1
					? qsTr("1 linked group object is not active with the current parameters (greyed): its group addresses are kept but not downloaded.")
					: qsTr("%1 linked group objects are not active with the current parameters (greyed): their group addresses are kept but not downloaded.").arg(root.vm.inactiveLinkedCount)
			}
		}

		RowLayout {
			Layout.fillWidth: true
			Layout.leftMargin: 20
			Layout.rightMargin: 20
			Layout.bottomMargin: Theme.gap
			visible: root.vm.catalogDevice && !root.catalogTable
			spacing: 8

			BusyIndicator {
				Layout.preferredWidth: 24
				Layout.preferredHeight: 24
				running: root.vm.catalogState === "loading"
				visible: running
			}
			Label {
				objectName: "deviceEditor.catalogMessage"
				Layout.fillWidth: true
				text: root.vm.catalogMessage
				wrapMode: Text.Wrap
				color: root.vm.catalogState === "loading" ? Theme.textSecondary : Theme.error
			}
		}

		UnderlineTabBar {
			id: tabs
			objectName: "deviceEditor.tabs"
			Layout.fillWidth: true
			leftPadding: 20
			visible: root.catalogTable

			UnderlineTabButton {
				objectName: "deviceEditor.objectsTab"
				text: qsTr("Group Objects")
				count: String(table.rows)
			}
			UnderlineTabButton {
				objectName: "deviceEditor.parametersTab"
				text: qsTr("Parameters")
				count: String(root.vm.parameterCount)
			}
		}

		Rectangle {
			// Manual devices have no tabs: a hairline separates the header from the table.
			Layout.fillWidth: true
			visible: !root.catalogTable
			implicitHeight: 1
			color: Theme.border
		}

		StackLayout {
			Layout.fillWidth: true
			Layout.fillHeight: true
			currentIndex: root.catalogTable ? tabs.currentIndex : 0

			ColumnLayout {
				spacing: 0

				Label {
					Layout.fillWidth: true
					Layout.leftMargin: 20
					Layout.rightMargin: 20
					Layout.topMargin: Theme.gap
					Layout.bottomMargin: Theme.gap
					text: qsTr("Drop a group address from the Group Addresses panel on a row to link it.")
					font.pixelSize: 12
					color: Theme.textSecondary
					elide: Text.ElideRight
				}

				HorizontalHeaderView {
					id: header
					objectName: "deviceEditor.header"
					Layout.fillWidth: true
					syncView: table
					clip: true
					delegate: HeaderCell {}
				}

				Item {
					Layout.fillWidth: true
					Layout.fillHeight: true
					Layout.minimumHeight: 120

					TableView {
						id: table
						objectName: root.catalogTable ? "deviceEditor.catalogComObjects" : "deviceEditor.comObjects"
						Accessible.name: qsTr("Group objects")
						Accessible.role: Accessible.Table
						anchors.fill: parent
						clip: true
						boundsBehavior: Flickable.StopAtBounds
						model: root.catalogTable ? root.vm.catalogComObjects : root.vm.comObjects
						selectionBehavior: TableView.SelectRows
						selectionMode: TableView.SingleSelection
						editTriggers: root.catalogTable ? TableView.NoEditTriggers : (TableView.EditKeyPressed | TableView.DoubleTapped)
						selectionModel: ItemSelectionModel {
							model: root.catalogTable ? root.vm.catalogComObjects : root.vm.comObjects
							onCurrentChanged: (current, previous) => {
								if (root.catalogTable)
									root.vm.setCurrentObjectRow(current.row)
								else
									root.vm.setCurrentIndex(current)
							}
						}

						property int dropRow: -1
						// Catalog: # | Text | Function | Channel | Size | Flags | DPT | Group addresses
						// Manual:  # | Name | Function | Size | Flags | DPT | Group addresses
						readonly property int addressColumn: root.catalogTable ? 7 : 6
						readonly property list<int> monoColumns: root.catalogTable ? [0, 5, 6] : [0, 4, 5]

						columnWidthProvider: column => {
							const fixed = root.catalogTable ? [52, 0, 0, 0, 64, 64, 96, 0] : [52, 0, 0, 70, 64, 96, 0]
							const share = root.catalogTable ? [0, 0.25, 0.2, 0.2, 0, 0, 0, 0.35] : [0, 0.3, 0.3, 0, 0, 0, 0.4]
							const used = fixed.reduce((a, b) => a + b, 0)
							const rest = Math.max(300, table.width - used)
							return fixed[column] > 0 ? fixed[column] : Math.floor(rest * share[column])
						}
						onWidthChanged: forceLayout()
						onModelChanged: forceLayout()

						ScrollBar.vertical: ScrollBar {}

						delegate: Item {
							id: cell

							required property string display
							required property int row
							required property int column
							required property bool selected
							required property bool current
							required property bool editing
							required property bool inactive

							readonly property bool addresses: cell.column === table.addressColumn

							implicitHeight: Theme.rowHeight + 2
							implicitWidth: cellView.implicitWidth
							Accessible.name: cell.display

							TableCell {
								id: cellView
								anchors.fill: parent
								visible: !cell.editing
								text: cell.addresses ? "" : cell.display
								selected: cell.selected || cell.row === table.dropRow
								firstColumn: cell.column === 0
								dimmed: cell.inactive
								mono: table.monoColumns.indexOf(cell.column) >= 0
							}

							// Group addresses as chips, the sending one (listed first) emphasised.
							Row {
								visible: cell.addresses && !cell.editing
								anchors.left: parent.left
								anchors.right: parent.right
								anchors.leftMargin: 10
								anchors.verticalCenter: parent.verticalCenter
								spacing: 4
								clip: true
								opacity: cell.inactive ? 0.5 : 1

								Repeater {
									model: cell.addresses && cell.display !== "" ? cell.display.split(", ") : []
									delegate: AddressChip {
										required property string modelData
										required property int index
										text: modelData
										emphasis: index === 0
									}
								}
							}

							TableView.editDelegate: TextField {
								objectName: "deviceEditor.nameEditor"
								Accessible.name: qsTr("Group object name")
								anchors.fill: parent
								text: cell.display
								Component.onCompleted: selectAll()
								TableView.onCommit: table.model.setData(table.index(cell.row, cell.column), text, Qt.EditRole)
							}
						}
					}

					// Group addresses dragged from the Group Addresses dock (a platform drag between QQuickWidgets).
					DropArea {
						id: dropArea
						objectName: "deviceEditor.dropArea"
						anchors.fill: table
						keys: [root.refFormat]

						function rowAt(x: real, y: real): int {
							const cell = table.cellAtPosition(table.contentItem.mapFromItem(dropArea, x, y), true)
							return cell.y
						}

						onPositionChanged: drag => table.dropRow = dropArea.rowAt(drag.x, drag.y)
						onExited: table.dropRow = -1
						onDropped: drop => {
							const row = dropArea.rowAt(drop.x, drop.y)
							table.dropRow = -1
							const ref = drop.getDataAsString(root.refFormat)
							const ok = root.catalogTable ? root.vm.dropOnObjectRow(row, ref) : root.vm.dropOnComObjectRow(row, ref)
							if (ok)
								drop.accept(Qt.CopyAction)
						}
					}

					Label {
						anchors.centerIn: table
						visible: table.rows === 0 && (!root.vm.catalogDevice || root.catalogTable)
						text: root.catalogTable ? qsTr("The application has no active group objects.") : qsTr("No group objects yet.")
						color: Theme.textSecondary
					}
				}

				// Manual devices: define a new group object.
				Rectangle {
					Layout.fillWidth: true
					visible: !root.vm.catalogDevice
					implicitHeight: newObject.implicitHeight + 2 * Theme.pad
					color: Theme.panel

					Rectangle {
						anchors.left: parent.left
						anchors.right: parent.right
						anchors.top: parent.top
						height: 1
						color: Theme.border
					}

					ColumnLayout {
						id: newObject
						anchors.fill: parent
						anchors.margins: Theme.pad
						anchors.leftMargin: 20
						anchors.rightMargin: 20
						spacing: 6

						SectionLabel {
							text: qsTr("New group object")
						}

						RowLayout {
							Layout.fillWidth: true
							spacing: 6

							TextField {
								id: newName
								objectName: "deviceEditor.newName"
								Accessible.name: qsTr("Name of the new group object")
								Layout.fillWidth: true
								placeholderText: qsTr("Name")
							}
							TextField {
								id: newFunction
								objectName: "deviceEditor.newFunction"
								Accessible.name: qsTr("Function of the new group object")
								Layout.fillWidth: true
								placeholderText: qsTr("Function")
							}
							ComboBox {
								id: newSize
								objectName: "deviceEditor.newSize"
								Accessible.name: qsTr("Size of the new group object")
								model: root.vm.sizeOptions
								textRole: "text"
								valueRole: "value"
							}
							TextField {
								id: newFlags
								objectName: "deviceEditor.newFlags"
								Accessible.name: qsTr("Flags of the new group object")
								implicitWidth: 70
								text: "CWT"
								placeholderText: qsTr("Flags")
							}
							TextField {
								id: newDpt
								objectName: "deviceEditor.newDpt"
								Accessible.name: qsTr("Datapoint type of the new group object")
								implicitWidth: 100
								placeholderText: qsTr("DPT")
							}
							Button {
								objectName: "deviceEditor.add"
								Accessible.name: text
								text: qsTr("Add")
								onClicked: {
									if (root.vm.addComObject(newName.text, newFunction.text, newSize.currentValue, newFlags.text, newDpt.text) !== "") {
										newName.clear()
										newFunction.clear()
									}
								}
							}
						}
					}
				}

				// Link bar: the selected object's group addresses and the address to link.
				Rectangle {
					Layout.fillWidth: true
					implicitHeight: associations.implicitHeight + 2 * Theme.pad - 2
					color: Theme.panelAlt

					Rectangle {
						anchors.left: parent.left
						anchors.right: parent.right
						anchors.top: parent.top
						height: 1
						color: Theme.border
					}

					ColumnLayout {
						id: associations
						anchors.fill: parent
						anchors.topMargin: Theme.pad - 2
						anchors.bottomMargin: Theme.pad - 2
						anchors.leftMargin: 20
						anchors.rightMargin: 20
						spacing: 6

						RowLayout {
							Layout.fillWidth: true
							spacing: 8

							Label {
								Layout.fillWidth: true
								elide: Text.ElideRight
								text: root.currentSubject === "" ? qsTr("Select a group object to link group addresses")
									: root.vm.currentLabel
								font.weight: root.currentSubject === "" ? Font.Normal : Font.Medium
								color: root.currentSubject === "" ? Theme.textSecondary : Theme.text
							}
							ComboBox {
								id: gaField
								objectName: "deviceEditor.groupAddress"
								Accessible.name: qsTr("Group address to link: pick one or type a new address")
								Layout.preferredWidth: Math.min(360, Math.max(220, root.width * 0.35))
								enabled: root.currentSubject !== ""
								editable: true
								model: root.vm.groupAddressOptions
								textRole: "text"
								valueRole: "address"
								currentIndex: -1
								onAccepted: root.linkTyped()
								// The editable combo box's text field (a TextField in Fusion) shows the hint.
								Component.onCompleted: {
									if (contentItem.placeholderText !== undefined)
										contentItem.placeholderText = qsTr("Pick a group address or type one, e.g. 1/2/3 (created if new)")
								}
							}
							Button {
								id: linkButton
								objectName: "deviceEditor.link"
								Accessible.name: text
								text: qsTr("Link")
								enabled: root.currentSubject !== "" && gaField.editText.trim() !== ""
								onClicked: root.linkTyped()
							}
							Button {
								objectName: "deviceEditor.remove"
								Accessible.name: text
								visible: !root.vm.catalogDevice
								enabled: root.currentSubject !== ""
								text: qsTr("Delete Object")
								onClicked: root.vm.removeComObject(root.vm.current)
							}
						}

						Repeater {
							model: root.vm.currentLinks
							delegate: RowLayout {
								id: linkRow

								required property var modelData

								Layout.fillWidth: true
								spacing: 8

								AddressChip {
									text: linkRow.modelData.address
									emphasis: linkRow.modelData.sending
								}
								Label {
									Layout.fillWidth: true
									text: linkRow.modelData.name
									elide: Text.ElideRight
								}
								Badge {
									visible: linkRow.modelData.sending
									text: qsTr("sending")
									tone: "info"
								}
								Button {
									objectName: "deviceEditor.makeSending"
									Accessible.name: text
									text: qsTr("Make Sending")
									flat: true
									enabled: !linkRow.modelData.sending
									onClicked: root.vm.setSending(linkRow.modelData.ref)
								}
								Button {
									objectName: "deviceEditor.unlink"
									Accessible.name: text
									text: qsTr("Unlink")
									flat: true
									onClicked: root.vm.unlink(linkRow.modelData.ref)
								}
							}
						}
					}
				}
			}

			// Parameters of a catalog device: active ParameterRefs grouped by channel and parameter block.
			ColumnLayout {
				spacing: 0

				Label {
					objectName: "deviceEditor.parameterWarnings"
					Layout.fillWidth: true
					Layout.leftMargin: 20
					Layout.rightMargin: 20
					Layout.topMargin: Theme.gap
					visible: text !== ""
					text: root.vm.parameterWarnings
					wrapMode: Text.Wrap
					font.pixelSize: 12
					color: Theme.textSecondary
				}

				ListView {
					id: parameterList
					objectName: "deviceEditor.parameters"
					Accessible.name: qsTr("Parameters")
					Accessible.role: Accessible.List
					Layout.fillWidth: true
					Layout.fillHeight: true
					Layout.leftMargin: 20
					Layout.rightMargin: 8
					clip: true
					boundsBehavior: Flickable.StopAtBounds
					model: root.vm.parameters
					spacing: 2
					topMargin: Theme.gap
					bottomMargin: Theme.pad
					reuseItems: false
					ScrollBar.vertical: ScrollBar {}

					delegate: Item {
						id: param

						required property string key
						required property string kind
						required property int depth
						required property string text
						required property string editor
						required property var options
						required property var minimum
						required property var maximum
						required property string value
						required property bool readOnly
						required property bool isDefault
						required property string valueText

						width: parameterList.width - 12
						implicitHeight: param.kind === "block" ? blockLabel.implicitHeight + 22 : Math.max(36, row.implicitHeight + 4)

						// A parameter block: heading with a hairline, some air above.
						Label {
							id: blockLabel
							visible: param.kind === "block"
							x: param.depth * 16
							anchors.bottom: parent.bottom
							anchors.bottomMargin: 6
							text: param.text
							font.weight: Font.DemiBold
							color: Theme.text
						}
						Rectangle {
							visible: param.kind === "block"
							x: blockLabel.x
							anchors.right: parent.right
							anchors.bottom: parent.bottom
							height: 1
							color: Theme.border
						}

						RowLayout {
							id: row
							visible: param.kind === "parameter"
							anchors.left: parent.left
							anchors.right: parent.right
							anchors.verticalCenter: parent.verticalCenter
							anchors.leftMargin: param.depth * 16
							spacing: 12

							Label {
								Layout.preferredWidth: Math.max(160, parameterList.width * 0.45 - param.depth * 16)
								text: param.text
								elide: Text.ElideRight
								// Changed values stand out from the defaults.
								color: param.isDefault ? Theme.textSecondary : Theme.text
								font.weight: param.isDefault ? Font.Normal : Font.Medium
								ToolTip.visible: labelHover.hovered
								ToolTip.text: param.text + (param.isDefault ? "" : qsTr(" (changed)"))

								HoverHandler {
									id: labelHover
								}
							}

							ComboBox {
								objectName: "deviceEditor.parameter.enum"
								visible: param.editor === "enum"
								Layout.fillWidth: true
								enabled: !param.readOnly
								model: param.editor === "enum" ? param.options : []
								textRole: "text"
								valueRole: "value"
								Accessible.name: param.text
								currentIndex: param.editor === "enum" && count > 0 ? indexOfValue(param.value) : -1
								onActivated: root.vm.setParameter(param.key, currentValue)
							}
							SpinBox {
								objectName: "deviceEditor.parameter.number"
								visible: param.editor === "number"
								Layout.fillWidth: true
								enabled: !param.readOnly
								editable: true
								from: param.editor === "number" ? Math.max(-2147483648, Number(param.minimum)) : 0
								to: param.editor === "number" ? Math.min(2147483647, Number(param.maximum)) : 0
								value: param.editor === "number" ? Number(param.value) : 0
								Accessible.name: param.text
								onValueModified: root.vm.setParameter(param.key, String(value))
							}
							TextField {
								objectName: "deviceEditor.parameter.text"
								visible: param.editor === "text" || param.editor === "float" || param.editor === "unsupported"
								Layout.fillWidth: true
								readOnly: param.readOnly
								text: param.value
								Accessible.name: param.text
								onEditingFinished: {
									if (text !== param.value)
										root.vm.setParameter(param.key, text)
								}
							}
							Item {
								visible: param.editor === "none"
								Layout.fillWidth: true
							}
							ToolButton {
								objectName: "deviceEditor.parameter.reset"
								text: qsTr("Reset")
								enabled: !param.isDefault && !param.readOnly
								opacity: enabled ? 1 : 0.3
								focusPolicy: Qt.NoFocus
								Accessible.name: qsTr("Reset %1 to its default").arg(param.text)
								onClicked: root.vm.resetParameter(param.key)
							}
						}
					}

					Label {
						anchors.centerIn: parent
						visible: parameterList.count === 0
						text: qsTr("No visible parameters.")
						color: Theme.textSecondary
					}
				}
			}
		}

		ErrorBanner {
			Layout.fillWidth: true
			vm: root.vm
		}
	}
}
