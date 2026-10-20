#include "pch.hpp"
#include "common_types.hpp"
#include "eti_declarations.hpp"

#if defined(USE_ETI)
template class std::vector<synth::AstNode>;
template class std::vector<synth::AstStmt>;
template class std::vector<synth::SymbolFact>;
template class std::vector<synth::TypeRecord>;
template class std::vector<synth::GraphEdge>;
template class std::vector<synth::IndexEntry>;
template class std::vector<synth::ValueObject>;
template class std::vector<std::string>;
template class std::map<std::string, synth::SymbolFact>;
template class std::map<int32_t, synth::AstNode>;
template class std::unordered_map<std::string, synth::TypeRecord>;
template class std::unordered_map<std::string, synth::ValueObject>;
#endif

namespace synth {
void explicit_instantiations_anchor() {
}
}
