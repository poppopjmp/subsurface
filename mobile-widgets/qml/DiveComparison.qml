// SPDX-License-Identifier: GPL-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.subsurfacedivelog.mobile 1.0
import org.kde.kirigami as Kirigami

// Shows how a dive compared with the plan it was made from.
//
// The rows come from DiveComparisonModel, the same model the desktop dialog
// uses, so both show the same metrics in the same units. Populate it by calling
// setDives() with the planned dive and the dive that was actually made.
Kirigami.ScrollablePage {
	id: comparisonPage

	property alias model: comparisonModel

	DiveComparisonModel { id: comparisonModel }

	background: Rectangle { color: subsurfaceTheme.backgroundColor }
	title: qsTr("Plan vs. dive")

	// Refresh the pickers whenever the page is shown: dives may have been
	// downloaded or planned since the last time.
	onVisibleChanged: if (visible) reload()

	function reload() {
		var dives = comparisonModel.selectableDives()
		planPicker.model = dives
		divePicker.model = dives
		// Default to the two most recent dives, newest as the dive that was made
		// and the one before it as the plan, which is the common case.
		if (dives.length >= 2) {
			divePicker.currentIndex = 0
			planPicker.currentIndex = 1
		}
		compare()
	}

	function compare() {
		if (planPicker.currentIndex < 0 || divePicker.currentIndex < 0)
			return
		comparisonModel.setDiveIds(planPicker.currentValue, divePicker.currentValue)
	}

	ColumnLayout {
		width: comparisonPage.width - Kirigami.Units.gridUnit
		spacing: Kirigami.Units.smallSpacing

		Controls.Label {
			Layout.fillWidth: true
			text: qsTr("Plan")
			color: subsurfaceTheme.textColor
		}
		Controls.ComboBox {
			id: planPicker
			Layout.fillWidth: true
			textRole: "label"
			valueRole: "id"
			onActivated: comparisonPage.compare()
		}

		Controls.Label {
			Layout.fillWidth: true
			text: qsTr("Dive")
			color: subsurfaceTheme.textColor
		}
		Controls.ComboBox {
			id: divePicker
			Layout.fillWidth: true
			textRole: "label"
			valueRole: "id"
			onActivated: comparisonPage.compare()
		}

		Kirigami.Separator { Layout.fillWidth: true }

		// Shown instead of the table when there is nothing to compare, so the
		// page never looks simply empty.
		Kirigami.InlineMessage {
			Layout.fillWidth: true
			visible: !comparisonModel.isValid
			type: Kirigami.MessageType.Information
			text: comparisonModel.errorString !== "" ? comparisonModel.errorString
								 : qsTr("Select a planned dive and the dive that was made.")
		}

		// Header row
		RowLayout {
			Layout.fillWidth: true
			visible: comparisonModel.isValid
			Kirigami.Heading {
				Layout.preferredWidth: comparisonPage.width * 0.34
				level: 5
				text: qsTr("Metric")
				color: subsurfaceTheme.textColor
			}
			Kirigami.Heading {
				Layout.preferredWidth: comparisonPage.width * 0.2
				level: 5
				horizontalAlignment: Text.AlignRight
				text: qsTr("Plan")
				color: subsurfaceTheme.textColor
			}
			Kirigami.Heading {
				Layout.preferredWidth: comparisonPage.width * 0.2
				level: 5
				horizontalAlignment: Text.AlignRight
				text: qsTr("Dive")
				color: subsurfaceTheme.textColor
			}
			Kirigami.Heading {
				Layout.fillWidth: true
				level: 5
				horizontalAlignment: Text.AlignRight
				text: qsTr("Difference")
				color: subsurfaceTheme.textColor
			}
		}

		Repeater {
			model: comparisonModel
			delegate: RowLayout {
				Layout.fillWidth: true
				width: comparisonPage.width - Kirigami.Units.gridUnit

				Controls.Label {
					Layout.preferredWidth: comparisonPage.width * 0.34
					text: metric
					color: subsurfaceTheme.textColor
					wrapMode: Text.WordWrap
				}
				Controls.Label {
					Layout.preferredWidth: comparisonPage.width * 0.2
					horizontalAlignment: Text.AlignRight
					text: planValue
					color: subsurfaceTheme.textColor
				}
				Controls.Label {
					Layout.preferredWidth: comparisonPage.width * 0.2
					horizontalAlignment: Text.AlignRight
					text: actualValue
					color: subsurfaceTheme.textColor
				}
				Controls.Label {
					Layout.fillWidth: true
					horizontalAlignment: Text.AlignRight
					text: delta
					// Going beyond the plan is the thing worth noticing.
					font.bold: exceeded
					color: exceeded ? subsurfaceTheme.contrastAccentColor
							: subsurfaceTheme.textColor
				}
			}
		}

		Kirigami.Separator {
			Layout.fillWidth: true
			visible: comparisonModel.isValid
		}

		Kirigami.Heading {
			Layout.fillWidth: true
			level: 4
			visible: comparisonModel.ascentViolations.length > 0
			text: qsTr("Ascent rate exceeded")
			color: subsurfaceTheme.textColor
		}

		Repeater {
			model: comparisonModel.ascentViolations
			delegate: Controls.Label {
				Layout.fillWidth: true
				text: modelData
				wrapMode: Text.WordWrap
				color: subsurfaceTheme.contrastAccentColor
			}
		}
	}
}
