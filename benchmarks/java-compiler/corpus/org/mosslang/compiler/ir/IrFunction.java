package org.mosslang.compiler.ir;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class IrFunction {
    private final String name;
    private final MutableList<IrBasicBlock> blocks = new FastList<>();

    public IrFunction(String name) {
        this.name = name;
    }

    public String name() { return name; }
    public MutableList<IrBasicBlock> blocks() { return blocks; }
    public IrBasicBlock createBlock() {
        IrBasicBlock bb = new IrBasicBlock(blocks.size());
        blocks.add(bb);
        return bb;
    }
}
