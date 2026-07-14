#pragma once
#include <score/command/AggregateCommand.hpp>

#include <Scenario/Commands/ScenarioCommandFactory.hpp>

#include <Process/TimeValue.hpp>

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
class StateModel;

/**
 * Promoted sequences: the sequence's structure lives directly in the parent
 * scenario as native elements, instead of being encapsulated in a process.
 *
 * Terminology (with Pia):
 *  - the SEQUENCE BRANCH: a chain of section intervals B1 — IS1 — B2 — … — Bn,
 *    where each IS is a real (state, event, timesync) shared between two
 *    consecutive sections (state: previous = Bk, next = Bk+1). Being native,
 *    ISes can carry triggers and be synced with anything in the scenario.
 *    Sections hold one Automation per numeric parameter and one Gradient per
 *    color parameter.
 *  - the PARALLEL BRANCH ("host"): one interval spanning the whole sequence,
 *    holding all other processes. It is flexible (min = nominal, max = ∞) so
 *    it keeps playing until the shared end sync AND-fires.
 *
 * Both branches share the sequence's start and end syncs (the diamond).
 *
 * Entry point: dragging the blue + from a state (Tool::CreateSequence):
 *  - bare state: immediately creates a promoted sequence up to the released
 *    date, seeding one lane per parameter of the state (ramping from the
 *    state's value to the current device value, as the old process did);
 *  - end state of a plain interval: converts it (automations/gradients move
 *    to the sequence branch) and extends to the released date;
 *  - end state of an old encapsulated-Sequence host: migrates it (curves and
 *    all), then extends;
 *  - end state of a promoted sequence: extends.
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

//! Blue-+ released on a state that ends `member` (see file comment).
bool convertOrExtend(
    const score::DocumentContext& ctx, const ProcessModel& scenar,
    const IntervalModel& member, TimeVal newEndDate);

//! Blue-+ released from a bare state: create a promoted sequence from
//! scratch, seeded from the state's parameters.
bool createFromState(
    const score::DocumentContext& ctx, const ProcessModel& scenar,
    const StateModel& startState, TimeVal endDate);
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
class CreatePromotedSequence final : public score::AggregateCommand
{
  SCORE_COMMAND_DECL(CommandFactoryName(), CreatePromotedSequence, "Create sequence")
};
}
}
