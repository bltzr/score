// This is an open source non-commercial project. Dear PVS-Studio, please check
// it. PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#include "SetFlexible.hpp"

#include <Process/TimeValueSerialization.hpp>

#include <Scenario/Document/Interval/IntervalModel.hpp>

#include <score/model/path/PathSerialization.hpp>

namespace Scenario::Command
{
SetFlexible::SetFlexible(const IntervalModel& itv, TimeVal min)
    : m_path{itv}
    , m_newMin{min}
    , m_oldRigid{itv.duration.isRigid()}
    , m_oldMinNull{itv.duration.isMinNull()}
    , m_oldMaxInf{itv.duration.isMaxInfinite()}
    , m_oldMin{itv.duration.minDuration()}
    , m_oldMax{itv.duration.maxDuration()}
{
}

void SetFlexible::redo(const score::DocumentContext& ctx) const
{
  auto& dur = m_path.find(ctx).duration;
  dur.setRigid(false);
  // Order matters: setMinDuration is a no-op while minNull is set.
  dur.setMinNull(false);
  dur.setMinDuration(m_newMin);
  dur.setMaxInfinite(true);
}

void SetFlexible::undo(const score::DocumentContext& ctx) const
{
  auto& dur = m_path.find(ctx).duration;
  dur.setMinNull(false);
  dur.setMinDuration(m_oldMin);
  dur.setMinNull(m_oldMinNull);
  dur.setMaxInfinite(false);
  dur.setMaxDuration(m_oldMax);
  dur.setMaxInfinite(m_oldMaxInf);
  dur.setRigid(m_oldRigid);
}

void SetFlexible::serializeImpl(DataStreamInput& s) const
{
  s << m_path << m_newMin << m_oldRigid << m_oldMinNull << m_oldMaxInf << m_oldMin
    << m_oldMax;
}

void SetFlexible::deserializeImpl(DataStreamOutput& s)
{
  s >> m_path >> m_newMin >> m_oldRigid >> m_oldMinNull >> m_oldMaxInf >> m_oldMin
      >> m_oldMax;
}
}
