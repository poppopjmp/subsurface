// SPDX-License-Identifier: GPL-2.0
//
// libFuzzer entry point for the dive log parsers.
//
// Subsurface's importers were written against files produced by real dive
// computers, but the files users actually open are whatever somebody sent them.
// These are the routines that turn those bytes into dives, so they are the ones
// worth fuzzing.
//
// Build with:
//   cmake -DSUBSURFACE_FUZZ_BUILD=ON -DCMAKE_C_COMPILER=clang \
//         -DCMAKE_CXX_COMPILER=clang++ ...
// then run e.g.
//   ./tests/fuzz/fuzz-parse-xml corpus/ -max_len=65536
//
// A crash, a sanitizer report, or a hang is a bug. Rejecting a malformed file is
// the expected outcome and must not be reported as a failure.

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/dive.h"
#include "core/divelog.h"
#include "core/errorhelper.h"
#include "core/file.h"
#include "core/parse.h"
#include "core/pref.h"
#include "core/subsurfacestartup.h"

// The git save path calls into the undo layer. tests/testbase.cpp solves this
// with a stub rather than linking the whole command stack, and there is even less
// reason to drag it in here.
namespace Command {
	std::string changesMade() { return {}; }
}

// The parsers report problems through this callback. Fuzzing must not be slowed
// down by writing them out, and a reported error is a normal result here.
static void silence_errors(std::string)
{
}

// Must not run from a global constructor: default_prefs lives in another
// translation unit and there is no ordering guarantee between the two, so doing
// this at static init time read an object that had not been constructed yet.
// libFuzzer calls this after all static initialisation has completed.
extern "C" int LLVMFuzzerInitialize(int *, char ***)
{
	prefs = default_prefs;
	set_error_cb(&silence_errors);
	return 0;
}

// Which parser this binary drives. Set by the per-target define below so that a
// single harness can back several fuzz targets, each with its own corpus - mixing
// them into one target would waste most of the fuzzer's time producing inputs the
// dispatcher rejects immediately.
#ifndef FUZZ_TARGET_NAME
#define FUZZ_TARGET_NAME parse_xml
#endif

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	// Cap the input: past this the parsers are simply slow, and the fuzzer is
	// better served by many small inputs than by a few enormous ones.
	if (size > 1 << 20)
		return 0;

	// The importers take a std::string and rely on it being NUL terminated,
	// which std::string guarantees one past the end.
	std::string mem(reinterpret_cast<const char *>(data), size);
	struct divelog log;

#if defined(FUZZ_PARSE_XML)
	parse_xml_buffer("fuzz.xml", mem.data(), mem.size(), &log, nullptr);
#elif defined(FUZZ_LIQUIVISION)
	try_to_open_liquivision("fuzz.lvd", mem, &log);
#elif defined(FUZZ_DATATRAK)
	// datatrak takes an optional companion .add buffer; feed it the same bytes
	// so that path is exercised too.
	std::string wl(mem);
	datatrak_import(mem, wl, &log);
#elif defined(FUZZ_COCHRAN)
	try_to_open_cochran("fuzz.can", mem, &log);
#elif defined(FUZZ_OSTCTOOLS)
	ostctools_import(mem, &log);
#else
#error "no fuzz target selected"
#endif

	return 0;
}
