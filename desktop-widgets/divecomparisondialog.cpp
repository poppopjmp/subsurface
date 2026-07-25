// SPDX-License-Identifier: GPL-2.0

#include "desktop-widgets/divecomparisondialog.h"

#include "qt-models/divecomparisonmodel.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableView>
#include <QVBoxLayout>

DiveComparisonDialog::DiveComparisonDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(tr("Plan vs. dive"));

	model = new DiveComparisonModel(this);

	message = new QLabel(this);
	message->setWordWrap(true);
	message->hide();

	table = new QTableView(this);
	table->setModel(model);
	table->setSelectionMode(QAbstractItemView::NoSelection);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->verticalHeader()->hide();
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	table->setAlternatingRowColors(true);

	ascents = new QLabel(this);
	ascents->setWordWrap(true);
	ascents->hide();

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(message);
	layout->addWidget(table);
	layout->addWidget(ascents);
	layout->addWidget(buttons);

	resize(560, 320);
}

void DiveComparisonDialog::setDives(const struct dive *plan, const struct dive *actual)
{
	model->setDives(plan, actual);

	// When there is nothing to compare, say why instead of showing an empty
	// table that looks like a comparison with no differences.
	const bool valid = model->isValid();
	table->setVisible(valid);
	message->setVisible(!valid);
	if (!valid) {
		message->setText(model->errorString());
		ascents->hide();
		return;
	}

	const QStringList violations = model->ascentViolations();
	if (violations.isEmpty()) {
		ascents->hide();
	} else {
		ascents->setText(QStringLiteral("<b>%1</b><br>%2")
					 .arg(tr("Ascent rate exceeded"), violations.join("<br>")));
		ascents->show();
	}
}
