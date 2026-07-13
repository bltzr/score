#pragma once
#include <Process/LayerPresenter.hpp>
#include <Process/ZoomHelper.hpp>

#include <Scenario/Commands/Interval/Rack/Slot/ResizeSlotVertically.hpp>
#include <Scenario/Sequence/SequenceModel.hpp>
#include <Scenario/Sequence/SequenceView.hpp>

#include <score/command/Dispatchers/SingleOngoingCommandDispatcher.hpp>
#include <score/model/Identifier.hpp>

#include <QVector>

#include <memory>

#include <score_plugin_scenario_export.h>
#include <verdigris>

namespace Scenario
{
class TemporalIntervalPresenter;
}
namespace Dataflow
{
class PortItem;
}

namespace Sequence
{

class SCORE_PLUGIN_SCENARIO_EXPORT SequencePresenter final
    : public Process::LayerPresenter
{
  W_OBJECT(SequencePresenter)

public:
  SequencePresenter(
      const SequenceModel& model, SequenceView* view, const Process::Context& ctx,
      QObject* parent);
  ~SequencePresenter() override;

  // Initial slot height in the parent rack; refined live by
  // updateParentSlotHeight() to fit the sections.
  static constexpr double recommendedHeight = 200.;

  void setWidth(qreal width, qreal defaultWidth) override;
  void setHeight(qreal height) override;
  void putToFront() override;
  void putBehind() override;
  void on_zoomRatioChanged(ZoomRatio ratio) override;
  void parentGeometryChanged() override;

private:
  void updateHandles();
  // Destroy and recreate all child section presenters from the current model.
  void rebuildSections();
  // Reposition existing section presenters based on current zoom + interval dates.
  void updateSectionLayout();
  // One port item per slot row, bound to the sequence-level outlet of the
  // row's front process parameter: a single port for all the instances of
  // that process across sections.
  void updateRowPorts();
  // Resizes the parent rack slot holding this sequence to fit the rail + the
  // section slots + the port footer, so the sequence grows/shrinks with its
  // slots.
  void updateParentSlotHeight();

  // Slot vertical resize: the nested sections aren't wired to the scenario's
  // slot-resize state machine, so we drive it directly from the section
  // presenters' footer-drag signals.
  void onSectionPressed(Scenario::TemporalIntervalPresenter* pres, QPointF sp);
  void onSectionMoved(Scenario::TemporalIntervalPresenter* pres, QPointF sp);
  void onSectionReleased();
  // Index of the slot whose footer band contains section-local y, or -1.
  int slotFooterAt(const Scenario::IntervalModel& itv, double localY) const;

  const SequenceModel& m_model;
  SequenceView& m_view;
  ZoomRatio m_zoom{};

  // One TemporalIntervalPresenter per section interval, owned by this presenter.
  QVector<Scenario::TemporalIntervalPresenter*> m_sectionPresenters;
  QVector<Dataflow::PortItem*> m_rowPorts;
  QVector<QMetaObject::Connection> m_rackConns;

  // Ongoing slot-resize gesture state
  SingleOngoingCommandDispatcher<Scenario::Command::ResizeSlotVertically> m_slotResizer;
  Scenario::TemporalIntervalPresenter* m_resizePres{};
  int m_resizeSlot{-1};
  double m_resizeOrigH{};
  double m_resizeOrigY{};
  // Guards the proportional inner-slot rescale in setHeight()
  bool m_scalingInner{};
};

} // namespace Sequence
