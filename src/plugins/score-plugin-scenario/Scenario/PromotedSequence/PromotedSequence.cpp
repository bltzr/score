// This is an open source non-commercial project. Dear PVS-Studio, please check
// it. PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#include "PromotedSequence.hpp"

#include "RewireIntervalEnd.hpp"
#include "SetFlexible.hpp"

#include <Process/ExpandMode.hpp>

#include <State/Message.hpp>
#include <State/ValueConversion.hpp>

#include <Curve/Segment/Linear/LinearSegment.hpp>

#include <Automation/AutomationModel.hpp>
#include <Automation/Commands/InitAutomation.hpp>
#include <Automation/AutomationProcessMetadata.hpp>

#include <Scenario/Commands/CommandAPI.hpp>
#include <Scenario/Commands/Scenario/Displacement/MoveEventMeta.hpp>
#include <Scenario/Document/Event/EventModel.hpp>
#include <Scenario/Document/Interval/IntervalModel.hpp>
#include <Scenario/Document/State/StateModel.hpp>
#include <Scenario/Document/TimeSync/TimeSyncModel.hpp>
#include <Scenario/Process/Algorithms/Accessors.hpp>
#include <Scenario/Process/ScenarioModel.hpp>
#include <Scenario/Sequence/SequenceModel.hpp>

