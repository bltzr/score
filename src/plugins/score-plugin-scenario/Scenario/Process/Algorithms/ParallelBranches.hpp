#pragma once
#include <Process/TimeValue.hpp>

#include <score_plugin_scenario_export.h>

namespace Scenario
{
class ProcessModel;
class IntervalModel;
class TimeSyncModel;

/**
 * Wait-absorption for "diamond" configurations (parallel branches converging
 * on a shared sync): any interval that can be made to WAIT at its end sync
 * must be flexible, so that early arrival is absorbed by keep-playing instead
 * of freezing the branch green-and-silent.
 *
 * Two flexible configurations exist:
 * - Elastic (cut + extend): the interval is a *redundant edge* — a parallel
 *   path joins its own two syncs — so it is accompaniment by construction.
 *   min := PERT floor of the parallel structure (masked mins, max at
 *   convergences), max := ∞.
 * - ExtendOnly (never cut): a structural lane that can be outlasted by a
 *   sibling branch. min := its own default duration, max := ∞.
 *
 * Deterministic diamonds (no trigger, no non-rigid interval anywhere in the
 * sibling structure) are left rigid: there is nothing to absorb.
 */
namespace ParallelBranches
{
enum class Flavor
{
  Rigid,
  Elastic,
  ExtendOnly
};

struct FlavorResult
{
  Flavor flavor{Flavor::Rigid};
  TimeVal min{};
};

//! Is there a directed path from -> to that does not use `avoid`?
//! (graph-edge intervals are never followed)
SCORE_PLUGIN_SCENARIO_EXPORT bool hasPathAvoiding(
    const Scenario::ProcessModel& s, const TimeSyncModel& from,
    const TimeSyncModel& to, const IntervalModel* avoid);

//! Earliest possible fire date of endSync(itv) relative to startSync(itv),
//! computed over the parallel structure excluding itv itself, with masked
//! minimums (a min-null interval contributes 0, as the engine sees it).
SCORE_PLUGIN_SCENARIO_EXPORT TimeVal
parallelFloor(const Scenario::ProcessModel& s, const IntervalModel& itv);

//! The wait-absorption taxonomy for one interval, evaluated on the current
//! graph. Returns Rigid when the interval should keep (or get back) its
//! authored rigidity.
SCORE_PLUGIN_SCENARIO_EXPORT FlavorResult
flavorFor(const Scenario::ProcessModel& s, const IntervalModel& itv);

//! Apply a flavor directly to the interval's durations (creation-time use,
//! where undo is the removal of the interval itself). Rigid is not applied.
SCORE_PLUGIN_SCENARIO_EXPORT void
applyFlavor(IntervalModel& itv, const FlavorResult& f);
}
}
