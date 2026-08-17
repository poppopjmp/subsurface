// SPDX-License-Identifier: GPL-2.0
//
// Robustness harness for the hand-written binary importers.
//
// Every one of these parsers walks a byte buffer that came straight off disk,
// from a file the user picked, and until now not one of them had a test of any
// kind. This does not check that they parse anything correctly - the format
// tests in testparse.cpp do that for the formats that have sample files. What
// it checks is the other half: that a file which is empty, truncated, or simply
// not what the parser expected is rejected rather than walked off the end of.
//
// On its own this catches outright crashes. Built with -fsanitize=address it
// catches the reads past the end that do not happen to segfault, which is what
// most of them are.

#include "testimport.h"

#include "core/divelog.h"
#include "core/errorhelper.h"
#include "core/file.h"

#include <QTest>

#include <string>
#include <vector>

namespace {

// TestBase turns any reported error into a test failure, which is right for
// tests that are supposed to parse cleanly and wrong here: "this file is not a
// Cochran log" is the correct, expected outcome for most of what we feed in.
void ignoreError(std::string)
{
}

// A deterministic byte source. Not random - a fixed sequence, so a failure here
// reproduces exactly rather than once in a hundred runs.
std::string pseudo_random_bytes(size_t len, uint32_t seed)
{
	std::string res;
	res.reserve(len);
	uint32_t state = seed;
	for (size_t i = 0; i < len; ++i) {
		// Numerical Recipes LCG; any cheap full-period generator will do.
		state = state * 1664525u + 1013904223u;
		res.push_back(static_cast<char>((state >> 16) & 0xff));
	}
	return res;
}

// Hand every importer the same buffer. They all take it by value or const ref
// except the two that want a mutable std::string, hence the copies.
void feed_all_importers(const std::string &data)
{
	struct divelog log;

	{ std::string copy = data; try_to_open_cochran("test", copy, &log); }
	{ std::string copy = data; try_to_open_liquivision("test", copy, &log); }
	{ std::string copy = data; ostctools_import(copy, &log); }
	{ std::string copy = data, wl; datatrak_import(copy, wl, &log); }
	logtrak_import(data, &log);
	scubapro_asd_import(data, &log);
	divesoft_import(data, &log);
}

} // namespace

void TestImport::initTestCase()
{
	TestBase::initTestCase();
	// Replace TestBase's fail-on-error callback: a rejected file reports an
	// error, and that is the outcome we are asking for.
	set_error_cb(&ignoreError);
}

void TestImport::testEmptyInput()
{
	feed_all_importers(std::string());
	QVERIFY(true); // reaching here without a crash or a sanitizer report is the test
}

void TestImport::testTruncatedInput()
{
	// A plausible header followed by nothing, at a spread of lengths that
	// straddle the sizes these parsers check against.
	const std::string full = pseudo_random_bytes(0x41000, 0x53535246u);
	for (size_t len: { size_t(1), size_t(2), size_t(3), size_t(4), size_t(7),
			   size_t(15), size_t(16), size_t(63), size_t(64), size_t(255),
			   size_t(256), size_t(257), size_t(1023), size_t(1024),
			   size_t(4095), size_t(4096), size_t(0xffff), size_t(0x10000) }) {
		feed_all_importers(full.substr(0, len));
	}
	QVERIFY(true);
}

void TestImport::testGarbageInput()
{
	for (uint32_t seed: { 1u, 42u, 1337u, 0xdeadbeefu }) {
		feed_all_importers(pseudo_random_bytes(0x20000, seed));
	}
	QVERIFY(true);
}

void TestImport::testCochranSizeBoundary()
{
	// try_to_open_cochran() reads its decode table at 0x40001 and indexes
	// 0x100 into it, so it needs 0x40102 bytes. The guard used to ask only for
	// 0x40000, which read 0x102 bytes past the end of a file of exactly that
	// size - and formed the out-of-range pointer before checking at all.
	//
	// The first four bytes are the offset of the first dive; make it valid so
	// the size check is what decides, not the offset check.
	for (size_t len: { size_t(0x3ffff), size_t(0x40000), size_t(0x40001),
			   size_t(0x40100), size_t(0x40101), size_t(0x40102),
			   size_t(0x40200) }) {
		std::string data = pseudo_random_bytes(len, 0xc0c47a2u);
		if (data.size() >= 8) {
			// dive1 = 0x40000, dive2 = 0x40100, little endian
			data[0] = 0x00; data[1] = 0x00; data[2] = 0x04; data[3] = 0x00;
			data[4] = 0x00; data[5] = 0x01; data[6] = 0x04; data[7] = 0x00;
		}
		struct divelog log;
		try_to_open_cochran("test", data, &log);
	}
	QVERIFY(true);
}

QTEST_GUILESS_MAIN(TestImport)
