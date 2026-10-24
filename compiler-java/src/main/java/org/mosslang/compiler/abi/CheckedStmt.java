package org.mosslang.compiler.abi;

import org.mosslang.compiler.types.TypeRef;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

public class CheckedStmt {
    public CheckedStmtKind kind = CheckedStmtKind.Return;
    public TypeRef type;
    public int localOrdinal = 0;
    public CheckedExpr expr;
    public List<CheckedExpr> operands = new ArrayList<>();
    public String semanticTargetId = "";
    public int memberOrdinal = 0;
    public String memberName = "";

    public List<CheckedStmt> thenBranch = new ArrayList<>();
    public List<CheckedStmt> elseBranch = new ArrayList<>();
    public List<CheckedStmt> body = new ArrayList<>();

    public CheckedStmt() {}

    public static CheckedStmt returnStmt(CheckedExpr expr) {
        CheckedStmt s = new CheckedStmt();
        s.kind = CheckedStmtKind.Return;
        s.expr = expr;
        s.type = expr != null ? expr.type : null;
        return s;
    }

    public static CheckedStmt letStmt(int localOrdinal, CheckedExpr expr) {
        CheckedStmt s = new CheckedStmt();
        s.kind = CheckedStmtKind.Let;
        s.localOrdinal = localOrdinal;
        s.expr = expr;
        s.type = expr != null ? expr.type : null;
        return s;
    }

    public static CheckedStmt varStmt(int localOrdinal, CheckedExpr expr) {
        CheckedStmt s = new CheckedStmt();
        s.kind = CheckedStmtKind.Var;
        s.localOrdinal = localOrdinal;
        s.expr = expr;
        s.type = expr != null ? expr.type : null;
        return s;
    }

    public static CheckedStmt assignStmt(int localOrdinal, CheckedExpr expr) {
        CheckedStmt s = new CheckedStmt();
        s.kind = CheckedStmtKind.Assign;
        s.localOrdinal = localOrdinal;
        s.expr = expr;
        s.type = expr != null ? expr.type : null;
        return s;
    }

    public static CheckedStmt replyStmt(CheckedExpr expr) {
        CheckedStmt s = new CheckedStmt();
        s.kind = CheckedStmtKind.Reply;
        s.expr = expr;
        s.type = expr != null ? expr.type : null;
        return s;
    }

    public static CheckedStmt exprStmt(CheckedExpr expr) {
        CheckedStmt s = new CheckedStmt();
        s.kind = CheckedStmtKind.ExprStmt;
        s.expr = expr;
        s.type = expr != null ? expr.type : null;
        return s;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || getClass() != o.getClass()) return false;
        CheckedStmt that = (CheckedStmt) o;
        return localOrdinal == that.localOrdinal &&
               memberOrdinal == that.memberOrdinal &&
               kind == that.kind &&
               Objects.equals(type, that.type) &&
               Objects.equals(expr, that.expr) &&
               Objects.equals(operands, that.operands) &&
               Objects.equals(semanticTargetId, that.semanticTargetId) &&
               Objects.equals(memberName, that.memberName) &&
               Objects.equals(thenBranch, that.thenBranch) &&
               Objects.equals(elseBranch, that.elseBranch) &&
               Objects.equals(body, that.body);
    }

    @Override
    public int hashCode() {
        return Objects.hash(kind, type, localOrdinal, expr, operands, semanticTargetId, memberOrdinal, memberName, thenBranch, elseBranch, body);
    }

    @Override
    public String toString() {
        return "CheckedStmt{" + kind + ", local=" + localOrdinal + ", expr=" + expr + "}";
    }
}
