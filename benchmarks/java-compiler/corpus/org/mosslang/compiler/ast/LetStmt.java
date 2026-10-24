package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;
import org.mosslang.compiler.types.TypeRef;

public record LetStmt(String name, TypeRef typeAnnotation, Expr init, SourceSpan span) implements Stmt {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitLetStmt(this, context);
    }
}
