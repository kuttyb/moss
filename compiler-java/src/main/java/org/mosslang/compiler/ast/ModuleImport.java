package org.mosslang.compiler.ast;

public class ModuleImport {
    public String name = "";
    public String ownerModule = "";
    public int line = 0;
    public String sourceFile = "";

    public ModuleImport() {}

    public ModuleImport(String name, String ownerModule, int line, String sourceFile) {
        this.name = name != null ? name : "";
        this.ownerModule = ownerModule != null ? ownerModule : "";
        this.line = line;
        this.sourceFile = sourceFile != null ? sourceFile : "";
    }

    @Override
    public String toString() {
        return "ModuleImport{" + name + " line=" + line + "}";
    }
}
