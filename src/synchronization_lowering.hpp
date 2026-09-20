#pragma once
#include "ast.hpp"

namespace moss {
// Validate stored analysis; never derive a replacement synchronization plan.
inline void validate_synchronization_lowering(const Program& program) {
    const auto& graph = program.concrete_domain_graph;
    const auto& plan = program.synchronization_plan;
    if (graph.instances.empty()) {
      synchronization_require(plan.domains.empty(), "plan without concrete graph");
      return;
    }
    synchronization_require(graph.closed && graph.identity == plan.graph_identity,
                            "lowering requires matching closed graph");
    synchronization_require(graph.instances.size() == plan.domains.size(), "missing instance plan");
    std::map<string, int> ranks;
    std::set<int> unique_ranks;
    for (const auto& instance : graph.instances) {
      synchronization_require(instance.domain_rank >= 0 && unique_ranks.insert(instance.domain_rank).second,
                              "invalid concrete domain rank");
      synchronization_require(ranks.emplace(instance.identity, instance.domain_rank).second, "duplicate instance");
      synchronization_require(instance.specialization_index.has_value(), "missing specialization linkage");
      const auto& specialization = program.domain_specializations.at(*instance.specialization_index);
      synchronization_require(specialization.identity() == instance.specialization, "specialization mismatch");
      auto found = std::find_if(plan.domains.begin(), plan.domains.end(),
          [&](const auto& entry) { return entry.concrete_instance_id == instance.identity; });
      synchronization_require(found != plan.domains.end(), "missing physical instance plan");
      const auto& domain = *found;
      synchronization_require(domain.concrete_instance_id == instance.identity &&
          domain.specialization_id == instance.specialization && domain.domain == instance.domain &&
          domain.domain_rank == instance.domain_rank, "physical plan instance mismatch");
      StateLeafSet partition;
      for (size_t rank = 0; rank < domain.classes.size(); ++rank) {
        const auto& cls = domain.classes[rank];
        synchronization_require(cls.class_rank == rank && !cls.member_leaves.empty(), "invalid class rank");
        synchronization_require(cls.mode_signature.size() == domain.handlers.size(), "invalid signature size");
        for (const auto& leaf : cls.member_leaves) {
          for (size_t h = 0; h < domain.handlers.size(); ++h)
            synchronization_require(cls.mode_signature[h] ==
                synchronization_mode(domain.handlers[h].effects.normalized(leaf)), "class signature mismatch");
          synchronization_require(domain.state_leaves.count(leaf) && domain.protected_mutable_leaves.count(leaf) &&
              partition.insert(leaf).second && domain.leaf_to_class.at(leaf) == rank, "invalid class partition");
        }
      }
      synchronization_require(partition == domain.protected_mutable_leaves &&
          domain.leaf_to_class.size() == partition.size(), "class partition differs from X*");
      std::set<string> handlers;
      StateLeafSet mutable_universe;
      for (size_t h = 0; h < domain.handlers.size(); ++h) {
        const auto& handler = domain.handlers[h];
        synchronization_require(handlers.insert(handler.name).second, "duplicate handler plan");
        StateLeafSet exclusive = handler.effects.writes;
        exclusive.insert(handler.effects.consumes.begin(), handler.effects.consumes.end());
        synchronization_require(exclusive == handler.exclusive_set, "exclusive storage set mismatch");
        mutable_universe.insert(exclusive.begin(), exclusive.end());
        for (const auto* effects : {&handler.effects.reads, &handler.effects.writes, &handler.effects.consumes})
          for (const auto& leaf : *effects)
            synchronization_require(domain.state_leaves.count(leaf), "effect outside planned state");
        StateLeafSet footprint;
        for (const auto& entry : handler.class_modes) {
          synchronization_require(entry.first < domain.classes.size() &&
              (entry.second == SynchronizationMode::Shared || entry.second == SynchronizationMode::Exclusive),
              "invalid acquisition class or mode");
          const auto& cls = domain.classes.at(entry.first);
          synchronization_require(cls.mode_signature.at(h) == entry.second, "acquisition/signature mismatch");
          footprint.insert(cls.member_leaves.begin(), cls.member_leaves.end());
        }
        synchronization_require(footprint == handler.lock_set, "acquisition differs from LockSet");
        for (const auto& leaf : domain.state_leaves) {
          auto effect = handler.effects.normalized(leaf);
          if (!effect) continue;
          if (!domain.protected_mutable_leaves.count(leaf)) {
            synchronization_require(*effect == Effect::Read, "unprotected mutable state");
            continue;
          }
          auto mode = handler.class_modes.find(domain.leaf_to_class.at(leaf));
          synchronization_require(mode != handler.class_modes.end() && mode->second == synchronization_mode(effect),
                                  "storage access not covered by final mode");
        }
      }
      synchronization_require(mutable_universe == domain.protected_mutable_leaves, "X* differs from exclusive union");
    }
    for (const auto& edge : graph.edges)
      synchronization_require(ranks.at(edge.source_instance) < ranks.at(edge.target_instance), "descending route rank");
  }

} // namespace moss
