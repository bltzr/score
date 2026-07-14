#pragma once
#include <Process/Dataflow/Port.hpp>
#include <Process/GenericProcessFactory.hpp>
#include <Process/ProcessMetadata.hpp>
#include <Process/Process.hpp>

#include <State/Address.hpp>

#include <Scenario/Commands/ScenarioCommandFactory.hpp>

#include <score/command/Command.hpp>
#include <score/model/path/Path.hpp>

#include <score_plugin_scenario_export.h>

#include <memory>
#include <vector>

namespace Scenario
{
class SequenceAnchor;
}

PROCESS_METADATA(
    SCORE_PLUGIN_SCENARIO_EXPORT, Scenario::SequenceAnchor,
    "9f6b7d42-51c3-4b8e-a2d7-8e4f0a1c6b3d", "SequenceAnchor", "Sequence anchor",
    Process::ProcessCategory::Structure, "Structure",
    "Owner of a promoted sequence's per-parameter ports and slot mirroring; "
    "lives in the sequence's parallel branch",
    "ossia score", {}, {}, {},
    QUrl(),
    Process::ProcessFlags::SupportsTemporal | Process::ProcessFlags::PutInNewSlot)

namespace Scenario
{
class IntervalModel;
class ProcessModel;

/**
 * The identity/owner object of a promoted sequence (decision with Pia:
 * "the parallel interval owns sequence-wide behavior, since there will
 * always be one"). This thin process lives in the parallel branch and owns
 * what the native elements cannot:
 *  - one (inlet, outlet) pair per sequence parameter: section lanes are
 *    auto-cabled into the inlets, users cable outward from the outlets —
 *    one cable per parameter for the whole sequence;
 *  - the live slot-mirroring watchers across sections;
 *  - the ordered parameter namespace.
 * It carries NO timing structure — that stays native in the scenario.
 */
class SCORE_PLUGIN_SCENARIO_EXPORT SequenceAnchor final : public Process::ProcessModel
{
  SCORE_SERIALIZE_FRIENDS
  PROCESS_METADATA_IMPL(Scenario::SequenceAnchor)
  W_OBJECT(SequenceAnchor)

public:
  SequenceAnchor(
      const TimeVal& duration, const Id<Process::ProcessModel>& id, QObject* parent);
  ~SequenceAnchor() override;

  template <typename Impl>
  SequenceAnchor(Impl& vis, QObject* parent)
      : Process::ProcessModel{vis, parent}
  {
    vis.writeTo(*this);
    wireWhenReady();
  }

  const QList<State::AddressAccessor>& paramNamespace() const noexcept
  {
    return m_namespace;
  }
  void setParamNamespace(const QList<State::AddressAccessor>& ns);

  Process::ValueInlet* inletFor(const State::AddressAccessor& addr) const noexcept;
  Process::ValueOutlet* outletFor(const State::AddressAccessor& addr) const noexcept;

  //! Re-derive the sections and re-wire the slot-mirroring watchers.
  void rewireWatchers();

private:
  void rebuildPorts(
      const std::vector<int32_t>& savedInletIds,
      const std::vector<int32_t>& savedOutletIds);
  void wireWhenReady();
  void watchSection(const Scenario::IntervalModel& itv);
  void scheduleMirror(const Scenario::IntervalModel& source);
  void mirrorRackLayout(const Scenario::IntervalModel& source);

  QList<State::AddressAccessor> m_namespace;
  std::vector<std::unique_ptr<Process::ValueInlet>> m_paramInlets;
  std::vector<std::unique_ptr<Process::ValueOutlet>> m_paramOutlets;

  std::vector<QMetaObject::Connection> m_watchConnections;
  Id<Scenario::IntervalModel> m_pendingMirrorSource{};
  bool m_mirrorScheduled{};
  bool m_mirroring{};
};

using SequenceAnchorFactory = Process::ProcessFactory_T<Scenario::SequenceAnchor>;

namespace Command
{
//! Set an anchor's parameter namespace (rebuilds its port pairs).
class SetAnchorNamespace final : public score::Command
{
  SCORE_COMMAND_DECL(
      CommandFactoryName(), SetAnchorNamespace, "Set sequence parameters")
public:
  SetAnchorNamespace(
      const SequenceAnchor& anchor, QList<State::AddressAccessor> ns);

  void undo(const score::DocumentContext& ctx) const override;
  void redo(const score::DocumentContext& ctx) const override;

protected:
  void serializeImpl(DataStreamInput&) const override;
  void deserializeImpl(DataStreamOutput&) override;

private:
  Path<SequenceAnchor> m_path;
  QList<State::AddressAccessor> m_new;
  QList<State::AddressAccessor> m_old;
};
}
}
