package org.mosslang.compiler.optimizer;

import java.util.ArrayList;
import java.util.List;

public class FunctionalPipeline {
    public int transientId = 0;
    public String semanticIdentity = "";
    public String sourceFile = "";
    public int line = 0;
    public String context = "";
    public String expression = "";
    public String sourceExpression = "";
    public String sourceType = "";
    public String outputType = "";
    public List<FunctionalNode> nodes = new ArrayList<>();
    public List<FunctionalSemanticStep> semanticSteps = new ArrayList<>();
    public List<FunctionalSemanticRewrite> semanticRewrites = new ArrayList<>();
    public boolean fusionEligible = true;
    public boolean elementIndependent = false;
    public boolean deterministic = false;
    public boolean reductionCompatible = false;
    public boolean fused = false;
    public boolean countUsesExactLength = false;
    public boolean countUsesKnownSize = false;
    public int knownSourceSize = 0;
    public boolean shortCircuitTerminal = false;
    public int virtualUpstreamPipelineId = 0;
    public int virtualizedIntoPipelineId = 0;
    public int traversalGroupId = 0;
    public String bindingName = "";
    public boolean bindingImmutable = false;
    public int bindingUseCount = 0;
    public String bindingMaterializationReason = "";
    public List<String> loweredProvenance = new ArrayList<>();
    public List<String> optimizationNotes = new ArrayList<>();
    public String decision = "";

    public FunctionalPipeline() {}

    @Override
    public String toString() {
        return "FunctionalPipeline{nodes=" + nodes.size() + ", steps=" + semanticSteps.size() +
                ", fused=" + fused + ", decision='" + decision + "'}";
    }
}
