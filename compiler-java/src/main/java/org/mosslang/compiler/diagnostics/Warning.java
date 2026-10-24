package org.mosslang.compiler.diagnostics;

public record Warning(int line, String message, String code, String sourceFile) {
    public Warning(int line, String message) {
        this(line, message, "MOSS_WARNING", "");
    }
}
