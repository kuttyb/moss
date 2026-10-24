package org.mosslang.compiler.diagnostics;

import java.util.Objects;

public final class SourceSpan {
    private final SourceLocation start;
    private final SourceLocation end;

    public SourceSpan(SourceLocation start, SourceLocation end) {
        this.start = start;
        this.end = end;
    }

    public SourceSpan(String sourceFile, int startLine, int startCol, int endLine, int endCol) {
        this(new SourceLocation(sourceFile, startLine, startCol),
             new SourceLocation(sourceFile, endLine, endCol));
    }

    public SourceLocation getStart() {
        return start;
    }

    public SourceLocation getEnd() {
        return end;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof SourceSpan)) return false;
        SourceSpan that = (SourceSpan) o;
        return Objects.equals(start, that.start) && Objects.equals(end, that.end);
    }

    @Override
    public int hashCode() {
        return Objects.hash(start, end);
    }

    @Override
    public String toString() {
        return start + "-" + end;
    }
}
