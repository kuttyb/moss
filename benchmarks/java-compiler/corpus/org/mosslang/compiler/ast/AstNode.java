package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;

public interface AstNode {
    SourceSpan span();
    <R, C> R accept(AstVisitor<R, C> visitor, C context);
}
