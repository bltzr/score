// This is an open source non-commercial project. Dear PVS-Studio, please check
// it. PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#include "SequenceAnchor.hpp"

#include "PromotedSequence.hpp"

#include <Process/Dataflow/PortSerialization.hpp>

#include <Automation/AutomationModel.hpp>
#include <Color/GradientModel.hpp>

#include <Scenario/Document/Interval/IntervalModel.hpp>
#include <Scenario/Document/Interval/Slot.hpp>
#include <Scenario/Process/ScenarioModel.hpp>

#include <score/model/path/PathSerialization.hpp>
#include <score/tools/Bind.hpp>
#include <score/serialization/DataStreamVisitor.hpp>
#include <score/serialization/JSONVisitor.hpp>

#include <ossia/detail/algorithms.hpp>

#include <wobjectimpl.h>
W_OBJECT_IMPL(Scenario::SequenceAnchor)

namespace Scenario
{
SequenceAnchor::SequenceAnchor(
    const TimeVal& duration, const Id<Process::ProcessModel>& id, QObject* parent)
    : Process::ProcessModel{duration, id, "SequenceAnchor", parent}
{
  metadata().setInstanceName(*this);
  wireWhenReady();
}

SequenceAnchor::~SequenceAnchor() { }

void SequenceAnchor::setParamNamespace(const QList<State::AddressAccessor>& ns)
{
  m_namespace = ns;
  rebuildPorts({}, {});
  rewireWatchers();
}

Process::ValueInlet*
SequenceAnchor::inletFor(const State::AddressAccessor& addr) const noexcept
{
  const QString label = addr.toString();
  for(auto& p : m_paramInlets)
    if(p && p->name() == label)
      return p.get();
  return nullptr;
}

Process::ValueOutlet*
SequenceAnchor::outletFor(const State::AddressAccessor& addr) const noexcept
{
  const QString label = addr.toString();
  for(auto& p : m_paramOutlets)
    if(p && p->name() == label)
      return p.get();
  return nullptr;
}

void SequenceAnchor::rebuildPorts(
    const std::vector<int32_t>& savedInletIds, const std::vector<int32_t>& savedOutletIds)
{
  m_inlets.clear();
  m_outlets.clear();

  std::vector<std::unique_ptr<Process::ValueInlet>> newIns;
  std::vector<std::unique_ptr<Process::ValueOutlet>> newOuts;
  newIns.reserve(m_namespace.size());
  newOuts.reserve(m_namespace.size());

  for(int i = 0; i < m_namespace.size(); i++)
  {
    const QString label = m_namespace[i].toString();

    // Keep existing ports for a parameter so cables stay attached.
    auto init = ossia::find_if(
        m_paramInlets, [&](auto& p) { return p && p->name() == label; });
    if(init != m_paramInlets.end())
    {
      newIns.push_back(std::move(*init));
    }
    else
    {
      const int32_t id
          = i < (int)savedInletIds.size() ? savedInletIds[i] : 1000 + i;
      auto port = std::make_unique<Process::ValueInlet>(
          label, Id<Process::Port>(id), this);
      newIns.push_back(std::move(port));
    }

    auto outit = ossia::find_if(
        m_paramOutlets, [&](auto& p) { return p && p->name() == label; });
    if(outit != m_paramOutlets.end())
    {
      newOuts.push_back(std::move(*outit));
    }
    else
    {
      const int32_t id = i < (int)savedOutletIds.size() ? savedOutletIds[i] : i;
      auto port = std::make_unique<Process::ValueOutlet>(
          label, Id<Process::Port>(id), this);
      newOuts.push_back(std::move(port));
    }
  }

  m_paramInlets = std::move(newIns);
  m_paramOutlets = std::move(newOuts);
  for(auto& p : m_paramInlets)
    m_inlets.push_back(p.get());
  for(auto& p : m_paramOutlets)
    m_outlets.push_back(p.get());

  inletsChanged();
  outletsChanged();
}

// ---- slot mirroring across sections (lifted from the old process) ----

void SequenceAnchor::wireWhenReady()
{
  // Sections may not exist yet (during construction / document load):
  // defer the initial wiring to the next event-loop cycle.
  QMetaObject::invokeMethod(
      this, [this] { rewireWatchers(); }, Qt::QueuedConnection);
}

void SequenceAnchor::rewireWatchers()
{
  for(auto& c : m_watchConnections)
    QObject::disconnect(c);
  m_watchConnections.clear();

  auto host = qobject_cast<Scenario::IntervalModel*>(parent());
  if(!host)
    return;
  auto scenar = qobject_cast<const Scenario::ProcessModel*>(host->parent());
  if(!scenar)
    return;

  auto st = PromotedSequence::locate(*scenar, *host);
  if(!st)
    return;

  for(auto* sec : st->sections)
    watchSection(*sec);

  // Extension moves the host's end: re-derive the sections afterwards.
  m_watchConnections.push_back(con(
      host->duration, &Scenario::IntervalDurations::defaultDurationChanged, this,
      [this](const TimeVal&) { wireWhenReady(); }, Qt::QueuedConnection));
}

void SequenceAnchor::watchSection(const Scenario::IntervalModel& itv)
{
  m_watchConnections.push_back(
      con(itv, &Scenario::IntervalModel::rackChanged, this,
          [this, &itv](Scenario::Slot::RackView v) {
    if(v == Scenario::Slot::SmallView)
      scheduleMirror(itv);
  }));
  m_watchConnections.push_back(
      con(itv, &Scenario::IntervalModel::slotResized, this,
          [this, &itv](const Scenario::SlotId&) { scheduleMirror(itv); }));
  m_watchConnections.push_back(
      con(itv, &Scenario::IntervalModel::slotAdded, this,
          [this, &itv](const Scenario::SlotId&) { scheduleMirror(itv); }));
  m_watchConnections.push_back(
      con(itv, &Scenario::IntervalModel::slotRemoved, this,
          [this, &itv](const Scenario::SlotId&) { scheduleMirror(itv); }));
  m_watchConnections.push_back(
      con(itv, &Scenario::IntervalModel::layerAdded, this,
          [this, &itv](Scenario::SlotId, Id<Process::ProcessModel>) {
    scheduleMirror(itv);
  }));
  m_watchConnections.push_back(
      con(itv, &Scenario::IntervalModel::layerRemoved, this,
          [this, &itv](Scenario::SlotId, Id<Process::ProcessModel>) {
    scheduleMirror(itv);
  }));
}

void SequenceAnchor::scheduleMirror(const Scenario::IntervalModel& source)
{
  if(m_mirroring)
    return;
  m_pendingMirrorSource = source.id();
  if(m_mirrorScheduled)
    return;
  m_mirrorScheduled = true;
  // Run after the current command's signals settle: moving a process across
  // slots emits several granular signals; mirroring on an intermediate state
  // would corrupt the other sections.
  QMetaObject::invokeMethod(
      this,
      [this] {
    m_mirrorScheduled = false;
    if(m_mirroring)
      return;
    auto host = qobject_cast<Scenario::IntervalModel*>(parent());
    if(!host)
      return;
    auto scenar = qobject_cast<const Scenario::ProcessModel*>(host->parent());
    if(!scenar)
      return;
    auto it = scenar->intervals.find(m_pendingMirrorSource);
    if(it != scenar->intervals.end())
      mirrorRackLayout(*it);
      },
      Qt::QueuedConnection);
}

// The process in `target` denoting the same parameter as `proc` in the
// source section — matched by address.
static const Process::ProcessModel* correspondingLane(
    const Scenario::IntervalModel& target, const Process::ProcessModel& proc)
{
  State::AddressAccessor addr;
  if(auto a = qobject_cast<const Automation::ProcessModel*>(&proc))
    addr = a->address();
  else if(auto g = qobject_cast<const Gradient::ProcessModel*>(&proc))
    addr = g->address();
  else
    return nullptr;

  for(auto& p : target.processes)
  {
    if(auto a = qobject_cast<const Automation::ProcessModel*>(&p))
    {
      if(a->address() == addr)
        return &p;
    }
    else if(auto g = qobject_cast<const Gradient::ProcessModel*>(&p))
    {
      if(g->address() == addr)
        return &p;
    }
  }
  return nullptr;
}

void SequenceAnchor::mirrorRackLayout(const Scenario::IntervalModel& source)
{
  auto host = qobject_cast<Scenario::IntervalModel*>(parent());
  if(!host)
    return;
  auto scenar = qobject_cast<const Scenario::ProcessModel*>(host->parent());
  if(!scenar)
    return;
  auto st = PromotedSequence::locate(*scenar, *host);
  if(!st)
    return;

  m_mirroring = true;
  for(auto* secP : st->sections)
  {
    auto& sec = *secP;
    if(sec.id() == source.id())
      continue;

    // Rebuild the target's small-view rack to match the source's, mapping
    // each layer to the corresponding parameter lane (matched by address).
    Scenario::Rack newRack;
    std::vector<Id<Process::ProcessModel>> used;
    for(const Scenario::Slot& srcSlot : source.smallView())
    {
      Scenario::Slot dst;
      dst.height = srcSlot.height;
      dst.nodal = srcSlot.nodal;
      for(const auto& procId : srcSlot.processes)
      {
        auto pit = source.processes.find(procId);
        if(pit == source.processes.end())
          continue;
        if(auto lane = correspondingLane(sec, *pit))
        {
          dst.processes.push_back(lane->id());
          used.push_back(lane->id());
          if(srcSlot.frontProcess && *srcSlot.frontProcess == procId)
            dst.frontProcess = lane->id();
        }
      }
      if(dst.processes.empty())
        continue;
      if(!dst.frontProcess || !ossia::contains(dst.processes, *dst.frontProcess))
        dst.frontProcess = dst.processes.front();
      newRack.push_back(std::move(dst));
    }

    // Lanes with no counterpart in the source keep their own slot so they
    // never silently disappear from view.
    for(auto& proc : sec.processes)
    {
      if(!ossia::contains(used, proc.id()))
        newRack.push_back(Scenario::Slot{{proc.id()}, proc.id(), 100.});
    }

    if(!newRack.empty())
      sec.replaceSmallView(newRack);
  }
  m_mirroring = false;
}
}

