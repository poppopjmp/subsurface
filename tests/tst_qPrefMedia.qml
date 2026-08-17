// SPDX-License-Identifier: GPL-2.0
import QtQuick 2.6
import QtTest 1.2

// These preferences used to live on qPrefGeneral and were tested there. They
// moved to qPrefMedia and the QML coverage did not follow, which nobody noticed
// because the whole QML test runner was stubbed out to return 0.
TestCase {
	name: "qPrefMedia"

	function test_variables() {
		var x1 = PrefMedia.auto_recalculate_thumbnails
		PrefMedia.auto_recalculate_thumbnails = true
		compare(PrefMedia.auto_recalculate_thumbnails, true)

		var x2 = PrefMedia.extract_video_thumbnails
		PrefMedia.extract_video_thumbnails = true
		compare(PrefMedia.extract_video_thumbnails, true)

		var x3 = PrefMedia.extract_video_thumbnails_position
		PrefMedia.extract_video_thumbnails_position = 17
		compare(PrefMedia.extract_video_thumbnails_position, 17)

		var x4 = PrefMedia.ffmpeg_executable
		PrefMedia.ffmpeg_executable = "my string"
		compare(PrefMedia.ffmpeg_executable, "my string")

		var x5 = PrefMedia.subtitles_format_string
		PrefMedia.subtitles_format_string = "my string"
		compare(PrefMedia.subtitles_format_string, "my string")
	}

	Item {
		id: spyCatcher

		property bool spy1 : false
		property bool spy2 : false
		property bool spy3 : false
		property bool spy4 : false
		property bool spy5 : false

		Connections {
			target: PrefMedia
			function onAuto_recalculate_thumbnailsChanged() {spyCatcher.spy1 = true }
			function onExtract_video_thumbnailsChanged() {spyCatcher.spy2 = true }
			function onExtract_video_thumbnails_positionChanged() {spyCatcher.spy3 = true }
			function onFfmpeg_executableChanged() {spyCatcher.spy4 = true }
			function onSubtitles_format_stringChanged() {spyCatcher.spy5 = true }
		}
	}

	function test_signals() {
		PrefMedia.auto_recalculate_thumbnails = ! PrefMedia.auto_recalculate_thumbnails
		PrefMedia.extract_video_thumbnails = ! PrefMedia.extract_video_thumbnails
		PrefMedia.extract_video_thumbnails_position = -17
		PrefMedia.ffmpeg_executable = "qml"
		PrefMedia.subtitles_format_string = "qml"

		compare(spyCatcher.spy1, true)
		compare(spyCatcher.spy2, true)
		compare(spyCatcher.spy3, true)
		compare(spyCatcher.spy4, true)
		compare(spyCatcher.spy5, true)
	}
}
