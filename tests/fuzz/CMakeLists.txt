# libFuzzer targets for the dive log parsers.
#
# Only built when SUBSURFACE_FUZZ_BUILD is on, and only with clang, since
# -fsanitize=fuzzer is a clang feature. Each target drives one parser so that
# every corpus stays relevant to the code it is exercising.

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	message(FATAL_ERROR "SUBSURFACE_FUZZ_BUILD requires clang (-fsanitize=fuzzer)")
endif()

set(FUZZ_TARGETS
	parse-xml:FUZZ_PARSE_XML
	liquivision:FUZZ_LIQUIVISION
	datatrak:FUZZ_DATATRAK
	cochran:FUZZ_COCHRAN
	ostctools:FUZZ_OSTCTOOLS
)

foreach(_entry ${FUZZ_TARGETS})
	string(REPLACE ":" ";" _parts ${_entry})
	list(GET _parts 0 _name)
	list(GET _parts 1 _define)

	add_executable(fuzz-${_name} fuzz-parse.cpp)
	target_compile_definitions(fuzz-${_name} PRIVATE ${_define})
	# The sanitizers have to reach both the compile and the link line.
	target_compile_options(fuzz-${_name} PRIVATE
		-fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
	target_link_options(fuzz-${_name} PRIVATE
		-fsanitize=fuzzer,address,undefined)
	# The harness stubs Command::changesMade() the same way tests/testbase.cpp
	# does, so the undo layer and the models do not need to be linked here.
	target_link_libraries(fuzz-${_name}
		subsurface_corelib
		${SUBSURFACE_LINK_LIBRARIES}
		${QT_LIBRARIES}
	)
endforeach()
