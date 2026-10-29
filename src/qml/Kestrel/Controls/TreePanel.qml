pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kestrel.ViewModels

/// Dock body for a tree navigator: a tool bar of actions, the error banner and the tree.
Pane {
	id: root

	required property TreePanelVM vm
	required property string viewName
	property string accessibleName: viewName
	property list<string> toolActions
	property real secondaryColumnWidth: 72
	property list<real> secondaryColumnWidths
	property list<int> monoColumns

	padding: 0

	ColumnLayout {
		anchors.fill: parent
		spacing: 0

		PanelToolBar {
			Layout.fillWidth: true

			Repeater {
				model: root.toolActions
				delegate: ActionButton {
					required property string modelData
					actions: root.vm.actions
					actionId: modelData
				}
			}
			Item {
				Layout.fillWidth: true
			}
		}

		ErrorBanner {
			Layout.fillWidth: true
			vm: root.vm
		}

		EntityTreeView {
			Layout.fillWidth: true
			Layout.fillHeight: true
			vm: root.vm
			viewName: root.viewName
			accessibleName: root.accessibleName
			secondaryColumnWidth: root.secondaryColumnWidth
			secondaryColumnWidths: root.secondaryColumnWidths
			monoColumns: root.monoColumns
		}
	}
}
