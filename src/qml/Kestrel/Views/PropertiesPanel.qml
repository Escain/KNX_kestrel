pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kestrel.ViewModels
import Kestrel.Controls

/// Properties dock: a form for the current selection (the project when nothing is selected).
/// Text fields commit on editingFinished; every edit is one undo step.
Pane {
	id: root

	required property PropertiesPanelVM vm

	objectName: "propertiesPanel"
	padding: 0

	ScrollView {
		id: scroll
		anchors.fill: parent
		contentWidth: availableWidth

		ColumnLayout {
			width: scroll.availableWidth
			spacing: 8

			ColumnLayout {
				Layout.fillWidth: true
				Layout.leftMargin: Theme.pad
				Layout.rightMargin: Theme.pad
				Layout.topMargin: 14
				Layout.bottomMargin: 4
				spacing: 4

				SectionLabel {
					objectName: "properties.kind"
					text: root.vm.kindLabel
				}
				Label {
					objectName: "properties.title"
					Layout.fillWidth: true
					text: root.vm.title
					font.weight: Font.DemiBold
					font.pixelSize: 16
					elide: Text.ElideRight
				}
			}

			GridLayout {
				Layout.fillWidth: true
				Layout.leftMargin: Theme.pad
				Layout.rightMargin: Theme.pad
				// Labels above their fields.
				columns: 1
				rowSpacing: 4

				FieldLabel {
					text: qsTr("Name")
				}
				TextField {
					objectName: "properties.name"
					Accessible.name: qsTr("Name")
					Layout.fillWidth: true
					text: root.vm.name
					enabled: root.vm.nameEditable
					onEditingFinished: root.vm.setName(text)
				}

				FieldLabel {
					visible: root.vm.numberVisible
					text: qsTr("Number")
				}
				TextField {
					objectName: "properties.number"
					Accessible.name: qsTr("Number")
					Layout.fillWidth: true
					visible: root.vm.numberVisible
					readOnly: !root.vm.numberEditable
					text: root.vm.number
					placeholderText: qsTr("not addressed")
					validator: IntValidator {
						bottom: 0
						top: 255
					}
					onEditingFinished: {
						if (!readOnly)
							root.vm.setNumber(text)
					}
				}

				FieldLabel {
					visible: root.vm.address !== ""
					text: qsTr("Address")
				}
				Label {
					objectName: "properties.address"
					Layout.fillWidth: true
					visible: root.vm.address !== ""
					text: root.vm.address
				}

				FieldLabel {
					visible: root.vm.dptVisible
					text: qsTr("DPT")
				}
				TextField {
					objectName: "properties.dpt"
					Accessible.name: qsTr("Datapoint type")
					Layout.fillWidth: true
					visible: root.vm.dptVisible
					text: root.vm.dpt
					placeholderText: qsTr("e.g. DPST-1-1 or 9.001")
					onEditingFinished: root.vm.setDpt(text)
				}

				FieldLabel {
					visible: root.vm.placeVisible
					text: qsTr("Location")
				}
				ComboBox {
					objectName: "properties.place"
					Accessible.name: qsTr("Location")
					Layout.fillWidth: true
					visible: root.vm.placeVisible
					model: root.vm.placeOptions
					currentIndex: root.vm.placeIndex
					onActivated: index => root.vm.setPlaceIndex(index)
				}

				FieldLabel {
					visible: root.vm.gaStyleVisible
					text: qsTr("Group addresses")
				}
				ColumnLayout {
					visible: root.vm.gaStyleVisible
					spacing: 0

					Repeater {
						model: [qsTr("3-level (main/middle/sub)"), qsTr("2-level (main/sub)"), qsTr("Free")]
						delegate: RadioButton {
							required property string modelData
							required property int index
							objectName: "properties.gaStyle." + index
							Accessible.name: modelData
							text: modelData
							checked: root.vm.gaStyle === index
							onClicked: root.vm.setGaStyle(index)
						}
					}
				}

				FieldLabel {
					visible: root.vm.descriptionEditable
					text: root.vm.descriptionLabel
				}
				TextArea {
					objectName: "properties.description"
					Accessible.name: root.vm.descriptionLabel
					Layout.fillWidth: true
					Layout.minimumHeight: 60
					visible: root.vm.descriptionEditable
					wrapMode: TextEdit.Wrap
					text: root.vm.description
					background: Rectangle {
						radius: 2
						color: Theme.panel
						border.color: parent.activeFocus ? Theme.accent : Theme.borderStrong
					}
					onEditingFinished: root.vm.setDescription(text)
				}
			}

			Label {
				objectName: "properties.details"
				Layout.fillWidth: true
				Layout.leftMargin: Theme.pad
				Layout.rightMargin: Theme.pad
				Layout.topMargin: 8
				visible: text !== ""
				text: root.vm.details
				color: Theme.textSecondary
				lineHeight: 1.3
				wrapMode: Text.Wrap
			}

			ErrorBanner {
				Layout.fillWidth: true
				Layout.margins: Theme.pad
				vm: root.vm
			}
		}
	}

	component FieldLabel: Label {
		Layout.topMargin: 8
		font.pixelSize: 12
		font.weight: Font.Medium
		color: Theme.textSecondary
	}
}
