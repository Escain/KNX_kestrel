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
				Layout.margins: 10
				spacing: 2

				Label {
					objectName: "properties.kind"
					text: root.vm.kindLabel
					opacity: 0.7
				}
				Label {
					objectName: "properties.title"
					Layout.fillWidth: true
					text: root.vm.title
					font.bold: true
					font.pixelSize: 15
					elide: Text.ElideRight
				}
			}

			GridLayout {
				Layout.fillWidth: true
				Layout.leftMargin: 10
				Layout.rightMargin: 10
				columns: 2
				columnSpacing: 8
				rowSpacing: 6

				Label {
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

				Label {
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

				Label {
					visible: root.vm.address !== ""
					text: qsTr("Address")
				}
				Label {
					objectName: "properties.address"
					Layout.fillWidth: true
					visible: root.vm.address !== ""
					text: root.vm.address
				}

				Label {
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

				Label {
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

				Label {
					visible: root.vm.gaStyleVisible
					Layout.alignment: Qt.AlignTop
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

				Label {
					visible: root.vm.descriptionEditable
					Layout.alignment: Qt.AlignTop
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
					onEditingFinished: root.vm.setDescription(text)
				}
			}

			Label {
				objectName: "properties.details"
				Layout.fillWidth: true
				Layout.leftMargin: 10
				Layout.rightMargin: 10
				visible: text !== ""
				text: root.vm.details
				opacity: 0.8
				wrapMode: Text.Wrap
			}

			ErrorBanner {
				Layout.fillWidth: true
				Layout.margins: 10
				vm: root.vm
			}
		}
	}
}
