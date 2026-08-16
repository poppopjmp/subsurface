// SPDX-License-Identifier: GPL-2.0
#ifndef TESTSTATSVARIABLES_H
#define TESTSTATSVARIABLES_H

#include "testbase.h"

class TestStatsVariables : public TestBase {
	Q_OBJECT
private slots:
	void testOxygenExposureVariables();
	void testSurfaceIntervalVariable();
	void testBinning();
};

#endif
