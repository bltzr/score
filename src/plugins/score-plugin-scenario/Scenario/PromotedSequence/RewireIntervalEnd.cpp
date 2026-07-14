// This is an open source non-commercial project. Dear PVS-Studio, please check
// it. PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#include "RewireIntervalEnd.hpp"

#include <Scenario/Document/Interval/IntervalModel.hpp>
#include <Scenario/Document/State/StateModel.hpp>
#include <Scenario/Process/Algorithms/ProcessPolicy.hpp>
#include <Scenario/Process/ScenarioModel.hpp>

#include <score/model/path/PathSerialization.hpp>

namespace Scenario::Command
{
RewireIntervalEnd::RewireIntervalEnd(
    const Scenario::ProcessModel& scenar, const IntervalModel& itv,
    const StateModel& newEnd)
    : m_scenario{scenar}
    , m_itv{itv.id()}
    , m_oldEnd{itv.endState()}
    , m_newEnd{newEnd.id()}
{
}

void RewireIntervalEnd::redo(const score::DocumentContext& ctx) const
{
  auto& scenar = m_scenario.find(ctx);
  auto& itv = scenar.intervals.at(m_itv);
  auto& oldSt = scenar.states.at(m_oldEnd);
  auto& newSt = scenar.states.at(m_newEnd);

  // Unregister the interval's processes from the old end state, re-point the
  // interval, register on the new state. SetPreviousInterval also wires the
  // process-state boundary sync (ProcessPolicy).
  SetNoPreviousInterval(oldSt);
  itv.setEndState(newSt.id());
  SetPreviousInterval(newSt, itv);
}

void RewireIntervalEnd::undo(const score::DocumentContext& ctx) const
{
  auto& scenar = m_scenario.find(ctx);
  auto& itv = scenar.intervals.at(m_itv);
  auto& oldSt = scenar.states.at(m_oldEnd);
  auto& newSt = scenar.states.at(m_newEnd);

  SetNoPreviousInterval(newSt);
  itv.setEndState(oldSt.id());
  SetPreviousInterval(oldSt, itv);
}

void RewireIntervalEnd::serializeImpl(DataStreamInput& s) const
{
  s << m_scenario << m_itv << m_oldEnd << m_newEnd;
}

void RewireIntervalEnd::deserializeImpl(DataStreamOutput& s)
{
  s >> m_scenario >> m_itv >> m_oldEnd >> m_newEnd;
}
}
