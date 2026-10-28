import Kestrel.Controls

/// Topology dock: areas, lines and devices (FR-TOP-1).
TreePanel {
	objectName: "topologyPanel"
	viewName: "topology"
	accessibleName: qsTr("Topology")
	toolActions: ["topology.addArea", "topology.addLine", "topology.addDevice"]
	// Address | Medium | Download
	secondaryColumnWidths: [52, 50, 74]
}
