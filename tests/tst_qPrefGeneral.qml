// SPDX-License-Identifier: GPL-2.0
import QtQuick 2.6
import QtTest 1.2

// auto_recalculate_thumbnails, extract_video_thumbnails,
// extract_video_thumbnails_position and ffmpeg_executable moved to qPrefMedia,
// default_cylinder moved to qPrefEquipment, and default_filename and
// use_default_file moved to qPrefLog. They used to be exercised here, which no
// longer tested anything: assigning to a property a QObject does not have just
// creates a plain JavaScript property on the wrapper, so reading it back
// returned the value that had just been written no matter what the C++ side did.
TestCase {
	name: "qPrefGeneral"

	function test_variables() {
		var x5 = PrefGeneral.defaultsetpoint
		PrefGeneral.defaultsetpoint = 17
		compare(PrefGeneral.defaultsetpoint, 17)

		var x9 = PrefGeneral.o2consumption
		PrefGeneral.o2consumption = 17
		compare(PrefGeneral.o2consumption, 17)

		var x10 = PrefGeneral.pscr_ratio
		PrefGeneral.pscr_ratio = 17
		compare(PrefGeneral.pscr_ratio, 17)

		var x12 = PrefGeneral.diveshareExport_uid
		PrefGeneral.diveshareExport_uid = "myUid"
		compare(PrefGeneral.diveshareExport_uid, "myUid")

		var x13 = PrefGeneral.diveshareExport_private
		PrefGeneral.diveshareExport_private = true
		compare(PrefGeneral.diveshareExport_private, true)
	}

	Item {
		id: spyCatcher

		property bool spy5 : false
		property bool spy9 : false
		property bool spy10 : false
		property bool spy12 : false
		property bool spy13 : false

		Connections {
			target: PrefGeneral
			function onDefaultsetpointChanged() { spyCatcher.spy5 = true }
			function onO2consumptionChanged() { spyCatcher.spy9 = true }
			function onPscr_ratioChanged() { spyCatcher.spy10 = true }
			function onDiveshareExport_uidChanged() { spyCatcher.spy12 = true }
			function onDiveshareExport_privateChanged() { spyCatcher.spy13 = true }
		}
	}

	function test_signals() {
		PrefGeneral.defaultsetpoint = -17
		PrefGeneral.o2consumption = -17
		PrefGeneral.pscr_ratio = -17
		PrefGeneral.diveshareExport_uid = "qml"
		PrefGeneral.diveshareExport_private = ! PrefGeneral.diveshareExport_private

		compare(spyCatcher.spy5, true)
		compare(spyCatcher.spy9, true)
		compare(spyCatcher.spy10, true)
		compare(spyCatcher.spy12, true)
		compare(spyCatcher.spy13, true)
	}
}
