# Planning and post-dive analysis - August 2026

Third document in the series. `2026-07-software-review.md` records what was
wrong with the codebase; `2026-07-update-plan.md` records the order in which to
fix it. This one records the diver-facing work: the plan-versus-dive comparison,
the defects found in the planner while building it, and what is left.

Every finding below is quoted from the tree as it stands, and every fix listed as
done is in the history of this branch.

---

## 1. What a diver asked for, and what was possible

The original request was to import a plan into a dive computer and read the dive
back out, so the two could be compared. Half of that already works and the other
half cannot be made to work:

- **Reading the dive back** is what Subsurface does for a living. Both the Suunto
  Ocean and the Ratio computers download through libdivecomputer.
- **Writing a plan to the computer** is not possible. libdivecomputer implements
  any write at all in six backends (`hw_ostc3`, `oceanic_atom2` and the older
  serial Suuntos), and what those write is device *settings* - clock, units,
  personal factors - not dive plans. There is no plan-upload protocol for either
  the Suunto Ocean or the Ratio, from libdivecomputer or from anything else that
  is public. No amount of work in this repository changes that.

So the comparison is done inside Subsurface. The planner already stores a plan as
an ordinary dive ("Save new"), which makes both sides of the comparison just
dives, and the dive computer is only ever a source.

## 2. The comparison

`core/divecomparison.{h,cpp}` computes the numbers, `qt-models/divecomparisonmodel.{h,cpp}`
presents them, and both the desktop (`desktop-widgets/divecomparisondialog.cpp`,
reachable from the dive list when exactly two dives are selected) and mobile
(`mobile-widgets/qml/DiveComparison.qml`, in the drawer) use the same model.

It reports max depth, mean depth, duration, decompression time, gas used and time
spent above the decompression ceiling, each as plan / dive / difference; the
largest depth deviation from the planned profile at the same elapsed time,
interpolated between samples; the deepest single ceiling breach; and any ascent
that exceeded 10 m/min.

`tests/testdivecomparison.cpp` covers it, including one case built through the
planner rather than through a hand-written fixture, so the planner side is
exercised end to end.

## 3. Defects found while building it

Building the comparison meant reading the planner closely. Everything in this
section was found that way and is fixed on this branch.

### 3.1 A plan recorded no trace of its own decompression

`create_dive_from_plan()` wrote depth, time, setpoint and gas onto the samples
but never `in_deco`. A saved deco plan was therefore indistinguishable from a
no-stop dive, and the comparison read every plan as having had zero deco.

The visible result was that every deco dive compared against its plan showed
"Plan 0:00 / Dive 12:00 / +12:00" in red. The single most safety-relevant row in
the table cried wolf on every dive, which teaches a diver to ignore it before the
day it is genuinely +6:00.

`plan()` now marks the segments it schedules as stops and `create_dive_from_plan()`
carries the flag onto the samples. The schedule itself is unchanged - the
existing planner tests pass without modification.

### 3.2 An unknown deco time is not a zero deco time

Plenty of dive computers record no decompression state at all. The comparison
treated "the computer said nothing" and "there was no deco" as the same thing,
and so attributed the other side's entire deco time to the difference.

`dive_comparison` now carries `plan_deco_known` / `actual_deco_known`, and the UI
prints `n/a` rather than a difference nobody measured.

### 3.3 The desktop entry point picked the wrong dive as the plan

It decided which of the two selected dives was the plan by comparing timestamps,
on the reasoning that a plan is made before the dive. But the planner stamps a
new plan an hour into the *future* (`qt-models/diveplannermodel.cpp`), so the
heuristic picked the wrong one almost every time and inverted the sign of every
delta. A dive 4.2 m deeper than planned was reported as 4.2 m shallower. That is
a wrong debrief, not a missing one.

It now asks the dive whether it is a planned dive.

### 3.4 A CCR plan got no oxygen warnings at all

`core/plannernotes.cpp` gated its only two pO2 checks - high and low - behind
`if (dive.dcs[0].divemode != CCR)`. The body of that loop already asks for the
dive mode at each point, so it was written to handle a plan that switches modes,
and a CCR plan's bailout legs are exactly the open circuit gas at depth that a
diver most wants checked. A closed circuit plan produced no oxygen warning of
any kind, whatever gas it put the diver on.

Two errors inside the same calculation came with it:

- On closed circuit, the partial pressure passed to `fill_pressures()` was
  ambient pressure times the mix's oxygen fraction - what the diluent would give
  on open circuit, not the setpoint the diver actually breathes.
- On pSCR the same expression drove `fill_pressures()` down its closed circuit
  branch, reporting the open circuit partial pressure and ignoring the dilution
  a semi-closed rebreather causes. `core/divelist.cpp` and `core/profile.cpp`
  both use `pscr_o2()` for this; the planner now agrees with them.

### 3.5 A setpoint cylinder was parsed with an unchecked `sscanf`

A cylinder named `SP 1.3` sets the setpoint to 1.3 bar. The value was read with

    float sp;
    sscanf(cylinder->type.description.c_str() + 3, "%f", &sp);
    return (int) (sp * 1000.0);

The return value was discarded and `sp` was never initialised, so a description
the parser could not handle - including `SP 1,3`, which is how much of the world
writes it - planned the whole dive at whatever happened to be on the stack.

