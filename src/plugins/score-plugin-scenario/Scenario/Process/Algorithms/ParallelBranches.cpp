#include "ParallelBranches.hpp"

#include <Scenario/Document/Interval/IntervalModel.hpp>
#include <Scenario/Document/TimeSync/TimeSyncModel.hpp>
#include <Scenario/Process/Algorithms/Accessors.hpp>
#include <Scenario/Process/ScenarioModel.hpp>

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Scenario::ParallelBranches
{
namespace
{
using SyncSet = std::unordered_set<const TimeSyncModel*>;

// Syncs reachable from `from` following intervals forward, never through
// `avoid`, never through graph edges. Includes `from`.
SyncSet forwardReach(
    const Scenario::ProcessModel& s, const TimeSyncModel& from,
    const IntervalModel* avoid)
{
  SyncSet seen{&from};
  std::vector<const TimeSyncModel*> stack{&from};
  while(!stack.empty())
  {
    auto tn = stack.back();
    stack.pop_back();
    for(const auto& itvId : nextNonGraphIntervals(*tn, s))
    {
      auto& itv = s.interval(itvId);
      if(&itv == avoid)
        continue;
      auto& next = Scenario::endTimeSync(itv, s);
      if(seen.insert(&next).second)
        stack.push_back(&next);
    }
  }
  return seen;
}

// Syncs from which `to` is reachable. Includes `to`.
SyncSet backwardReach(
    const Scenario::ProcessModel& s, const TimeSyncModel& to,
    const IntervalModel* avoid)
{
  SyncSet seen{&to};
  std::vector<const TimeSyncModel*> stack{&to};
  while(!stack.empty())
  {
    auto tn = stack.back();
    stack.pop_back();
    for(const auto& itvId : previousNonGraphIntervals(*tn, s))
    {
      auto& itv = s.interval(itvId);
      if(&itv == avoid)
        continue;
      auto& prev = Scenario::startTimeSync(itv, s);
      if(seen.insert(&prev).second)
        stack.push_back(&prev);
    }
  }
  return seen;
}

// Any active trigger or non-rigid interval in the upstream cone of `tn`
// (the syncs/intervals from which tn can be reached), tn's own trigger
// included.
bool upstreamNondeterministic(
    const Scenario::ProcessModel& s, const TimeSyncModel& tn)
{
  SyncSet seen{&tn};
  std::vector<const TimeSyncModel*> stack{&tn};
  while(!stack.empty())
  {
    auto cur = stack.back();
    stack.pop_back();
    if(cur->active())
      return true;
    for(const auto& itvId : previousNonGraphIntervals(*cur, s))
    {
      auto& itv = s.interval(itvId);
      if(!itv.duration.isRigid())
        return true;
      auto& prev = Scenario::startTimeSync(itv, s);
      if(seen.insert(&prev).second)
        stack.push_back(&prev);
    }
  }
  return false;
}
}

bool hasPathAvoiding(
    const Scenario::ProcessModel& s, const TimeSyncModel& from,
    const TimeSyncModel& to, const IntervalModel* avoid)
{
  if(&from == &to)
    return true;
  return forwardReach(s, from, avoid).count(&to) > 0;
}

TimeVal parallelFloor(const Scenario::ProcessModel& s, const IntervalModel& itv)
{
  auto& from = Scenario::startTimeSync(itv, s);
  auto& to = Scenario::endTimeSync(itv, s);

  const auto F = forwardReach(s, from, &itv);
  const auto B = backwardReach(s, to, &itv);

  // Longest path over the parallel region (PERT forward pass): an edge is in
  // the region iff its start can be reached from `from` and its end can still
  // reach `to`. Masked mins: what the engine will actually enforce.
  std::unordered_map<const TimeSyncModel*, TimeVal> earliest;
  earliest[&from] = TimeVal::zero();

  bool changed = true;
  while(changed)
  {
    changed = false;
    for(auto tn : F)
    {
      auto it = earliest.find(tn);
      if(it == earliest.end())
        continue;
      for(const auto& itvId : nextNonGraphIntervals(*tn, s))
      {
        auto& edge = s.interval(itvId);
        if(&edge == &itv)
          continue;
        auto& end = Scenario::endTimeSync(edge, s);
        if(!B.count(&end))
          continue;
        const TimeVal cand = it->second + edge.duration.minDuration();
        auto [eit, inserted] = earliest.emplace(&end, cand);
        if(!inserted && cand > eit->second)
        {
          eit->second = cand;
          changed = true;
        }
        else if(inserted)
        {
          changed = true;
        }
      }
    }
  }

  auto found = earliest.find(&to);
  return found != earliest.end() ? found->second : itv.duration.defaultDuration();
}

FlavorResult flavorFor(const Scenario::ProcessModel& s, const IntervalModel& itv)
{
  if(itv.graphal())
    return {};

  auto& sync = Scenario::endTimeSync(itv, s);
  const auto incoming = previousNonGraphIntervals(sync, s);
  if(incoming.size() < 2)
    return {};

  // Nothing to absorb in a fully deterministic diamond; and a trigger on the
  // shared sync itself already makes every incoming branch waitable.
  bool nondeterministic = sync.active();
  if(!nondeterministic)
  {
    for(const auto& sibId : incoming)
    {
      auto& sib = s.interval(sibId);
      if(&sib == &itv)
        continue;
      if(!sib.duration.isRigid()
         || upstreamNondeterministic(s, Scenario::startTimeSync(sib, s)))
      {
        nondeterministic = true;
        break;
      }
    }
  }
  if(!nondeterministic)
    return {};

  auto& start = Scenario::startTimeSync(itv, s);
  if(hasPathAvoiding(s, start, sync, &itv))
    return {Flavor::Elastic, parallelFloor(s, itv)};

  return {Flavor::ExtendOnly, itv.duration.defaultDuration()};
}

void applyFlavor(IntervalModel& itv, const FlavorResult& f)
{
  if(f.flavor == Flavor::Rigid)
    return;

  auto& d = itv.duration;
  d.setRigid(false);
  // minNull must be cleared before setMinDuration (silent no-op otherwise)
  d.setMinNull(false);
  d.setMaxInfinite(true);
  d.setMinDuration(f.min);
}
}
