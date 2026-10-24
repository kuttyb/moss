package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.mosslang.compiler.driver.SourceSpan;

public record PipelineExpr(Expr initial, MutableList<Expr> stages, SourceSpan span) implements Expr {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitPipelineExpr(this, context);
    }
}
