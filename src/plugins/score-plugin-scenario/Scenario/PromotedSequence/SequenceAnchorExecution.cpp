// This is an open source non-commercial project. Dear PVS-Studio, please check
// it. PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#include "SequenceAnchorExecution.hpp"

#include <Process/ExecutionContext.hpp>

#include <ossia/dataflow/graph_node.hpp>
#include <ossia/dataflow/port.hpp>

namespace
{
class anchor_node final : public ossia::graph_node
{
public:
  explicit anchor_node(std::size_t n_params)
  {
    // graph_node owns and deletes its ports
    for(std::size_t i = 0; i < n_params; i++)
    {
      m_inlets.push_back(new ossia::value_inlet);
      m_outlets.push_back(new ossia::value_outlet);
    }
  }

  [[nodiscard]] std::string label() const noexcept override { return "anchor_node"; }

  void run(const ossia::token_request&, ossia::exec_state_facade) noexcept override
  {
    for(std::size_t i = 0; i < m_inlets.size(); i++)
    {
      ossia::value_port& ip = *m_inlets[i]->target<ossia::value_port>();
      ossia::value_port& op = *m_outlets[i]->target<ossia::value_port>();
      for(const auto& tv : ip.get_data())
        op.write_value(tv.value, tv.timestamp);
    }
  }
};
}

namespace Execution
{
SequenceAnchorComponent::SequenceAnchorComponent(
    Scenario::SequenceAnchor& element, const ::Execution::Context& ctx, QObject* parent)
    : ::Execution::ProcessComponent_T<Scenario::SequenceAnchor, ossia::node_process>{
        element, ctx, "Executor::SequenceAnchorComponent", parent}
{
  auto node = std::make_shared<anchor_node>(element.paramNamespace().size());
  this->node = node;
  m_ossia_process = std::make_shared<ossia::node_process>(node);
}

SequenceAnchorComponent::~SequenceAnchorComponent() { }
}
