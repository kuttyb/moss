package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;

public record ReturnStmt(Expr value, SourceSpan span) implements Stmt {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitReturnStmt(this, context);
    }
}
