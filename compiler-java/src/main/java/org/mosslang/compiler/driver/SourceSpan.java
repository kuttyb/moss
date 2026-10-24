package org.mosslang.compiler.driver;

public final class SourceSpan {
    private final org.mosslang.compiler.diagnostics.SourceSpan delegate;

    public SourceSpan(org.mosslang.compiler.diagnostics.SourceLocation start, org.mosslang.compiler.diagnostics.SourceLocation end) {
        this.delegate = new org.mosslang.compiler.diagnostics.SourceSpan(start, end);
    }

    public SourceSpan(String sourceFile, int startLine, int startCol, int endLine, int endCol) {
        this.delegate = new org.mosslang.compiler.diagnostics.SourceSpan(sourceFile, startLine, startCol, endLine, endCol);
    }

    public org.mosslang.compiler.diagnostics.SourceLocation getStart() { return delegate.getStart(); }
    public org.mosslang.compiler.diagnostics.SourceLocation getEnd() { return delegate.getEnd(); }

    @Override
    public String toString() { return delegate.toString(); }
    @Override
    public boolean equals(Object o) { return delegate.equals(o); }
    @Override
    public int hashCode() { return delegate.hashCode(); }
}
