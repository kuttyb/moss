package org.mosslang.compiler.ir;

public record IrInstruction(
    IrOpcode opcode,
    int destRegister,
    int srcRegister1,
    int srcRegister2,
    long immediate
) {
    public static IrInstruction constInt(int dest, long imm) {
        return new IrInstruction(IrOpcode.CONST_INT, dest, -1, -1, imm);
    }

    public static IrInstruction binOp(IrOpcode op, int dest, int src1, int src2) {
        return new IrInstruction(op, dest, src1, src2, 0);
    }

    public static IrInstruction ret(int src) {
        return new IrInstruction(IrOpcode.RETURN, -1, src, -1, 0);
    }
}
