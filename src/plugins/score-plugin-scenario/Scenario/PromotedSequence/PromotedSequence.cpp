// This is an open source non-commercial project. Dear PVS-Studio, please check
// it. PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#include "PromotedSequence.hpp"

#include "RewireIntervalEnd.hpp"
#include "SetFlexible.hpp"

#include <Process/Commands/EditPort.hpp>
#include <Process/ExpandMode.hpp>
#include <Process/State/MessageNode.hpp>

#include <State/Message.hpp>
#include <State/ValueConversion.hpp>

#include <Device/Address/AddressSettings.hpp>
#include <Device/Node/DeviceNode.hpp>

#include <Explorer/DocumentPlugin/DeviceDocumentPlugin.hpp>
#include <Explorer/DocumentPlugin/NodeUpdateProxy.hpp>

#include <Curve/Segment/Linear/LinearSegment.hpp>

#include <Automation/AutomationModel.hpp>
#include <Automation/AutomationProcessMetadata.hpp>
#include <Automation/Commands/InitAutomation.hpp>

#include <Color/GradientMetadata.hpp>
#include <Color/GradientModel.hpp>
#include <Color/GradientPresenter.hpp>

#include <Scenario/Commands/CommandAPI.hpp>
#include <Scenario/Commands/Scenario/Displacement/MoveEventMeta.hpp>
#include <Scenario/Document/Event/EventModel.hpp>
#include <Scenario/Document/Interval/IntervalModel.hpp>
#include <Scenario/Document/State/ItemModel/MessageItemModel.hpp>
#include <Scenario/Document/State/StateModel.hpp>
#include <Scenario/Document/TimeSync/TimeSyncModel.hpp>
#include <Scenario/Process/Algorithms/Accessors.hpp>
#include <Scenario/Process/ScenarioModel.hpp>
#include <Scenario/Sequence/SequenceModel.hpp>

#include <ossia/network/common/destination_qualifiers.hpp>
#include <ossia/network/dataspace/color.hpp>
#include <ossia/network/dataspace/dataspace_variant_visitors.hpp>
#include <ossia/network/dataspace/dataspace_visitors.hpp>
#include <ossia/network/value/value_conversion.hpp>

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

// One linear segment from v0 to v1 (flat when equal).
static std::vector<Curve::SegmentData> rampSegments(double v0, double v1)
{
  std::vector<Curve::SegmentData> segs;
  segs.push_back(Curve::SegmentData{
      Id<Curve::SegmentModel>{0},
      Curve::Point{0., v0},
      Curve::Point{1., v1},
      {},
      {},
      Metadata<ConcreteKey_k, Curve::LinearSegment>::get(),
      QVariant::fromValue(Curve::LinearSegmentData{})});
  return segs;
}

// ---- color helpers (mirroring the old sequence process) ----

static const ossia::color_u* colorUnit(const State::AddressAccessor& addr)
{
  return addr.qualifiers.get().unit.v.target<ossia::color_u>();
}

struct color_to_qcolor
{
  template <typename Color>
  QColor operator()(const typename Color::value_type& value, const Color&)
  {
    auto rgba = ossia::rgba{ossia::strong_value<Color>{value}};
    auto& col = rgba.dataspace_value;
    return QColor::fromRgbF((qreal)col[0], (qreal)col[1], (qreal)col[2], (qreal)col[3]);
  }

  template <typename... Args>
  QColor operator()(Args&&...)
  {
    return QColor{};
  }
};

static QColor valueToColor(const ossia::value& v, const ossia::color_u& u)
{
  QColor c = ossia::apply(color_to_qcolor{}, v.v, u);
  if(!c.isValid())
  {
    const ossia::value coerced{ossia::convert<ossia::vec4f>(v)};
    c = ossia::apply(color_to_qcolor{}, coerced.v, u);
  }
  return c.isValid() ? c : QColor::fromRgbF(0., 0., 0., 1.);
}

static ossia::value colorToValue(const QColor& c, const ossia::color_u& u)
{
  ossia::rgba col{
      (float)c.redF(), (float)c.greenF(), (float)c.blueF(), (float)c.alphaF()};
  return ossia::to_value(ossia::convert(col, ossia::unit_t{u}));
}

