package org.mosslang.compiler.ir;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class IrModule {
    private final String moduleName;
    private final MutableList<IrFunction> functions = new FastList<>();

    public IrModule(String moduleName) {
        this.moduleName = moduleName;
    }

    public String moduleName() { return moduleName; }
    public MutableList<IrFunction> functions() { return functions; }
    public void addFunction(IrFunction func) { functions.add(func); }
}
