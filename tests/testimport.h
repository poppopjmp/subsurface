// SPDX-License-Identifier: GPL-2.0
#ifndef TESTIMPORT_H
#define TESTIMPORT_H

#include "testbase.h"

class TestImport : public TestBase {
	Q_OBJECT
private slots:
	void initTestCase();
	void testEmptyInput();
	void testTruncatedInput();
	void testGarbageInput();
	void testCochranSizeBoundary();
};

#endif
