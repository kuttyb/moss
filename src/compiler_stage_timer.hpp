#pragma once

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace moss {

struct TraceEvent {
  std::string name;
  std::string cat;
  uint64_t ts_us;
  uint64_t dur_us;
  uint32_t pid;
  uint32_t tid;
  std::map<std::string, std::string> args;
};

class CompilerTraceRecorder {
 private:
  bool profile_compiler_{false};
  bool trace_enabled_{false};
  std::string trace_file_;
  std::chrono::steady_clock::time_point start_time_;
  std::mutex mutex_;
  std::vector<TraceEvent> events_;
  std::map<std::string, uint64_t> counters_;
  struct OperationMetric {
    uint64_t count{0};
    uint64_t total_ns{0};
    std::unordered_set<std::string> distinct_keys;
  };
  std::map<std::string, OperationMetric> operations_;
  bool flushed_{false};
  uint32_t pid_{1};

  CompilerTraceRecorder() {
    const char* profile_env = std::getenv("MOSS_PROFILE_COMPILER");
    const char* trace_env = std::getenv("MOSS_PROFILE_TRACE");
    const char* hotspot_env = std::getenv("MOSS_PROFILE_HOTSPOTS");
    profile_compiler_ = (profile_env != nullptr) || (hotspot_env != nullptr);
    if (trace_env != nullptr && *trace_env != '\0') {
      trace_enabled_ = true;
      trace_file_ = trace_env;
    } else if (profile_env != nullptr && std::string(profile_env) == "trace") {
      trace_enabled_ = true;
      trace_file_ = "moss_trace.json";
    }
    start_time_ = std::chrono::steady_clock::now();
#if defined(__unix__) || defined(__APPLE__)
    pid_ = static_cast<uint32_t>(getpid());
#else
    pid_ = 1;
#endif
  }

 public:
  static CompilerTraceRecorder& instance() {
    static CompilerTraceRecorder recorder;
    return recorder;
  }

  bool is_enabled() const {
    return profile_compiler_ || trace_enabled_;
  }

  bool is_trace_enabled() const {
    return trace_enabled_;
  }

  void record_event(const std::string& name, const std::string& cat,
                    std::chrono::steady_clock::time_point begin,
                    std::chrono::steady_clock::time_point end,
                    std::map<std::string, std::string> args = {}) {
    if (!trace_enabled_) return;
    uint64_t ts = std::chrono::duration_cast<std::chrono::microseconds>(
        begin - start_time_).count();
    uint64_t dur = std::chrono::duration_cast<std::chrono::microseconds>(
        end - begin).count();
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(TraceEvent{name, cat, ts, dur, pid_, 1, std::move(args)});
  }

  void increment_counter(const std::string& name, uint64_t amount = 1) {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_[name] += amount;
  }

  void set_counter(const std::string& name, uint64_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_[name] = value;
  }

  uint64_t get_counter(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = counters_.find(name);
    return it != counters_.end() ? it->second : 0;
  }

  std::map<std::string, uint64_t> all_counters() {
    std::lock_guard<std::mutex> lock(mutex_);
    return counters_;
  }

  void record_operation(const std::string& name, uint64_t dur_ns, const std::string& key = "") {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& m = operations_[name];
    m.count++;
    m.total_ns += dur_ns;
    if (!key.empty()) m.distinct_keys.insert(key);
  }

  std::map<std::string, OperationMetric> all_operations() {
    std::lock_guard<std::mutex> lock(mutex_);
    return operations_;
  }

  void dump_hotspot_report() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (operations_.empty()) return;
    std::cerr << "\n================================ COMPILER HOTSPOT BREAKDOWN ================================\n";
    std::cerr << std::left << std::setw(34) << "Compiler Operation"
              << std::right << std::setw(12) << "Time (ms)"
              << std::setw(10) << "Calls"
              << std::setw(12) << "Distinct"
              << std::setw(12) << "Repeated"
              << std::setw(12) << "Avg (us)" << "\n";
    std::cerr << "--------------------------------------------------------------------------------------------\n";
    for (const auto& [name, op] : operations_) {
      double total_ms = static_cast<double>(op.total_ns) / 1000000.0;
      double avg_us = op.count ? static_cast<double>(op.total_ns) / (1000.0 * op.count) : 0.0;
      std::cerr << std::left << std::setw(34) << name
                << std::right << std::setw(12) << std::fixed << std::setprecision(2) << total_ms
                << std::setw(10) << op.count
                << std::setw(12) << op.distinct_keys.size()
                << std::setw(12) << (op.count > op.distinct_keys.size() ? op.count - op.distinct_keys.size() : 0)
                << std::setw(12) << std::fixed << std::setprecision(1) << avg_us << "\n";
    }
    std::cerr << "============================================================================================\n\n";
  }

  void flush_trace() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!trace_enabled_ || flushed_ || trace_file_.empty()) return;
    flushed_ = true;

    std::ofstream out(trace_file_);
    if (!out) {
      std::cerr << "moss: warning: cannot open trace output file: " << trace_file_ << "\n";
      return;
    }

    out << "{\n  \"traceEvents\": [\n";
    out << "    {\"name\": \"process_name\", \"ph\": \"M\", \"pid\": " << pid_
        << ", \"args\": {\"name\": \"moss\"}},\n";
    out << "    {\"name\": \"thread_name\", \"ph\": \"M\", \"pid\": " << pid_
        << ", \"tid\": 1, \"args\": {\"name\": \"main\"}},\n";

    for (size_t i = 0; i < events_.size(); ++i) {
      const auto& ev = events_[i];
      out << "    {\"name\": \"" << ev.name << "\", \"cat\": \"" << ev.cat
          << "\", \"ph\": \"X\", \"ts\": " << ev.ts_us
          << ", \"dur\": " << ev.dur_us
          << ", \"pid\": " << ev.pid
          << ", \"tid\": " << ev.tid
          << ", \"args\": {";
      bool first_arg = true;
      for (const auto& [k, v] : ev.args) {
        if (!first_arg) out << ", ";
        first_arg = false;
        out << "\"" << k << "\": \"" << v << "\"";
      }
      out << "}},\n";
    }

    // Emit counter event
    uint64_t now_ts = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start_time_).count();
    out << "    {\"name\": \"compiler_counters\", \"cat\": \"counters\", \"ph\": \"C\", \"ts\": "
        << now_ts << ", \"pid\": " << pid_ << ", \"tid\": 1, \"args\": {";
    bool first_c = true;
    for (const auto& [k, v] : counters_) {
      if (!first_c) out << ", ";
      first_c = false;
      out << "\"" << k << "\": " << v;
    }
    out << "}}\n";

    out << "  ],\n  \"displayTimeUnit\": \"ms\",\n  \"metadata\": {\n";
    out << "    \"format\": \"Chrome Trace Event JSON\",\n";
    out << "    \"compiler\": \"moss-opt\",\n";
    out << "    \"counters\": {\n";
    first_c = true;
    for (const auto& [k, v] : counters_) {
      if (!first_c) out << ",\n";
      first_c = false;
      out << "      \"" << k << "\": " << v;
    }
    out << "\n    },\n";

    out << "    \"operations\": {\n";
    bool first_op = true;
    for (const auto& [name, op] : operations_) {
      if (!first_op) out << ",\n";
      first_op = false;
      double total_ms = static_cast<double>(op.total_ns) / 1000000.0;
      double avg_us = op.count ? static_cast<double>(op.total_ns) / (1000.0 * op.count) : 0.0;
      out << "      \"" << name << "\": {\n";
      out << "        \"calls\": " << op.count << ",\n";
      out << "        \"total_ms\": " << total_ms << ",\n";
      out << "        \"distinct_inputs\": " << op.distinct_keys.size() << ",\n";
      out << "        \"repeated_calls\": " << (op.count > op.distinct_keys.size() ? op.count - op.distinct_keys.size() : 0) << ",\n";
      out << "        \"avg_us\": " << avg_us << "\n";
      out << "      }";
    }
    out << "\n    }\n  }\n}\n";
  }

  ~CompilerTraceRecorder() {
    flush_trace();
    if (std::getenv("MOSS_PROFILE_HOTSPOTS") != nullptr) {
      dump_hotspot_report();
    }
  }
};

