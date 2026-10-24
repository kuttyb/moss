package org.mosslang.compiler.driver;

public enum DiagnosticCode {
    UNDECLARED_SYMBOL("M1001"),
    DUPLICATE_DECLARATION("M1002"),
    TYPE_MISMATCH("M1003"),
    UNRESOLVED_CALL("M1004"),
    USE_AFTER_CONSUME("M1005"),
    ILLEGAL_MUTATION("M1006"),
    UNREACHABLE_CODE("M1007"),
    MISSING_RETURN("M1008"),
    OCCURRENCE_MISMATCH("M1009"),
    ABI_INTEGRITY_FAILURE("M1010"),
    CONSTRAINT_UNSATISFIABLE("M1011"),
    SYNTAX_ERROR("M1012");

    private final String code;
    DiagnosticCode(String code) { this.code = code; }
    public String getCode() { return code; }
}