It now parses with `permissive_strtod()`, which accepts either decimal separator
regardless of locale, and falls back to treating the cylinder as an ordinary one.

### 3.6 The planner's depth-time integral overflowed

`average_max_depth()` accumulated depth times time into a `depth_t`, a 32-bit
millimetre count. A plan averaging 80 m over eight hours reaches 2.3e9 mm.s and
wraps. The result feeds `ascent_velocity()`, so the consequence is not a slightly
wrong number - it is a wrong ascent rate for the whole dive. Long deep dives are
exactly the ones a planner is for. It is 64-bit now.

## 4. Desktop and mobile parity

### 4.1 The mobile planner did not know where the water was

Mobile had no altitude or atmospheric pressure input at all, so
`DivePlannerPointsModel::calculatePlan()` fell back to the surface pressure of a
dive that had none: sea level. A plan for a mountain lake was silently computed
as if it were at the coast, which shortens the ascent the diver is told to make.
The desktop planner has offered the control since forever. Mobile now has the
same setting over the same range.

### 4.2 Mobile settings exposed two of the profile preferences

The mobile profile is drawn by the same `ProfileScene` as the desktop one and
reads the same preferences, but mobile settings exposed only the DC reported
ceiling and the calculated ceiling. Added: all tissue ceilings, 3 m ceiling
rounding, the gas bar, the pO2 graph, the CCR setpoint, the CCR oxygen sensors
and the pSCR open circuit pO2.

Not added, deliberately: pN2 and pHe graphs, heart rate, the tissue percentage
graph and the mean depth line. `ProfileScene::updateVisibility()` takes a
`simplified` branch on mobile which does not place those items at all, so a
switch for them would change a preference and nothing on screen. Making them work
on mobile is a change to the renderer, not to the settings page, and is listed
below rather than pretended at.

## 5. What is left

In rough order of how much it matters to a diver.

### 5.1 Ceiling violations are not reported for a single dive

`core/profile.cpp` computes a per-sample ceiling for every dive, logged ones
included, but only compared it with the diver's actual depth when planning:

    // In the planner, if the ceiling is violated, add an event.
    if (in_planner && !pi.waypoint_above_ceiling &&
        entry.depth.mm < max_ceiling.mm - 100 && entry.sec > 0) {

The comparison now does that comparison itself, for both sides, and reports
"Time above ceiling" and "Deepest ceiling breach". It calls
`create_plot_info_new()` from `core/divecomparison.cpp` and reads `entry.ceiling`
rather than duplicating the deco model, and it uses the same 10 cm slop the
planner does so the two agree about what counts as a violation. The test builds
the clean side through the planner rather than by hand, because hand writing a
"clean" profile is only a guess at the schedule.

What is still missing is the same answer for **one** dive, without a plan to
compare it against - which is the common case. That needs a surface in the dive
details on both platforms, not more computation: `analyse_ceiling()` already
returns everything such a view would show. It is not simply reusing the planner's
path, because that path casts away `const` and writes an event into the dive,
which is acceptable for a throwaway planner dive and not for the user's log.

### 5.2 A CCR plan gets no minimum gas line

The minimum gas calculation in `core/plannernotes.cpp` is gated on

    dive.dcs[0].divemode == OC && pref_deco_mode(true) != RECREATIONAL

so a rebreather plan never gets one - including when `prefs.dobailout` is set and
the whole ascent is therefore planned on open circuit. That is the case where the
number matters most.

This is left undone on purpose. Extending it means deciding which cylinder's
reserve is being checked on a rebreather and what "problem solving" assumes when
the diver is already on a bailout gas. Getting that wrong produces a green
"Minimum gas" figure that is too optimistic, which is worse than no figure at
all. It needs a rebreather diver's judgement, not an inference from the OC code.

### 5.3 Per-dive ascent rate readout

The comparison flags ascents above 10 m/min against a plan. A logged dive on its
own gets no such readout, though the samples make it trivial.

### 5.4 Statistics beyond CNS, OTU and surface interval

`stats/` gained three variables - CNS, OTU and surface interval - so the
statistics view can answer "how did my oxygen exposure accumulate across this
trip" and "how long was I up between dives", which are ordinary questions for
anyone diving nitrox repetitively. OTU can be summed and CNS cannot, because OTU
accumulates across a day of diving while CNS decays between dives. A dive that
reports zero exposure is left out rather than counted as a clean dive: a dive
with no profile also reports zero, and counting those would pull an exposure
histogram towards a floor nobody actually dived.

`stats/` had no test at all before this; `tests/teststatsvariables.cpp` is the
first, and `TEST_EXTRA_LIBRARIES` in `tests/CMakeLists.txt` lets it link
`subsurface_stats` without adding that library to every other test binary.

Still missing: gradient factor and ceiling-derived variables, and anything that
needs the profile rather than the dive summary - the variables all read fields
that fixup already computed.

### 5.5 Mobile profile items the simplified renderer omits

See 4.2. Heart rate, the tissue percentage graph, the mean depth line and the
pN2/pHe traces are not drawn on mobile at all. Whether they should be is a
question about what fits on a phone screen, and answering it belongs with the
people who chose the simplified layout.
