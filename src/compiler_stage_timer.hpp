#pragma once

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
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
  bool flushed_{false};
  uint32_t pid_{1};

  CompilerTraceRecorder() {
    const char* profile_env = std::getenv("MOSS_PROFILE_COMPILER");
    const char* trace_env = std::getenv("MOSS_PROFILE_TRACE");
    profile_compiler_ = (profile_env != nullptr);
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
    out << "\n    }\n  }\n}\n";
  }

  ~CompilerTraceRecorder() {
    flush_trace();
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