static QColor gradientBoundaryColor(const Gradient::ProcessModel& g, bool end)
{
  const auto& stops = g.gradient();
  if(stops.empty())
    return QColor::fromRgbF(0., 0., 0., 1.);
  return end ? stops.rbegin()->second : stops.begin()->second;
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

// ---- lane creation helpers ----

// Create an automation lane in a section: address, domain, linear ramp.
static void addAutomationLane(
    Scenario::Command::Macro& m, const ProcessModel& scenar,
    const IntervalModel& section, const State::AddressAccessor& addr, double min,
    double max, double v0Norm, double v1Norm)
{
  auto created = m.createProcess(
      section, Metadata<ConcreteKey_k, Automation::ProcessModel>::get(), QString{},
      QPointF{});
  if(!created)
    return;
  auto& newAuto = *safe_cast<Automation::ProcessModel*>(created);
  m.submit(new Automation::InitAutomation{
      newAuto, addr, min, max, rampSegments(v0Norm, v1Norm)});
  m.addLayerInNewSlot(section, *created);
}

// Create a gradient lane in a section.
static void addGradientLane(
    Scenario::Command::Macro& m, const ProcessModel& scenar,
    const IntervalModel& section, const State::AddressAccessor& addr,
    const Gradient::ProcessModel::gradient_colors& stops)
{
  auto created = m.createProcess(
      section, Metadata<ConcreteKey_k, Gradient::ProcessModel>::get(), QString{},
      QPointF{});
  if(!created)
    return;
  auto& grad = *safe_cast<Gradient::ProcessModel*>(created);
  m.submit(new Process::ChangePortAddress{*grad.outlet, addr});
  m.submit(new Gradient::ChangeGradient{grad, stops});
  m.addLayerInNewSlot(section, *created);
}

// ---- the operations, composable into a single macro ----

// Convert: fan two new states off the host's start/end events, create the
// first section between them, move the host's automations and gradients into
// it, make the host flexible, record boundary values on the shared states.
// Returns the created section.
static IntervalModel& convertInto(
    Scenario::Command::Macro& m, const ProcessModel& scenar, const IntervalModel& host)
{
  std::vector<Id<Process::ProcessModel>> lanes;
  for(auto& proc : host.processes)
  {
    if(qobject_cast<const Automation::ProcessModel*>(&proc)
       || qobject_cast<const Gradient::ProcessModel*>(&proc))
      lanes.push_back(proc.id());
  }

  const double y = std::min(0.9, host.heightPercentage() + 0.1);
  auto& startEv = Scenario::startEvent(host, scenar);
  auto& endEv = Scenario::endEvent(host, scenar);

  auto& s0 = m.createState(scenar, startEv.id(), y);
  auto& s1 = m.createState(scenar, endEv.id(), y);
  auto& b1 = m.createInterval(scenar, s0.id(), s1.id());

  for(auto& id : lanes)
    m.moveProcess(host, b1, id);
  if(!lanes.empty())
    m.showRack(b1);

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
    else if(auto g = qobject_cast<const Gradient::ProcessModel*>(&proc))
    {
      const auto& addr = g->address();
      if(auto u = colorUnit(addr))
      {
        startMsgs.push_back(
            State::Message{addr, colorToValue(gradientBoundaryColor(*g, false), *u)});
        endMsgs.push_back(
            State::Message{addr, colorToValue(gradientBoundaryColor(*g, true), *u)});
      }
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
    if(auto a = qobject_cast<const Automation::ProcessModel*>(&proc))
    {
      const double vNorm = curveEndY(*a);
      addAutomationLane(
          m, scenar, bNew, a->address(), a->min(), a->max(), vNorm, vNorm);
      isMsgs.push_back(
          State::Message{a->address(), ossia::value{float(realValue(*a, vNorm))}});
    }
    else if(auto g = qobject_cast<const Gradient::ProcessModel*>(&proc))
    {
      const auto& addr = g->address();
      auto u = colorUnit(addr);
      if(!u)
        continue;
      const QColor c = gradientBoundaryColor(*g, true);
      Gradient::ProcessModel::gradient_colors stops;
      stops.insert(std::make_pair(0., c));
      addGradientLane(m, scenar, bNew, addr, stops);
      isMsgs.push_back(State::Message{addr, colorToValue(c, *u)});
    }
  }
  if(!isMsgs.empty())
  {
    m.addMessages(isState, std::move(isMsgs));
    m.showRack(bNew);
  }
}

// Migrate an old encapsulated Sequence process into the promoted form:
// recreate its internal sections/ISes as native elements (full curve and
// gradient copy), remove the old process, make the host flexible.
// Returns the new sections.
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
      if(auto a = qobject_cast<const Automation::ProcessModel*>(&proc))
      {
        auto created = m.createProcess(
            newItv, Metadata<ConcreteKey_k, Automation::ProcessModel>::get(), QString{},
            QPointF{});
        if(!created)
          continue;
        auto& newAuto = *safe_cast<Automation::ProcessModel*>(created);
        m.submit(new Automation::InitAutomation{
            newAuto, a->address(), a->min(), a->max(), a->curve().toCurveData()});
        m.addLayerInNewSlot(newItv, *created);

        if(k == 0)
          startMsgs.push_back(State::Message{
              a->address(), ossia::value{float(realValue(*a, curveStartY(*a)))}});
        endMsgs.push_back(State::Message{
            a->address(), ossia::value{float(realValue(*a, curveEndY(*a)))}});
      }
      else if(auto g = qobject_cast<const Gradient::ProcessModel*>(&proc))
      {
        const auto& addr = g->address();
        auto u = colorUnit(addr);
        addGradientLane(m, scenar, newItv, addr, g->gradient());
        if(u)
        {
          if(k == 0)
            startMsgs.push_back(State::Message{
                addr, colorToValue(gradientBoundaryColor(*g, false), *u)});
          endMsgs.push_back(
              State::Message{addr, colorToValue(gradientBoundaryColor(*g, true), *u)});
        }
      }
    }
    if(!startMsgs.empty())
      m.addMessages(*prev, std::move(startMsgs));
    if(!endMsgs.empty())
      m.addMessages(*next, std::move(endMsgs));
    m.showRack(newItv);

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

