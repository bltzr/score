// This is an open source non-commercial project. Dear PVS-Studio, please check
// it. PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#include "SequencePresenter.hpp"

#include <Automation/AutomationModel.hpp>

#include <Color/GradientModel.hpp>

#include <Process/Dataflow/PortFactory.hpp>
#include <Process/Dataflow/PortItem.hpp>

#include <Scenario/Document/Interval/IntervalModel.hpp>
#include <Scenario/Document/Interval/SlotHeader.hpp>
#include <Scenario/Document/Interval/Temporal/TemporalIntervalPresenter.hpp>
#include <Scenario/Document/Interval/Temporal/TemporalIntervalView.hpp>
#include <Scenario/Sequence/Commands/MoveSequenceIS.hpp>

#include <Process/TimeValue.hpp>

#include <score/command/Dispatchers/CommandDispatcher.hpp>
#include <score/command/Dispatchers/OngoingCommandDispatcher.hpp>
#include <score/document/DocumentContext.hpp>

#include <wobjectimpl.h>

W_OBJECT_IMPL(Sequence::SequencePresenter)

namespace Sequence
{

// Extra room below the last section slot for the row's cable port.
static constexpr qreal k_portFooter = 12.;

SequencePresenter::SequencePresenter(
    const SequenceModel& model, SequenceView* view, const Process::Context& ctx,
    QObject* parent)
    : Process::LayerPresenter{model, view, ctx, parent}
    , m_model{model}
    , m_view{*view}
    , m_slotResizer{this->m_context.context.commandStack}
{
  // Update handles + sections when model structure changes
  connect(
      &model, &SequenceModel::structureChanged, this,
      [this] {
        rebuildSections();
        updateHandles();
      });

  // Handle drag: dispatch the ongoing move command — plain (neighbours
  // resize) or ripple (shift held: what follows shifts, parent end moves).
  connect(
      view, &SequenceView::handleDragMoved, this,
      [this](Id<Scenario::TimeSyncModel> tsId, double newX, bool ripple) {
        if(m_zoom <= 0)
          return;
        const auto newDate = TimeVal::fromPixels(newX, m_zoom);
        if(ripple)
          m_context.context.dispatcher
              .submit<Sequence::Command::MoveSequenceISRipple>(
                  m_model, tsId, newDate);
        else
          m_context.context.dispatcher.submit<Sequence::Command::MoveSequenceIS>(
              m_model, tsId, newDate);
      });

  connect(
      view, &SequenceView::handleDragReleased, this,
      [this](Id<Scenario::TimeSyncModel>, double, bool) {
        m_context.context.dispatcher.commit();
      });

  connect(view, &SequenceView::handleDragCancelled, this, [this]() {
    m_context.context.dispatcher.rollback();
    updateHandles();
  });

  // Rail double-click: insert an IS at that date, splitting the section and
  // its automation curves.
  connect(view, &SequenceView::railDoubleClicked, this, [this](double x) {
    if(m_zoom <= 0)
      return;
    const auto date = TimeVal::fromPixels(x, m_zoom);
    auto cmd = new Sequence::Command::InsertSequenceIS{m_model, date};
    if(cmd->valid())
      CommandDispatcher<>{m_context.context.commandStack}.submit(cmd);
    else
      delete cmd;
  });

  // Build section presenters for initial model state.
  // Zoom hasn't been set yet so we defer layout to on_zoomRatioChanged.
  rebuildSections();
}

SequencePresenter::~SequencePresenter()
{
  qDeleteAll(m_rowPorts);
  qDeleteAll(m_sectionPresenters);
}

void SequencePresenter::setWidth(qreal width, qreal defaultWidth)
{
  m_view.setWidth(width);
  updateHandles();
  updateSectionLayout();
}

void SequencePresenter::setHeight(qreal height)
{
  m_view.setHeight(height);
  // Sections occupy the band below the rail and above the port footer.
  const qreal sectionH = std::max(0., height - SequenceView::RailHeight - k_portFooter);
  for(auto* p : m_sectionPresenters)
    p->view()->setHeight(sectionH);

  // If the parent slot was resized to something other than our auto-computed
  // sum, the user dragged the sequence's own footer: scale the inner slots
  // proportionally so they fill the new height (the auto-height then matches).
  if(m_scalingInner)
    return;
  const auto ord = m_model.orderedIntervals();
  if(ord.empty())
    return;
  const auto& first = m_model.intervals.at(ord.front());
  const int nSlots = (int)first.smallView().size();
  if(nSlots == 0)
    return;

  const qreal chrome
      = Scenario::SlotHeader::headerHeight() + Scenario::SlotFooter::footerHeight();
  const qreal fixed = SequenceView::RailHeight + k_portFooter + nSlots * chrome;
  qreal contentSum = 0.;
  for(const auto& s : first.smallView())
    contentSum += s.height;
  const qreal target = height - fixed;
  if(contentSum <= 1. || target <= 1.)
    return;
  const double factor = target / contentSum;
  if(std::abs(factor - 1.) < 0.01)
    return;

  m_scalingInner = true;
  for(auto* p : m_sectionPresenters)
  {
    auto& itv = const_cast<Scenario::IntervalModel&>(p->model());
    for(int i = 0; i < (int)itv.smallView().size(); ++i)
    {
      const double nh = std::max(20., itv.smallView()[i].height * factor);
      itv.setSlotHeight(Scenario::SlotId{i, Scenario::Slot::SmallView}, nh);
    }
  }
  m_scalingInner = false;
}

void SequencePresenter::putToFront()
{
  m_view.setVisible(true);
}

void SequencePresenter::putBehind()
{
  m_view.setVisible(false);
}

void SequencePresenter::on_zoomRatioChanged(ZoomRatio ratio)
{
  m_zoom = ratio;
  for(auto* p : m_sectionPresenters)
    p->on_zoomRatioChanged(ratio);
  updateHandles();
  updateSectionLayout();
}

void SequencePresenter::parentGeometryChanged()
{
  updateHandles();
  updateSectionLayout();
}

void SequencePresenter::rebuildSections()
{
  qDeleteAll(m_sectionPresenters);
  m_sectionPresenters.clear();

  // Track the reference section's rack layout so the row ports follow
  // slot resizes / front-process switches / reorganizations.
  for(auto& conn : m_rackConns)
    disconnect(conn);
  m_rackConns.clear();
  if(auto ord = m_model.orderedIntervals(); !ord.empty())
  {
    auto& first = m_model.intervals.at(ord.front());
    const auto upd = [this] {
      updateRowPorts();
      updateParentSlotHeight();
    };
    m_rackConns.push_back(connect(
        &first, &Scenario::IntervalModel::rackChanged, this, [upd](auto) { upd(); }));
    m_rackConns.push_back(connect(
        &first, &Scenario::IntervalModel::slotResized, this, [upd](auto) { upd(); }));
    m_rackConns.push_back(connect(
        &first, &Scenario::IntervalModel::slotAdded, this, [upd](auto) { upd(); }));
    m_rackConns.push_back(connect(
        &first, &Scenario::IntervalModel::slotRemoved, this, [upd](auto) { upd(); }));
    m_rackConns.push_back(connect(
        &first, &Scenario::IntervalModel::slotsSwapped, this,
        [upd](auto, auto, auto) { upd(); }));
    m_rackConns.push_back(connect(
        &first, &Scenario::IntervalModel::frontLayerChanged, this,
        [upd](auto, auto) { upd(); }));
    m_rackConns.push_back(connect(
        &first, &Scenario::IntervalModel::layerAdded, this,
        [upd](auto, auto) { upd(); }));
    m_rackConns.push_back(connect(
        &first, &Scenario::IntervalModel::layerRemoved, this,
        [upd](auto, auto) { upd(); }));
  }

  const auto& startTsId = m_model.startTimeSyncId();
  const auto& endTsId = m_model.endTimeSyncId();

  // Use ordered intervals so section presenters are created left-to-right.
  // Later sections get a higher zValue so that, at a shared IS boundary, the
  // right section's start breakpoint takes the click over the left section's
  // end breakpoint.
  int zi = 0;
  for(const auto& itvId : m_model.orderedIntervals())
  {
    const auto& itv = m_model.intervals.at(itvId);

    // Section intervals span from one boundary IS to the next.
    // handles = true: slot footers are draggable (vertical resize) and slot
    // headers allow switching / moving processes.
    auto* pres = new Scenario::TemporalIntervalPresenter{
        m_zoom, itv, m_context.context, true, &m_view, this};
    pres->on_zoomRatioChanged(m_zoom);
    pres->view()->setZValue(zi++);
    // The section interval body/edges must not grab clicks (they'd shadow the
    // IS and the boundary breakpoints); its header, footers and automation
    // points are separate child items and stay interactive.
    pres->view()->setAcceptedMouseButtons(Qt::NoButton);
    // Constrain automation point moves to the section box, so endpoints can't
    // be dragged out of the section and lost (they move freely in y).
    pres->setBoundedLayers(true);
    // Drive slot vertical resize from the footer-drag signals (the nested
    // sections aren't wired to the scenario's slot-resize state machine).
    connect(pres, &Scenario::IntervalPresenter::pressed, this, [this, pres](QPointF sp) {
      onSectionPressed(pres, sp);
    });
    connect(pres, &Scenario::IntervalPresenter::moved, this, [this, pres](QPointF sp) {
      onSectionMoved(pres, sp);
    });
    connect(
        pres, &Scenario::IntervalPresenter::released, this,
        [this](QPointF) { onSectionReleased(); });
    m_sectionPresenters.append(pres);
  }

  updateSectionLayout();
  updateRowPorts();
  updateParentSlotHeight();
}

int SequencePresenter::slotFooterAt(
    const Scenario::IntervalModel& itv, double localY) const
{
  qreal y = 0.;
  const auto& sv = itv.smallView();
  for(int i = 0; i < (int)sv.size(); ++i)
  {
    const qreal contentBottom
        = y + Scenario::SlotHeader::headerHeight() + sv[i].height;
    const qreal footerBottom = contentBottom + Scenario::SlotFooter::footerHeight();
    // A generous band around the footer so the grab is forgiving.
    if(localY >= contentBottom - 2. && localY <= footerBottom + 2.)
      return i;
    y = footerBottom;
  }
  return -1;
}

void SequencePresenter::onSectionPressed(
    Scenario::TemporalIntervalPresenter* pres, QPointF sp)
{
  const double localY = pres->view()->mapFromScene(sp).y();
  const int idx = slotFooterAt(pres->model(), localY);
  if(idx < 0)
  {
    m_resizeSlot = -1;
    return;
  }
  m_resizePres = pres;
  m_resizeSlot = idx;
  m_resizeOrigH = pres->model().smallView()[idx].height;
  m_resizeOrigY = sp.y();
}

void SequencePresenter::onSectionMoved(
    Scenario::TemporalIntervalPresenter* pres, QPointF sp)
{
  if(m_resizeSlot < 0 || pres != m_resizePres)
    return;
  const double newH = std::max(20., m_resizeOrigH + (sp.y() - m_resizeOrigY));
  m_slotResizer.submit(
      pres->model(),
      Scenario::SlotPath{pres->model(), m_resizeSlot, Scenario::Slot::SmallView}, newH);
}

void SequencePresenter::onSectionReleased()
{
  if(m_resizeSlot < 0)
    return;
  m_slotResizer.commit();
  m_resizeSlot = -1;
  m_resizePres = nullptr;
}

void SequencePresenter::updateParentSlotHeight()
{
  auto* parentItv = qobject_cast<Scenario::IntervalModel*>(m_model.parent());
  if(!parentItv)
    return;

  const auto ord = m_model.orderedIntervals();
  if(ord.empty())
    return;
  const auto& first = m_model.intervals.at(ord.front());

  qreal wanted = SequenceView::RailHeight;
  for(const auto& slot : first.smallView())
    wanted += Scenario::SlotHeader::headerHeight() + slot.height
              + Scenario::SlotFooter::footerHeight();
  wanted += k_portFooter;

  // Locate the parent rack slot holding this sequence process.
  const auto seqId = m_model.id();
  const auto& sv = parentItv->smallView();
  int idx = -1;
  for(int i = 0; i < (int)sv.size(); ++i)
  {
    const auto& ps = sv[i].processes;
    if(std::find(ps.begin(), ps.end(), seqId) != ps.end())
    {
      idx = i;
      break;
    }
  }
  if(idx < 0)
    return;

  const Scenario::SlotId sid{idx, Scenario::Slot::SmallView};
  // Derived value: set directly on the model (not via command) and guard the
  // re-entrancy loop through the parent's slotResized signal.
  if(std::abs(parentItv->getSlotHeight(sid) - wanted) > 0.5)
    parentItv->setSlotHeight(sid, wanted);
}

void SequencePresenter::updateRowPorts()
{
  qDeleteAll(m_rowPorts);
  m_rowPorts.clear();

  const auto ord = m_model.orderedIntervals();
  if(ord.empty())
    return;
  const auto& first = m_model.intervals.at(ord.front());

  auto& portFactory = m_context.context.app.interfaces<Process::PortFactoryList>();

  // Row layout mirrors TemporalIntervalPresenter::updatePositions:
  // each slot is [header][content][footer], stacked from y = 1.
  static constexpr qreal portDiam = 8.;
  qreal y = SequenceView::RailHeight + 1.;
  for(const auto& slot : first.smallView())
  {
    const qreal headerY = y;
    // Bottom of the automation content (below the header, above the footer)
    const qreal contentBottom
        = headerY + Scenario::SlotHeader::headerHeight() + slot.height;
    y += Scenario::SlotHeader::headerHeight() + slot.height
         + Scenario::SlotFooter::footerHeight();

    if(!slot.frontProcess)
      continue;
    auto pit = first.processes.find(*slot.frontProcess);
    if(pit == first.processes.end())
      continue;

    State::AddressAccessor addr;
    if(auto* a = qobject_cast<const Automation::ProcessModel*>(&*pit))
      addr = a->address();
    else if(auto* g = qobject_cast<const Gradient::ProcessModel*>(&*pit))
      addr = g->address();
    else
      continue;

    // The sequence-level outlet for this row's parameter
    const QString label = addr.toString();
    for(const auto& outlet : m_model.paramOutlets())
    {
      if(outlet->name() == label)
      {
        if(auto fact = portFactory.get(outlet->concreteKey()))
        {
          auto& port = const_cast<Process::ValueOutlet&>(*outlet);
          if(auto* item = fact->makePortItem(port, m_context.context, &m_view, this))
          {
            // Bottom-left of the automation, not in the header
            item->setPos(2., contentBottom - portDiam - 2.);
            item->setZValue(11.);
            m_rowPorts.push_back(item);
          }
        }
        break;
      }
    }
  }
}

void SequencePresenter::updateSectionLayout()
{
  if(m_zoom <= 0)
    return;

  for(auto* pres : m_sectionPresenters)
  {
    const auto& itv = pres->model();
    const double x = itv.date().toPixels(m_zoom);
    const double w = itv.duration.defaultDuration().toPixels(m_zoom);
    pres->view()->setPos(x, SequenceView::RailHeight);
    pres->view()->setDefaultWidth(w);
    pres->view()->setMinWidth(itv.duration.minDuration().toPixels(m_zoom));
    pres->view()->setMaxWidth(
        itv.duration.isMaxInfinite(),
        itv.duration.isMaxInfinite() ? -1.
                                     : itv.duration.maxDuration().toPixels(m_zoom));
  }
}

void SequencePresenter::updateHandles()
{
  if(m_zoom <= 0)
    return;

  const auto& startTsId = m_model.startTimeSyncId();
  const auto& endTsId = m_model.endTimeSyncId();

  QVector<SequenceView::HandleData> handles;
  for(const auto& ts : m_model.timeSyncs)
  {
    // Skip boundary timeSyncs — they map to the parent scenario's states
    if(ts.id() == startTsId || ts.id() == endTsId)
      continue;
    const double x = ts.date().toPixelsRaw(m_zoom);
    handles.push_back({ts.id(), x});
  }

  m_view.setHandles(handles);
}

} // namespace Sequence
