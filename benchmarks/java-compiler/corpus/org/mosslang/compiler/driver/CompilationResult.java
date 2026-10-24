package org.mosslang.compiler.driver;

import org.eclipse.collections.api.list.MutableList;

public record CompilationResult(
    boolean success,
    int errorCount,
    int warningCount,
    byte[] abiPayload,
    int generatedBytecodeSize,
    MutableList<Diagnostic> diagnostics
) {
    public boolean isSuccess() {
        return success && errorCount == 0;
    }
}
