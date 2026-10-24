package org.mosslang.compiler.driver;

public record SourceSpan(SourceLocation start, SourceLocation end) {
    public static final SourceSpan NONE = new SourceSpan(SourceLocation.UNKNOWN, SourceLocation.UNKNOWN);

    public boolean contains(int line, int col) {
        if (line < start.line() || line > end.line()) return false;
        if (line == start.line() && col < start.column()) return false;
        if (line == end.line() && col > end.column()) return false;
        return true;
    }
}
