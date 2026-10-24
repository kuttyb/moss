package org.mosslang.compiler.ast;

public class Field {
    public String name = "";
    public String type = "";
    public String init = "";
    public String header = "";
    public int line = 0;
    public String sourceFile = "";
    public boolean inferred = false;
    public boolean exported = false;

    public Field() {}

    public Field(String name, String type, String init, String header, int line, String sourceFile, boolean inferred, boolean exported) {
        this.name = name != null ? name : "";
        this.type = type != null ? type : "";
        this.init = init != null ? init : "";
        this.header = header != null ? header : "";
        this.line = line;
        this.sourceFile = sourceFile != null ? sourceFile : "";
        this.inferred = inferred;
        this.exported = exported;
    }

    @Override
    public String toString() {
        return "Field{name='" + name + "', type='" + type + "', line=" + line + "}";
    }
}
