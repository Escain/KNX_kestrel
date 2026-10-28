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
	/// Optional per-column widths of columns 1.. (overrides secondaryColumnWidth).
	property list<real> secondaryColumnWidths
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
		columnWidthProvider: column => {
			const widthOf = c => c - 1 < root.secondaryColumnWidths.length ? root.secondaryColumnWidths[c - 1] : root.secondaryColumnWidth
			if (column !== 0)
				return widthOf(column)
			let others = 0
			for (let c = 1; c < tree.columns; ++c)
				others += widthOf(c)
			return Math.max(120, tree.width - others)
		}
		onWidthChanged: forceLayout()

		ScrollBar.vertical: ScrollBar {}

		delegate: TreeViewDelegate {
			id: cell
			Accessible.name: String(cell.model.display ?? "")

			// Rows the view model allows (e.g. group addresses) drag out as entity refs. Drag.Automatic runs a
			// platform drag, so another dock (another QQuickWidget) can receive it.
			Drag.dragType: Drag.Automatic
			Drag.supportedActions: Qt.CopyAction | Qt.LinkAction
			Drag.mimeData: ({ "application/x-kestrel-ref": String(cell.model.ref ?? ""), "text/plain": String(cell.model.display ?? "") })
			Drag.active: dragHandler.active

			DragHandler {
				id: dragHandler
				target: null
				enabled: cell.row >= 0 && root.vm.canDrag(tree.index(cell.row, 0))
				grabPermissions: PointerHandler.CanTakeOverFromAnything
			}
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

	// Drops from other docks (formats listed by the view model, e.g. catalog products on Topology).
	DropArea {
		id: dropArea
		objectName: root.viewName + ".dropArea"
		anchors.fill: tree
		keys: root.vm.dropFormats
		enabled: root.vm.dropFormats.length > 0

		function formatOf(drag: var): string {
			for (const format of root.vm.dropFormats) {
				if (drag.formats.indexOf(format) >= 0)
					return format
			}
			return ""
		}

		onPositionChanged: drag => {
			const index = root.indexAt(Qt.point(drag.x, drag.y))
			drag.accepted = root.vm.acceptsDrop(index, dropArea.formatOf(drag))
		}
		onDropped: drop => {
			const format = dropArea.formatOf(drop)
			const index = root.indexAt(Qt.point(drop.x, drop.y))
			if (format !== "" && root.vm.dropOn(index, format, drop.getDataAsString(format)))
				drop.accept(Qt.CopyAction)
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
