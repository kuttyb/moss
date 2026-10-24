package org.mosslang.compiler.driver;

public final class SourceLocation {
    private final org.mosslang.compiler.diagnostics.SourceLocation delegate;

    public SourceLocation(String sourceFile, int line, int column) {
        this.delegate = new org.mosslang.compiler.diagnostics.SourceLocation(sourceFile, line, column);
    }

    public SourceLocation(int line, int column) {
        this("", line, column);
    }

    public String getSourceFile() { return delegate.getSourceFile(); }
    public int getLine() { return delegate.getLine(); }
    public int getColumn() { return delegate.getColumn(); }

    @Override
    public String toString() { return delegate.toString(); }
    @Override
    public boolean equals(Object o) { return delegate.equals(o); }
    @Override
    public int hashCode() { return delegate.hashCode(); }
}
