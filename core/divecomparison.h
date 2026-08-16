// SPDX-License-Identifier: GPL-2.0
//
// Compare a planned dive against the dive that was actually made.
//
// The planner already stores a plan as an ordinary dive ("Save new" in the
// planner), so both sides of the comparison are just dives and no dive computer
// interaction is involved. That matters, because libdivecomputer has no way to
// write a plan to a dive computer: only six backends implement any write at all
// (hw_ostc3, oceanic_atom2 and the older serial Suuntos), and those write device
// settings, not dive plans.
//
// The numbers here are deliberately the ones a diver would check afterwards:
// did I go deeper, stay longer, pick up more deco or use more gas than planned,
// and did I ascend faster than I should have.

#ifndef DIVECOMPARISON_H
#define DIVECOMPARISON_H

#include "units.h"

#include <string>
#include <vector>

struct dive;

// One ascent that exceeded the rate limit, in the dive being evaluated.
struct ascent_violation {
	duration_t start;	// when it began
	duration_t duration;
	depth_t from;
	depth_t to;
	int rate_mm_per_min;	// actual ascent rate
};

struct dive_comparison {
	bool valid = false;		// false when either side was missing
	std::string error;		// why, when not valid

	depth_t plan_maxdepth, actual_maxdepth;
	depth_t plan_meandepth, actual_meandepth;
	duration_t plan_duration, actual_duration;
	// Time spent in mandatory decompression, derived from the samples. Plenty
	// of dive computers never record a decompression state at all, and a dive
	// that does not say it was in deco is not the same thing as a dive that
	// was not: without the *_deco_known flag below, an unknown reads as zero
	// and every comparison against it shows the other side as pure excess.
	duration_t plan_deco_time, actual_deco_time;
	bool plan_deco_known = false, actual_deco_known = false;
	// Sum over all cylinders.
	volume_t plan_gas_used, actual_gas_used;

	// actual - plan. Positive means the dive exceeded the plan.
	depth_t maxdepth_delta, meandepth_delta;
	duration_t duration_delta;
	// Only meaningful when both sides of the deco time are known.
	duration_t deco_time_delta;
	volume_t gas_used_delta;

	bool deco_time_comparable() const { return plan_deco_known && actual_deco_known; }

	// Sampled every sample of the actual dive: how far its depth was from the
	// planned depth at the same elapsed time. Useful as a single "did I fly the
	// profile" number.
	depth_t max_depth_deviation;
	duration_t max_depth_deviation_at;

	std::vector<ascent_violation> ascent_violations;
};

// The rate above which an ascent is reported. Subsurface's own default ascent
// rates are below this; 10 m/min is the widely taught recreational ceiling.
constexpr int default_ascent_limit_mm_per_min = 10000;

dive_comparison compare_dives(const struct dive *plan, const struct dive *actual,
			      int ascent_limit_mm_per_min = default_ascent_limit_mm_per_min);

// The dives in the log that can take part in a comparison, newest first. A dive
// without a profile cannot be compared, so offering it as a choice would only
// produce an error once picked.
std::vector<const struct dive *> comparable_dives();

#endif // DIVECOMPARISON_H
