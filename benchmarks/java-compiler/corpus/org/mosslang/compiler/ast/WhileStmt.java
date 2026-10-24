package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;

public record WhileStmt(Expr condition, Stmt body, SourceSpan span) implements Stmt {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitWhileStmt(this, context);
    }
}
