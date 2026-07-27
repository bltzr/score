// This is an open source non-commercial project. Dear PVS-Studio, please check
// it. PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

// In-app structural self-test for promoted sequences: convert, extend, undo,
// redo, save/reload, create-from-state. Runs inside the real application when
// SCORE_PROMOTED_SEQ_SELFTEST is set, then exits with a status code.
// Exercises the exact command macros the blue-+ gesture runs.

#include <State/Address.hpp>

#include <Automation/AutomationModel.hpp>
#include <Automation/Commands/InitAutomation.hpp>
#include <Automation/AutomationProcessMetadata.hpp>

#include <Scenario/Application/ScenarioValidity.hpp>
#include <Scenario/Commands/CommandAPI.hpp>
#include <Scenario/Commands/Interval/CreateProcessInNewSlot.hpp>
#include <Scenario/Document/Interval/IntervalModel.hpp>
#include <Scenario/Document/ScenarioDocument/ScenarioDocumentModel.hpp>
#include <Scenario/Document/State/StateModel.hpp>
#include <Scenario/Process/Algorithms/Accessors.hpp>
#include <Scenario/Process/ScenarioModel.hpp>
#include <Scenario/Commands/Scenario/Deletions/RemoveSelection.hpp>
#include <Scenario/Commands/TimeSync/AddTrigger.hpp>
#include <Scenario/Commands/Scenario/Creations/CreateEvent_State.hpp>
#include <Scenario/Commands/TimeSync/SplitTimeSync.hpp>
#include <Scenario/Document/Event/EventModel.hpp>
#include <Scenario/Document/TimeSync/TimeSyncModel.hpp>
#include <Scenario/Process/Algorithms/ParallelBranches.hpp>
#include <Scenario/PromotedSequence/PromotedSequence.hpp>
#include <Scenario/PromotedSequence/SequenceAnchor.hpp>
#include <Scenario/Sequence/SequenceModel.hpp>
#include <Scenario/Sequence/Commands/SetSequenceNamespace.hpp>

#include <score/plugins/documentdelegate/DocumentDelegateFactory.hpp>
#include <score/serialization/DataStreamVisitor.hpp>

#include <core/command/CommandStack.hpp>
#include <core/document/Document.hpp>
#include <core/document/DocumentModel.hpp>
#include <core/presenter/DocumentManager.hpp>

#include <QApplication>
#include <QLocale>

#include <csignal>
#include <execinfo.h>
#include <unistd.h>

static void seqtestTrapHandler(int sig)
{
  fprintf(stderr, "=== SEQTEST trap signal %d — backtrace: ===\n", sig);
  void* frames[48];
  int n = backtrace(frames, 48);
  backtrace_symbols_fd(frames, n, 2);
  _exit(3);
}
#include <clocale>

static int failures = 0;
#define REQUIRE(cond)                                                       \
  do                                                                        \
  {                                                                         \
    if(!(cond))                                                             \
    {                                                                       \
      qCritical() << "FAILED:" << #cond << "at line" << __LINE__;           \
      ++failures;                                                           \
    }                                                                       \
    else                                                                    \
    {                                                                       \
      qDebug() << "ok:" << #cond;                                           \
    }                                                                       \
  } while(0)

static State::AddressAccessor addr(const QString& s)
{
  auto res = State::parseAddressAccessor(s);
  SCORE_ASSERT(res);
  return *res;
}

static int countLanes(const Scenario::IntervalModel& itv)
{
  int n = 0;
  for(auto& p : itv.processes)
    if(qobject_cast<const Automation::ProcessModel*>(&p))
      n++;
  return n;
}

static Scenario::SequenceAnchor* anchorOf(const Scenario::IntervalModel& host)
{
  for(auto& p : host.processes)
    if(auto a = qobject_cast<const Scenario::SequenceAnchor*>(&p))
      return const_cast<Scenario::SequenceAnchor*>(a);
  return nullptr;
}

