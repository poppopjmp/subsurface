// SPDX-License-Identifier: GPL-2.0

#include "qt-models/divecomparisonmodel.h"

#include "core/dive.h"
#include "core/divelist.h"
#include "core/divelog.h"
#include "core/qthelper.h"
#include "core/string-format.h"

#include <QCoreApplication>
#include <QVariantMap>

DiveComparisonModel::DiveComparisonModel(QObject *parent) : QAbstractTableModel(parent)
{
}

// Deltas are signed, and a bare "-3.0 m" reads as "3 m less" rather than
// "3 m under the plan". Prefix a plus so the direction is unambiguous.
static QString withSign(const QString &formatted, int raw)
{
	if (raw > 0)
		return QStringLiteral("+") + formatted;
	return formatted;
}

void DiveComparisonModel::setDives(const struct dive *plan, const struct dive *actual)
{
	beginResetModel();
	comparison = compare_dives(plan, actual);
	rebuild();
	endResetModel();
	emit comparisonChanged();
}

void DiveComparisonModel::setDiveIds(int planId, int actualId)
{
	setDives(divelog.dives.get_by_uniq_id(planId), divelog.dives.get_by_uniq_id(actualId));
}

QVariantList DiveComparisonModel::selectableDives() const
{
	QVariantList res;
	for (const struct dive *d: comparable_dives()) {
		QVariantMap entry;
		entry["id"] = d->id;
		QString location = QString::fromStdString(d->get_location());
		entry["label"] = location.isEmpty()
					 ? tr("#%1 %2").arg(d->number).arg(get_short_dive_date_string(d->when))
					 : tr("#%1 %2 - %3").arg(d->number).arg(get_short_dive_date_string(d->when), location);
		res.append(entry);
	}
	return res;
}

void DiveComparisonModel::clear()
{
	beginResetModel();
	comparison = dive_comparison();
	rows.clear();
	endResetModel();
	emit comparisonChanged();
}

void DiveComparisonModel::rebuild()
{
	rows.clear();
	if (!comparison.valid)
		return;

	const bool units = true; // always show units, the numbers are meaningless without

	rows.push_back({tr("Max depth"),
			get_depth_string(comparison.plan_maxdepth, units),
			get_depth_string(comparison.actual_maxdepth, units),
			withSign(get_depth_string(comparison.maxdepth_delta, units),
				 comparison.maxdepth_delta.mm),
			comparison.maxdepth_delta.mm > 0});

	rows.push_back({tr("Mean depth"),
			get_depth_string(comparison.plan_meandepth, units),
			get_depth_string(comparison.actual_meandepth, units),
			withSign(get_depth_string(comparison.meandepth_delta, units),
				 comparison.meandepth_delta.mm),
			comparison.meandepth_delta.mm > 0});

	rows.push_back({tr("Duration"),
			formatMinutes(comparison.plan_duration.seconds),
			formatMinutes(comparison.actual_duration.seconds),
			withSign(formatMinutes(comparison.duration_delta.seconds),
				 comparison.duration_delta.seconds),
			comparison.duration_delta.seconds > 0});

	// A dive computer that never reported a decompression state has not told us
	// the deco time was zero, so say we do not know rather than turning the
	// other side's real deco into a difference it did not have.
	const QString unknown = tr("n/a");
	rows.push_back({tr("Deco time"),
			comparison.plan_deco_known ? formatMinutes(comparison.plan_deco_time.seconds)
						   : unknown,
			comparison.actual_deco_known ? formatMinutes(comparison.actual_deco_time.seconds)
						     : unknown,
			comparison.deco_time_comparable()
				? withSign(formatMinutes(comparison.deco_time_delta.seconds),
					   comparison.deco_time_delta.seconds)
				: unknown,
			comparison.deco_time_comparable() && comparison.deco_time_delta.seconds > 0});

	rows.push_back({tr("Gas used"),
			get_volume_string(comparison.plan_gas_used, units),
			get_volume_string(comparison.actual_gas_used, units),
			withSign(get_volume_string(comparison.gas_used_delta, units),
				 comparison.gas_used_delta.mliter),
			comparison.gas_used_delta.mliter > 0});

	// Not a plan/actual pair - it is a single number describing how far the
	// dive strayed from the planned profile, so only the delta column is filled.
	rows.push_back({tr("Max deviation from plan"),
			QString(),
			QString(),
			QStringLiteral("%1 @ %2")
				.arg(get_depth_string(comparison.max_depth_deviation, units),
				     formatMinutes(comparison.max_depth_deviation_at.seconds)),
			comparison.max_depth_deviation.mm > 0});
}

bool DiveComparisonModel::isValid() const
{
	return comparison.valid;
}

QString DiveComparisonModel::errorString() const
{
	if (comparison.valid)
		return QString();
	return QString::fromStdString(comparison.error);
}

QStringList DiveComparisonModel::ascentViolations() const
{
	QStringList res;
	for (const auto &v: comparison.ascent_violations) {
		res << tr("%1 to %2 at %3/min, starting at %4")
			   .arg(get_depth_string(v.from, true),
				get_depth_string(v.to, true),
				get_depth_string(depth_t{ .mm = v.rate_mm_per_min }, true),
				formatMinutes(v.start.seconds));
	}
	return res;
}

int DiveComparisonModel::rowCount(const QModelIndex &parent) const
{
	if (parent.isValid())
		return 0;
	return (int)rows.size();
}

int DiveComparisonModel::columnCount(const QModelIndex &parent) const
{
	if (parent.isValid())
		return 0;
	return COLUMNS;
}

QVariant DiveComparisonModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= (int)rows.size())
		return QVariant();

	const Row &row = rows[index.row()];

	switch (role) {
	case MetricRole: return row.metric;
	case PlanRole: return row.plan;
	case ActualRole: return row.actual;
	case DeltaRole: return row.delta;
	case ExceededRole: return row.exceeded;
	case Qt::DisplayRole:
		switch (index.column()) {
		case METRIC: return row.metric;
		case PLAN: return row.plan;
		case ACTUAL: return row.actual;
		case DELTA: return row.delta;
		}
		return QVariant();
	case Qt::TextAlignmentRole:
		return index.column() == METRIC ? QVariant(Qt::AlignLeft | Qt::AlignVCenter)
						: QVariant(Qt::AlignRight | Qt::AlignVCenter);
	}
	return QVariant();
}

QVariant DiveComparisonModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
		return QVariant();

	switch (section) {
	case METRIC: return tr("Metric");
	case PLAN: return tr("Plan");
	case ACTUAL: return tr("Dive");
	case DELTA: return tr("Difference");
	}
	return QVariant();
}

QHash<int, QByteArray> DiveComparisonModel::roleNames() const
{
	return {
		{ MetricRole, "metric" },
		{ PlanRole, "planValue" },
		{ ActualRole, "actualValue" },
		{ DeltaRole, "delta" },
		{ ExceededRole, "exceeded" },
	};
}
