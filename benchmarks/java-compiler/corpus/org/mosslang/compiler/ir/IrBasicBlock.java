package org.mosslang.compiler.ir;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class IrBasicBlock {
    private final int blockId;
    private final MutableList<IrInstruction> instructions = new FastList<>();

    public IrBasicBlock(int blockId) {
        this.blockId = blockId;
    }

    public int blockId() { return blockId; }
    public MutableList<IrInstruction> instructions() { return instructions; }
    public void emit(IrInstruction inst) { instructions.add(inst); }
}
