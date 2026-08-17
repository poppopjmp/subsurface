// SPDX-License-Identifier: GPL-2.0
import QtQuick 2.6
import QtTest 1.2

// qPrefUnit exposes exactly two Q_PROPERTYs to QML. The rest of the unit
// settings - unit_system, length, pressure, temperature, volume, weight,
// duration_units, vertical_speed_time - have setters, getters and change
// signals in C++ but no Q_PROPERTY, so QML cannot read or write them at all.
// This file used to assign to PrefUnits.unit_system, which silently created a
// JavaScript property on the wrapper and left the preference untouched, and
// then asserted that a signal nobody emitted had arrived. Those settings are
// covered by the C++ test, TestQPrefUnits.
TestCase {
	name: "qPrefUnits"

	function test_variables() {
		var x1 = PrefUnits.coordinates_traditional
		PrefUnits.coordinates_traditional = true
		compare(PrefUnits.coordinates_traditional, true)

		var x2 = PrefUnits.show_units_table
		PrefUnits.show_units_table = true
		compare(PrefUnits.show_units_table, true)
	}

	Item {
		id: spyCatcher

		property bool spy1 : false
		property bool spy2 : false

		Connections {
			target: PrefUnits
			function onCoordinates_traditionalChanged() {spyCatcher.spy1 = true }
			function onShow_units_tableChanged() {spyCatcher.spy2 = true }
		}
	}

	function test_signals() {
		PrefUnits.coordinates_traditional = ! PrefUnits.coordinates_traditional
		PrefUnits.show_units_table = ! PrefUnits.show_units_table

		compare(spyCatcher.spy1, true)
		compare(spyCatcher.spy2, true)
	}
}
