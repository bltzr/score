#pragma once
#include <Scenario/Commands/ScenarioCommandFactory.hpp>

#include <score/command/Command.hpp>
#include <score/model/path/Path.hpp>

#include <Process/TimeValue.hpp>

namespace Scenario
{
class IntervalModel;
namespace Command
{
/**
 * Make an interval flexible for a "diamond" configuration: it plays at least
 * `min`, then keeps playing (max = infinite) until its end sync fires
 * (AND-convergence with the parallel branches).
 *
 * Unlike SetRigidity(false) — which is trigger-oriented and sets min = 0 —
 * this keeps a real minimum, so a trigger-less shared end sync cannot fire
 * before the parallel structure allows it.
 */
class SetFlexible final : public score::Command
{
  SCORE_COMMAND_DECL(CommandFactoryName(), SetFlexible, "Make an interval flexible")
public:
  SetFlexible(const IntervalModel& itv, TimeVal min);

  void undo(const score::DocumentContext& ctx) const override;
  void redo(const score::DocumentContext& ctx) const override;

protected:
  void serializeImpl(DataStreamInput&) const override;
  void deserializeImpl(DataStreamOutput&) override;

private:
  Path<IntervalModel> m_path;
  TimeVal m_newMin{};

  // saved state for undo
  bool m_oldRigid{};
  bool m_oldMinNull{};
  bool m_oldMaxInf{};
  TimeVal m_oldMin{};
  TimeVal m_oldMax{};
};
}
}
