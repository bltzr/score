#include "MergeTimeSyncs.hpp"

#include <Process/TimeValueSerialization.hpp>

#include <Scenario/Application/ScenarioValidity.hpp>
#include <Scenario/Process/Algorithms/Accessors.hpp>
#include <Scenario/Process/Algorithms/ParallelBranches.hpp>

#include <score/document/DocumentContext.hpp>
#include <score/model/EntitySerialization.hpp>
#include <score/model/tree/TreeNodeSerialization.hpp>

#include <core/document/Document.hpp>

namespace Scenario
{
namespace Command
{

MergeTimeSyncs::MergeTimeSyncs(
    const ProcessModel& scenario, Id<TimeSyncModel> clickedTn,
    Id<TimeSyncModel> hoveredTn)
    : m_scenarioPath{scenario}
    , m_movingTnId{std::move(clickedTn)}
    , m_destinationTnId{std::move(hoveredTn)}
{
  auto& tn = scenario.timeSync(m_movingTnId);
  auto& destinantionTn = scenario.timeSync(m_destinationTnId);

  QByteArray arr;
  DataStream::Serializer s{&arr};
  s.readFrom(tn);
  m_serializedTimeSync = arr;

  m_moveCommand = new MoveEvent<GoodOldDisplacementPolicy>{
      scenario, tn.events().front(), destinantionTn.date(), ExpandMode::Scale,
      LockMode::Free};

  m_targetTrigger = destinantionTn.expression();
  m_targetTriggerActive = destinantionTn.active();
}

MergeTimeSyncs::~MergeTimeSyncs()
{
  delete m_moveCommand;
}

void MergeTimeSyncs::undo(const score::DocumentContext& ctx) const
{
  auto& scenar = m_scenarioPath.find(ctx);

  for(auto it = m_flexCmds.rbegin(); it != m_flexCmds.rend(); ++it)
    it->undo(ctx);

  auto& globalTn = scenar.timeSync(m_destinationTnId);

  DataStream::Deserializer s{m_serializedTimeSync};
  auto recreatedTn = new TimeSyncModel{s, &scenar};

  auto events_in_timesync = recreatedTn->events();
  // we remove and re-add events in recreated Tn
  // to ensure correct parentship between elements.
  for(const auto& evId : events_in_timesync)
  {
    recreatedTn->removeEvent(evId);
    globalTn.removeEvent(evId);
  }

  scenar.timeSyncs.add(recreatedTn);
  for(const auto& evId : events_in_timesync)
  {
    recreatedTn->addEvent(evId);
  }

  globalTn.setExpression(m_targetTrigger);
  globalTn.setActive(m_targetTriggerActive);

  m_moveCommand->undo(ctx);
}

void MergeTimeSyncs::redo(const score::DocumentContext& ctx) const
{
  auto& scenar = m_scenarioPath.find(ctx);

  m_moveCommand->redo(ctx);

  auto& movingTn = scenar.timeSync(m_movingTnId);
  auto& destinationTn = scenar.timeSync(m_destinationTnId);

  auto movingEvents = movingTn.events();
  for(auto& evId : movingEvents)
  {
    movingTn.removeEvent(evId);
    destinationTn.addEvent(evId);
  }
  destinationTn.setActive(destinationTn.active() || movingTn.active());
  destinationTn.setExpression(movingTn.expression());

  scenar.timeSyncs.remove(m_movingTnId);

  // The merged sync may now be a diamond convergence: flex the lanes that
  // became waitable (two passes — a lane flexed in the first pass can make
  // its siblings waitable in turn).
  if(!m_flexComputed)
  {
    m_flexComputed = true;
    if(!destinationTn.active())
    {
      for(int pass = 0; pass < 2; pass++)
      {
        for(const auto& pid : previousNonGraphIntervals(destinationTn, scenar))
        {
          auto& p = scenar.intervals.at(pid);
          if(!p.duration.isRigid())
            continue;
          auto f = ParallelBranches::flavorFor(scenar, p);
          if(f.flavor != ParallelBranches::Flavor::Rigid)
          {
            m_flexCmds.emplace_back(p, f.min);
            m_flexCmds.back().redo(ctx);
          }
        }
      }
    }
  }
  else
  {
    for(const auto& cmd : m_flexCmds)
      cmd.redo(ctx);
  }
}

void MergeTimeSyncs::update(
    unused_t scenar, const Id<TimeSyncModel>& clickedTn,
    const Id<TimeSyncModel>& hoveredTn)
{
}

void MergeTimeSyncs::serializeImpl(DataStreamInput& s) const
{
  s << m_scenarioPath << m_movingTnId << m_destinationTnId << m_serializedTimeSync
    << m_moveCommand->serialize() << m_targetTrigger << m_targetTriggerActive;
  s << m_flexComputed << (int32_t)m_flexCmds.size();
  for(const auto& cmd : m_flexCmds)
    s << cmd.serialize();
}

void MergeTimeSyncs::deserializeImpl(DataStreamOutput& s)
{
  QByteArray cmd;

  s >> m_scenarioPath >> m_movingTnId >> m_destinationTnId >> m_serializedTimeSync >> cmd
      >> m_targetTrigger >> m_targetTriggerActive;

  m_moveCommand = new MoveEvent<GoodOldDisplacementPolicy>{};
  m_moveCommand->deserialize(cmd);
  bool computed{};
  int32_t n{};
  s >> computed >> n;
  m_flexComputed = computed;
  m_flexCmds.resize(n);
  for(int32_t i = 0; i < n; i++)
  {
    QByteArray a;
    s >> a;
    m_flexCmds[i].deserialize(a);
  }
}
}
}
