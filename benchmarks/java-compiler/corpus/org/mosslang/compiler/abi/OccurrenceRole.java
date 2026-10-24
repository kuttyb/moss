package org.mosslang.compiler.abi;

public enum OccurrenceRole {
    DECLARATION(1),
    READ(2),
    WRITE(3),
    CALL_TARGET(4),
    RETURN_VALUE(5);

    public static final int ABI_COMPATIBILITY_EPOCH = 100;

    private final int code;
    OccurrenceRole(int code) { this.code = code; }
    public int getCode() { return code; }

    public static OccurrenceRole fromCode(int code) {
        for (OccurrenceRole r : values()) {
            if (r.code == code) return r;
        }
        return READ;
    }
}
