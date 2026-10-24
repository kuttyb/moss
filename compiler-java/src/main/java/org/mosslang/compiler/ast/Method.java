package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.eclipse.collections.impl.list.mutable.primitive.IntArrayList;

public class Method {
    public String owner = "";
    public String name = "";
    public MutableList<Param> params = new FastList<>();
    public String returnType = null;
    public MutableList<Stmt> body = new FastList<>();
    public String resultExpression = null;
    public int resultLine = 0;
    public IntArrayList resultContinuationLines = new IntArrayList();
    public int line = 0;
    public String sourceFile = "";

    public Method() {}

    public Method(String owner, String name, int line, String sourceFile) {
        this.owner = owner != null ? owner : "";
        this.name = name != null ? name : "";
        this.line = line;
        this.sourceFile = sourceFile != null ? sourceFile : "";
    }

    public void setReturnType(String returnType) {
        this.returnType = returnType;
    }

    @Override
    public String toString() {
        return "Method{" + owner + "." + name + " line=" + line + "}";
    }
}
