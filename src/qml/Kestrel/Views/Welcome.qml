pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kestrel.Controls
import Kestrel.ViewModels

/// Welcome tab, shown when no editor is open.
Pane {
	id: root

	required property WelcomeVM vm

	objectName: "welcome"
	padding: 0

	Flickable {
		anchors.fill: parent
		contentHeight: column.implicitHeight + 96
		boundsBehavior: Flickable.StopAtBounds
		clip: true

		ColumnLayout {
			id: column
			x: Math.max(24, (parent.width - width) / 2)
			y: 56
			width: Math.min(640, root.availableWidth - 48)
			spacing: 24

			RowLayout {
				spacing: 16

				Rectangle {
					implicitWidth: 52
					implicitHeight: 52
					radius: 12
					color: Theme.selection

					Label {
						anchors.centerIn: parent
						text: "K"
						font.pixelSize: 26
						font.weight: Font.DemiBold
						color: Theme.accent
					}
				}
				ColumnLayout {
					spacing: 2

					Label {
						text: "Kestrel"
						font.pixelSize: 24
						font.weight: Font.DemiBold
					}
					Label {
						text: qsTr("Open configuration tool for KNX installations")
						color: Theme.textSecondary
					}
				}
			}

			Label {
				objectName: "welcome.project"
				Layout.fillWidth: true
				text: qsTr("Current project: %1").arg(root.vm.session.title)
				elide: Text.ElideMiddle
				color: Theme.textSecondary
			}

			GridLayout {
				Layout.fillWidth: true
				columns: 2
				columnSpacing: 12

				WelcomeCard {
					objectName: "welcome.new"
					text: qsTr("New Project")
					detail: qsTr("Start with an empty installation")
					glyph: "+"
					primary: true
					enabled: root.vm.actions.revision >= 0 && root.vm.actions.isEnabled("file.new")
					onClicked: root.vm.actions.trigger("file.new")
				}
				WelcomeCard {
					objectName: "welcome.open"
					text: qsTr("Open Project…")
					detail: qsTr("A Kestrel project folder")
					glyph: "↗"
					enabled: root.vm.actions.revision >= 0 && root.vm.actions.isEnabled("file.open")
					onClicked: root.vm.actions.trigger("file.open")
				}
			}

			ColumnLayout {
				Layout.fillWidth: true
				visible: root.vm.recentProjects.length > 0
				spacing: 8

				SectionLabel {
					text: qsTr("Recent projects")
				}
				Rectangle {
					Layout.fillWidth: true
					implicitHeight: recentColumn.implicitHeight + 2
					radius: 8
					color: Theme.panel
					border.color: Theme.border

					ColumnLayout {
						id: recentColumn
						x: 1
						y: 1
						width: parent.width - 2
						spacing: 0

						Repeater {
							model: root.vm.recentProjects
							delegate: ItemDelegate {
								id: entry

								required property string modelData
								required property int index

								objectName: "welcome.recent"
								Accessible.name: modelData
								Layout.fillWidth: true
								implicitHeight: 52
								leftPadding: 14
								rightPadding: 14
								onClicked: root.vm.openRecent(modelData)

								background: Rectangle {
									color: entry.down ? Theme.border : entry.hovered ? Theme.hover : "transparent"
									radius: 7
									Rectangle {
										visible: entry.index > 0
										anchors.left: parent.left
										anchors.right: parent.right
										height: 1
										color: Theme.border
									}
								}
								contentItem: ColumnLayout {
									spacing: 1

									Label {
										Layout.fillWidth: true
										text: entry.modelData.split("/").filter(part => part !== "").pop() ?? entry.modelData
										font.weight: Font.DemiBold
										elide: Text.ElideRight
									}
									Label {
										Layout.fillWidth: true
										text: entry.modelData
										font.pixelSize: 12
										color: Theme.textSecondary
										elide: Text.ElideMiddle
									}
								}
							}
						}
					}
				}
			}

			Label {
				Layout.fillWidth: true
				text: qsTr("Start in the Topology panel: add an area and a line. Import the manufacturers' product files in the Catalog panel and add products to the selected line (or drag them onto it). Open a device (double-click) to set its parameters and link its group objects to group addresses (drag them from the Group Addresses panel). Then connect to the bus to program addresses and download.")
				color: Theme.textSecondary
				lineHeight: 1.3
				wrapMode: Text.Wrap
			}
		}
	}

	component WelcomeCard: AbstractButton {
		id: card

		property string detail
		property string glyph
		property bool primary: false

		Layout.fillWidth: true
		Accessible.name: text
		implicitHeight: 72
		padding: 16
		hoverEnabled: true

		background: Rectangle {
			radius: 8
			color: card.down ? Theme.hover : Theme.panel
			border.color: card.hovered ? Theme.borderStrong : Theme.border
		}
		contentItem: RowLayout {
			spacing: 12
			opacity: card.enabled ? 1 : 0.5

			Rectangle {
				implicitWidth: 36
				implicitHeight: 36
				radius: 8
				color: card.primary ? Theme.accent : Theme.panelAlt

				Label {
					anchors.centerIn: parent
					text: card.glyph
					font.pixelSize: 18
					font.weight: Font.DemiBold
					color: card.primary ? Theme.onAccent : Theme.text
				}
			}
			ColumnLayout {
				Layout.fillWidth: true
				spacing: 2

				Label {
					text: card.text
					font.weight: Font.DemiBold
				}
				Label {
					Layout.fillWidth: true
					text: card.detail
					color: Theme.textSecondary
					elide: Text.ElideRight
				}
			}
		}
	}
}
