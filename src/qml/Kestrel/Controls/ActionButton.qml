import QtQuick
import QtQuick.Controls
import Kestrel.ViewModels

/// Tool button bound to an ActionRegistry action: text and enablement come from C++.
ToolButton {
	id: control

	required property ActionRegistry actions
	required property string actionId

	objectName: "action." + actionId
	text: actions.revision >= 0 ? actions.text(actionId) : ""
	enabled: actions.revision >= 0 && actions.isEnabled(actionId)
	focusPolicy: Qt.NoFocus
	display: AbstractButton.TextOnly
	Accessible.name: text
	onClicked: actions.trigger(actionId)
}
