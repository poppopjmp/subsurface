// SPDX-License-Identifier: GPL-2.0
import QtQuick 2.6
import QtTest 1.2

TestCase {
	name: "qPrefGeneral"

	function test_variables() {
		var x1 = PrefGeneral.defaultsetpoint
		PrefGeneral.defaultsetpoint = 17
		compare(PrefGeneral.defaultsetpoint, 17)

		var x2 = PrefGeneral.o2consumption
		PrefGeneral.o2consumption = 17
		compare(PrefGeneral.o2consumption, 17)

		var x3 = PrefGeneral.pscr_ratio
		PrefGeneral.pscr_ratio = 17
		compare(PrefGeneral.pscr_ratio, 17)

		var x4 = PrefGeneral.diveshareExport_uid
		PrefGeneral.diveshareExport_uid = "myUid"
		compare(PrefGeneral.diveshareExport_uid, "myUid")

		var x5 = PrefGeneral.diveshareExport_private
		PrefGeneral.diveshareExport_private = true
		compare(PrefGeneral.diveshareExport_private, true)
	}

	Item {
		id: spyCatcher

		property bool spy1 : false
		property bool spy2 : false
		property bool spy3 : false
		property bool spy4 : false
		property bool spy5 : false

		Connections {
			target: PrefGeneral
			function onDefaultsetpointChanged() {spyCatcher.spy1 = true }
			function onO2consumptionChanged() {spyCatcher.spy2 = true }
			function onPscr_ratioChanged() {spyCatcher.spy3 = true }
			function onDiveshareExport_uidChanged() {spyCatcher.spy4 = true }
			function onDiveshareExport_privateChanged() {spyCatcher.spy5 = true }
		}
	}

	function test_signals() {
		PrefGeneral.defaultsetpoint = -17
		PrefGeneral.o2consumption = -17
		PrefGeneral.pscr_ratio = -17
		PrefGeneral.diveshareExport_uid = "qml"
		PrefGeneral.diveshareExport_private = ! PrefGeneral.diveshareExport_private

		compare(spyCatcher.spy1, true)
		compare(spyCatcher.spy2, true)
		compare(spyCatcher.spy3, true)
		compare(spyCatcher.spy4, true)
		compare(spyCatcher.spy5, true)
	}
}
