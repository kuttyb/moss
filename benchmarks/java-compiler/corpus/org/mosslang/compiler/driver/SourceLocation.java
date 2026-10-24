package org.mosslang.compiler.driver;

public record SourceLocation(String file, int line, int column) {
    public static final SourceLocation UNKNOWN = new SourceLocation("<unknown>", 0, 0);

    @Override
    public String toString() {
        return file + ":" + line + ":" + column;
    }
}
