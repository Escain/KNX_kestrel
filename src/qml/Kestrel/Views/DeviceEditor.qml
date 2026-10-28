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

	function linkTyped() {
		const text = gaField.editText.trim()
		const ok = root.catalogTable ? root.vm.linkObject(root.vm.currentObject, text)
			: root.vm.linkGroupAddress(root.vm.current, text)
		if (ok)
			gaField.editText = ""
	}

	ColumnLayout {
		anchors.fill: parent
		spacing: 8

		Label {
			objectName: "deviceEditor.title"
			Layout.fillWidth: true
			text: root.vm.title
			font.pixelSize: 18
			font.bold: true
			elide: Text.ElideRight
		}

		GridLayout {
			objectName: "deviceEditor.product"
			Layout.fillWidth: true
			visible: root.vm.catalogDevice
			columns: 2
			columnSpacing: 12
			rowSpacing: 2

			Label {
				text: qsTr("Product")
				opacity: 0.7
			}
			Label {
				objectName: "deviceEditor.productText"
				Layout.fillWidth: true
				elide: Text.ElideRight
				text: root.vm.orderNumber !== "" ? qsTr("%1  (%2)").arg(root.vm.productText).arg(root.vm.orderNumber)
					: root.vm.productText
			}
			Label {
				text: qsTr("Application")
				opacity: 0.7
			}
			Label {
				objectName: "deviceEditor.applicationText"
				Layout.fillWidth: true
				elide: Text.ElideRight
				text: root.vm.applicationText
			}
			Label {
				text: qsTr("Download")
				opacity: 0.7
				visible: root.vm.downloadState !== ""
			}
			Label {
				objectName: "deviceEditor.downloadState"
				Layout.fillWidth: true
				visible: text !== ""
				elide: Text.ElideRight
				text: root.vm.downloadState
				font.bold: root.vm.needsDownload
				color: root.vm.needsDownload ? "#ef6c00" : palette.windowText
			}
		}

		Label {
			objectName: "deviceEditor.inactiveWarning"
			Layout.fillWidth: true
			visible: root.vm.catalogDevice && root.vm.inactiveLinkedCount > 0
			wrapMode: Text.Wrap
			color: "#ef6c00"
			text: root.vm.inactiveLinkedCount === 1
				? qsTr("1 linked group object is not active with the current parameters (greyed): its group addresses are kept but not downloaded.")
				: qsTr("%1 linked group objects are not active with the current parameters (greyed): their group addresses are kept but not downloaded.").arg(root.vm.inactiveLinkedCount)
		}

		RowLayout {
			Layout.fillWidth: true
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
				color: root.vm.catalogState === "loading" ? palette.windowText : "#c62828"
			}
		}

		TabBar {
			id: tabs
			objectName: "deviceEditor.tabs"
			Layout.fillWidth: true
			visible: root.catalogTable

			TabButton {
				objectName: "deviceEditor.objectsTab"
				Accessible.name: text
				text: qsTr("Group Objects")
			}
			TabButton {
				objectName: "deviceEditor.parametersTab"
				Accessible.name: text
				text: qsTr("Parameters (%1)").arg(root.vm.parameterCount)
			}
		}

		StackLayout {
			Layout.fillWidth: true
			Layout.fillHeight: true
			currentIndex: root.catalogTable ? tabs.currentIndex : 0

			ColumnLayout {
				spacing: 8

				Label {
					text: root.catalogTable ? qsTr("Group objects (drop a group address on a row to link it)") : qsTr("Group objects")
					font.bold: true
				}

				HorizontalHeaderView {
					id: header
					objectName: "deviceEditor.header"
					Layout.fillWidth: true
					syncView: table
					clip: true
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
						columnWidthProvider: column => {
							const fixed = root.catalogTable ? [44, 0, 0, 0, 64, 60, 90, 0] : [44, 0, 0, 70, 60, 90, 0]
							const share = root.catalogTable ? [0, 0.25, 0.2, 0.2, 0, 0, 0, 0.35] : [0, 0.3, 0.3, 0, 0, 0, 0.4]
							const used = fixed.reduce((a, b) => a + b, 0)
							const rest = Math.max(300, table.width - used)
							return fixed[column] > 0 ? fixed[column] : Math.floor(rest * share[column])
						}
						onWidthChanged: forceLayout()
						onModelChanged: forceLayout()

						ScrollBar.vertical: ScrollBar {}

						delegate: Rectangle {
							id: cell

							required property string display
							required property int row
							required property int column
							required property bool selected
							required property bool current
							required property bool editing
							required property bool inactive

							implicitHeight: label.implicitHeight + 10
							opacity: cell.inactive ? 0.45 : 1
							color: cell.row === table.dropRow ? table.palette.highlight
								: cell.selected ? table.palette.highlight
								: (cell.row % 2 ? table.palette.alternateBase : table.palette.base)
							Accessible.name: cell.display

							Label {
								id: label
								anchors.fill: parent
								anchors.leftMargin: 6
								anchors.rightMargin: 6
								visible: !cell.editing
								verticalAlignment: Text.AlignVCenter
								elide: Text.ElideRight
								text: cell.display
								color: cell.selected || cell.row === table.dropRow ? table.palette.highlightedText : table.palette.text
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
						opacity: 0.6
					}
				}

				GroupBox {
					title: qsTr("New group object")
					Layout.fillWidth: true
					visible: !root.vm.catalogDevice

					RowLayout {
						anchors.fill: parent
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

				GroupBox {
					title: root.currentSubject === "" ? qsTr("Associations (select a group object)")
						: qsTr("Associations of %1").arg(root.vm.currentLabel)
					enabled: root.currentSubject !== ""
					Layout.fillWidth: true

					ColumnLayout {
						anchors.fill: parent
						spacing: 4

						RowLayout {
							Layout.fillWidth: true

							ComboBox {
								id: gaField
								objectName: "deviceEditor.groupAddress"
								Accessible.name: qsTr("Group address to link: pick one or type a new address")
								Layout.fillWidth: true
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
								enabled: gaField.editText.trim() !== ""
								onClicked: root.linkTyped()
							}
							Button {
								objectName: "deviceEditor.remove"
								Accessible.name: text
								visible: !root.vm.catalogDevice
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

								Label {
									Layout.fillWidth: true
									text: linkRow.modelData.address + "  " + linkRow.modelData.name
										+ (linkRow.modelData.sending ? qsTr("  (sending)") : "")
									elide: Text.ElideRight
								}
								Button {
									objectName: "deviceEditor.makeSending"
									Accessible.name: text
									text: qsTr("Make Sending")
									enabled: !linkRow.modelData.sending
									onClicked: root.vm.setSending(linkRow.modelData.ref)
								}
								Button {
									objectName: "deviceEditor.unlink"
									Accessible.name: text
									text: qsTr("Unlink")
									onClicked: root.vm.unlink(linkRow.modelData.ref)
								}
							}
						}
					}
				}
			}

			// Parameters of a catalog device: active ParameterRefs grouped by channel and parameter block.
			ColumnLayout {
				spacing: 4

				Label {
					objectName: "deviceEditor.parameterWarnings"
					Layout.fillWidth: true
					visible: text !== ""
					text: root.vm.parameterWarnings
					wrapMode: Text.Wrap
					opacity: 0.7
				}

				ListView {
					id: parameterList
					objectName: "deviceEditor.parameters"
					Accessible.name: qsTr("Parameters")
					Accessible.role: Accessible.List
					Layout.fillWidth: true
					Layout.fillHeight: true
					clip: true
					boundsBehavior: Flickable.StopAtBounds
					model: root.vm.parameters
					spacing: 2
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
						implicitHeight: param.kind === "block" ? blockLabel.implicitHeight + 10 : Math.max(32, row.implicitHeight)

						Label {
							id: blockLabel
							visible: param.kind === "block"
							x: param.depth * 16
							anchors.bottom: parent.bottom
							text: param.text
							font.bold: true
						}

						RowLayout {
							id: row
							visible: param.kind === "parameter"
							anchors.left: parent.left
							anchors.right: parent.right
							anchors.leftMargin: param.depth * 16
							spacing: 8

							Label {
								Layout.preferredWidth: Math.max(160, parameterList.width * 0.45 - param.depth * 16)
								text: param.text
								elide: Text.ElideRight
								font.italic: !param.isDefault
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
						opacity: 0.6
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
