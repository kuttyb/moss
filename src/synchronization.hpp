#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "constraints.hpp"

namespace moss {

// Semantic observations remain distinct, including READ + WRITE and CONSUME.
// Paths use the checker's storage locations and checked aggregate fields.
using StateLeafSet = std::set<std::string>;
struct StateLeafEffects {
  StateLeafSet reads, writes, consumes;
  void observe(const std::string& leaf, Effect effect) {
    (effect == Effect::Consume ? consumes :
     effect == Effect::Write ? writes : reads).insert(leaf);
  }
  std::optional<Effect> normalized(const std::string& leaf) const {
    if (consumes.count(leaf)) return Effect::Consume;
    if (writes.count(leaf)) return Effect::Write;
    if (reads.count(leaf)) return Effect::Read;
    return std::nullopt;
  }
};

enum class SynchronizationMode { None, Shared, Exclusive };
inline const char* synchronization_mode_name(SynchronizationMode mode) {
  return mode == SynchronizationMode::Exclusive ? "EXCLUSIVE" :
      mode == SynchronizationMode::Shared ? "SHARED" : "NONE";
}
inline SynchronizationMode synchronization_mode(std::optional<Effect> effect) {
  if (!effect) return SynchronizationMode::None;
  return *effect == Effect::Read ? SynchronizationMode::Shared :
                                 SynchronizationMode::Exclusive;
}

struct SynchronizationClass {
  std::string class_id;
  size_t class_rank = 0;
  StateLeafSet member_leaves;
  // Parallel to DomainSynchronizationPlan::handlers, in identity order.
  std::vector<SynchronizationMode> mode_signature;
};
struct HandlerSynchronizationPlan {
  std::string handler_identity, name;
  StateLeafEffects effects;
  StateLeafSet exclusive_set, protected_read_set, lock_set;
  // Class ordinals are deliberately a different type/container from leaves.
  std::map<size_t, SynchronizationMode> class_modes;
  bool self_conflict() const { return !exclusive_set.empty(); }
};
struct DomainSynchronizationPlan {
  std::string concrete_instance_id, specialization_id, domain, source_file;
  int line = 0, domain_rank = -1;
  StateLeafSet state_leaves, protected_mutable_leaves;
  std::map<std::string, size_t> leaf_to_class;
  std::vector<SynchronizationClass> classes;
  std::vector<HandlerSynchronizationPlan> handlers;
};
struct SynchronizationPlan {
  // Supplied graph context, never a process-global or runtime activation ID.
  std::string graph_identity;
  std::vector<DomainSynchronizationPlan> domains;
};

inline void synchronization_require(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(std::string("internal synchronization invariant: ") + message);
}

inline void derive_domain_synchronization(DomainSynchronizationPlan& domain) {
  std::sort(domain.handlers.begin(), domain.handlers.end(),
      [](const auto& a, const auto& b) { return a.handler_identity < b.handler_identity; });
  for (auto& handler : domain.handlers) {
    handler.exclusive_set = handler.effects.writes;
    handler.exclusive_set.insert(handler.effects.consumes.begin(), handler.effects.consumes.end());
    domain.protected_mutable_leaves.insert(handler.exclusive_set.begin(), handler.exclusive_set.end());
  }
  // First lexicographic member determines class order, independent of hashes.
  std::map<std::vector<SynchronizationMode>, size_t> by_signature;
  for (const auto& leaf : domain.protected_mutable_leaves) {
    synchronization_require(domain.state_leaves.count(leaf), "effect outside checked state leaves");
    std::vector<SynchronizationMode> signature;
    for (const auto& handler : domain.handlers)
      signature.push_back(synchronization_mode(handler.effects.normalized(leaf)));
    auto inserted = by_signature.emplace(signature, domain.classes.size());
    size_t rank = inserted.first->second;
    if (inserted.second) {
      SynchronizationClass cls;
      cls.class_rank = rank;
      cls.class_id = domain.concrete_instance_id + "::sync:" + std::to_string(rank);
      cls.mode_signature = std::move(signature);
      domain.classes.push_back(std::move(cls));
    }
    domain.classes.at(rank).member_leaves.insert(leaf);
    synchronization_require(domain.leaf_to_class.emplace(leaf, rank).second, "duplicate leaf class");
  }
  for (size_t h = 0; h < domain.handlers.size(); ++h) {
    auto& handler = domain.handlers[h];
    for (const auto& leaf : handler.effects.reads)
      if (domain.protected_mutable_leaves.count(leaf)) handler.protected_read_set.insert(leaf);
    handler.lock_set = handler.exclusive_set;
    handler.lock_set.insert(handler.protected_read_set.begin(), handler.protected_read_set.end());
    for (const auto& leaf : handler.lock_set) {
      synchronization_require(domain.protected_mutable_leaves.count(leaf), "LockSet outside X*");
      size_t rank = domain.leaf_to_class.at(leaf);
      auto mode = synchronization_mode(handler.effects.normalized(leaf));
      synchronization_require(mode != SynchronizationMode::None, "empty acquisition mode");
      synchronization_require(domain.classes.at(rank).mode_signature.at(h) == mode, "inconsistent class mode");
      handler.class_modes.emplace(rank, mode);
    }
    bool exclusive = false;
    for (const auto& acquisition : handler.class_modes)
      exclusive |= acquisition.second == SynchronizationMode::Exclusive;
    synchronization_require(exclusive == handler.self_conflict(), "self conflict mismatch");
  }
  for (const auto& leaf : domain.state_leaves)
    synchronization_require(domain.leaf_to_class.count(leaf) == domain.protected_mutable_leaves.count(leaf),
                            "class partition differs from X*");
  for (size_t rank = 0; rank < domain.classes.size(); ++rank) {
    const auto& cls = domain.classes[rank];
    synchronization_require(cls.class_rank == rank && !cls.member_leaves.empty(), "invalid class rank/members");
    for (const auto& leaf : cls.member_leaves) {
      synchronization_require(domain.leaf_to_class.at(leaf) == rank, "invalid inverse class map");
      for (size_t h = 0; h < domain.handlers.size(); ++h)
        synchronization_require(cls.mode_signature.at(h) ==
            synchronization_mode(domain.handlers[h].effects.normalized(leaf)), "class signature mismatch");
    }
  }
}

// Conflict witnesses are projections of the plan, not another analysis.
inline std::vector<size_t> synchronization_conflicts(
    const HandlerSynchronizationPlan& left, const HandlerSynchronizationPlan& right) {
  std::vector<size_t> result;
  for (const auto& entry : left.class_modes) {
    auto other = right.class_modes.find(entry.first);
    if (other != right.class_modes.end() &&
        (entry.second == SynchronizationMode::Exclusive || other->second == SynchronizationMode::Exclusive))
      result.push_back(entry.first);
  }
  return result;
}

} // namespace moss
