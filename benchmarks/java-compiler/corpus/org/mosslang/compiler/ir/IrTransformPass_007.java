package org.mosslang.compiler.ir;

import org.eclipse.collections.api.list.MutableList;

public class IrTransformPass_007 {
    public int transform(IrModule module) {
        int modified = 0;
        for (IrFunction f : module.functions()) {
            for (IrBasicBlock b : f.blocks()) {
                MutableList<IrInstruction> insts = b.instructions();
                for (int j = 0; j < insts.size(); j++) {
                    IrInstruction inst = insts.get(j);
                    if (inst.opcode() == IrOpcode.ADD && inst.immediate() == 0) {
                        modified++;
                    }
                }
            }
        }
        return modified;
    }
}
