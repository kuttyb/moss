package org.mosslang.compiler.optimizer;

public enum FunctionalNodeKind {
    Source,
    Map,
    Filter,
    Reduce,
    Sum,
    Count,
    Any,
    All;

    public boolean isTerminal() {
        return this == Reduce || this == Sum || this == Count || this == Any || this == All;
    }
}
