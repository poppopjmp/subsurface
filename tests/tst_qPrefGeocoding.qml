// SPDX-License-Identifier: GPL-2.0
import QtQuick 2.6
import QtTest 1.2

// Every line of this file used to be commented out, so it reported two passing
// tests that asserted nothing. The three properties are taxonomy_category
// enums, which qPref.cpp registers as a metatype, so QML can read and write
// them as integers: TC_NONE is 0 and TC_OCEAN, TC_COUNTRY, TC_ADMIN_L1,
// TC_ADMIN_L2, TC_LOCALNAME, TC_ADMIN_L3 follow in that order.
TestCase {
	name: "qPrefGeocoding"

	readonly property int tcCountry: 2
	readonly property int tcAdminL1: 3
	readonly property int tcLocalname: 5

	function test_variables() {
		var x1 = PrefGeocoding.first_taxonomy_category
		PrefGeocoding.first_taxonomy_category = tcCountry
		compare(PrefGeocoding.first_taxonomy_category, tcCountry)

		var x2 = PrefGeocoding.second_taxonomy_category
		PrefGeocoding.second_taxonomy_category = tcAdminL1
		compare(PrefGeocoding.second_taxonomy_category, tcAdminL1)

		var x3 = PrefGeocoding.third_taxonomy_category
		PrefGeocoding.third_taxonomy_category = tcLocalname
		compare(PrefGeocoding.third_taxonomy_category, tcLocalname)
	}

	Item {
		id: spyCatcher

		property bool spy1 : false
		property bool spy2 : false
		property bool spy3 : false

		Connections {
			target: PrefGeocoding
			function onFirst_taxonomy_categoryChanged() {spyCatcher.spy1 = true }
			function onSecond_taxonomy_categoryChanged() {spyCatcher.spy2 = true }
			function onThird_taxonomy_categoryChanged() {spyCatcher.spy3 = true }
		}
	}

	function test_signals() {
		// values different from the ones test_variables left behind, so the
		// setters really do see a change
		PrefGeocoding.first_taxonomy_category = tcAdminL1
		PrefGeocoding.second_taxonomy_category = tcLocalname
		PrefGeocoding.third_taxonomy_category = tcCountry

		compare(spyCatcher.spy1, true)
		compare(spyCatcher.spy2, true)
		compare(spyCatcher.spy3, true)
	}
}
