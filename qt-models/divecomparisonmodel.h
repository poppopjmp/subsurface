// SPDX-License-Identifier: GPL-2.0
//
// Presents a dive_comparison (core/divecomparison.h) for display.
//
// This lives in the generic model sources so that it is compiled into both
// subsurface_models_desktop and subsurface_models_mobile: the desktop dialog
// and the mobile QML page show the same rows, formatted the same way and in the
// user's own units, rather than each growing its own copy of the formatting.

#ifndef DIVECOMPARISONMODEL_H
#define DIVECOMPARISONMODEL_H

#include "core/divecomparison.h"

#include <QAbstractTableModel>
#include <QStringList>

struct dive;

class DiveComparisonModel : public QAbstractTableModel {
	Q_OBJECT
	// Exposed as properties rather than plain getters so the QML page can bind
	// to them; they all change together whenever the compared dives change.
	Q_PROPERTY(bool isValid READ isValid NOTIFY comparisonChanged)
	Q_PROPERTY(QString errorString READ errorString NOTIFY comparisonChanged)
	Q_PROPERTY(QStringList ascentViolations READ ascentViolations NOTIFY comparisonChanged)
public:
	enum Column {
		METRIC,
		PLAN,
		ACTUAL,
		DELTA,
		COLUMNS
	};

	// Named roles so QML can address the same data by name; the desktop table
	// view uses the columns above.
	enum Roles {
		MetricRole = Qt::UserRole + 1,
		PlanRole,
		ActualRole,
		DeltaRole,
		ExceededRole	// true when the dive went beyond the plan
	};

	explicit DiveComparisonModel(QObject *parent = nullptr);

	void setDives(const struct dive *plan, const struct dive *actual);
	// QML has no dive pointers, so let it name the dives by id.
	Q_INVOKABLE void setDiveIds(int planId, int actualId);
	Q_INVOKABLE void clear();

	bool isValid() const;
	// Empty when the comparison is valid.
	QString errorString() const;
	// One human readable line per ascent that exceeded the rate limit.
	QStringList ascentViolations() const;

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	int columnCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation,
			    int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

signals:
	void comparisonChanged();

private:
	struct Row {
		QString metric;
		QString plan;
		QString actual;
		QString delta;
		bool exceeded = false;
	};

	void rebuild();

	dive_comparison comparison;
	std::vector<Row> rows;
};

#endif // DIVECOMPARISONMODEL_H
