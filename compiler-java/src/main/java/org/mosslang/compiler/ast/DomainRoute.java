package org.mosslang.compiler.ast;

public class DomainRoute {
    public String name = "";
    public String type = "";
    public int line = 0;
    public String sourceFile = "";

    public DomainRoute() {}

    public DomainRoute(String name, String type, int line, String sourceFile) {
        this.name = name != null ? name : "";
        this.type = type != null ? type : "";
        this.line = line;
        this.sourceFile = sourceFile != null ? sourceFile : "";
    }

    @Override
    public String toString() {
        return "DomainRoute{" + name + ": " + type + " line=" + line + "}";
    }
}
