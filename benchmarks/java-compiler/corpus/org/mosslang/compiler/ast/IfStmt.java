package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;

public record IfStmt(Expr condition, Stmt thenBranch, Stmt elseBranch, SourceSpan span) implements Stmt {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitIfStmt(this, context);
    }
}
