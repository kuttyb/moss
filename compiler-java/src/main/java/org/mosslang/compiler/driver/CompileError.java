package org.mosslang.compiler.driver;

public class CompileError extends org.mosslang.compiler.diagnostics.CompileError {
    public CompileError(int line, String message) {
        super(line, message);
    }

    public CompileError(int line, String message, String code) {
        super(line, message, code);
    }

    public CompileError(int line, String message, String code, String sourceFile) {
        super(line, message, code, sourceFile);
    }
}