class ScopedOpTimer {
  const char* name_;
  std::string key_;
  bool enabled_;
  std::chrono::steady_clock::time_point begin_;

 public:
  explicit ScopedOpTimer(const char* name, std::string key = "")
      : name_(name),
        key_(std::move(key)),
        enabled_(CompilerTraceRecorder::instance().is_enabled()) {
    if (enabled_) begin_ = std::chrono::steady_clock::now();
  }

  ~ScopedOpTimer() {
    if (!enabled_) return;
    auto end = std::chrono::steady_clock::now();
    auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin_).count();
    CompilerTraceRecorder::instance().record_operation(name_, duration_ns, key_);
  }
};

class CompilerStageTimer {
  const char* stage_;
  const char* category_;
  bool enabled_;
  std::chrono::steady_clock::time_point begin_;
  std::map<std::string, std::string> args_;

 public:
  explicit CompilerStageTimer(const char* stage, const char* category = "compiler",
                              std::map<std::string, std::string> args = {})
      : stage_(stage),
        category_(category),
        enabled_(CompilerTraceRecorder::instance().is_enabled()),
        args_(std::move(args)) {
    if (enabled_) begin_ = std::chrono::steady_clock::now();
  }

  ~CompilerStageTimer() {
    if (!enabled_) return;
    auto end = std::chrono::steady_clock::now();
    auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin_).count();
    if (std::getenv("MOSS_PROFILE_COMPILER") != nullptr) {
      std::cerr << "MOSS_PROFILE|" << stage_ << '|' << duration_ns << '\n';
    }
    CompilerTraceRecorder::instance().record_event(stage_, category_, begin_, end, std::move(args_));
  }
};

} // namespace moss
