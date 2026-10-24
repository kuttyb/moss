package org.mosslang.compiler.abi;

import org.mosslang.compiler.types.TypeRef;
import java.util.ArrayList;
import java.util.List;

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
}
