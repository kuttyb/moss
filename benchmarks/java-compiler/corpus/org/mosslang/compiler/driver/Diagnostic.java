package org.mosslang.compiler.driver;

public record Diagnostic(DiagnosticCode code, Severity severity, String message, SourceSpan span) {
    public String format() {
        return String.format("[%s] %s at %s: %s", severity, code.getCode(), span.start(), message);
    }
}
