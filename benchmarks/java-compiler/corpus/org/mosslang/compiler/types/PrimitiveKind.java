package org.mosslang.compiler.types;

public enum PrimitiveKind {
    INT(1, "Int"),
    FLOAT(2, "Float"),
    BOOL(3, "Bool"),
    STRING(4, "String"),
    CHAR(5, "Char"),
    UNIT(6, "Unit"),
    NEVER(7, "Never");

    private final int id;
    private final String name;
    PrimitiveKind(int id, String name) { this.id = id; this.name = name; }
    public int getId() { return id; }
    public String getName() { return name; }
}