bool createFromState(
    const score::DocumentContext& ctx, const ProcessModel& scenar,
    const StateModel& startState, TimeVal endDate)
{
  using namespace Scenario::Command;

  if(startState.nextInterval())
    return false;

  auto& startEv = scenar.events.at(startState.eventId());
  if(endDate <= startEv.date() + TimeVal::fromMsecs(10))
    return false;

  Macro m{new CreatePromotedSequence, ctx};

  // The parallel branch, empty for now.
  auto& host = m.createIntervalAfter(
      scenar, startState.id(),
      Scenario::Point{endDate, startState.heightPercentage()});

  // The sequence branch: one section between fresh states on both events.
  auto& b1 = convertInto(m, scenar, host);

  // Seed one lane per parameter of the start state, ramping from the state's
  // value to the current device value — like the old process did.
  auto devPlugin = ctx.findPlugin<Explorer::DeviceDocumentPlugin>();
  const auto startMessages = Process::flatten(startState.messages().rootNode());

  State::MessageList startMsgs, endMsgs;
  for(const auto& msg : startMessages)
  {
    const ossia::color_u* cu = colorUnit(msg.address);
    const bool isVec = msg.value.get_type() == ossia::val_type::VEC2F
                       || msg.value.get_type() == ossia::val_type::VEC3F
                       || msg.value.get_type() == ossia::val_type::VEC4F;
    if(!ossia::is_numeric(msg.value) && !cu && !isVec)
      continue;

    // Current device value = the ramp's destination.
    ossia::value endVal = msg.value;
    const Device::Node* node{};
    if(devPlugin)
    {
      node = Device::try_getNodeFromAddress(devPlugin->rootNode(), msg.address.address);
      if(node && node->is<Device::AddressSettings>())
      {
        devPlugin->updateProxy.refreshRemoteValue(msg.address.address);
        endVal = node->get<Device::AddressSettings>().value;
      }
    }

    if(cu)
    {
      Gradient::ProcessModel::gradient_colors stops;
      stops.insert(std::make_pair(0., valueToColor(msg.value, *cu)));
      stops.insert(std::make_pair(1., valueToColor(endVal, *cu)));
      addGradientLane(m, scenar, b1, msg.address, stops);
      startMsgs.push_back(State::Message{msg.address, msg.value});
      endMsgs.push_back(State::Message{msg.address, endVal});
      continue;
    }

    // Domain: from the device node when available, else span of the values.
    auto makeLane = [&](const State::AddressAccessor& addr, double v0, double v1) {
      double min = std::min(v0, v1), max = std::max(v0, v1);
      if(node && node->is<Device::AddressSettings>())
      {
        const auto& dom = node->get<Device::AddressSettings>().domain.get();
        const auto dmin = dom.get_min(), dmax = dom.get_max();
        if(dmin.valid() && dmax.valid())
        {
          min = std::min(min, double(ossia::convert<float>(dmin)));
          max = std::max(max, double(ossia::convert<float>(dmax)));
        }
      }
      if(max - min < 1e-9)
      {
        min = std::min(min, 0.);
        max = std::max(max, 1.);
      }
      const double n0 = (v0 - min) / (max - min);
      const double n1 = (v1 - min) / (max - min);
      addAutomationLane(m, scenar, b1, addr, min, max, n0, n1);
      startMsgs.push_back(State::Message{addr, ossia::value{float(v0)}});
      endMsgs.push_back(State::Message{addr, ossia::value{float(v1)}});
    };

    if(isVec)
    {
      const int n = msg.value.get_type() == ossia::val_type::VEC2F   ? 2
                    : msg.value.get_type() == ossia::val_type::VEC3F ? 3
                                                                     : 4;
      for(int i = 0; i < n; ++i)
      {
        auto sub = msg.address;
        auto& acc = sub.qualifiers.get().accessors;
        acc.clear();
        acc.push_back(i);

        const auto v0 = ossia::get_value_at_index(msg.value, {i});
        const auto v1 = ossia::get_value_at_index(endVal, {i});
        if(!v0.valid() || !v1.valid())
          continue;
        makeLane(sub, ossia::convert<float>(v0), ossia::convert<float>(v1));
      }
      continue;
    }

    makeLane(
        msg.address, ossia::convert<float>(msg.value), ossia::convert<float>(endVal));
  }

  // Boundary values on the shared states.
  auto& s0 = Scenario::startState(b1, scenar);
  auto& s1 = Scenario::endState(b1, scenar);
  if(!startMsgs.empty())
    m.addMessages(s0, std::move(startMsgs));
  if(!endMsgs.empty())
    m.addMessages(s1, std::move(endMsgs));
  if(!b1.processes.empty())
    m.showRack(b1);

  m.commit();
  return true;
}
}
