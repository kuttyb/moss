package org.mosslang.compiler.semantics;

import org.mosslang.compiler.ast.Program;
import org.mosslang.compiler.diagnostics.Warning;

import java.util.List;

public class CheckedProgram {
    private final Program program;
    private final SemanticChecker checker;

    public CheckedProgram(Program program) {
        this.program = program;
        this.checker = new SemanticChecker(program);
    }

    public void run() {
        checker.run();
    }

    public List<Warning> warnings() {
        return checker.getWarnings();
    }
}
