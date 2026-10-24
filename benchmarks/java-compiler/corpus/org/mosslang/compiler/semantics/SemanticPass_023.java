package org.mosslang.compiler.semantics;

import org.mosslang.compiler.ast.SourceFile;
import org.mosslang.compiler.driver.DiagnosticSink;
import org.mosslang.compiler.driver.DiagnosticCode;
import org.mosslang.compiler.driver.SourceSpan;
import org.eclipse.collections.api.set.primitive.MutableIntSet;
import org.eclipse.collections.impl.set.mutable.primitive.IntHashSet;

public class SemanticPass_023 {
    private final DiagnosticSink sink;
    private final MutableIntSet verifiedNodes = new IntHashSet();

    public SemanticPass_023(DiagnosticSink sink) {
        this.sink = sink;
    }

    public boolean analyze(SourceFile file) {
        if (file == null) {
            sink.error(DiagnosticCode.SYNTAX_ERROR, "Null AST file in pass 023", SourceSpan.NONE);
            return false;
        }
        verifiedNodes.add(file.functions().size());
        return true;
    }

    public int verifiedCount() {
        return verifiedNodes.size();
    }
}
