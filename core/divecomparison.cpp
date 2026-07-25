// SPDX-License-Identifier: GPL-2.0

#include "divecomparison.h"

#include "dive.h"
#include "divecomputer.h"
#include "sample.h"
#include "statistics.h"

#include <algorithm>

// Depth of the dive at an elapsed time, linearly interpolated between samples.
// Returns 0 before the first sample and holds the last depth after the final
// one, which is what "the dive ended" looks like for comparison purposes.
static depth_t depth_at(const struct divecomputer &dc, int seconds)
{
	if (dc.samples.empty())
		return depth_t();

	const struct sample *prev = nullptr;
	for (const auto &s: dc.samples) {
		if (s.time.seconds == seconds)
			return s.depth;
		if (s.time.seconds > seconds) {
			if (!prev)
				return depth_t();
			int span = s.time.seconds - prev->time.seconds;
			if (span <= 0)
				return prev->depth;
			int into = seconds - prev->time.seconds;
			depth_t res;
			res.mm = prev->depth.mm +
				 (int)((int64_t)(s.depth.mm - prev->depth.mm) * into / span);
			return res;
		}
		prev = &s;
	}
	return prev ? prev->depth : depth_t();
}

// Time spent under a mandatory stop. Samples carry in_deco, and where a dive
// computer does not set it we fall back to a non-zero stopdepth, which is the
// same thing expressed differently.
static duration_t deco_time(const struct divecomputer &dc)
{
	duration_t total;
	const struct sample *prev = nullptr;
	for (const auto &s: dc.samples) {
		if (prev && (prev->in_deco || prev->stopdepth.mm > 0))
			total.seconds += s.time.seconds - prev->time.seconds;
		prev = &s;
	}
	return total;
}

static std::vector<ascent_violation> find_ascent_violations(const struct divecomputer &dc,
							    int limit_mm_per_min)
{
	std::vector<ascent_violation> res;
	if (limit_mm_per_min <= 0)
		return res;

	const struct sample *prev = nullptr;
	for (const auto &s: dc.samples) {
		if (prev) {
			int span = s.time.seconds - prev->time.seconds;
			int rise = prev->depth.mm - s.depth.mm; // positive going up
			if (span > 0 && rise > 0) {
				int rate = (int)((int64_t)rise * 60 / span);
				if (rate > limit_mm_per_min) {
					ascent_violation v;
					v.start = prev->time;
					v.duration.seconds = span;
					v.from = prev->depth;
					v.to = s.depth;
					v.rate_mm_per_min = rate;
					res.push_back(v);
				}
			}
		}
		prev = &s;
	}
	return res;
}

static volume_t total_gas_used(const struct dive *d)
{
	volume_t total;
	// get_gas_used() only reads the dive, but predates const correctness here.
	for (const auto &g: get_gas_used(const_cast<struct dive *>(d)))
		total.mliter += g.mliter;
	return total;
}

dive_comparison compare_dives(const struct dive *plan, const struct dive *actual,
			      int ascent_limit_mm_per_min)
{
	dive_comparison res;

	if (!plan || !actual) {
		res.error = "need both a planned dive and an actual dive to compare";
		return res;
	}
	// A default constructed dive already carries one (empty) divecomputer, so
	// checking dcs.empty() is not enough - what makes a dive comparable is
	// having a profile.
	if (plan->dcs.empty() || actual->dcs.empty() ||
	    plan->dcs[0].samples.empty() || actual->dcs[0].samples.empty()) {
		res.error = "one of the dives has no profile to compare";
		return res;
	}

	const struct divecomputer &plan_dc = plan->dcs[0];
	const struct divecomputer &actual_dc = actual->dcs[0];

	res.plan_maxdepth = plan->maxdepth;
	res.actual_maxdepth = actual->maxdepth;
	res.plan_meandepth = plan->meandepth;
	res.actual_meandepth = actual->meandepth;
	res.plan_duration = plan->duration;
	res.actual_duration = actual->duration;
	res.plan_deco_time = deco_time(plan_dc);
	res.actual_deco_time = deco_time(actual_dc);
	res.plan_gas_used = total_gas_used(plan);
	res.actual_gas_used = total_gas_used(actual);

	res.maxdepth_delta = res.actual_maxdepth - res.plan_maxdepth;
	res.meandepth_delta = res.actual_meandepth - res.plan_meandepth;
	res.duration_delta = res.actual_duration - res.plan_duration;
	res.deco_time_delta = res.actual_deco_time - res.plan_deco_time;
	res.gas_used_delta = res.actual_gas_used - res.plan_gas_used;

	// Largest gap between the two profiles at the same elapsed time. Walk the
	// actual dive's samples, since that is the one being evaluated.
	for (const auto &s: actual_dc.samples) {
		depth_t planned = depth_at(plan_dc, s.time.seconds);
		int diff = std::abs(s.depth.mm - planned.mm);
		if (diff > res.max_depth_deviation.mm) {
			res.max_depth_deviation.mm = diff;
			res.max_depth_deviation_at = s.time;
		}
	}

	res.ascent_violations = find_ascent_violations(actual_dc, ascent_limit_mm_per_min);

	res.valid = true;
	return res;
}
