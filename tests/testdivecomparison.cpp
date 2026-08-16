// SPDX-License-Identifier: GPL-2.0
#include "testdivecomparison.h"

#include "core/deco.h"
#include "core/dive.h"
#include "core/divecomparison.h"
#include "core/divecomputer.h"
#include "core/divelist.h"
#include "core/divelog.h"
#include "core/equipment.h"
#include "core/gas.h"
#include "core/planner.h"
#include "core/pref.h"
#include "core/sample.h"

#include <QTest>

namespace {

// Build a dive with one dive computer and the given (seconds, mm) profile.
// maxdepth/meandepth/duration are set from the profile so the summary numbers
// and the sample based ones agree, which is what a real dive looks like after
// fixup.
std::unique_ptr<struct dive> make_dive(const std::vector<std::pair<int, int>> &profile)
{
	auto d = std::make_unique<struct dive>();
	d->dcs.emplace_back();
	struct divecomputer &dc = d->dcs[0];

	int maxdepth = 0;
	int64_t depth_area = 0;
	int prev_time = 0, prev_depth = 0;
	for (auto [seconds, mm]: profile) {
		struct sample s;
		s.time.seconds = seconds;
		s.depth.mm = mm;
		dc.samples.push_back(s);

		maxdepth = std::max(maxdepth, mm);
		depth_area += (int64_t)(mm + prev_depth) * (seconds - prev_time) / 2;
		prev_time = seconds;
		prev_depth = mm;
	}

	d->maxdepth.mm = maxdepth;
	d->duration.seconds = prev_time;
	d->meandepth.mm = prev_time ? (int)(depth_area / prev_time) : 0;
	return d;
}

// A square profile: descend, hold, then ascend at 6 m/min, which is inside the
// 10 m/min limit so the fixture itself does not produce ascent violations.
std::unique_ptr<struct dive> square_dive(int depth_mm, int bottom_seconds)
{
	int ascent = depth_mm * 60 / 6000;
	return make_dive({{0, 0}, {60, depth_mm}, {60 + bottom_seconds, depth_mm},
			  {60 + bottom_seconds + ascent, 0}});
}

// A dive computer that tracks decompression says so even when there is none to
// do, by reporting a no-stop time. Fixtures that want to stand in for such a
// computer have to say it too, otherwise their deco time is unknown rather
// than zero - which is the whole point of the distinction.
void mark_deco_tracked(struct dive &d)
{
	for (auto &s: d.dcs[0].samples)
		s.ndl.seconds = 99 * 60;
}

// Build a plan the way the planner does: enter a descent and a bottom segment
// by hand, then let plan() compute the ascent and any stops it needs.
std::unique_ptr<struct dive> planned_dive(depth_t depth, int bottom_seconds)
{
	auto d = std::make_unique<struct dive>();
	d->dcs.emplace_back();
	make_planner_dc(&d->dcs[0]);

	cylinder_t *cyl = d->get_or_create_cylinder(0);
	cyl->gasmix = gasmix_air;
	cyl->type.size = 24_l;
	cyl->type.workingpressure = 232_bar;
	reset_cylinders(d.get(), true);

	diveplan dp;
	dp.salinity = 10300;
	dp.surface_pressure = 1_atm;
	dp.bottomsac = prefs.bottomsac;
	dp.decosac = prefs.decosac;
	dp.gflow = 100;
	dp.gfhigh = 100;
	plan_add_segment(dp, depth.mm / prefs.descrate, depth, 0, 0, true, OC);
	plan_add_segment(dp, bottom_seconds, depth, 0, 0, true, OC);

	struct deco_state ds = {};
	deco_state_cache cache;
	plan(&ds, dp, d.get(), 0, 60, cache, true, false, nullptr);
	return d;
}

} // namespace

void TestDiveComparison::testRejectsMissingDives()
{
	auto d = square_dive(30000, 1200);

	QVERIFY(!compare_dives(nullptr, d.get()).valid);
	QVERIFY(!compare_dives(d.get(), nullptr).valid);
	QVERIFY(!compare_dives(nullptr, nullptr).valid);
	// and says why, rather than returning silently empty numbers
	QVERIFY(!compare_dives(nullptr, d.get()).error.empty());

	// a dive with no dive computer data is not comparable either
	auto empty = std::make_unique<struct dive>();
	QVERIFY(!compare_dives(empty.get(), d.get()).valid);
}

