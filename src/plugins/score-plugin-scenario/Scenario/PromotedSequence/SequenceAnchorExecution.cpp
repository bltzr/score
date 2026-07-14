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
    m_pins.reserve(n_params);
    for(std::size_t i = 0; i < n_params; i++)
    {
      auto in = std::make_unique<ossia::value_inlet>();
      auto out = std::make_unique<ossia::value_outlet>();
      m_inlets.push_back(in.get());
      m_outlets.push_back(out.get());
      m_pins.emplace_back(std::move(in), std::move(out));
    }
  }

  [[nodiscard]] std::string label() const noexcept override { return "anchor_node"; }

  void run(const ossia::token_request&, ossia::exec_state_facade) noexcept override
  {
    for(auto& [in, out] : m_pins)
    {
      ossia::value_port& ip = **in;
      ossia::value_port& op = **out;
      for(const auto& tv : ip.get_data())
        op.write_value(tv.value, tv.timestamp);
    }
  }

private:
  std::vector<std::pair<
      std::unique_ptr<ossia::value_inlet>, std::unique_ptr<ossia::value_outlet>>>
      m_pins;
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
