import QtQuick
import QtQuick.Controls

/// Small upper-case heading of a group of fields or a list.
Label {
	font.pixelSize: Theme.smallSize
	font.weight: Font.DemiBold
	font.letterSpacing: 0.6
	font.capitalization: Font.AllUppercase
	color: Theme.textSecondary
}
