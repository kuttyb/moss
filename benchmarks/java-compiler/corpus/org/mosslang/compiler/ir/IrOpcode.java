package org.mosslang.compiler.ir;

public enum IrOpcode {
    CONST_INT,
    LOAD_VAR,
    STORE_VAR,
    ADD,
    SUB,
    MUL,
    DIV,
    CMP_EQ,
    CMP_LT,
    JUMP,
    JUMP_IF_FALSE,
    CALL,
    RETURN
}
