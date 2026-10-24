package org.mosslang.compiler.ir;

import org.mosslang.compiler.ast.*;

public class IrBuilder extends AstVisitorBase<Integer, IrFunction> {
    private int nextReg = 1;

    public IrModule build(SourceFile file) {
        IrModule mod = new IrModule(file.fileName());
        for (FunctionDecl f : file.functions()) {
            IrFunction irFunc = new IrFunction(f.name());
            IrBasicBlock entry = irFunc.createBlock();
            f.body().accept(this, irFunc);
            mod.addFunction(irFunc);
        }
        return mod;
    }

    @Override
    public Integer visitVariableExpr(VariableExpr expr, IrFunction ctx) {
        int r = nextReg++;
        IrBasicBlock bb = ctx.blocks().getLast();
        bb.emit(new IrInstruction(IrOpcode.LOAD_VAR, r, expr.resolvedSymbolId(), -1, 0));
        return r;
    }

    @Override
    public Integer visitLetStmt(LetStmt stmt, IrFunction ctx) {
        int initReg = stmt.init() != null ? stmt.init().accept(this, ctx) : 0;
        IrBasicBlock bb = ctx.blocks().getLast();
        bb.emit(new IrInstruction(IrOpcode.STORE_VAR, -1, initReg, -1, 0));
        return initReg;
    }

    @Override
    public Integer visitLiteralExpr(LiteralExpr expr, IrFunction ctx) {
        int r = nextReg++;
        IrBasicBlock bb = ctx.blocks().getLast();
        long val = 0;
        if (expr.value() instanceof Number n) val = n.longValue();
        else if (expr.value() instanceof Boolean b) val = b ? 1 : 0;
        bb.emit(IrInstruction.constInt(r, val));
        return r;
    }

    @Override
    public Integer visitBinaryExpr(BinaryExpr expr, IrFunction ctx) {
        int left = expr.left().accept(this, ctx);
        int right = expr.right().accept(this, ctx);
        int dest = nextReg++;
        IrOpcode op = switch (expr.op()) {
            case ADD -> IrOpcode.ADD;
            case SUB -> IrOpcode.SUB;
            case MUL -> IrOpcode.MUL;
            case DIV -> IrOpcode.DIV;
            case EQ -> IrOpcode.CMP_EQ;
            case LT -> IrOpcode.CMP_LT;
            default -> IrOpcode.ADD;
        };
        ctx.blocks().getLast().emit(IrInstruction.binOp(op, dest, left, right));
        return dest;
    }

    @Override
    public Integer visitReturnStmt(ReturnStmt stmt, IrFunction ctx) {
        int r = stmt.value() != null ? stmt.value().accept(this, ctx) : 0;
        ctx.blocks().getLast().emit(IrInstruction.ret(r));
        return r;
    }
}
