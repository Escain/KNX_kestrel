import Kestrel.Controls

/// Buildings dock: buildings, floors, rooms and the devices placed in them (FR-BLD-1/2).
TreePanel {
	objectName: "buildingsPanel"
	viewName: "buildings"
	accessibleName: qsTr("Buildings")
	toolActions: ["building.addBuilding", "building.addFloor", "building.addRoom"]
}
