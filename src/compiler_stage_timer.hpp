#pragma once

#include <chrono>

namespace moss {

class CompilerStageTimer {
  const char* stage_;
  bool enabled_;
  std::chrono::steady_clock::time_point begin_;

 public:
  explicit CompilerStageTimer(const char* stage);
  ~CompilerStageTimer();
};

}