void TestDiveComparison::testIdenticalDivesHaveNoDeltas()
{
	auto plan = square_dive(30000, 1200);
	auto actual = square_dive(30000, 1200);

	auto c = compare_dives(plan.get(), actual.get());
	QVERIFY(c.valid);
	QCOMPARE(c.maxdepth_delta.mm, 0);
	QCOMPARE(c.meandepth_delta.mm, 0);
	QCOMPARE(c.duration_delta.seconds, 0);
	QCOMPARE(c.deco_time_delta.seconds, 0);
	QCOMPARE(c.max_depth_deviation.mm, 0);
	QVERIFY(c.ascent_violations.empty());
}

void TestDiveComparison::testDeeperAndLongerThanPlanned()
{
	auto plan = square_dive(30000, 1200);	// 30 m, 20 min bottom
	auto actual = square_dive(33000, 1500);	// 33 m, 25 min bottom

	auto c = compare_dives(plan.get(), actual.get());
	QVERIFY(c.valid);

	// deltas are actual - plan, so exceeding the plan is positive
	QCOMPARE(c.maxdepth_delta.mm, 3000);
	// 300 s more bottom time, plus 30 s more ascent because the deeper dive
	// takes longer to come up at the same 6 m/min
	QCOMPARE(c.duration_delta.seconds, 330);
	QVERIFY(c.meandepth_delta.mm > 0);

	QCOMPARE(c.plan_maxdepth.mm, 30000);
	QCOMPARE(c.actual_maxdepth.mm, 33000);

	// and the reverse comparison flips the signs
	auto rev = compare_dives(actual.get(), plan.get());
	QCOMPARE(rev.maxdepth_delta.mm, -3000);
	QCOMPARE(rev.duration_delta.seconds, -330);
}

void TestDiveComparison::testDepthDeviationIsInterpolated()
{
	// plan descends linearly to 40 m over 400 s and stays there
	auto plan = make_dive({{0, 0}, {400, 40000}, {1000, 40000}});
	// the actual dive follows it but is 5 m deeper at t=200, where the planned
	// depth is only defined by interpolation between the samples (20 m)
	auto actual = make_dive({{0, 0}, {200, 25000}, {400, 40000}, {1000, 40000}});

	auto c = compare_dives(plan.get(), actual.get());
	QVERIFY(c.valid);
	QCOMPARE(c.max_depth_deviation.mm, 5000);
	QCOMPARE(c.max_depth_deviation_at.seconds, 200);
}

void TestDiveComparison::testAscentRateViolations()
{
	auto plan = make_dive({{0, 0}, {60, 30000}, {1200, 30000}, {1500, 0}});
	// same dive, but the last 30 m are done in 60 s = 30 m/min
	auto actual = make_dive({{0, 0}, {60, 30000}, {1200, 30000}, {1260, 0}});

	auto c = compare_dives(plan.get(), actual.get());
	QVERIFY(c.valid);
	QCOMPARE((int)c.ascent_violations.size(), 1);

	const auto &v = c.ascent_violations[0];
	QCOMPARE(v.from.mm, 30000);
	QCOMPARE(v.to.mm, 0);
	QCOMPARE(v.rate_mm_per_min, 30000);	// 30 m/min
	QCOMPARE(v.start.seconds, 1200);

	// the planned ascent (30 m in 300 s = 6 m/min) is within the limit
	auto planned_only = compare_dives(actual.get(), plan.get());
	QVERIFY(planned_only.ascent_violations.empty());

	// raising the limit above the actual rate clears the violation
	auto lenient = compare_dives(plan.get(), actual.get(), 40000);
	QVERIFY(lenient.ascent_violations.empty());
}