// ---- serialization ----

template <>
void DataStreamReader::read(const Scenario::SequenceAnchor& proc)
{
  m_stream << (int32_t)proc.m_namespace.size();
  for(const auto& addr : proc.m_namespace)
    read(addr);
  m_stream << (int32_t)proc.m_paramInlets.size();
  for(const auto& p : proc.m_paramInlets)
    m_stream << (int32_t)p->id().val();
  m_stream << (int32_t)proc.m_paramOutlets.size();
  for(const auto& p : proc.m_paramOutlets)
    m_stream << (int32_t)p->id().val();
  insertDelimiter();
}

template <>
void DataStreamWriter::write(Scenario::SequenceAnchor& proc)
{
  int32_t n{};
  m_stream >> n;
  proc.m_namespace.clear();
  for(int32_t i = 0; i < n; i++)
  {
    State::AddressAccessor addr;
    write(addr);
    proc.m_namespace.push_back(std::move(addr));
  }
  std::vector<int32_t> inIds, outIds;
  int32_t ni{};
  m_stream >> ni;
  for(int32_t i = 0; i < ni; i++)
  {
    int32_t v{};
    m_stream >> v;
    inIds.push_back(v);
  }
  int32_t no{};
  m_stream >> no;
  for(int32_t i = 0; i < no; i++)
  {
    int32_t v{};
    m_stream >> v;
    outIds.push_back(v);
  }
  proc.rebuildPorts(inIds, outIds);
  checkDelimiter();
}

