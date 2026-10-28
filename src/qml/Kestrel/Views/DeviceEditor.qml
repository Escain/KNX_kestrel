pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kestrel.ViewModels
import Kestrel.Controls

/// Device editor (central tab): group objects and their group address associations (FR-GA-3).
Pane {
	id: root

	required property DeviceEditorVM vm

	objectName: "deviceEditor"

	ColumnLayout {
		anchors.fill: parent
		spacing: 8

		RowLayout {
			Layout.fillWidth: true

			Label {
				objectName: "deviceEditor.title"
				Layout.fillWidth: true
				text: root.vm.title
				font.pixelSize: 18
				font.bold: true
				elide: Text.ElideRight
			}
		}

		Label {
			text: qsTr("Group objects")
			font.bold: true
		}

		HorizontalHeaderView {
			id: header
			objectName: "deviceEditor.header"
			Layout.fillWidth: true
			syncView: table
			clip: true
		}

		TableView {
			id: table
			objectName: "deviceEditor.comObjects"
			Accessible.name: qsTr("Group objects")
			Accessible.role: Accessible.Table
			Layout.fillWidth: true
			Layout.fillHeight: true
			Layout.minimumHeight: 120
			clip: true
			boundsBehavior: Flickable.StopAtBounds
			model: root.vm.comObjects
			selectionBehavior: TableView.SelectRows
			selectionMode: TableView.SingleSelection
			editTriggers: TableView.EditKeyPressed | TableView.DoubleTapped
			selectionModel: ItemSelectionModel {
				onCurrentChanged: (current, previous) => root.vm.setCurrentIndex(current)
			}
			// #, Size, Flags and DPT are fixed; Name, Function and Group addresses share the rest.
			columnWidthProvider: column => {
				const fixed = [44, 0, 0, 70, 60, 90, 0]
				const share = [0, 0.3, 0.3, 0, 0, 0, 0.4]
				const rest = Math.max(300, table.width - 264)
				return fixed[column] > 0 ? fixed[column] : Math.floor(rest * share[column])
			}
			onWidthChanged: forceLayout()

			ScrollBar.vertical: ScrollBar {}

			delegate: Rectangle {
				id: cell

				required property string display
				required property int row
				required property int column
				required property bool selected
				required property bool current
				required property bool editing

				implicitHeight: label.implicitHeight + 10
				color: cell.selected ? table.palette.highlight : (cell.row % 2 ? table.palette.alternateBase : table.palette.base)
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
					color: cell.selected ? table.palette.highlightedText : table.palette.text
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

		GroupBox {
			title: qsTr("New group object")
			Layout.fillWidth: true

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
			title: root.vm.current === "" ? qsTr("Associations (select a group object)")
				: qsTr("Associations of %1").arg(root.vm.currentLabel)
			enabled: root.vm.current !== ""
			Layout.fillWidth: true

			ColumnLayout {
				anchors.fill: parent
				spacing: 4

				RowLayout {
					Layout.fillWidth: true

					TextField {
						id: gaText
						objectName: "deviceEditor.groupAddress"
						Accessible.name: qsTr("Group address to link")
						Layout.fillWidth: true
						placeholderText: qsTr("Group address, e.g. 1/2/3")
						onAccepted: linkButton.clicked()
					}
					Button {
						id: linkButton
						objectName: "deviceEditor.link"
						Accessible.name: text
						text: qsTr("Link")
						onClicked: {
							if (root.vm.linkGroupAddress(root.vm.current, gaText.text))
								gaText.clear()
						}
					}
					Button {
						objectName: "deviceEditor.remove"
						Accessible.name: text
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

		ErrorBanner {
			Layout.fillWidth: true
			vm: root.vm
		}
	}
}
