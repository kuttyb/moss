package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;

public record UnaryExpr(UnaryOp op, Expr operand, SourceSpan span) implements Expr {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitUnaryExpr(this, context);
    }
}
