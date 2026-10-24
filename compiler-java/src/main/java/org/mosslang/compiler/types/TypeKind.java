package org.mosslang.compiler.types;

public enum TypeKind {
    Primitive,
    Builtin,
    NominalInstance,
    Enum,
    Domain,
    TraitConstraint,
    InferenceVar,
    Unit
}
