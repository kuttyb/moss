package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;

public class VariableExpr implements Expr {
    private final String name;
    private final SourceSpan span;
    private int resolvedSymbolId = -1;

    public VariableExpr(String name, SourceSpan span) {
        this.name = name;
        this.span = span;
    }

    public String name() { return name; }
    @Override public SourceSpan span() { return span; }
    public int resolvedSymbolId() { return resolvedSymbolId; }
    public void setResolvedSymbolId(int id) { this.resolvedSymbolId = id; }

    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitVariableExpr(this, context);
    }
}
