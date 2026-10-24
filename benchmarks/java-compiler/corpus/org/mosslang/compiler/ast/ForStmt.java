package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;

public record ForStmt(String varName, Expr collection, Stmt body, SourceSpan span) implements Stmt {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitForStmt(this, context);
    }
}
