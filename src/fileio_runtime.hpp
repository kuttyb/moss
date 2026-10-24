#pragma once

#include "fileio_runtime_text.hpp"

namespace moss {

inline bool contains_fileio_token(const std::string& s, const char* token_str) {
  std::string_view token = token_str;
  size_t pos = 0;
  while ((pos = s.find(token, pos)) != std::string::npos) {
    bool before_ok = (pos == 0 || !(std::isalnum(static_cast<unsigned char>(s[pos - 1])) || s[pos - 1] == '_'));
    size_t end = pos + token.size();
    bool after_ok = (end == s.size() || !(std::isalnum(static_cast<unsigned char>(s[end])) || s[end] == '_'));
    if (before_ok && after_ok) return true;
    pos = end;
  }
  return false;
}

inline bool is_fileio_type(const std::string& type_str) {
  static const char* const types[] = {"FileIO", "RangeBatch", "Range"};
  for (const char* const t : types) {
    if (contains_fileio_token(type_str, t)) return true;
  }
  return false;
}

inline bool expr_uses_fileio(const std::string& s) {
  if (s.find("FileIO.") != std::string::npos || s.find("FileIO(") != std::string::npos ||
      s.find("RangeBatch.") != std::string::npos || s.find("RangeBatch(") != std::string::npos ||
      s.find("Range.empty") != std::string::npos || s.find("Range.from_") != std::string::npos ||
      s.find("Range::") != std::string::npos || s.find("FileIO::") != std::string::npos) {
    return true;
  }
  return false;
}

inline bool statement_uses_fileio(const Stmt& s) {
  return expr_uses_fileio(s.text) || expr_uses_fileio(s.a) || expr_uses_fileio(s.b);
}

inline bool program_uses_fileio(const Program& p) {
  if (p.main) {
    for (const auto& s : p.main->body) {
      if (statement_uses_fileio(s)) return true;
    }
  }
  for (const auto& obj : p.objects) {
    for (const auto& f : obj.fields) {
      if (is_fileio_type(f.type) || expr_uses_fileio(f.init)) return true;
    }
    for (const auto& m : obj.methods) {
      if (m.return_type && is_fileio_type(*m.return_type)) return true;
      for (const auto& param : m.params) {
        if (is_fileio_type(param.type)) return true;
      }
      for (const auto& s : m.body) {
        if (statement_uses_fileio(s)) return true;
      }
    }
  }
  for (const auto& d : p.domains) {
    for (const auto& f : d.state) {
      if (is_fileio_type(f.type) || expr_uses_fileio(f.init)) return true;
    }
    for (const auto& h : d.handlers) {
      if (h.reply_type && is_fileio_type(*h.reply_type)) return true;
      for (const auto& param : h.params) {
        if (is_fileio_type(param.type)) return true;
      }
      for (const auto& s : h.body) {
        if (statement_uses_fileio(s)) return true;
      }
      for (const auto& arm : h.failure_arms) {
        for (const auto& s : arm.body) {
          if (statement_uses_fileio(s)) return true;
        }
      }
    }
  }
  for (const auto& fn : p.functions) {
    if (fn.return_type && is_fileio_type(*fn.return_type)) return true;
    for (const auto& param : fn.params) {
      if (is_fileio_type(param.type)) return true;
    }
    for (const auto& s : fn.body) {
      if (statement_uses_fileio(s)) return true;
    }
  }
  for (const auto& e : p.enums) {
    for (const auto& c : e.cases) {
      for (const auto& f : c.fields) {
        if (is_fileio_type(f.type)) return true;
      }
    }
  }
  for (const auto& t : p.tests) {
    for (const auto& s : t.body) {
      if (statement_uses_fileio(s)) return true;
    }
  }
  for (const auto& b : p.benchmarks) {
    for (const auto& s : b.body) {
      if (statement_uses_fileio(s)) return true;
    }
  }
  return false;
}

const char* fileio_runtime_rust();

const char* fileio_root_runtime_rust();

} // namespace moss
