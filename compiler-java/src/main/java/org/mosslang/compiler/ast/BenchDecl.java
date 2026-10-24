package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class BenchDecl {
    public String name = "";
    public String header = "";
    public String semanticIdentity = "";
    public MutableList<Stmt> body = new FastList<>();
    public int line = 0;
    public String sourceFile = "";

    public BenchDecl() {}

    public BenchDecl(String name, String header, int line, String sourceFile) {
        this.name = name != null ? name : "";
        this.header = header != null ? header : "";
        this.line = line;
        this.sourceFile = sourceFile != null ? sourceFile : "";
    }

    @Override
    public String toString() {
        return "BenchDecl{" + name + " line=" + line + "}";
    }
}
