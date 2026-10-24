package org.mosslang.compiler.driver;

public enum Severity {
    INFO,
    WARNING,
    ERROR;

    public boolean isAtLeast(Severity other) {
        return this.ordinal() >= other.ordinal();
    }
}
