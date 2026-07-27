#pragma once

#include <Scenario/Commands/ScenarioCommandFactory.hpp>
#include <Scenario/Commands/Interval/SetRigidity.hpp>

#include <score/command/Command.hpp>
#include <score/model/Identifier.hpp>
#include <score/model/path/Path.hpp>
#include <score/tools/std/Optional.hpp>

#include <QVector>

struct DataStreamInput;
struct DataStreamOutput;

namespace Scenario
{
class EventModel;
class TimeSyncModel;
namespace Command
{
class SplitTimeSync final : public score::Command
{
  SCORE_COMMAND_DECL(CommandFactoryName(), SplitTimeSync, "Desynchronize")
public:
  SplitTimeSync(
      const TimeSyncModel& path, std::vector<Id<EventModel>> eventsInNewTimeSync);
  void undo(const score::DocumentContext& ctx) const override;
  void redo(const score::DocumentContext& ctx) const override;

protected:
  void serializeImpl(DataStreamInput&) const override;
  void deserializeImpl(DataStreamOutput&) override;

private:
  Path<TimeSyncModel> m_path;
  std::vector<Id<EventModel>> m_eventsInNewTimeSync;

  Id<TimeSyncModel> m_originalTimeSyncId;
  Id<TimeSyncModel> m_newTimeSyncId;

  // Splitting can dissolve a diamond: lanes that are no longer waitable get
  // their authored rigidity back (see ParallelBranches.hpp). Captured on
  // first redo.
  mutable bool m_rigidComputed{};
  mutable std::vector<SetRigidity> m_rigidCmds;
};

class SCORE_PLUGIN_SCENARIO_EXPORT SplitWholeSync final : public score::Command
{
  SCORE_COMMAND_DECL(CommandFactoryName(), SplitWholeSync, "Desynchronize")
public:
  SplitWholeSync(const TimeSyncModel& path);
  SplitWholeSync(const TimeSyncModel& path, std::vector<Id<TimeSyncModel>> new_ids);
  void undo(const score::DocumentContext& ctx) const override;
  void redo(const score::DocumentContext& ctx) const override;

protected:
  void serializeImpl(DataStreamInput&) const override;
  void deserializeImpl(DataStreamOutput&) override;

private:
  Path<TimeSyncModel> m_path;

  Id<TimeSyncModel> m_originalTimeSync;
  std::vector<Id<TimeSyncModel>> m_newTimeSyncs;

  mutable bool m_rigidComputed{};
  mutable std::vector<SetRigidity> m_rigidCmds;
};
}
}
