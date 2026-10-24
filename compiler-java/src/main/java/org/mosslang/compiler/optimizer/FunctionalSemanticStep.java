package org.mosslang.compiler.optimizer;

import java.util.ArrayList;
import java.util.List;

public class FunctionalSemanticStep {
    public FunctionalNodeKind kind = FunctionalNodeKind.Source;
    public List<Integer> sourceNodeIndices = new ArrayList<>();
    public List<String> provenance = new ArrayList<>();
    public String composedExpression = "";

    public FunctionalSemanticStep() {}

    public FunctionalSemanticStep(FunctionalNodeKind kind, List<Integer> sourceNodeIndices) {
        this.kind = kind;
        this.sourceNodeIndices = sourceNodeIndices != null ? new ArrayList<>(sourceNodeIndices) : new ArrayList<>();
    }

    @Override
    public String toString() {
        return "FunctionalSemanticStep{" + kind + ", indices=" + sourceNodeIndices +
                (composedExpression.isEmpty() ? "" : ", composed='" + composedExpression + "'") + "}";
    }
}
