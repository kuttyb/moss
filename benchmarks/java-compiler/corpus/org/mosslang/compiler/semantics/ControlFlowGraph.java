package org.mosslang.compiler.semantics;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.eclipse.collections.api.set.primitive.MutableIntSet;
import org.eclipse.collections.impl.set.mutable.primitive.IntHashSet;

public class ControlFlowGraph {
    private final MutableList<BasicBlock> blocks = new FastList<>();
    private int entryBlockId;
    private int exitBlockId;

    public BasicBlock createBlock() {
        int id = blocks.size();
        BasicBlock bb = new BasicBlock(id);
        blocks.add(bb);
        return bb;
    }

    public void setEntry(int id) { this.entryBlockId = id; }
    public void setExit(int id) { this.exitBlockId = id; }
    public int getEntry() { return entryBlockId; }
    public int getExit() { return exitBlockId; }
    public MutableList<BasicBlock> getBlocks() { return blocks; }

    public MutableIntSet computeReachable() {
        MutableIntSet visited = new IntHashSet();
        dfs(entryBlockId, visited);
        return visited;
    }

    private void dfs(int blockId, MutableIntSet visited) {
        if (!visited.add(blockId)) return;
        BasicBlock bb = blocks.get(blockId);
        for (int i = 0; i < bb.successors().size(); i++) {
            dfs(bb.successors().get(i), visited);
        }
    }
}
