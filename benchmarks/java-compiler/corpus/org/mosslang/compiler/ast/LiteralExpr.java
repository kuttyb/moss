package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;
import org.mosslang.compiler.types.PrimitiveType;

public record LiteralExpr(Object value, PrimitiveType type, SourceSpan span) implements Expr {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitLiteralExpr(this, context);
    }
}
