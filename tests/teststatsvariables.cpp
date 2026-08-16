// SPDX-License-Identifier: GPL-2.0
#include "teststatsvariables.h"

#include "core/dive.h"
#include "core/divelist.h"
#include "core/divelog.h"
#include "stats/statsvariables.h"

#include <QTest>

#include <algorithm>

namespace {

// The variables are looked up by the name they present in the UI, because that
// is the only handle the statistics view itself has on them.
const StatsVariable *findVariable(const QString &name)
{
	auto it = std::find_if(stats_variables.begin(), stats_variables.end(),
			       [&name](const StatsVariable *v) { return v->name() == name; });
	return it == stats_variables.end() ? nullptr : *it;
}

// A dive with nothing but the fields these variables read. Duration and oxygen
// exposure are normally derived from the profile by fixup, which record_dive()
// runs; set them afterwards so the test is about the variables rather than
// about the exposure maths, and so fixup does not zero them again.
dive *addDive(timestamp_t when, int duration_seconds, int maxcns, int otu)
{
	auto d = std::make_unique<struct dive>();
	d->when = when;
	dive *res = d.get();
	divelog.dives.record_dive(std::move(d));

	res->duration.seconds = duration_seconds;
	res->maxcns = maxcns;
	res->otu = otu;
	return res;
}

} // namespace

void TestStatsVariables::testOxygenExposureVariables()
{
	divelog.clear();
	dive *a = addDive(0, 3600, 12, 40);
	// A dive with no oxygen exposure recorded. It is not a dive that generated
	// none - a dive with no profile also reports zero - so it must not be
	// counted as a clean dive in an exposure statistic.
	dive *b = addDive(10000, 3600, 0, 0);

	const StatsVariable *cns = findVariable("CNS");
	const StatsVariable *otu = findVariable("OTU");
	QVERIFY(cns != nullptr);
	QVERIFY(otu != nullptr);
	QCOMPARE(cns->type(), StatsVariable::Type::Numeric);
	QCOMPARE(otu->type(), StatsVariable::Type::Numeric);

	std::vector<dive *> dives = { a, b };
	auto cnsValues = cns->values(dives);
	QCOMPARE((int)cnsValues.size(), 1);
	QCOMPARE(cnsValues[0].v, 12.0);
	QCOMPARE(cnsValues[0].d, a);

	auto otuValues = otu->values(dives);
	QCOMPARE((int)otuValues.size(), 1);
	QCOMPARE(otuValues[0].v, 40.0);

	// OTU accumulates over a day of diving and so can be summed; CNS decays
	// between dives and so cannot.
	auto otuOps = otu->supportedOperations();
	QVERIFY(std::find(otuOps.begin(), otuOps.end(), StatsOperation::Sum) != otuOps.end());
	auto cnsOps = cns->supportedOperations();
	QVERIFY(std::find(cnsOps.begin(), cnsOps.end(), StatsOperation::Sum) == cnsOps.end());

	divelog.clear();
}

void TestStatsVariables::testSurfaceIntervalVariable()
{
	divelog.clear();
	// one hour under, two hours up, one hour under, half an hour up, ...
	dive *first = addDive(0, 3600, 0, 0);
	dive *second = addDive(3600 + 7200, 3600, 0, 0);
	dive *third = addDive(3600 + 7200 + 3600 + 1800, 3600, 0, 0);

	const StatsVariable *interval = findVariable("Surface interval");
	QVERIFY(interval != nullptr);

	// The first dive in the log has no dive before it, so it has no interval -
	// counting it as zero would put a spurious dive in the shortest bin. The
	// other two are measured from the end of the preceding dive, not its start.
	auto values = interval->values({ first, second, third });
	QCOMPARE((int)values.size(), 2);
	// sorted ascending by value
	QCOMPARE(values[0].v, 0.5);
	QCOMPARE(values[0].d, third);
	QCOMPARE(values[1].v, 2.0);
	QCOMPARE(values[1].d, second);

	divelog.clear();
}

void TestStatsVariables::testBinning()
{
	divelog.clear();
	addDive(0, 3600, 4, 5);		// CNS 4%  -> 0-5 bin
	addDive(10000, 3600, 7, 15);	// CNS 7%  -> 5-10 bin
	addDive(20000, 3600, 9, 95);	// CNS 9%  -> 5-10 bin as well
	addDive(30000, 3600, 0, 0);	// no exposure, must not land in any bin

	const StatsVariable *cns = findVariable("CNS");
	QVERIFY(cns != nullptr);
	auto binners = cns->binners();
	QVERIFY(!binners.empty());

	std::vector<dive *> dives;
	for (auto &d: divelog.dives)
		dives.push_back(d.get());
	QCOMPARE((int)dives.size(), 4);

	// The first binner is the finest one, 5% steps.
	auto bins = binners[0]->bin_dives(dives, false);
	QCOMPARE((int)bins.size(), 2);
	QCOMPARE((int)bins[0].value.size(), 1);
	QCOMPARE((int)bins[1].value.size(), 2);

	divelog.clear();
}

QTEST_GUILESS_MAIN(TestStatsVariables)
