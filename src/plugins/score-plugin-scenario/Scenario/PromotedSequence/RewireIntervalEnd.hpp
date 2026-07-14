#pragma once
#include <Scenario/Commands/ScenarioCommandFactory.hpp>

#include <score/command/Command.hpp>
#include <score/model/path/Path.hpp>
#include <score/model/Identifier.hpp>

namespace Scenario
{
class ProcessModel;
class IntervalModel;
class StateModel;

namespace Command
{
/**
 * Re-point an interval's end to another state.
 *
 * Used when extending a promoted sequence: the last section's end is moved
 * from the shared end state to a freshly created intermediate IS state, so a
 * new section can be created between the IS and the (subsequently moved)
 * shared end.
 *
 * Precondition: the new end state must sit at the same date as the old one
 * (this command does not touch durations), and must have no previous
 * interval.
 */
class RewireIntervalEnd final : public score::Command
{
  SCORE_COMMAND_DECL(
      CommandFactoryName(), RewireIntervalEnd, "Rewire an interval's end")
public:
  RewireIntervalEnd(
      const Scenario::ProcessModel& scenar, const IntervalModel& itv,
      const StateModel& newEnd);

  void undo(const score::DocumentContext& ctx) const override;
  void redo(const score::DocumentContext& ctx) const override;

protected:
  void serializeImpl(DataStreamInput&) const override;
  void deserializeImpl(DataStreamOutput&) override;

private:
  Path<Scenario::ProcessModel> m_scenario;
  Id<IntervalModel> m_itv;
  Id<StateModel> m_oldEnd;
  Id<StateModel> m_newEnd;
};
}
}
