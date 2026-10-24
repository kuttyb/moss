package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class EnumCase {
    public String name = "";
    public MutableList<Field> fields = new FastList<>();
    public int line = 0;
    public String sourceFile = "";

    public EnumCase() {}

    public EnumCase(String name, int line, String sourceFile) {
        this.name = name != null ? name : "";
        this.line = line;
        this.sourceFile = sourceFile != null ? sourceFile : "";
    }

    @Override
    public String toString() {
        return "EnumCase{" + name + " line=" + line + "}";
    }
}