template <>
void JSONReader::read(const Scenario::SequenceAnchor& proc)
{
  obj["Namespace"] = proc.m_namespace;
  {
    std::vector<int32_t> ids;
    for(const auto& p : proc.m_paramInlets)
      ids.push_back(p->id().val());
    obj["ParamInletIds"] = ids;
  }
  {
    std::vector<int32_t> ids;
    for(const auto& p : proc.m_paramOutlets)
      ids.push_back(p->id().val());
    obj["ParamOutletIds"] = ids;
  }
}

template <>
void JSONWriter::write(Scenario::SequenceAnchor& proc)
{
  proc.m_namespace <<= obj["Namespace"];
  std::vector<int32_t> inIds, outIds;
  if(auto v = obj.tryGet("ParamInletIds"))
    inIds <<= *v;
  if(auto v = obj.tryGet("ParamOutletIds"))
    outIds <<= *v;
  proc.rebuildPorts(inIds, outIds);
}

// ---- command ----

namespace Scenario::Command
{
SetAnchorNamespace::SetAnchorNamespace(
    const SequenceAnchor& anchor, QList<State::AddressAccessor> ns)
    : m_path{anchor}
    , m_new{std::move(ns)}
    , m_old{anchor.paramNamespace()}
{
}

void SetAnchorNamespace::redo(const score::DocumentContext& ctx) const
{
  m_path.find(ctx).setParamNamespace(m_new);
}

void SetAnchorNamespace::undo(const score::DocumentContext& ctx) const
{
  m_path.find(ctx).setParamNamespace(m_old);
}

void SetAnchorNamespace::serializeImpl(DataStreamInput& s) const
{
  s << m_path;
  s << (int32_t)m_new.size();
  for(const auto& a : m_new)
    s << a;
  s << (int32_t)m_old.size();
  for(const auto& a : m_old)
    s << a;
}

void SetAnchorNamespace::deserializeImpl(DataStreamOutput& s)
{
  s >> m_path;
  int32_t n{};
  s >> n;
  m_new.clear();
  for(int32_t i = 0; i < n; i++)
  {
    State::AddressAccessor a;
    s >> a;
    m_new.push_back(std::move(a));
  }
  s >> n;
  m_old.clear();
  for(int32_t i = 0; i < n; i++)
  {
    State::AddressAccessor a;
    s >> a;
    m_old.push_back(std::move(a));
  }
}
}
