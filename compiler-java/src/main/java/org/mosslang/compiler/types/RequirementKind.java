package org.mosslang.compiler.types;

public enum RequirementKind {
    SameType,
    Field,
    Method,
    Operator,
    Index,
    Iterate,
    Callable,
    MapKey
}
