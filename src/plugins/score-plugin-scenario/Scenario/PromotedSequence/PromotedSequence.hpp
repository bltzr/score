#pragma once
#include <score/command/AggregateCommand.hpp>

#include <Scenario/Commands/ScenarioCommandFactory.hpp>

#include <optional>
#include <vector>

namespace score
{
struct DocumentContext;
}
namespace Scenario
{
class ProcessModel;
class IntervalModel;

/**
 * Promoted sequences: the sequence's structure lives directly in the parent
 * scenario as native elements, instead of being encapsulated in a process.
 *
 * Terminology (with Pia):
 *  - the SEQUENCE BRANCH: a chain of section intervals B1 — IS1 — B2 — … — Bn,
 *    where each IS is a real (state, event, timesync) shared between two
 *    consecutive sections (state: previous = Bk, next = Bk+1). Being native,
 *    ISes can carry triggers and be synced with anything in the scenario.
 *  - the PARALLEL BRANCH ("host"): one interval spanning the whole sequence,
 *    holding all non-automation processes. It is flexible (min = nominal,
 *    max = ∞) so it keeps playing until the shared end sync AND-fires.
 *
 * Both branches share the sequence's start and end syncs (the diamond).
 */
namespace PromotedSequence
{
struct Structure
{
  IntervalModel* host{};                  // the parallel branch
  std::vector<IntervalModel*> sections;   // the sequence branch, in order
};

//! Recognize the structure from any member interval (host or section).
//! Purely structural — no stored identity.
std::optional<Structure> locate(const ProcessModel& scenar, const IntervalModel& any);

//! Convert an existing interval into a sequence: its automations move to a
//! new single-section sequence branch; everything else stays in the interval,
//! which becomes the (flexible) parallel branch.
bool convertToSequence(
    const score::DocumentContext& ctx, const ProcessModel& scenar,
    const IntervalModel& host);

//! Append a section at the end of the sequence branch: the old shared end
//! becomes an intermediate IS, a new section continues each parameter from
//! its boundary value, and the parallel branch stretches accordingly.
bool extendSequence(
    const score::DocumentContext& ctx, const ProcessModel& scenar,
    const IntervalModel& anyMember);
}

namespace Command
{
class ConvertToPromotedSequence final : public score::AggregateCommand
{
  SCORE_COMMAND_DECL(
      CommandFactoryName(), ConvertToPromotedSequence, "Convert to sequence")
};
class ExtendPromotedSequence final : public score::AggregateCommand
{
  SCORE_COMMAND_DECL(CommandFactoryName(), ExtendPromotedSequence, "Extend sequence")
};
}
}
