package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;
import org.mosslang.compiler.types.TypeRef;
import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class SyntaxNode_002 implements AstNode {
    private final SourceSpan span;
    private final MutableList<AstNode> children = new FastList<>();
    private TypeRef inferredType;

    public SyntaxNode_002(SourceSpan span) {
        this.span = span;
    }

    public void addChild(AstNode child) { children.add(child); }
    public MutableList<AstNode> getChildren() { return children; }
    public TypeRef getInferredType() { return inferredType; }
    public void setInferredType(TypeRef t) { this.inferredType = t; }

    @Override
    public SourceSpan span() { return span; }

    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        for (AstNode c : children) c.accept(visitor, context);
        return null;
    }
}
