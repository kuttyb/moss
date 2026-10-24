package org.mosslang.compiler.diagnostics;

import java.util.Objects;

public final class SourceLocation {
    private final String sourceFile;
    private final int line;
    private final int column;

    public SourceLocation(String sourceFile, int line, int column) {
        this.sourceFile = sourceFile != null ? sourceFile : "";
        this.line = line;
        this.column = column;
    }

    public SourceLocation(int line, int column) {
        this("", line, column);
    }

    public String getSourceFile() {
        return sourceFile;
    }

    public int getLine() {
        return line;
    }

    public int getColumn() {
        return column;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof SourceLocation)) return false;
        SourceLocation that = (SourceLocation) o;
        return line == that.line && column == that.column && Objects.equals(sourceFile, that.sourceFile);
    }

    @Override
    public int hashCode() {
        return Objects.hash(sourceFile, line, column);
    }

    @Override
    public String toString() {
        if (sourceFile.isEmpty()) {
            return line + ":" + column;
        }
        return sourceFile + ":" + line + ":" + column;
    }
}
