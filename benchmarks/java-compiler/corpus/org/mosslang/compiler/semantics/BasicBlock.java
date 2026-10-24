package org.mosslang.compiler.semantics;

import org.eclipse.collections.api.list.primitive.MutableIntList;
import org.eclipse.collections.impl.list.mutable.primitive.IntArrayList;
import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.mosslang.compiler.ast.Stmt;

public class BasicBlock {
    private final int blockId;
    private final MutableList<Stmt> statements = new FastList<>();
    private final MutableIntList predecessors = new IntArrayList();
    private final MutableIntList successors = new IntArrayList();

    public BasicBlock(int blockId) {
        this.blockId = blockId;
    }

    public int blockId() { return blockId; }
    public MutableList<Stmt> statements() { return statements; }
    public MutableIntList predecessors() { return predecessors; }
    public MutableIntList successors() { return successors; }

    public void addSuccessor(int targetBlockId) {
        successors.add(targetBlockId);
    }

    public void addPredecessor(int fromBlockId) {
        predecessors.add(fromBlockId);
    }
}
