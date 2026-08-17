// SPDX-License-Identifier: GPL-2.0
import QtQuick 2.6
import QtTest 1.2

// default_filename, use_default_file and show_average_depth moved here from
// qPrefGeneral and qPrefTechnicalDetails; their QML coverage did not follow.
TestCase {
	name: "qPrefLog"

	function test_variables() {
		var x1 = PrefLog.default_filename
		PrefLog.default_filename = "my string"
		compare(PrefLog.default_filename, "my string")

		var x2 = PrefLog.use_default_file
		PrefLog.use_default_file = true
		compare(PrefLog.use_default_file, true)

		var x3 = PrefLog.extraEnvironmentalDefault
		PrefLog.extraEnvironmentalDefault = true
		compare(PrefLog.extraEnvironmentalDefault, true)

		var x4 = PrefLog.salinityEditDefault
		PrefLog.salinityEditDefault = true
		compare(PrefLog.salinityEditDefault, true)

		var x5 = PrefLog.show_average_depth
		PrefLog.show_average_depth = true
		compare(PrefLog.show_average_depth, true)
	}

	Item {
		id: spyCatcher

		property bool spy1 : false
		property bool spy2 : false
		property bool spy3 : false
		property bool spy4 : false
		property bool spy5 : false

		Connections {
			target: PrefLog
			function onDefault_filenameChanged() {spyCatcher.spy1 = true }
			function onUse_default_fileChanged() {spyCatcher.spy2 = true }
			function onExtraEnvironmentalDefaultChanged() {spyCatcher.spy3 = true }
			function onSalinityEditDefaultChanged() {spyCatcher.spy4 = true }
			function onShow_average_depthChanged() {spyCatcher.spy5 = true }
		}
	}

	function test_signals() {
		PrefLog.default_filename = "qml"
		PrefLog.use_default_file = ! PrefLog.use_default_file
		PrefLog.extraEnvironmentalDefault = ! PrefLog.extraEnvironmentalDefault
		PrefLog.salinityEditDefault = ! PrefLog.salinityEditDefault
		PrefLog.show_average_depth = ! PrefLog.show_average_depth

		compare(spyCatcher.spy1, true)
		compare(spyCatcher.spy2, true)
		compare(spyCatcher.spy3, true)
		compare(spyCatcher.spy4, true)
		compare(spyCatcher.spy5, true)
	}
}
