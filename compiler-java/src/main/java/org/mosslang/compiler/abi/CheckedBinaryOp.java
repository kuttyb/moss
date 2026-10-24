package org.mosslang.compiler.abi;

public enum CheckedBinaryOp {
    Add(1),
    Sub(2),
    Mul(3),
    Div(4),
    Mod(5),
    Equal(6),
    NotEqual(7),
    Less(8),
    LessEqual(9),
    Greater(10),
    GreaterEqual(11),
    And(12),
    Or(13),
    Xor(14);

    public final int tag;
    CheckedBinaryOp(int tag) { this.tag = tag; }
}
