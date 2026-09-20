#include "synchronization.hpp"
#include <cassert>
#include <iostream>

using namespace moss;

static DomainSynchronizationPlan fixture(bool split) {
  DomainSynchronizationPlan domain;
  domain.concrete_instance_id = "template::account";
  domain.state_leaves = {"immutable", "x", "y", "z"};
  HandlerSynchronizationPlan update;
  update.handler_identity = "update";
  update.effects.reads = {"immutable", "x", "y"};
  update.effects.writes = {"x", "y"};
  update.effects.consumes = {"y"};
  domain.handlers.push_back(update);
  HandlerSynchronizationPlan read;
  read.handler_identity = "read";
  read.effects.reads = split ? StateLeafSet{"x"} : StateLeafSet{"x", "y"};
  domain.handlers.push_back(read);
  derive_domain_synchronization(domain);
  return domain;
}

int main() {
  auto shared = fixture(false);
  assert(shared.classes.size() == 1);
  assert(shared.classes[0].member_leaves == (StateLeafSet{"x", "y"}));
  assert(shared.leaf_to_class.count("immutable") == 0);
  assert(shared.leaf_to_class.count("z") == 0);
  const auto& reader = shared.handlers[0];
  const auto& writer = shared.handlers[1];
  assert(writer.effects.normalized("x") == Effect::Write);
  assert(writer.effects.normalized("y") == Effect::Consume);
  assert(synchronization_mode(writer.effects.normalized("x")) == SynchronizationMode::Exclusive);
  assert(synchronization_mode(writer.effects.normalized("y")) == SynchronizationMode::Exclusive);
  assert(writer.class_modes.size() == 1);
  assert(writer.exclusive_set == (StateLeafSet{"x", "y"}));
  assert(writer.effects.consumes == StateLeafSet{"y"});
  assert(writer.self_conflict());
  assert(!reader.self_conflict());
  assert(synchronization_conflicts(reader, reader).empty());
  assert(synchronization_conflicts(reader, writer) == std::vector<size_t>{0});
  auto split = fixture(true);
  assert(split.classes.size() == 2);
  assert(split.leaf_to_class.at("x") == 0);
  assert(split.leaf_to_class.at("y") == 1);
  assert(split.handlers[0].class_modes.size() == 1);
  assert(split.handlers[1].class_modes.size() == 2);
  auto repeated = fixture(true);
  assert(split.leaf_to_class == repeated.leaf_to_class);
  assert(split.classes[1].class_id == repeated.classes[1].class_id);
  DomainSynchronizationPlan invalid;
  invalid.handlers.push_back(writer);
  bool rejected = false;
  try { derive_domain_synchronization(invalid); }
  catch (const std::runtime_error&) { rejected = true; }
  assert(rejected);
  std::cout << "SynchronizationPlan semantic normalization and partition checks passed\n";
}
