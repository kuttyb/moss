#pragma once
#include "pch.hpp"
#include "common_types.hpp"

#if defined(USE_ETI)
extern template class std::vector<synth::AstNode>;
extern template class std::vector<synth::AstStmt>;
extern template class std::vector<synth::SymbolFact>;
extern template class std::vector<synth::TypeRecord>;
extern template class std::vector<synth::GraphEdge>;
extern template class std::vector<synth::IndexEntry>;
extern template class std::vector<synth::ValueObject>;
extern template class std::vector<std::string>;
extern template class std::map<std::string, synth::SymbolFact>;
extern template class std::map<int32_t, synth::AstNode>;
extern template class std::unordered_map<std::string, synth::TypeRecord>;
extern template class std::unordered_map<std::string, synth::ValueObject>;
#endif
