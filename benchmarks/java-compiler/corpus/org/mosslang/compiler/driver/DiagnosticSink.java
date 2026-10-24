package org.mosslang.compiler.driver;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class DiagnosticSink {
    private final MutableList<Diagnostic> diagnostics = new FastList<>();
    private int errorCount = 0;
    private int warningCount = 0;

    public synchronized void report(Diagnostic diagnostic) {
        diagnostics.add(diagnostic);
        if (diagnostic.severity() == Severity.ERROR) {
            errorCount++;
        } else if (diagnostic.severity() == Severity.WARNING) {
            warningCount++;
        }
    }

    public void error(DiagnosticCode code, String message, SourceSpan span) {
        report(new Diagnostic(code, Severity.ERROR, message, span));
    }

    public void warn(DiagnosticCode code, String message, SourceSpan span) {
        report(new Diagnostic(code, Severity.WARNING, message, span));
    }

    public boolean hasErrors() {
        return errorCount > 0;
    }

    public int getErrorCount() {
        return errorCount;
    }

    public int getWarningCount() {
        return warningCount;
    }

    public MutableList<Diagnostic> getDiagnostics() {
        return diagnostics.asUnmodifiable();
    }

    public void clear() {
        diagnostics.clear();
        errorCount = 0;
        warningCount = 0;
    }
}
