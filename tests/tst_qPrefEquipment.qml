// SPDX-License-Identifier: GPL-2.0
import QtQuick 2.6
import QtTest 1.2

// default_cylinder moved here from qPrefGeneral, and include_unused_tanks from
// qPrefTechnicalDetails, where it was still being tested under its former name
// display_unused_tanks.
TestCase {
	name: "qPrefEquipment"

	function test_variables() {
		var x1 = PrefEquipment.default_cylinder
		PrefEquipment.default_cylinder = "my string"
		compare(PrefEquipment.default_cylinder, "my string")

		var x2 = PrefEquipment.include_unused_tanks
		PrefEquipment.include_unused_tanks = true
		compare(PrefEquipment.include_unused_tanks, true)

		var x3 = PrefEquipment.display_default_tank_infos
		PrefEquipment.display_default_tank_infos = true
		compare(PrefEquipment.display_default_tank_infos, true)
	}

	Item {
		id: spyCatcher

		property bool spy1 : false
		property bool spy2 : false
		property bool spy3 : false

		Connections {
			target: PrefEquipment
			function onDefault_cylinderChanged() {spyCatcher.spy1 = true }
			function onInclude_unused_tanksChanged() {spyCatcher.spy2 = true }
			function onDisplay_default_tank_infosChanged() {spyCatcher.spy3 = true }
		}
	}

	function test_signals() {
		PrefEquipment.default_cylinder = "qml"
		PrefEquipment.include_unused_tanks = ! PrefEquipment.include_unused_tanks
		PrefEquipment.display_default_tank_infos = ! PrefEquipment.display_default_tank_infos

		compare(spyCatcher.spy1, true)
		compare(spyCatcher.spy2, true)
		compare(spyCatcher.spy3, true)
	}
}
