package org.mosslang.compiler.ast;

public class Param {
    public String name = "";
    public String type = "";
    public boolean inferred = false;

    public Param() {}

    public Param(String name, String type, boolean inferred) {
        this.name = name != null ? name : "";
        this.type = type != null ? type : "";
        this.inferred = inferred;
    }

    public Param(String name, String type) {
        this(name, type, false);
    }

    public void setType(String type) {
        this.type = type != null ? type : "";
    }

    @Override
    public String toString() {
        return "Param{name='" + name + "', type='" + type + "'}";
    }
}
