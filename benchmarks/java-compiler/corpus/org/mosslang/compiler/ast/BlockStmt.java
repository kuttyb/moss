package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.mosslang.compiler.driver.SourceSpan;

public record BlockStmt(MutableList<Stmt> statements, SourceSpan span) implements Stmt {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitBlockStmt(this, context);
    }
}
