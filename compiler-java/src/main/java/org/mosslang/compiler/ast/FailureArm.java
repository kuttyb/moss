package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import java.util.LinkedHashMap;
import java.util.Map;

public class FailureArm {
    public int line = 0;
    public String sourceFile = "";
    public MutableList<String> patterns = new FastList<>();
    public String binding = "";
    public MutableList<Stmt> body = new FastList<>();
    /** Checker-proved handler-local captures available on selected explicit raise edges. */
    public final Map<String, String> checkedCaptures = new LinkedHashMap<>();

    public FailureArm() {}

    public FailureArm(int line, String sourceFile) {
        this.line = line;
        this.sourceFile = sourceFile != null ? sourceFile : "";
    }

    @Override
    public String toString() {
        return "FailureArm{line=" + line + ", patterns=" + patterns + "}";
    }
}
