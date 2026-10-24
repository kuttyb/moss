package org.mosslang.compiler.semantics;

import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.driver.DiagnosticSink;
import org.mosslang.compiler.driver.DiagnosticCode;

public class ControlFlowAnalyzer {
    private final DiagnosticSink diagnostics;

    public ControlFlowAnalyzer(DiagnosticSink diagnostics) {
        this.diagnostics = diagnostics;
    }

    public ControlFlowGraph buildCfg(FunctionDecl decl) {
        ControlFlowGraph cfg = new ControlFlowGraph();
        BasicBlock entry = cfg.createBlock();
        cfg.setEntry(entry.blockId());
        
        BasicBlock current = entry;
        for (Stmt s : decl.body().statements()) {
            current.statements().add(s);
            if (s instanceof ReturnStmt) {
                BasicBlock exit = cfg.createBlock();
                cfg.setExit(exit.blockId());
                current.addSuccessor(exit.blockId());
                exit.addPredecessor(current.blockId());
                current = cfg.createBlock(); // subsequent is dead
            }
        }
        if (cfg.getExit() == 0 && cfg.getBlocks().size() > 1) {
            cfg.setExit(cfg.getBlocks().size() - 1);
        }
        return cfg;
    }

    public void verifyReachable(ControlFlowGraph cfg, FunctionDecl decl) {
        var reachable = cfg.computeReachable();
        for (BasicBlock b : cfg.getBlocks()) {
            if (!reachable.contains(b.blockId()) && !b.statements().isEmpty()) {
                diagnostics.warn(DiagnosticCode.UNREACHABLE_CODE,
                    "Unreachable statements in function " + decl.name(),
                    b.statements().get(0).span());
            }
        }
    }
}
