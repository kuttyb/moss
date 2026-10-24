package org.mosslang.compiler.abi;

public enum CheckedStmtKind {
    Let(1),
    Var(2),
    Assign(3),
    AssignField(4),
    AssignIndex(5),
    ExprStmt(6),
    Return(7),
    Reply(8),
    If(9),
    While(10),
    For(11),
    Match(12),
    Pass(13),
    IfJoin(14),
    Try(15),
    Raise(16);

    public final int tag;
    CheckedStmtKind(int tag) { this.tag = tag; }
}
