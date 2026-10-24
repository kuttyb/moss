package org.mosslang.compiler.optimizer;

import java.util.ArrayList;
import java.util.List;

public class FunctionalNode {
    public int transientId = 0;
    public String semanticIdentity = "";
    public FunctionalNodeKind kind = FunctionalNodeKind.Source;
    public int line = 0;
    public int stage = 0;
    public String sourceText = "";
    public String operation = "";
    public String argument = "";
    public String inputType = "";
    public String outputType = "";
    public String callableIdentity = "";
    public String callableExpression = "";
    public List<String> captures = new ArrayList<>();
    public String ownership = "Read";
    public ObservableEffects effects = new ObservableEffects();
    public boolean logicalMaterialization = false;
    public boolean materializationEliminated = false;
    public FunctionalMaterializationKind materialization = FunctionalMaterializationKind.NotApplicable;
    public boolean escapes = false;
    public boolean multipleConsumers = false;
    public boolean barrierRequired = false;
    public boolean deadStageEliminated = false;
    public boolean semanticWorkEliminated = false;
    public String semanticEliminationReason = "";
    public String materializationReason = "";
    public List<String> provenance = new ArrayList<>();

    public FunctionalNode() {}

    public FunctionalNode(FunctionalNodeKind kind, String operation, String argument) {
        this.kind = kind;
        this.operation = operation != null ? operation : "";
        this.argument = argument != null ? argument : "";
        this.callableExpression = this.argument;
    }

    public FunctionalNode(FunctionalNodeKind kind, String argument) {
        this(kind, kind.name().toLowerCase(), argument);
    }

    @Override
    public String toString() {
        return "FunctionalNode{" + kind + ", op='" + operation + "', arg='" + argument + "'}";
    }
}
