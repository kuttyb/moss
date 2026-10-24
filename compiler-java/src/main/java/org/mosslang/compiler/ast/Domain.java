package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class Domain {
    public String name = "";
    public String header = "";
    public MutableList<Field> state = new FastList<>();
    public MutableList<DomainRoute> routes = new FastList<>();
    public MutableList<Handler> handlers = new FastList<>();
    public boolean exported = false;
    public int line = 0;
    public String sourceFile = "";
    public String definingModule = "";
    public String declarationName = "";

    public Domain() {}

    public Domain(String name, String header, int line, String sourceFile, boolean exported) {
        this.name = name != null ? name : "";
        this.header = header != null ? header : "";
        this.line = line;
        this.sourceFile = sourceFile != null ? sourceFile : "";
        this.exported = exported;
    }

    @Override
    public String toString() {
        return "Domain{" + name + " line=" + line + "}";
    }
}
