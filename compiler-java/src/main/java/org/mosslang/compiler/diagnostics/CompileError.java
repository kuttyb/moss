package org.mosslang.compiler.diagnostics;

public class CompileError extends RuntimeException {
    private final int line;
    private String code;
    private String semanticIdentity;
    private String symbol;
    private String sourceFile;
    private SourceSpan span;

    public CompileError(int line, String message) {
        this(line, message, "");
    }

    public CompileError(int line, String message, String code) {
        super(message);
        this.line = line;
        this.code = code != null ? code : "";
        this.semanticIdentity = "";
        this.symbol = "";
        this.sourceFile = "";
    }

    public CompileError(int line, String message, String code, String sourceFile) {
        this(line, message, code);
        this.sourceFile = sourceFile != null ? sourceFile : "";
    }

    public int getLine() {
        return line;
    }

    public String getCode() {
        return code;
    }

    public void setCode(String code) {
        this.code = code != null ? code : "";
    }

    public String getSemanticIdentity() {
        return semanticIdentity;
    }

    public void setSemanticIdentity(String semanticIdentity) {
        this.semanticIdentity = semanticIdentity;
    }

    public String getSymbol() {
        return symbol;
    }

    public void setSymbol(String symbol) {
        this.symbol = symbol;
    }

    public String getSourceFile() {
        return sourceFile;
    }

    public void setSourceFile(String sourceFile) {
        this.sourceFile = sourceFile != null ? sourceFile : "";
    }

    public SourceSpan getSpan() {
        return span;
    }

    public void setSpan(SourceSpan span) {
        this.span = span;
    }

    @Override
    public String toString() {
        String loc = sourceFile.isEmpty() ? ("line " + line) : (sourceFile + ":" + line);
        if (code != null && !code.isEmpty()) {
            return loc + ": [" + code + "] " + getMessage();
        }
        return loc + ": " + getMessage();
    }
}
