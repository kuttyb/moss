package org.mosslang.compiler.abi;

public enum CheckedUnaryOp {
    Neg(1),
    Not(2);

    public final int tag;
    CheckedUnaryOp(int tag) { this.tag = tag; }
}
