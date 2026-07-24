# without this a "make clean" would delete the .ts files
set_directory_properties(PROPERTIES CLEAN_NO_CUSTOM 1)

# active translations - these will be built
#
set(TRANSLATION_FILES
	subsurface_source.ts
	subsurface_bg_BG.ts
	subsurface_ca.ts
	subsurface_cs.ts
	subsurface_da_DK.ts
	subsurface_de_CH.ts
	subsurface_de_DE.ts
	subsurface_el_GR.ts
	subsurface_en_GB.ts
	subsurface_es_ES.ts
	subsurface_et_EE.ts
	subsurface_fi_FI.ts
	subsurface_fr_FR.ts
	subsurface_he.ts
	subsurface_hr_HR.ts
	subsurface_it_IT.ts
	subsurface_ko_KR.ts
	subsurface_nb_NO.ts
	subsurface_nl_NL.ts
	subsurface_pl_PL.ts
	subsurface_pt_BR.ts
	subsurface_pt_PT.ts
	subsurface_ro_RO.ts
	subsurface_ru_RU.ts
	subsurface_sk_SK.ts
	subsurface_sv_SE.ts
	subsurface_zh_TW.ts
)

set(US_EN_PLURALS subsurface_en_US.ts)

# disabled translations as they are below 40%:
#	subsurface_hu.ts
#	subsurface_lv_LV.ts
#	subsurface_tr.ts

# new translations that didn't make it far enough:
#	subsurface_af.ts
#	subsurface_ar.ts
#	subsurface_hi_IN.ts
#	subsurface_hu.ts
#	subsurface_id.ts
#	subsurface_ja_JP.ts
#	subsurface_sl.ts
#	subsurface_sw_KE.ts
#	subsurface_te.ts
#	subsurface_th.ts
#	subsurface_vi.ts
#	subsurface_zh_CN.ts

if(USINGQT6)
	file(GLOB MOBILE_SOURCES ${CMAKE_SOURCE_DIR}/mobile-widgets/*.cpp ${CMAKE_SOURCE_DIR}/mobile-widgets/qml/*.qml)

	# qt_add_translations() cannot be used here. It calls qt_add_lrelease(), which does
	#     set_source_files_properties(... TARGET_DIRECTORY ${target} ...)
	# immediately, so the target has to exist already - but the top level CMakeLists.txt
	# adds this directory well before the executable target is created. Qt >= 6.8 happens
	# to tolerate that ordering, Qt 6.4 and 6.5 (what the current Ubuntu LTS and Debian
	# stable ship) fail to configure with
	#     set_source_files_properties given non-existent target for TARGET_DIRECTORY
	#
	# qt_add_lupdate() only refers to the target through generator expressions, which are
	# evaluated after the whole project has been processed, so it is safe to call here and
	# still provides the update_translations target that
	# scripts/updatetranslationsource.sh drives.
	qt_add_lupdate(${SUBSURFACE_TARGET}
		TS_FILES ${TRANSLATION_FILES}
		SOURCES ${MOBILE_SOURCES})

	# Drive lrelease ourselves: this has no target dependency and behaves identically on
	# every Qt6 release. The .qm names follow lrelease's own convention, so the resulting
	# file list is the same one qt_add_translations() would have reported.
	set(TRANSLATIONS "")
	foreach(_ts_file ${TRANSLATION_FILES} ${US_EN_PLURALS})
		get_filename_component(_ts_name ${_ts_file} NAME_WE)
		set(_qm_file ${CMAKE_CURRENT_BINARY_DIR}/${_ts_name}.qm)
		add_custom_command(OUTPUT ${_qm_file}
			COMMAND ${QT_CMAKE_EXPORT_NAMESPACE}::lrelease
				${CMAKE_CURRENT_SOURCE_DIR}/${_ts_file} -qm ${_qm_file}
			DEPENDS ${QT_CMAKE_EXPORT_NAMESPACE}::lrelease ${CMAKE_CURRENT_SOURCE_DIR}/${_ts_file}
			COMMENT "Generating ${_ts_name}.qm"
			VERBATIM)
		list(APPEND TRANSLATIONS ${_qm_file})
	endforeach()
	add_custom_target(translations ALL DEPENDS ${TRANSLATIONS})
else()
	qt5_add_translation(TRANSLATIONS ${TRANSLATION_FILES} ${US_EN_PLURALS})
	add_custom_target(translations ALL DEPENDS ${TRANSLATIONS})
endif()

set(TRANSLATIONS ${TRANSLATIONS} PARENT_SCOPE)
