package org.mosslang.compiler.optimizer;

public enum FunctionalMaterializationKind {
    NotApplicable,
    Virtual,
    Materialize,
    ReusableStorageCandidate
}
