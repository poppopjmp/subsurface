// SPDX-License-Identifier: GPL-2.0
//
// Desktop half of the plan/dive comparison. The QML page in
// mobile-widgets/qml/DiveComparison.qml is the mobile half; both drive the same
// DiveComparisonModel so the two platforms show the same numbers.

#ifndef DIVECOMPARISONDIALOG_H
#define DIVECOMPARISONDIALOG_H

#include <QDialog>

class DiveComparisonModel;
class QLabel;
class QTableView;

struct dive;

class DiveComparisonDialog : public QDialog {
	Q_OBJECT
public:
	explicit DiveComparisonDialog(QWidget *parent = nullptr);

	// Which dive is the plan and which is the dive that was made matters: the
	// differences are reported as actual - plan.
	void setDives(const struct dive *plan, const struct dive *actual);

private:
	DiveComparisonModel *model;
	QTableView *table;
	QLabel *message;
	QLabel *ascents;
};

#endif // DIVECOMPARISONDIALOG_H
