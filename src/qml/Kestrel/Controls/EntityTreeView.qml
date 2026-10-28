pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Kestrel.ViewModels

/// TreeView over a TreePanelVM's model, kept in sync with the SelectionService:
/// the current row is published as the selection, selections made elsewhere are revealed,
/// double-click / Enter activates, right-click asks the shell for a native context menu.
Item {
	id: root

	required property TreePanelVM vm
	/// objectName prefix: "topology" -> "topology.tree"
	required property string viewName
	property string accessibleName: viewName
	property real secondaryColumnWidth: 72
	/// Levels expanded when a project is shown.
	property int initialDepth: 2

	readonly property alias treeView: tree

	function indexAt(position: point): var {
		const inContent = tree.contentItem.mapFromItem(tree, position)
		const cell = tree.cellAtPosition(inContent, true)
		return cell.y >= 0 ? tree.index(cell.y, 0) : root.vm.indexOfRef("")
	}

	function expandInitially() {
		tree.expandRecursively(-1, root.initialDepth)
	}

	function reveal(index: var) {
		tree.expandToIndex(index)
		tree.forceLayout()
		const row = tree.rowAtIndex(index)
		if (row >= 0)
			tree.positionViewAtRow(row, TableView.Contain)
		tree.selectionModel.setCurrentIndex(index, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
	}

	HorizontalHeaderView {
		id: header
		objectName: root.viewName + ".header"
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.top: parent.top
		syncView: tree
		clip: true
	}

	TreeView {
		id: tree
		objectName: root.viewName + ".tree"
		Accessible.name: root.accessibleName
		Accessible.role: Accessible.Tree
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.top: header.bottom
		anchors.bottom: parent.bottom
		clip: true
		focus: true
		boundsBehavior: Flickable.StopAtBounds
		model: root.vm.model
		selectionBehavior: TableView.SelectRows
		selectionMode: TableView.SingleSelection
		editTriggers: TableView.EditKeyPressed
		selectionModel: ItemSelectionModel {
			onCurrentChanged: (current, previous) => root.vm.selectIndex(current)
		}
		columnWidthProvider: column => column === 0
			? Math.max(120, tree.width - (tree.columns - 1) * root.secondaryColumnWidth)
			: root.secondaryColumnWidth
		onWidthChanged: forceLayout()

		ScrollBar.vertical: ScrollBar {}

		delegate: TreeViewDelegate {
			id: cell
			Accessible.name: String(cell.model.display ?? "")
		}

		// Deferred: the view syncs with its model after this handler runs.
		Component.onCompleted: Qt.callLater(root.expandInitially)

		Keys.onReturnPressed: root.vm.activateIndex(tree.selectionModel.currentIndex)
		Keys.onEnterPressed: root.vm.activateIndex(tree.selectionModel.currentIndex)

		TapHandler {
			acceptedButtons: Qt.RightButton
			onTapped: eventPoint => {
				const index = root.indexAt(eventPoint.position)
				if (index.valid)
					tree.selectionModel.setCurrentIndex(index, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
				root.vm.contextMenuAt(index)
			}
		}
		TapHandler {
			acceptedButtons: Qt.LeftButton
			onDoubleTapped: eventPoint => {
				const index = root.indexAt(eventPoint.position)
				if (index.valid)
					root.vm.activateIndex(index)
			}
		}
	}

	Connections {
		target: tree.model
		function onModelReset() {
			Qt.callLater(root.expandInitially)
		}
	}

	Connections {
		target: root.vm
		function onRevealRequested(index) {
			root.reveal(index)
		}
		function onEditRequested(index) {
			root.reveal(index)
			tree.edit(index)
		}
	}
}
