// Diagnostic projections of the stored plan only. No effects or partition are
// inferred here; pair counts exclude the diagonal, whose self-conflicts are
// reported separately. Ratios with an empty denominator are JSON null.
struct SynchronizationMetrics {
  size_t readonly_handlers = 0, self_conflicting = 0;
  size_t pairs = 0, conflicting = 0, shared_pairs = 0;
};
static SynchronizationMetrics synchronization_metrics(const DomainSynchronizationPlan& domain) {
  SynchronizationMetrics m;
  for (const auto& h : domain.handlers) {
    m.readonly_handlers += h.effects.writes.empty() && h.effects.consumes.empty();
    m.self_conflicting += h.self_conflict();
  }
  for (size_t a = 0; a < domain.handlers.size(); ++a)
    for (size_t b = a + 1; b < domain.handlers.size(); ++b) {
      ++m.pairs;
      bool conflict = !synchronization_conflicts(domain.handlers[a], domain.handlers[b]).empty();
      m.conflicting += conflict;
      bool shared = false;
      for (const auto& entry : domain.handlers[a].class_modes) {
        auto other = domain.handlers[b].class_modes.find(entry.first);
        shared |= other != domain.handlers[b].class_modes.end() &&
            entry.second == SynchronizationMode::Shared && other->second == SynchronizationMode::Shared;
      }
      m.shared_pairs += shared && !conflict;
    }
  return m;
}
static size_t shared_acquisitions(const HandlerSynchronizationPlan& h) {
  return std::count_if(h.class_modes.begin(), h.class_modes.end(), [](const auto& entry) {
    return entry.second == SynchronizationMode::Shared;
  });
}
static void write_partition_diagnostics(std::ostream& out, const DomainSynchronizationPlan& domain) {
  out << ",\"handler_order\":[";
  for (size_t h = 0; h < domain.handlers.size(); ++h) {
    if (h) out << ',';
    write_debug_json_string(out, domain.handlers[h].handler_identity);
  }
  out << "],\"conflict_matrix\":[";
  for (size_t a = 0; a < domain.handlers.size(); ++a) {
    if (a) out << ',';
    out << '[';
    for (size_t b = 0; b < domain.handlers.size(); ++b) {
      if (b) out << ',';
      out << (synchronization_conflicts(domain.handlers[a], domain.handlers[b]).empty() ? "false" : "true");
    }
    out << ']';
  }
  out << "],\"class_opportunities\":[";
  for (size_t c = 0; c < domain.classes.size(); ++c) {
    if (c) out << ',';
    const auto& cls = domain.classes[c];
    out << "{\"class_id\":"; write_debug_json_string(out, cls.class_id);
    out << ",\"leaf_count\":" << cls.member_leaves.size();
    for (auto mode : {SynchronizationMode::Shared, SynchronizationMode::Exclusive}) {
      out << (mode == SynchronizationMode::Shared ? ",\"reader_handlers\":[" : ",\"writer_handlers\":[");
      bool first = true;
      for (size_t h = 0; h < domain.handlers.size(); ++h) if (cls.mode_signature[h] == mode) {
        if (!first) out << ',';
        first = false;
        write_debug_json_string(out, domain.handlers[h].handler_identity);
      }
      out << ']';
    }
    size_t readers = std::count(cls.mode_signature.begin(), cls.mode_signature.end(), SynchronizationMode::Shared);
    out << ",\"shared_shared_pairs\":" << (readers ? readers * (readers - 1) / 2 : 0)
        << ",\"collapse_reason\":\"Members have identical complete per-handler mode signatures\"}";
  }
  out << "],\"class_splits\":[";
  bool first = true;
  for (size_t a = 0; a < domain.classes.size(); ++a)
    for (size_t b = a + 1; b < domain.classes.size(); ++b) {
      const auto& left = domain.classes[a]; const auto& right = domain.classes[b];
      for (size_t h = 0; h < domain.handlers.size(); ++h) {
        if (left.mode_signature[h] == right.mode_signature[h]) continue;
        if (!first) out << ',';
        first = false;
        out << "{\"left\":"; write_debug_json_string(out, left.class_id);
        out << ",\"right\":"; write_debug_json_string(out, right.class_id);
        out << ",\"handler\":"; write_debug_json_string(out, domain.handlers[h].handler_identity);
        out << ",\"left_mode\":"; write_debug_json_string(out, synchronization_mode_name(left.mode_signature[h]));
        out << ",\"right_mode\":"; write_debug_json_string(out, synchronization_mode_name(right.mode_signature[h]));
        out << '}'; break; // first deterministic distinguishing handler suffices
      }
    }
  out << ']';
}
