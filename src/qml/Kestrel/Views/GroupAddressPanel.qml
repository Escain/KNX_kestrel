import Kestrel.Controls

/// Group addresses dock: main / middle groups and group addresses with their DPT (FR-GA-1/2).
TreePanel {
	objectName: "groupAddressPanel"
	viewName: "groupAddresses"
	accessibleName: qsTr("Group addresses")
	secondaryColumnWidth: 80
	toolActions: ["ga.addMainGroup", "ga.addMiddleGroup", "ga.addAddress"]
}
