package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class TraitMethod {
    public String name = "";
    public MutableList<Param> params = new FastList<>();
    public String returnType = null;
    public int line = 0;
    public String sourceFile = "";

    public TraitMethod() {}

    public TraitMethod(String name, int line, String sourceFile) {
        this.name = name != null ? name : "";
        this.line = line;
        this.sourceFile = sourceFile != null ? sourceFile : "";
    }

    public void setReturnType(String returnType) {
        this.returnType = returnType;
    }

    @Override
    public String toString() {
        return "TraitMethod{" + name + " line=" + line + "}";
    }
}
