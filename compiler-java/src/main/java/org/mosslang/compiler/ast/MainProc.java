package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class MainProc {
    public MutableList<Stmt> body = new FastList<>();
    public int line = 0;
    public String header = "";
    public String sourceFile = "";

    public MainProc() {}

    public MainProc(MutableList<Stmt> body, int line, String header, String sourceFile) {
        this.body = body != null ? body : new FastList<>();
        this.line = line;
        this.header = header != null ? header : "";
        this.sourceFile = sourceFile != null ? sourceFile : "";
    }

    @Override
    public String toString() {
        return "MainProc{line=" + line + "}";
    }
}
