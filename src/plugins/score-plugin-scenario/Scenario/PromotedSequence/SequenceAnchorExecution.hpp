#pragma once
#include <Process/Execution/ProcessComponent.hpp>

#include <Scenario/PromotedSequence/SequenceAnchor.hpp>

#include <ossia/dataflow/node_process.hpp>

#include <memory>

namespace Execution
{
/**
 * Execution of a promoted sequence's anchor: one (inlet, outlet) value pin
 * pair per parameter; each tick forwards inlet values to the matching outlet.
 * Sections' lanes are cabled into the inlets (only one section is active at a
 * time, so the merge is trivial); users cable outward from the outlets.
 */
class SequenceAnchorComponent final
    : public ::Execution::ProcessComponent_T<Scenario::SequenceAnchor, ossia::node_process>
{
  COMPONENT_METADATA("3c8e2f71-6a94-4d05-b9e8-1f7a25c4d6a2")
public:
  SequenceAnchorComponent(
      Scenario::SequenceAnchor& element, const ::Execution::Context& ctx,
      QObject* parent);
  ~SequenceAnchorComponent() override;
};
using SequenceAnchorComponentFactory
    = ::Execution::ProcessComponentFactory_T<SequenceAnchorComponent>;
}

SCORE_CONCRETE_COMPONENT_FACTORY(
    Execution::ProcessComponentFactory, Execution::SequenceAnchorComponentFactory)
