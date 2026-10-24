package org.mosslang.compiler.abi;

import org.mosslang.compiler.types.TypeRef;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

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

    public static CheckedExpr floatLiteral(String value, TypeRef type) {
        CheckedExpr e = new CheckedExpr();
        e.kind = CheckedExprKind.FloatLiteral;
        e.literalValue = value;
        e.type = type;
        return e;
    }

    public static CheckedExpr stringLiteral(String value, TypeRef type) {
        CheckedExpr e = new CheckedExpr();
        e.kind = CheckedExprKind.StringLiteral;
        e.literalValue = value;
        e.type = type;
        return e;
    }

    public static CheckedExpr boolLiteral(boolean value, TypeRef type) {
        CheckedExpr e = new CheckedExpr();
        e.kind = CheckedExprKind.BoolLiteral;
        e.literalValue = value ? "true" : "false";
        e.type = type;
        return e;
    }

    public static CheckedExpr localRead(int ordinal, TypeRef type) {
        CheckedExpr e = new CheckedExpr();
        e.kind = CheckedExprKind.LocalRead;
        e.ordinal = ordinal;
        e.type = type;
        return e;
    }

    public static CheckedExpr unary(CheckedUnaryOp op, CheckedExpr operand, TypeRef type) {
        CheckedExpr e = new CheckedExpr();
        e.kind = CheckedExprKind.Unary;
        e.unaryOp = op;
        e.operands.add(operand);
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

    public static CheckedExpr fieldAccess(String targetId, int memberOrdinal, String memberName, TypeRef type) {
        CheckedExpr e = new CheckedExpr();
        e.kind = CheckedExprKind.Field;
        e.semanticTargetId = targetId;
        e.memberOrdinal = memberOrdinal;
        e.memberName = memberName;
        e.type = type;
        return e;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || getClass() != o.getClass()) return false;
        CheckedExpr that = (CheckedExpr) o;
        return ordinal == that.ordinal &&
               memberOrdinal == that.memberOrdinal &&
               kind == that.kind &&
               Objects.equals(type, that.type) &&
               Objects.equals(literalValue, that.literalValue) &&
               unaryOp == that.unaryOp &&
               binaryOp == that.binaryOp &&
               Objects.equals(operands, that.operands) &&
               Objects.equals(semanticTargetId, that.semanticTargetId) &&
               Objects.equals(memberName, that.memberName);
    }

    @Override
    public int hashCode() {
        return Objects.hash(kind, type, ordinal, literalValue, unaryOp, binaryOp, operands, semanticTargetId, memberOrdinal, memberName);
    }

    @Override
    public String toString() {
        return "CheckedExpr{" + kind + (literalValue.isEmpty() ? "" : ":" + literalValue) + "}";
    }
}
