#include "synchronization_lowering.hpp"
#include <cassert>
#include <functional>
#include <iostream>
using namespace moss;

static Program fixture() {
  Program p;
  p.concrete_domain_graph.identity = "graph";
  p.concrete_domain_graph.closed = true;
  p.synchronization_plan.graph_identity = "graph";
  for (int i = 0; i < 2; ++i) {
    DomainSpecialization spec;
    spec.source_domain = "D"; spec.instance = "d" + std::to_string(i);
    p.domain_specializations.push_back(spec);
    ConcreteDomainInstance instance;
    instance.identity = "main::" + spec.instance; instance.binding = spec.instance;
    instance.domain = "D"; instance.specialization = spec.identity();
    instance.specialization_index = static_cast<size_t>(i); instance.domain_rank = i;
    p.concrete_domain_graph.instances.push_back(instance);
    DomainSynchronizationPlan d;
    d.concrete_instance_id = instance.identity; d.specialization_id = instance.specialization;
    d.domain = "D"; d.domain_rank = i; d.state_leaves = {"x", "y", "fixed"};
    HandlerSynchronizationPlan write;
    write.name = "Write"; write.handler_identity = "Write";
    write.effects.writes = {"x", "y"};
    HandlerSynchronizationPlan read;
    read.name = "Read"; read.handler_identity = "Read";
    read.effects.reads = {"x", "fixed"};
    d.handlers = {write, read};
    derive_domain_synchronization(d);
    p.synchronization_plan.domains.push_back(d);
  }
  ConcreteRouteEdge edge;
  edge.source_instance = "main::d0"; edge.target_instance = "main::d1";
  p.concrete_domain_graph.edges.push_back(edge);
  return p;
}

int main() {
  validate_synchronization_lowering(fixture());
  const std::vector<std::function<void(Program&)>> corruptions = {
    [](auto& p) { p.concrete_domain_graph.closed = false; },
    [](auto& p) { p.synchronization_plan.graph_identity = "wrong"; },
    [](auto& p) { p.concrete_domain_graph.instances[1].domain_rank = 0; },
    [](auto& p) { p.concrete_domain_graph.instances[0].specialization_index.reset(); },
    [](auto& p) { p.synchronization_plan.domains[0].specialization_id = "wrong"; },
    [](auto& p) { p.synchronization_plan.domains[0].concrete_instance_id = "wrong"; },
    [](auto& p) { p.synchronization_plan.domains[0].classes[0].class_rank = 2; },
    [](auto& p) { p.synchronization_plan.domains[0].classes[0].mode_signature[0] = SynchronizationMode::None; },
    [](auto& p) { p.synchronization_plan.domains[0].classes[0].member_leaves.insert("fixed"); },
    [](auto& p) { p.synchronization_plan.domains[0].handlers[0].class_modes[0] = SynchronizationMode::None; },
    [](auto& p) { p.synchronization_plan.domains[0].handlers[0].class_modes[999] = SynchronizationMode::Shared; },
    [](auto& p) { p.synchronization_plan.domains[0].handlers[0].class_modes.clear(); },
    [](auto& p) { p.synchronization_plan.domains[0].handlers[0].effects.writes.insert("fixed"); },
    [](auto& p) { p.synchronization_plan.domains[0].handlers[0].exclusive_set.insert("fixed"); },
    [](auto& p) { p.synchronization_plan.domains[0].handlers[0].effects.reads.insert("missing"); },
    [](auto& p) { std::swap(p.concrete_domain_graph.edges[0].source_instance, p.concrete_domain_graph.edges[0].target_instance); },
  };
  for (const auto& corrupt : corruptions) {
    auto p = fixture(); corrupt(p); bool rejected = false;
    try { validate_synchronization_lowering(p); } catch (const std::exception&) { rejected = true; }
    assert(rejected);
  }
  std::cout << "physical synchronization validation rejects corrupted plans\n";
}
