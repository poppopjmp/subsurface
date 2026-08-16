// SPDX-License-Identifier: GPL-2.0
#ifndef TESTDIVECOMPARISON_H
#define TESTDIVECOMPARISON_H

#include "testbase.h"

class TestDiveComparison : public TestBase {
	Q_OBJECT
private slots:
	void testRejectsMissingDives();
	void testIdenticalDivesHaveNoDeltas();
	void testDeeperAndLongerThanPlanned();
	void testDepthDeviationIsInterpolated();
	void testAscentRateViolations();
	void testDecoTimeFromSamples();
	void testDecoTimeUnknownIsNotZero();
	void testPlannerRecordsItsOwnDecoTime();
	void testCeilingBreachIsDetectedOnLoggedDives();
	void testSelectableDivesSkipsProfilelessDives();
};

#endif
