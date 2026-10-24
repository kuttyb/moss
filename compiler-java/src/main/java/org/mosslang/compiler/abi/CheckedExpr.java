package org.mosslang.compiler.abi;

import org.mosslang.compiler.types.TypeRef;
import java.util.ArrayList;
import java.util.List;

public class CheckedExpr {
    public CheckedExprKind kind = CheckedExprKind.Parameter;
    public TypeRef type;
    public int ordinal = 0;
    public String literalValue = "";
    public CheckedUnaryOp unaryOp = CheckedUnaryOp.Neg;
    public CheckedBinaryOp binaryOp = CheckedBinaryOp.Add;
    public List<CheckedExpr> operands = new ArrayList<>();
    public String semanticTargetId = "";
    public int memberOrdinal = 0;
    public String memberName = "";

    public CheckedExpr() {}

    public static CheckedExpr parameter(int index, TypeRef type) {
        CheckedExpr e = new CheckedExpr();
        e.kind = CheckedExprKind.Parameter;
        e.ordinal = index;
        e.type = type;
        return e;
    }

    public static CheckedExpr integerLiteral(long value, TypeRef type) {
        CheckedExpr e = new CheckedExpr();
        e.kind = CheckedExprKind.IntegerLiteral;
        e.literalValue = Long.toString(value);
        e.type = type;
        return e;
    }

    public static CheckedExpr binary(CheckedBinaryOp op, CheckedExpr left, CheckedExpr right, TypeRef type) {
        CheckedExpr e = new CheckedExpr();
        e.kind = CheckedExprKind.Binary;
        e.binaryOp = op;
        e.operands.add(left);
        e.operands.add(right);
        e.type = type;
        return e;
    }
}
