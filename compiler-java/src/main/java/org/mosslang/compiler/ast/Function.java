package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.eclipse.collections.impl.list.mutable.primitive.IntArrayList;

public class Function {
    public String name = "";
    public String header = "";
    public MutableList<Param> params = new FastList<>();
    public String returnType = null;
    public MutableList<Stmt> body = new FastList<>();
    public String definingModule = "";
    public String declarationName = "";
    public String semanticCallableId = "";
    public String resultExpression = null;
    public int resultLine = 0;
    public boolean expressionBody = false;
    public IntArrayList resultContinuationLines = new IntArrayList();
    public boolean generic = false;
    public boolean staticDispatch = false;
    public int line = 0;
    public boolean exported = false;
    public String sourceFile = "";

    public Function() {}

    public Function(String name, String header, int line, String sourceFile, boolean exported) {
        this.name = name != null ? name : "";
        this.header = header != null ? header : "";
        this.line = line;
        this.sourceFile = sourceFile != null ? sourceFile : "";
        this.exported = exported;
    }

    public void setReturnType(String returnType) {
        this.returnType = returnType;
    }

    @Override
    public String toString() {
        return "Function{" + name + " line=" + line + "}";
    }
}