namespace Scenario::PromotedSequence
{
void runSelfTest()
{
  std::signal(SIGTRAP, seqtestTrapHandler);
  std::signal(SIGABRT, seqtestTrapHandler);
  std::signal(SIGSEGV, seqtestTrapHandler);

  const auto& ctx = score::GUIAppContext();
  auto& doctype = *ctx.interfaces<score::DocumentDelegateList>().begin();
  auto doc = ctx.docManager.newDocument(
      ctx, Id<score::DocumentModel>{1000}, doctype);
  REQUIRE(doc);
  QApplication::processEvents();

  auto& sdm
      = static_cast<Scenario::ScenarioDocumentModel&>(doc->model().modelDelegate());
  auto& scenar
      = static_cast<Scenario::ProcessModel&>(*sdm.baseInterval().processes.begin());
  auto& dctx = doc->context();

  const auto a1 = addr("dev:/foo");
  const auto a2 = addr("dev:/bar");

  // ---- setup: one interval with two automations ----
  Id<Scenario::IntervalModel> hostId;
  {
    Scenario::Command::Macro m{
        new Scenario::Command::AddProcessInNewSlot, dctx};
    auto& box = m.createBox(
        scenar, TimeVal::fromMsecs(1000), TimeVal::fromMsecs(5000), 0.4);
    hostId = box.id();

    auto p1 = m.createProcess(
        box, Metadata<ConcreteKey_k, Automation::ProcessModel>::get(), QString{},
        QPointF{});
    REQUIRE(p1);
    m.submit(new Automation::InitAutomation{
        *safe_cast<Automation::ProcessModel*>(p1), a1, 0., 1.});
    m.addLayerInNewSlot(box, *p1);

    auto p2 = m.createProcess(
        box, Metadata<ConcreteKey_k, Automation::ProcessModel>::get(), QString{},
        QPointF{});
    REQUIRE(p2);
    m.submit(new Automation::InitAutomation{
        *safe_cast<Automation::ProcessModel*>(p2), a2, 0., 100.});
    m.addLayerInNewSlot(box, *p2);

    m.commit();
  }
  QApplication::processEvents();

  auto& host = scenar.intervals.at(hostId);
  REQUIRE(countLanes(host) == 2);
  REQUIRE(!Scenario::PromotedSequence::locate(scenar, host));

  const auto cablesBefore = sdm.cables.size();

  // ---- convert (blue-+ on the end state, released past the end) ----
  try
  {
    const bool ok = Scenario::PromotedSequence::convertOrExtend(
        dctx, scenar, host, TimeVal::fromMsecs(8000));
    REQUIRE(ok);
  }
  catch(const std::exception& e)
  {
    qCritical() << "EXCEPTION in convert:" << e.what();
    std::exit(2);
  }
  QApplication::processEvents();

  {
    auto st = Scenario::PromotedSequence::locate(scenar, host);
    REQUIRE(st.has_value());
    REQUIRE(st->host == &host);
    // released past the end -> convert + one appended section
    REQUIRE(st->sections.size() == 2);
    REQUIRE(countLanes(host) == 0);               // lanes moved out
    REQUIRE(countLanes(*st->sections[0]) == 2);   // into section 1
    REQUIRE(countLanes(*st->sections[1]) == 2);   // continued in section 2

    // the parallel branch is flexible
    REQUIRE(!host.duration.isRigid());
    REQUIRE(host.duration.isMaxInfinite());
    REQUIRE(host.duration.minDuration() == host.duration.defaultDuration());

    // shared IS state between consecutive sections
    auto& isState = Scenario::endState(*st->sections[0], scenar);
    REQUIRE(isState.previousInterval());
    REQUIRE(isState.nextInterval());
    REQUIRE(*isState.previousInterval() == st->sections[0]->id());
    REQUIRE(*isState.nextInterval() == st->sections[1]->id());

    // anchor: present in the parallel branch, 2 params, wired
    auto anchor = anchorOf(host);
    REQUIRE(anchor);
    REQUIRE(anchor->paramNamespace().size() == 2);
    REQUIRE(anchor->inletFor(a1));
    REQUIRE(anchor->outletFor(a1));
    REQUIRE(anchor->inletFor(a2));
    REQUIRE(anchor->outletFor(a2));
    // 2 lanes x 2 sections cabled into the anchor
    REQUIRE(sdm.cables.size() == cablesBefore + 4);

    Scenario::ScenarioValidityChecker::checkValidity(scenar);
  }

  // ---- extend again ----
  try
  {
    const bool ok = Scenario::PromotedSequence::convertOrExtend(
        dctx, scenar, host, TimeVal::fromMsecs(11000));
    REQUIRE(ok);
  }
  catch(const std::exception& e)
  {
    qCritical() << "EXCEPTION in extend:" << e.what();
    std::exit(2);
  }
  QApplication::processEvents();

  {
    auto st = Scenario::PromotedSequence::locate(scenar, host);
    REQUIRE(st.has_value());
    REQUIRE(st->sections.size() == 3);
    REQUIRE(countLanes(*st->sections[2]) == 2);
    REQUIRE(sdm.cables.size() == cablesBefore + 6);
    Scenario::ScenarioValidityChecker::checkValidity(scenar);
  }

  // ---- undo both gestures ----
  auto& stack = doc->commandStack();
  stack.undo();
  QApplication::processEvents();
  {
    auto st = Scenario::PromotedSequence::locate(scenar, host);
    REQUIRE(st.has_value());
    REQUIRE(st->sections.size() == 2);
    Scenario::ScenarioValidityChecker::checkValidity(scenar);
  }
  stack.undo();
  QApplication::processEvents();
  {
    REQUIRE(!Scenario::PromotedSequence::locate(scenar, host));
    REQUIRE(countLanes(host) == 2);       // lanes are back
    REQUIRE(!anchorOf(host));             // anchor gone
    REQUIRE(host.duration.isRigid());     // rigidity restored
    REQUIRE(sdm.cables.size() == cablesBefore);
    Scenario::ScenarioValidityChecker::checkValidity(scenar);
  }

  // ---- redo both ----
  stack.redo();
  QApplication::processEvents();
  stack.redo();
  QApplication::processEvents();
  {
    auto st = Scenario::PromotedSequence::locate(scenar, host);
    REQUIRE(st.has_value());
    REQUIRE(st->sections.size() == 3);
    REQUIRE(anchorOf(host));
    REQUIRE(sdm.cables.size() == cablesBefore + 6);
    Scenario::ScenarioValidityChecker::checkValidity(scenar);
  }

  // ---- save, close, reload; structure must survive ----
  auto bytes = doc->saveAsByteArray();
  QApplication::processEvents();
  ctx.docManager.forceCloseDocument(ctx, *doc);
  QApplication::processEvents();

  auto doc2 = ctx.docManager.loadDocument(
      ctx, QString("promoted-test"), bytes, DataStream::type(), doctype);
  REQUIRE(doc2);
  QApplication::processEvents();
  {
    auto& sdm2
        = static_cast<Scenario::ScenarioDocumentModel&>(doc2->model().modelDelegate());
    auto& scenar2
        = static_cast<Scenario::ProcessModel&>(*sdm2.baseInterval().processes.begin());

    // find the host again: the interval carrying a SequenceAnchor
    Scenario::IntervalModel* host2{};
    for(auto& itv : scenar2.intervals)
      if(anchorOf(itv))
        host2 = const_cast<Scenario::IntervalModel*>(&itv);
    REQUIRE(host2);
    if(host2)
    {
      auto st = Scenario::PromotedSequence::locate(scenar2, *host2);
      REQUIRE(st.has_value());
      REQUIRE(st->sections.size() == 3);
      auto anchor = anchorOf(*host2);
      REQUIRE(anchor->paramNamespace().size() == 2);
      REQUIRE(anchor->inletFor(a1));
      REQUIRE(anchor->outletFor(a2));
      REQUIRE(!host2->duration.isRigid());
      REQUIRE(host2->duration.isMaxInfinite());
      Scenario::ScenarioValidityChecker::checkValidity(scenar2);

      // ---- create-from-state on the reloaded doc ----
      auto& dctx2 = doc2->context();
      Id<Scenario::StateModel> bareId;
      {
        Scenario::Command::Macro m{
            new Scenario::Command::AddProcessInNewSlot, dctx2};
        auto [ts, ev, stt]
            = m.createDot(scenar2, Scenario::Point{TimeVal::fromMsecs(15000), 0.7});
        bareId = stt.id();
        m.commit();
      }
      QApplication::processEvents();

      auto& bare = scenar2.states.at(bareId);
      const bool ok = Scenario::PromotedSequence::createFromState(
          dctx2, scenar2, bare, TimeVal::fromMsecs(20000));
      REQUIRE(ok);
      QApplication::processEvents();

      REQUIRE(bare.nextInterval());
      if(bare.nextInterval())
      {
        auto& newHost = scenar2.intervals.at(*bare.nextInterval());
        auto st2 = Scenario::PromotedSequence::locate(scenar2, newHost);
        REQUIRE(st2.has_value());
        REQUIRE(st2->sections.size() == 1);
        REQUIRE(anchorOf(newHost)); // anchor exists even with no parameters
        Scenario::ScenarioValidityChecker::checkValidity(scenar2);
      }
    }
    // ---- migration: old encapsulated Sequence -> promoted ----
    {
      auto& dctx2 = doc2->context();
      Id<Scenario::IntervalModel> oldHostId;
      const Sequence::SequenceModel* oldSeq{};
      {
        Scenario::Command::Macro m{
            new Scenario::Command::AddProcessInNewSlot, dctx2};
        auto& box = m.createBox(
            scenar2, TimeVal::fromMsecs(25000), TimeVal::fromMsecs(29000), 0.3);
        oldHostId = box.id();
        auto proc = m.createProcess(
            box, Metadata<ConcreteKey_k, Sequence::SequenceModel>::get(), QString{},
            QPointF{});
        REQUIRE(proc);
        if(proc)
        {
          auto& seq = *safe_cast<Sequence::SequenceModel*>(proc);
          m.submit(new Sequence::Command::AddSequenceParameter{seq, a1});
          m.submit(new Sequence::Command::AddSequenceParameter{seq, a2});
          oldSeq = &seq;
        }
        m.commit();
      }
      QApplication::processEvents();

      if(oldSeq)
      {
        auto& oldHost = scenar2.intervals.at(oldHostId);
        REQUIRE(oldSeq->intervals.size() == 1);   // one internal section

        const bool ok = Scenario::PromotedSequence::convertOrExtend(
            dctx2, scenar2, oldHost, TimeVal::fromMsecs(32000));
        REQUIRE(ok);
        QApplication::processEvents();

        auto st = Scenario::PromotedSequence::locate(scenar2, oldHost);
        REQUIRE(st.has_value());
        if(st)
        {
          REQUIRE(st->sections.size() == 2); // migrated + extension
          REQUIRE(countLanes(*st->sections[0]) == 2);
          REQUIRE(countLanes(*st->sections[1]) == 2);
        }
        // the old process is gone, replaced by the anchor
        bool oldSeqStillThere = false;
        for(auto& p : oldHost.processes)
          if(qobject_cast<const Sequence::SequenceModel*>(&p))
            oldSeqStillThere = true;
        REQUIRE(!oldSeqStillThere);
        REQUIRE(anchorOf(oldHost));
        REQUIRE(!oldHost.duration.isRigid());
        Scenario::ScenarioValidityChecker::checkValidity(scenar2);

        // undo restores the old encapsulated process intact
        doc2->commandStack().undo();
        QApplication::processEvents();
        bool oldSeqBack = false;
        for(auto& p : oldHost.processes)
          if(qobject_cast<const Sequence::SequenceModel*>(&p))
            oldSeqBack = true;
        REQUIRE(oldSeqBack);
        REQUIRE(!anchorOf(oldHost));
        REQUIRE(!Scenario::PromotedSequence::locate(scenar2, oldHost));
        Scenario::ScenarioValidityChecker::checkValidity(scenar2);

        doc2->commandStack().redo();
        QApplication::processEvents();
        REQUIRE(Scenario::PromotedSequence::locate(scenar2, oldHost).has_value());
        Scenario::ScenarioValidityChecker::checkValidity(scenar2);
      }
    }

    // ---- delete a section + undo (RemoveSelection landmine) ----
    if(host2)
    {
      auto st = Scenario::PromotedSequence::locate(scenar2, *host2);
      REQUIRE(st.has_value());
      if(st)
      {
        auto* lastSection = st->sections.back();
        Selection sel;
        sel.append(lastSection);
        Scenario::Command::RemoveSelection cmd(scenar2, sel);
        cmd.redo(doc2->context());
        QApplication::processEvents();
        Scenario::ScenarioValidityChecker::checkValidity(scenar2);
        cmd.undo(doc2->context());
        QApplication::processEvents();
        Scenario::ScenarioValidityChecker::checkValidity(scenar2);
        // the sequence must be recognizable again after undo
        auto st2 = Scenario::PromotedSequence::locate(scenar2, *host2);
        REQUIRE(st2.has_value());
        if(st2)
          REQUIRE(st2->sections.size() == st->sections.size());
      }
    }

    // ---- JSON round-trip ----
    JSONObject::Serializer jw;
    doc2->saveAsJson(jw);
    auto jsonArr = jw.toByteArray();
    QApplication::processEvents();
    ctx.docManager.forceCloseDocument(ctx, *doc2);
    QApplication::processEvents();
    auto doc3 = ctx.docManager.loadDocument(
        ctx, QString("promoted-test-json"), jsonArr, JSONObject::type(), doctype);
    // let the presenter's queued init events (minimap zoom...) fire before
    // we do anything else, so closing the doc later doesn't deliver them
    // into a torn-down presenter
    for(int i = 0; i < 10; i++)
      QApplication::processEvents();
    REQUIRE(doc3);
    if(doc3)
    {
      auto& sdm3 = static_cast<Scenario::ScenarioDocumentModel&>(
          doc3->model().modelDelegate());
      auto& scenar3 = static_cast<Scenario::ProcessModel&>(
          *sdm3.baseInterval().processes.begin());
      int promotedCount = 0;
      for(auto& itv : scenar3.intervals)
      {
        if(anchorOf(itv))
        {
          promotedCount++;
          auto st = Scenario::PromotedSequence::locate(
              scenar3, const_cast<Scenario::IntervalModel&>(itv));
          REQUIRE(st.has_value());
        }
      }
      // the converted host, the created-from-state host, the migrated host
      REQUIRE(promotedCount == 3);
      qDebug("SEQTEST: json checkValidity...");
      Scenario::ScenarioValidityChecker::checkValidity(scenar3);
      qDebug("SEQTEST: json close...");
      ctx.docManager.forceCloseDocument(ctx, *doc3);
      qDebug("SEQTEST: json closed");
    }
  }

  // ---- auto-flex diamonds (wait-absorption, ParallelBranches) ----
  {
    auto doc4 = ctx.docManager.newDocument(
        ctx, Id<score::DocumentModel>{1004}, doctype);
    REQUIRE(doc4);
    QApplication::processEvents();
    auto& sdm4
        = static_cast<Scenario::ScenarioDocumentModel&>(doc4->model().modelDelegate());
    auto& scenar4
        = static_cast<Scenario::ProcessModel&>(*sdm4.baseInterval().processes.begin());
    auto& dctx4 = doc4->context();

    // Diamond: A (1000..5000) in parallel with i1(1000ms) -> boxB(1500ms,
    // triggered end) -> i2(1500ms), all between A's two syncs.
    Id<Scenario::IntervalModel> aId, i2Id;
    {
      Scenario::Command::Macro m{new Scenario::Command::AddProcessInNewSlot, dctx4};
      auto& boxA = m.createBox(
          scenar4, TimeVal::fromMsecs(1000), TimeVal::fromMsecs(5000), 0.2);
      aId = boxA.id();
      auto& boxB = m.createBox(
          scenar4, TimeVal::fromMsecs(2000), TimeVal::fromMsecs(3500), 0.6);

      auto& aStart = Scenario::startState(boxA, scenar4);
      auto& sB0 = m.createState(scenar4, aStart.eventId(), 0.5);
      m.createInterval(scenar4, sB0.id(), Scenario::startState(boxB, scenar4).id());

      m.submit(new Scenario::Command::AddTrigger<Scenario::ProcessModel>(
          Scenario::endTimeSync(boxB, scenar4)));

      // i2 ends on its own event of the shared sync, so SplitTimeSync can
      // later separate the two lanes (splitting moves whole events)
      auto& sharedSync = Scenario::endTimeSync(boxA, scenar4);
      auto evCmd = new Scenario::Command::CreateEvent_State{
          scenar4, sharedSync.id(), 0.5};
      m.submit(evCmd);
      auto& i2 = m.createInterval(
          scenar4, Scenario::endState(boxB, scenar4).id(), evCmd->createdState());
      i2Id = i2.id();
      m.commit();
    }
    QApplication::processEvents();
    Scenario::ScenarioValidityChecker::checkValidity(scenar4);

    auto& A = scenar4.intervals.at(aId);
    auto& i2 = scenar4.intervals.at(i2Id);

    // A spans the diamond -> fully elastic; its min is the PERT floor of the
    // parallel branch: 1000 + 0 (boxB is trigger-flexed, masked min 0) + 1500.
    REQUIRE(!A.duration.isRigid());
    REQUIRE(A.duration.isMaxInfinite());
    REQUIRE(!A.duration.isMinNull());
    REQUIRE(A.duration.minDuration() == TimeVal::fromMsecs(2500));
    // i2 is structural but faces the elastic A -> extend-only, min = its own
    // default duration.
    REQUIRE(!i2.duration.isRigid());
    REQUIRE(i2.duration.isMaxInfinite());
    REQUIRE(i2.duration.minDuration() == TimeVal::fromMsecs(1500));

    // one undo removes the whole diamond, redo brings the flex back
    doc4->commandStack().undo();
    QApplication::processEvents();
    REQUIRE(scenar4.intervals.find(aId) == scenar4.intervals.end());
    Scenario::ScenarioValidityChecker::checkValidity(scenar4);
    doc4->commandStack().redo();
    QApplication::processEvents();
    {
      auto& A2 = scenar4.intervals.at(aId);
      REQUIRE(!A2.duration.isRigid());
      REQUIRE(A2.duration.minDuration() == TimeVal::fromMsecs(2500));
    }
    Scenario::ScenarioValidityChecker::checkValidity(scenar4);

    // deleting the closing edge dissolves the diamond: A gets its authored
    // rigidity back, and undo restores the elastic state
    {
      auto& A2 = scenar4.intervals.at(aId);
      auto& i2b = scenar4.intervals.at(i2Id);
      Selection sel;
      sel.append(&i2b);
      Scenario::Command::RemoveSelection cmd(scenar4, sel);
      cmd.redo(dctx4);
      QApplication::processEvents();
      REQUIRE(A2.duration.isRigid());
      REQUIRE(A2.duration.minDuration() == A2.duration.defaultDuration());
      Scenario::ScenarioValidityChecker::checkValidity(scenar4);
      cmd.undo(dctx4);
      QApplication::processEvents();
      REQUIRE(!A2.duration.isRigid());
      REQUIRE(A2.duration.minDuration() == TimeVal::fromMsecs(2500));
      REQUIRE(!scenar4.intervals.at(i2Id).duration.isRigid());
      Scenario::ScenarioValidityChecker::checkValidity(scenar4);
    }

    // splitting the shared sync dissolves it too, symmetrically
    {
      auto& A2 = scenar4.intervals.at(aId);
      auto& i2b = scenar4.intervals.at(i2Id);
      auto& sharedSync = Scenario::endTimeSync(A2, scenar4);
      auto& i2EndState = Scenario::endState(i2b, scenar4);
      Scenario::Command::SplitTimeSync cmd(
          sharedSync, {i2EndState.eventId()});
      cmd.redo(dctx4);
      QApplication::processEvents();
      REQUIRE(A2.duration.isRigid());
      REQUIRE(i2b.duration.isRigid());
      Scenario::ScenarioValidityChecker::checkValidity(scenar4);
      cmd.undo(dctx4);
      QApplication::processEvents();
      REQUIRE(!A2.duration.isRigid());
      REQUIRE(A2.duration.minDuration() == TimeVal::fromMsecs(2500));
      REQUIRE(!i2b.duration.isRigid());
      Scenario::ScenarioValidityChecker::checkValidity(scenar4);
    }

    ctx.docManager.forceCloseDocument(ctx, *doc4);
    QApplication::processEvents();
  }

  if(failures == 0)
    qDebug() << "ALL PROMOTED-SEQUENCE TESTS PASSED";
  else
    qCritical() << failures << "FAILURES";
  std::exit(failures == 0 ? 0 : 1);
}
}