namespace Scenario::PromotedSequence
{
namespace
{
// ---- curve/value helpers ----

// Normalized y of an automation's curve at its start (x = 0) / end (x = 1).
static double curveStartY(const Automation::ProcessModel& a)
{
  double bestX = std::numeric_limits<double>::max();
  double y = 0.;
  for(auto& seg : a.curve().segments())
  {
    if(seg.start().x() < bestX)
    {
      bestX = seg.start().x();
      y = seg.start().y();
    }
  }
  return y;
}

static double curveEndY(const Automation::ProcessModel& a)
{
  double bestX = std::numeric_limits<double>::lowest();
  double y = 0.;
  for(auto& seg : a.curve().segments())
  {
    if(seg.end().x() > bestX)
    {
      bestX = seg.end().x();
      y = seg.end().y();
    }
  }
  return y;
}

static double realValue(const Automation::ProcessModel& a, double normY)
{
  return a.min() + normY * (a.max() - a.min());
}

// One flat linear segment at normalized value v.
static std::vector<Curve::SegmentData> flatSegments(double v)
{
  std::vector<Curve::SegmentData> segs;
  segs.push_back(Curve::SegmentData{
      Id<Curve::SegmentModel>{0},
      Curve::Point{0., v},
      Curve::Point{1., v},
      {},
      {},
      Metadata<ConcreteKey_k, Curve::LinearSegment>::get(),
      QVariant::fromValue(Curve::LinearSegmentData{})});
  return segs;
}

// ---- structure walking ----

// Walk a chain forward from a state: returns the sections traversed and the
// final state (which has no next interval), or nullopt on inconsistency.
struct ChainWalk
{
  std::vector<IntervalModel*> sections;
  const StateModel* tail{};
};

static std::optional<ChainWalk>
walkChain(const ProcessModel& scenar, const StateModel& head)
{
  ChainWalk w;
  const StateModel* cur = &head;
  int guard = 10000;
  while(cur->nextInterval() && guard-- > 0)
  {
    auto it = scenar.intervals.find(*cur->nextInterval());
    if(it == scenar.intervals.end())
      return std::nullopt;
    auto& itv = *it;
    w.sections.push_back(const_cast<IntervalModel*>(&itv));
    auto st = scenar.states.find(itv.endState());
    if(st == scenar.states.end())
      return std::nullopt;
    cur = &*st;
  }
  if(guard <= 0)
    return std::nullopt;
  w.tail = cur;
  return w;
}

// Try to interpret `h` as the host of a sequence: look for a parallel chain
// of >= 1 intervals between h's start and end syncs.
static std::optional<Structure>
locateFromHost(const ProcessModel& scenar, const IntervalModel& h)
{
  auto& startSync = Scenario::startTimeSync(h, scenar);
  auto& endSync = Scenario::endTimeSync(h, scenar);
  if(&startSync == &endSync)
    return std::nullopt;

  for(auto& stId : Scenario::states(startSync, scenar))
  {
    auto st = scenar.states.find(stId);
    if(st == scenar.states.end())
      continue;
    if(st->id() == h.startState())
      continue;
    if(!st->nextInterval())
      continue;
    if(*st->nextInterval() == h.id())
      continue;

    auto walk = walkChain(scenar, *st);
    if(!walk || walk->sections.empty())
      continue;
    // chain must not contain the host and must end on the host's end sync
    bool containsHost = false;
    for(auto* s : walk->sections)
      if(s->id() == h.id())
        containsHost = true;
    if(containsHost)
      continue;

    auto& tailEv = scenar.events.at(walk->tail->eventId());
    if(tailEv.timeSync() == endSync.id())
    {
      Structure res;
      res.host = const_cast<IntervalModel*>(&h);
      res.sections = std::move(walk->sections);
      return res;
    }
  }
  return std::nullopt;
}

// ---- the two operations, composable into a single macro ----

// Convert: fan two new states off the host's start/end events, create the
// first section between them, move the host's automations into it, make the
// host flexible, record boundary values on the shared states.
// Returns the created section.
static IntervalModel& convertInto(
    Scenario::Command::Macro& m, const ProcessModel& scenar, const IntervalModel& host)
{
  std::vector<Id<Process::ProcessModel>> autos;
  for(auto& proc : host.processes)
  {
    if(qobject_cast<const Automation::ProcessModel*>(&proc))
      autos.push_back(proc.id());
  }

  const double y = std::min(0.9, host.heightPercentage() + 0.1);
  auto& startEv = Scenario::startEvent(host, scenar);
  auto& endEv = Scenario::endEvent(host, scenar);

  auto& s0 = m.createState(scenar, startEv.id(), y);
  auto& s1 = m.createState(scenar, endEv.id(), y);
  auto& b1 = m.createInterval(scenar, s0.id(), s1.id());

  for(auto& id : autos)
    m.moveProcess(host, b1, id);

  // The parallel branch: plays at least its nominal length, then keeps
  // playing until the shared end sync fires.
  m.submit(new Scenario::Command::SetFlexible{host, host.duration.defaultDuration()});

  // The IS-boundary values live on the shared states.
  State::MessageList startMsgs, endMsgs;
  for(auto& proc : b1.processes)
  {
    if(auto a = qobject_cast<const Automation::ProcessModel*>(&proc))
    {
      startMsgs.push_back(State::Message{
          a->address(), ossia::value{float(realValue(*a, curveStartY(*a)))}});
      endMsgs.push_back(State::Message{
          a->address(), ossia::value{float(realValue(*a, curveEndY(*a)))}});
    }
  }
  if(!startMsgs.empty())
    m.addMessages(s0, std::move(startMsgs));
  if(!endMsgs.empty())
    m.addMessages(s1, std::move(endMsgs));

  return b1;
}

// Extend: new IS at the current end date, rewire the last section onto it,
// push the shared end sync to newEndDate, create the new section, continue
// every parameter flat from its boundary value.
static void extendInto(
    Scenario::Command::Macro& m, const ProcessModel& scenar, const Structure& st,
    TimeVal newEndDate)
{
  auto& host = *st.host;
  auto& last = *st.sections.back();
  auto& tailState = Scenario::endState(last, scenar); // on the shared end sync
  auto& hostEndEv = Scenario::endEvent(host, scenar);
  auto& endSync = Scenario::endTimeSync(host, scenar);

  const TimeVal endDate = endSync.date();
  const double yChain = last.heightPercentage();

  // 1. A new IS where the old end was.
  auto [isSync, isEv, isState]
      = m.createDot(scenar, Scenario::Point{endDate, yChain});

  // 2. The last section now ends at the IS instead of the shared end.
  m.submit(new Scenario::Command::RewireIntervalEnd{scenar, last, isState});

  // 3. Push the shared end forward; the parallel branch stretches
  //    (GrowShrink: its content keeps its absolute timing).
  m.submit(new Scenario::Command::MoveEventMeta{
      scenar, hostEndEv.id(), newEndDate, host.heightPercentage(),
      ExpandMode::GrowShrink, LockMode::Free});

  // 4. New section between the IS and the shared end.
  auto& bNew = m.createInterval(scenar, isState.id(), tailState.id());

  // 5. Continue every parameter from its boundary value (flat, same domain).
  State::MessageList isMsgs;
  for(auto& proc : last.processes)
  {
    auto a = qobject_cast<const Automation::ProcessModel*>(&proc);
    if(!a)
      continue;

    const double vNorm = curveEndY(*a);
    auto created = m.createProcess(
        bNew, Metadata<ConcreteKey_k, Automation::ProcessModel>::get(), QString{},
        QPointF{});
    if(!created)
      continue;
    auto& newAuto = *safe_cast<Automation::ProcessModel*>(created);
    m.submit(new Automation::InitAutomation{
        newAuto, a->address(), a->min(), a->max(), flatSegments(vNorm)});

    isMsgs.push_back(
        State::Message{a->address(), ossia::value{float(realValue(*a, vNorm))}});
  }
  if(!isMsgs.empty())
    m.addMessages(isState, std::move(isMsgs));
}

// Migrate an old encapsulated Sequence process into the promoted form:
// recreate its internal sections/ISes as native elements (full curve copy),
// remove the old process, make the host flexible. Returns the new sections.
static std::vector<IntervalModel*> migrateInto(
    Scenario::Command::Macro& m, const ProcessModel& scenar, const IntervalModel& host,
    const Sequence::SequenceModel& seq)
{
  // Old sections, in temporal order (internal dates are host-relative).
  std::vector<const IntervalModel*> oldSections;
  for(auto& itv : seq.intervals)
    oldSections.push_back(&itv);
  std::sort(oldSections.begin(), oldSections.end(), [](auto* a, auto* b) {
    return a->date() < b->date();
  });

  const TimeVal base = host.date();
  const double y = std::min(0.9, host.heightPercentage() + 0.1);
  auto& startEv = Scenario::startEvent(host, scenar);
  auto& endEv = Scenario::endEvent(host, scenar);

  std::vector<IntervalModel*> newSections;
  const StateModel* prev = &m.createState(scenar, startEv.id(), y);

  for(std::size_t k = 0; k < oldSections.size(); ++k)
  {
    auto& sec = *oldSections[k];
    const bool lastSec = (k == oldSections.size() - 1);

    const StateModel* next{};
    if(lastSec)
    {
      next = &m.createState(scenar, endEv.id(), y);
    }
    else
    {
      const TimeVal isDate = base + Scenario::endTimeSync(sec, seq).date();
      auto [ts, ev, st] = m.createDot(scenar, Scenario::Point{isDate, y});
      next = &st;
    }

    auto& newItv = m.createInterval(scenar, prev->id(), next->id());
    newSections.push_back(&newItv);

    State::MessageList startMsgs, endMsgs;
    for(auto& proc : sec.processes)
    {
      auto a = qobject_cast<const Automation::ProcessModel*>(&proc);
      if(!a)
        continue; // V1: gradients/other section processes are not migrated

      auto created = m.createProcess(
          newItv, Metadata<ConcreteKey_k, Automation::ProcessModel>::get(), QString{},
          QPointF{});
      if(!created)
        continue;
      auto& newAuto = *safe_cast<Automation::ProcessModel*>(created);
      m.submit(new Automation::InitAutomation{
          newAuto, a->address(), a->min(), a->max(), a->curve().toCurveData()});

      if(k == 0)
        startMsgs.push_back(State::Message{
            a->address(), ossia::value{float(realValue(*a, curveStartY(*a)))}});
      endMsgs.push_back(State::Message{
          a->address(), ossia::value{float(realValue(*a, curveEndY(*a)))}});
    }
    if(!startMsgs.empty())
      m.addMessages(*prev, std::move(startMsgs));
    if(!endMsgs.empty())
      m.addMessages(*next, std::move(endMsgs));

    prev = next;
  }

  m.removeProcess(host, seq.id());
  m.submit(new Scenario::Command::SetFlexible{host, host.duration.defaultDuration()});

  return newSections;
}

}

std::optional<Structure> locate(const ProcessModel& scenar, const IntervalModel& any)
{
  // Case 1: `any` is the host.
  if(auto s = locateFromHost(scenar, any))
    return s;

  // Case 2: `any` is a section. Walk back to the chain head, then find the
  // host among the states of the head's sync.
  const StateModel* head = &Scenario::startState(any, scenar);
  int guard = 10000;
  while(head->previousInterval() && guard-- > 0)
  {
    auto it = scenar.intervals.find(*head->previousInterval());
    if(it == scenar.intervals.end())
      return std::nullopt;
    head = &Scenario::startState(*it, scenar);
  }
  if(guard <= 0)
    return std::nullopt;

  auto walk = walkChain(scenar, *head);
  if(!walk || walk->sections.empty())
    return std::nullopt;
  auto& tailEv = scenar.events.at(walk->tail->eventId());

  auto& headEv = scenar.events.at(head->eventId());
  auto headSync = scenar.timeSyncs.find(headEv.timeSync());
  if(headSync == scenar.timeSyncs.end())
    return std::nullopt;

  for(auto& stId : Scenario::states(*headSync, scenar))
  {
    auto st = scenar.states.find(stId);
    if(st == scenar.states.end())
      continue;
    if(st->id() == head->id())
      continue;
    if(!st->nextInterval())
      continue;
    auto hostIt = scenar.intervals.find(*st->nextInterval());
    if(hostIt == scenar.intervals.end())
      continue;
    auto& candidate = *hostIt;
    // candidate must span head sync -> tail sync directly and not be a section
    bool isSection = false;
    for(auto* s : walk->sections)
      if(s->id() == candidate.id())
        isSection = true;
    if(isSection)
      continue;
    auto& cEndEv = Scenario::endEvent(candidate, scenar);
    if(cEndEv.timeSync() == tailEv.timeSync())
    {
      Structure res;
      res.host = const_cast<IntervalModel*>(&candidate);
      res.sections = std::move(walk->sections);
      return res;
    }
  }
  return std::nullopt;
}

bool convertOrExtend(
    const score::DocumentContext& ctx, const ProcessModel& scenar,
    const IntervalModel& member, TimeVal newEndDate)
{
  using namespace Scenario::Command;

  if(auto st = locate(scenar, member))
  {
    // Already a sequence: extend to the released date.
    auto& endSync = Scenario::endTimeSync(*st->host, scenar);
    if(newEndDate <= endSync.date() + TimeVal::fromMsecs(1))
      return false;

    Macro m{new ExtendPromotedSequence, ctx};
    extendInto(m, scenar, *st, newEndDate);
    m.commit();
    return true;
  }

  // Old encapsulated Sequence process? Migrate it to the promoted form,
  // then extend if the drag went beyond the end.
  const Sequence::SequenceModel* oldSeq{};
  for(auto& proc : member.processes)
  {
    if(auto s = qobject_cast<const Sequence::SequenceModel*>(&proc))
    {
      oldSeq = s;
      break;
    }
  }
  if(oldSeq)
  {
    Macro m{new ConvertToPromotedSequence, ctx};
    auto sections = migrateInto(m, scenar, member, *oldSeq);
    if(sections.empty())
      return false;

    auto& endSync = Scenario::endTimeSync(member, scenar);
    if(newEndDate > endSync.date() + TimeVal::fromMsecs(10))
    {
      Structure st;
      st.host = const_cast<IntervalModel*>(&member);
      st.sections = std::move(sections);
      extendInto(m, scenar, st, newEndDate);
    }
    m.commit();
    return true;
  }

  // Not a sequence yet: convert, and extend if the drag went beyond the end.
  Macro m{new ConvertToPromotedSequence, ctx};
  auto& b1 = convertInto(m, scenar, member);

  auto& endSync = Scenario::endTimeSync(member, scenar);
  if(newEndDate > endSync.date() + TimeVal::fromMsecs(10))
  {
    Structure st;
    st.host = const_cast<IntervalModel*>(&member);
    st.sections = {&b1};
    extendInto(m, scenar, st, newEndDate);
  }

  m.commit();
  return true;
}
}