void TestDiveComparison::testDecoTimeFromSamples()
{
	auto plan = square_dive(30000, 1200);
	mark_deco_tracked(*plan);
	auto actual = square_dive(30000, 1200);

	// mark one interval of the actual dive as being in deco. Each sample's
	// flag covers the span until the next sample.
	actual->dcs[0].samples[1].in_deco = true;	// t=60 .. t=1260
	QVERIFY(actual->dcs[0].samples.size() >= 3);

	auto c = compare_dives(plan.get(), actual.get());
	QVERIFY(c.valid);
	QVERIFY(c.deco_time_comparable());
	QCOMPARE(c.plan_deco_time.seconds, 0);
	QCOMPARE(c.actual_deco_time.seconds, 1200);
	QCOMPARE(c.deco_time_delta.seconds, 1200);

	// a stopdepth also counts, for computers that do not set in_deco
	auto viaStop = square_dive(30000, 1200);
	viaStop->dcs[0].samples[1].stopdepth.mm = 6000;
	auto c2 = compare_dives(plan.get(), viaStop.get());
	QCOMPARE(c2.actual_deco_time.seconds, 1200);
}

void TestDiveComparison::testDecoTimeUnknownIsNotZero()
{
	// Neither dive says anything at all about decompression, which is what a
	// depth-and-time-only computer - or an imported profile - looks like.
	auto plan = square_dive(30000, 1200);
	auto actual = square_dive(30000, 1200);

	auto c = compare_dives(plan.get(), actual.get());
	QVERIFY(c.valid);
	QVERIFY(!c.plan_deco_known);
	QVERIFY(!c.actual_deco_known);
	QVERIFY(!c.deco_time_comparable());

	// The dive picked up deco but the plan never recorded whether it had any.
	// Reporting that as "20 minutes more than planned" would be an invention,
	// so the delta stays put and the caller is told why.
	auto withDeco = square_dive(30000, 1200);
	withDeco->dcs[0].samples[1].in_deco = true;
	auto c2 = compare_dives(plan.get(), withDeco.get());
	QVERIFY(c2.actual_deco_known);
	QVERIFY(!c2.plan_deco_known);
	QVERIFY(!c2.deco_time_comparable());
	QCOMPARE(c2.deco_time_delta.seconds, 0);
}

void TestDiveComparison::testPlannerRecordsItsOwnDecoTime()
{
	prefs = default_prefs;
	prefs.unit_system = METRIC;
	prefs.units.length = units::METERS;
	prefs.planner_deco_mode = BUEHLMANN;
	prefs.last_stop = false;

	// 45 m for 25 minutes on air is well past any no-stop limit, so the
	// planner has to schedule stops for it.
	auto plan = planned_dive(45_m, 25 * 60);
	QVERIFY(!plan->dcs[0].samples.empty());

	auto c = compare_dives(plan.get(), plan.get());
	QVERIFY(c.valid);
	// A plan is authoritative about its own decompression, so it is never
	// "unknown" even when the answer is zero.
	QVERIFY(c.plan_deco_known);
	QVERIFY(c.deco_time_comparable());
	QVERIFY(c.plan_deco_time.seconds > 0);
	// and the deco time is a part of the dive, not longer than the whole
	QVERIFY(c.plan_deco_time.seconds < plan->dcs[0].duration.seconds);

	// A no-stop dive plans no stops, and says so rather than saying nothing.
	auto shallow = planned_dive(12_m, 20 * 60);
	auto c2 = compare_dives(shallow.get(), shallow.get());
	QVERIFY(c2.valid);
	QVERIFY(c2.plan_deco_known);
	QCOMPARE(c2.plan_deco_time.seconds, 0);
}

void TestDiveComparison::testSelectableDivesSkipsProfilelessDives()
{
	divelog.clear();

	// two comparable dives ...
	auto a = square_dive(30000, 1200);
	a->number = 1;
	a->when = 1000;
	// ... and one without a profile, which cannot be compared and so must not
	// be offered as a choice
	auto noProfile = std::make_unique<struct dive>();
	noProfile->number = 2;
	noProfile->when = 2000;
	auto b = square_dive(20000, 600);
	b->number = 3;
	b->when = 3000;

	divelog.dives.record_dive(std::move(a));
	divelog.dives.record_dive(std::move(noProfile));
	divelog.dives.record_dive(std::move(b));

	std::vector<const struct dive *> dives = comparable_dives();
	QCOMPARE((int)dives.size(), 2);
	// newest first
	QCOMPARE(dives[0]->number, 3);
	QCOMPARE(dives[1]->number, 1);

	// and the two it offers really do compare
	auto c = compare_dives(dives[1], dives[0]);
	QVERIFY(c.valid);

	divelog.clear();
}

QTEST_GUILESS_MAIN(TestDiveComparison)
