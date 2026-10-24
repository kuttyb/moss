package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;

public record BinaryExpr(Expr left, BinaryOp op, Expr right, SourceSpan span) implements Expr {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitBinaryExpr(this, context);
    }
}
