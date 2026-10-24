package org.mosslang.compiler.optimizer;

import java.util.*;
import java.util.function.Predicate;

public class FunctionalOptimizer {
    private final List<FunctionalPipeline> pipelines;

    public FunctionalOptimizer() {
        this.pipelines = new ArrayList<>();
    }

    public FunctionalOptimizer(List<FunctionalPipeline> pipelines) {
        this.pipelines = pipelines != null ? pipelines : new ArrayList<>();
    }

    public void addPipeline(FunctionalPipeline pipeline) {
        if (pipeline != null) {
            this.pipelines.add(pipeline);
        }
    }

    public List<FunctionalPipeline> getPipelines() {
        return pipelines;
    }

    public void run(boolean enabled) {
        for (FunctionalPipeline pipeline : pipelines) {
            optimizePipeline(pipeline, enabled);
        }
    }

    public static void optimizePipeline(FunctionalPipeline pipeline, boolean enabled) {
        pipeline.countUsesExactLength = false;
        pipeline.countUsesKnownSize = false;
        pipeline.knownSourceSize = 0;
        pipeline.shortCircuitTerminal = false;
        pipeline.virtualUpstreamPipelineId = 0;
        pipeline.virtualizedIntoPipelineId = 0;
        pipeline.traversalGroupId = 0;
        pipeline.bindingName = "";
        pipeline.bindingImmutable = false;
        pipeline.bindingUseCount = 0;
        pipeline.bindingMaterializationReason = "";
        pipeline.optimizationNotes.clear();
        pipeline.semanticRewrites.clear();
        pipeline.fused = enabled && pipeline.fusionEligible;
        pipeline.loweredProvenance.clear();

        for (FunctionalNode node : pipeline.nodes) {
            node.materializationEliminated = false;
            node.materialization = FunctionalMaterializationKind.NotApplicable;
            node.escapes = false;
            node.multipleConsumers = false;
            node.barrierRequired = false;
            node.deadStageEliminated = false;
            node.semanticWorkEliminated = false;
            node.semanticEliminationReason = "";
            node.materializationReason = "";
        }

        initializeSemanticPlan(pipeline);

        if (pipeline.fused) {
            pipeline.decision = "fused stages 1-" + (pipeline.nodes.size() - 1) + "; intermediates eliminated";
        } else if (!enabled && pipeline.fusionEligible) {
            pipeline.decision = "eager reference lowering (-O0); logical intermediates retained";
        }

        if (enabled) {
            rewriteIdentityMapFilter(pipeline);
            composeAdjacentSteps(pipeline, FunctionalNodeKind.Map, "compose-map");
            composeAdjacentSteps(pipeline, FunctionalNodeKind.Filter, "compose-filter");
        }

        planTerminalLowering(pipeline, enabled);

        if (enabled) {
            planTerminalAwareSemantics(pipeline);
        }
    }

    public static void initializeSemanticPlan(FunctionalPipeline pipeline) {
        pipeline.semanticSteps.clear();
        for (int nodeIndex = 1; nodeIndex < pipeline.nodes.size(); ++nodeIndex) {
            FunctionalSemanticStep step = new FunctionalSemanticStep();
            step.kind = pipeline.nodes.get(nodeIndex).kind;
            step.sourceNodeIndices.add(nodeIndex);
            step.provenance.addAll(pipeline.nodes.get(nodeIndex).provenance);
            pipeline.semanticSteps.add(step);
        }
    }

    public static void recordSemanticRewrite(FunctionalPipeline pipeline, String name, String detail, List<String> provenance) {
        pipeline.semanticRewrites.add(new FunctionalSemanticRewrite(name, detail, provenance));
    }

    private static boolean stepNodesSatisfy(FunctionalPipeline pipeline, FunctionalSemanticStep step, Predicate<FunctionalNode> predicate) {
        if (step.sourceNodeIndices.isEmpty()) return false;
        for (int nodeIndex : step.sourceNodeIndices) {
            if (nodeIndex == 0 || nodeIndex >= pipeline.nodes.size() || !predicate.test(pipeline.nodes.get(nodeIndex))) {
                return false;
            }
        }
        return true;
    }

    private static boolean identityMapStep(FunctionalPipeline pipeline, FunctionalSemanticStep step) {
        if (step.kind != FunctionalNodeKind.Map || step.sourceNodeIndices.size() != 1) {
            return false;
        }
        int nodeIndex = step.sourceNodeIndices.get(0);
        if (nodeIndex == 0 || nodeIndex >= pipeline.nodes.size()) return false;
        FunctionalNode node = pipeline.nodes.get(nodeIndex);
        String expr = node.callableExpression != null ? node.callableExpression.trim() : "";
        if (expr.isEmpty() && node.argument != null) expr = node.argument.trim();
        return "_".equals(expr) &&
                (node.inputType.isEmpty() || node.inputType.equals(node.outputType)) &&
                node.effects.isFusionSafe();
    }

    public static void rewriteIdentityMapFilter(FunctionalPipeline pipeline) {
        for (int index = 0; index + 1 < pipeline.semanticSteps.size(); ) {
            FunctionalSemanticStep mapStep = pipeline.semanticSteps.get(index);
            FunctionalSemanticStep filterStep = pipeline.semanticSteps.get(index + 1);
            if (mapStep.kind != FunctionalNodeKind.Map || filterStep.kind != FunctionalNodeKind.Filter) {
                ++index;
                continue;
            }

            String disabledReason = null;
            if (!pipeline.fusionEligible) {
                disabledReason = "pipeline has " + barrierReason(pipeline);
            } else if (!identityMapStep(pipeline, mapStep)) {
                disabledReason = "map is not a proven identity";
            } else if (!stepNodesSatisfy(pipeline, filterStep, node -> node.effects.isFusionSafe())) {
                disabledReason = "predicate has an observable or ownership barrier";
            }

            if (disabledReason != null) {
                pipeline.optimizationNotes.add("semantic-opt: predicate-pushdown disabled; reason: " + disabledReason);
                ++index;
                continue;
            }

            List<String> provenance = new ArrayList<>(mapStep.provenance);
            provenance.addAll(filterStep.provenance);
            for (int nodeIndex : mapStep.sourceNodeIndices) {
                FunctionalNode node = pipeline.nodes.get(nodeIndex);
                node.semanticWorkEliminated = true;
                node.semanticEliminationReason = "predicate moved through a proven identity map";
            }
            recordSemanticRewrite(pipeline, "predicate-pushdown",
                    "filter moved to the earlier value through map(_)", provenance);
            pipeline.optimizationNotes.add("semantic-opt: predicate-pushdown through proven identity map");
            pipeline.semanticSteps.remove(index);
        }
    }

    private static boolean isNamedCallable(String s) {
        if (s == null || s.isEmpty() || "_".equals(s)) return false;
        if (!Character.isLetter(s.charAt(0)) && s.charAt(0) != '_') return false;
        for (int i = 1; i < s.length(); i++) {
            char c = s.charAt(i);
            if (!Character.isLetterOrDigit(c) && c != '_') return false;
        }
        return true;
    }

    private static boolean hasPlaceholder(String expr) {
        if ("_".equals(expr)) return true;
        for (int i = 0; i < expr.length(); i++) {
            if (expr.charAt(i) == '_') {
                boolean left = (i == 0) || (!Character.isLetterOrDigit(expr.charAt(i - 1)) && expr.charAt(i - 1) != '_');
                boolean right = (i == expr.length() - 1) || (!Character.isLetterOrDigit(expr.charAt(i + 1)) && expr.charAt(i + 1) != '_');
                if (left && right) return true;
            }
        }
        return false;
    }

    public static String composeMapExpressions(List<String> expressions) {
        if (expressions == null || expressions.isEmpty()) return "";
        String current = expressions.get(0).trim();
        if (isNamedCallable(current)) {
            current = current + "(x)";
        }
        for (int i = 1; i < expressions.size(); i++) {
            String next = expressions.get(i).trim();
            if (isNamedCallable(next)) {
                current = next + "(" + current + ")";
            } else if (hasPlaceholder(next)) {
                current = next.replaceAll("\\b_\\b", "(" + current + ")");
            } else {
                current = next + "(" + current + ")";
            }
        }
        return current;
    }

    public static String composeFilterConditions(List<String> expressions) {
        if (expressions == null || expressions.isEmpty()) return "";
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < expressions.size(); i++) {
            if (i > 0) sb.append(" and ");
            sb.append(expressions.get(i).trim());
        }
        return sb.toString();
    }

    public static void composeAdjacentSteps(FunctionalPipeline pipeline, FunctionalNodeKind kind, String label) {
        if (!pipeline.fusionEligible) {
            for (int index = 0; index + 1 < pipeline.semanticSteps.size(); ++index) {
                if (pipeline.semanticSteps.get(index).kind == kind &&
                        pipeline.semanticSteps.get(index + 1).kind == kind) {
                    pipeline.optimizationNotes.add(
                            "semantic-opt: " + label + " disabled; reason: " + barrierReason(pipeline));
                    break;
                }
            }
            return;
        }

        for (int index = 0; index < pipeline.semanticSteps.size(); ) {
            if (pipeline.semanticSteps.get(index).kind != kind) {
                ++index;
                continue;
            }
            if (!stepNodesSatisfy(pipeline, pipeline.semanticSteps.get(index), node -> node.effects.isFusionSafe())) {
                ++index;
                continue;
            }
            int end = index + 1;
            while (end < pipeline.semanticSteps.size() &&
                    pipeline.semanticSteps.get(end).kind == kind &&
                    stepNodesSatisfy(pipeline, pipeline.semanticSteps.get(end), node -> node.effects.isFusionSafe())) {
                ++end;
            }
            if (end == index + 1) {
                ++index;
                continue;
            }

            FunctionalSemanticStep composed = pipeline.semanticSteps.get(index);
            List<String> nodeExprs = new ArrayList<>();
            for (int nodeIdx : composed.sourceNodeIndices) {
                String arg = pipeline.nodes.get(nodeIdx).argument;
                if (arg.isEmpty()) arg = pipeline.nodes.get(nodeIdx).operation;
                nodeExprs.add(arg);
            }

            for (int part = index + 1; part < end; ++part) {
                FunctionalSemanticStep step = pipeline.semanticSteps.get(part);
                composed.sourceNodeIndices.addAll(step.sourceNodeIndices);
                composed.provenance.addAll(step.provenance);
                for (int nodeIdx : step.sourceNodeIndices) {
                    String arg = pipeline.nodes.get(nodeIdx).argument;
                    if (arg.isEmpty()) arg = pipeline.nodes.get(nodeIdx).operation;
                    nodeExprs.add(arg);
                }
            }

            if (kind == FunctionalNodeKind.Map) {
                composed.composedExpression = composeMapExpressions(nodeExprs);
            } else if (kind == FunctionalNodeKind.Filter) {
                composed.composedExpression = composeFilterConditions(nodeExprs);
            }

            int count = end - index;
            String typeName = kind == FunctionalNodeKind.Map ? "maps" : "filters";
            recordSemanticRewrite(pipeline, label,
                    "composed " + count + " adjacent " + typeName +
                            (composed.composedExpression.isEmpty() ? "" : ": " + composed.composedExpression),
                    composed.provenance);
            pipeline.optimizationNotes.add("semantic-opt: " + label + " (" + count + " stages)");

            for (int k = end - 1; k >= index; --k) {
                pipeline.semanticSteps.remove(k);
            }
            pipeline.semanticSteps.add(index, composed);
            ++index;
        }
    }

    public static void planTerminalLowering(FunctionalPipeline pipeline, boolean enabled) {
        boolean countTerminal = !pipeline.semanticSteps.isEmpty() &&
                pipeline.semanticSteps.get(pipeline.semanticSteps.size() - 1).kind == FunctionalNodeKind.Count;
        boolean mappedCount = countTerminal && hasMapStage(pipeline);

        if (enabled && countTerminal) {
            if (pipeline.fusionEligible) {
                boolean eliminated = eliminateTrailingMapsBeforeCount(pipeline);
                boolean blockedTrailingMap = pipeline.semanticSteps.size() >= 2 &&
                        pipeline.semanticSteps.get(pipeline.semanticSteps.size() - 2).kind == FunctionalNodeKind.Map;
                if (blockedTrailingMap) {
                    FunctionalSemanticStep blocked = pipeline.semanticSteps.get(pipeline.semanticSteps.size() - 2);
                    disableCountElimination(pipeline, semanticStepSkipBarrier(pipeline, blocked), mappedCount);
                } else if (pipeline.semanticSteps.size() == 1) {
                    pipeline.countUsesExactLength = true;
                    List<String> provenance = new ArrayList<>();
                    for (FunctionalNode node : pipeline.nodes) {
                        provenance.addAll(node.provenance);
                    }
                    recordSemanticRewrite(pipeline, "terminal-count",
                            "replaced traversal with exact source cardinality", provenance);
                    pipeline.optimizationNotes.add("count -> len; reason: exact source cardinality known");

                    Integer size = inertLiteralVectorSize(pipeline.sourceExpression);
                    if (size != null) {
                        pipeline.countUsesKnownSize = true;
                        pipeline.knownSourceSize = size;
                        recordSemanticRewrite(pipeline, "known-size-count",
                                "inert vector literal has " + size + " elements",
                                pipeline.nodes.isEmpty() ? Collections.emptyList() : pipeline.nodes.get(0).provenance);
                        pipeline.optimizationNotes.add("count -> constant " + size + "; reason: inert literal cardinality known");
                    }
                } else if (eliminated) {
                    pipeline.optimizationNotes.add("terminal count retains only cardinality-changing stages");
                }
            } else if (mappedCount) {
                disableCountElimination(pipeline, countEliminationBarrierReason(pipeline), true);
            }
        }

        if (pipeline.semanticSteps.isEmpty()) return;
        FunctionalNodeKind terminal = pipeline.semanticSteps.get(pipeline.semanticSteps.size() - 1).kind;
        if (terminal != FunctionalNodeKind.Any && terminal != FunctionalNodeKind.All) {
            return;
        }
        String name = terminal == FunctionalNodeKind.Any ? "any" : "all";
        if (enabled && pipeline.fusionEligible && callbacksSafeToSkip(pipeline)) {
            pipeline.shortCircuitTerminal = true;
            recordSemanticRewrite(pipeline, "short-circuit-" + name,
                    "later callback invocations are safe to skip",
                    pipeline.semanticSteps.get(pipeline.semanticSteps.size() - 1).provenance);
            pipeline.optimizationNotes.add("short-circuit " + name + " enabled");
        } else {
            disableShortCircuit(pipeline, enabled
                    ? skippedCallbackBarrierReason(pipeline)
                    : "eager -O0 reference traversal");
        }
    }

    public static void planTerminalAwareSemantics(FunctionalPipeline pipeline) {
        if (!pipeline.fused || pipeline.semanticSteps.isEmpty()) return;
        FunctionalNodeKind terminal = pipeline.semanticSteps.get(pipeline.semanticSteps.size() - 1).kind;
        if (terminal == FunctionalNodeKind.Count && !pipeline.countUsesExactLength) {
            boolean hasFilter = false;
            for (FunctionalSemanticStep step : pipeline.semanticSteps) {
                if (step.kind == FunctionalNodeKind.Filter) {
                    hasFilter = true;
                    break;
                }
            }
            if (hasFilter) {
                recordSemanticRewrite(pipeline, "terminal-filter-count",
                        "count matching elements without materializing filter output",
                        pipeline.semanticSteps.get(pipeline.semanticSteps.size() - 1).provenance);
            }
        } else if (terminal == FunctionalNodeKind.Sum || terminal == FunctionalNodeKind.Reduce) {
            recordSemanticRewrite(pipeline,
                    terminal == FunctionalNodeKind.Sum ? "terminal-sum" : "terminal-reduce",
                    "terminal accumulator consumes upstream values without an intermediate collection",
                    pipeline.semanticSteps.get(pipeline.semanticSteps.size() - 1).provenance);
        }
    }

    private static boolean hasMapStage(FunctionalPipeline pipeline) {
        for (FunctionalNode node : pipeline.nodes) {
            if (node.kind == FunctionalNodeKind.Map) return true;
        }
        return false;
    }

    private static boolean callbacksSafeToSkip(FunctionalPipeline pipeline) {
        if (pipeline.nodes.size() < 2) return true;
        for (int i = 1; i < pipeline.nodes.size(); i++) {
            if (!pipeline.nodes.get(i).effects.isSafeToSkip()) return false;
        }
        return true;
    }

    private static boolean eliminateTrailingMapsBeforeCount(FunctionalPipeline pipeline) {
        if (pipeline.semanticSteps.isEmpty() ||
                pipeline.semanticSteps.get(pipeline.semanticSteps.size() - 1).kind != FunctionalNodeKind.Count) {
            return false;
        }
        boolean eliminated = false;
        while (pipeline.semanticSteps.size() >= 2) {
            int mapIndex = pipeline.semanticSteps.size() - 2;
            FunctionalSemanticStep step = pipeline.semanticSteps.get(mapIndex);
            if (step.kind != FunctionalNodeKind.Map) break;
            if (!stepNodesSatisfy(pipeline, step, node -> node.effects.isSafeToSkip())) break;
            eliminateSemanticStep(pipeline, mapIndex,
                    "mapped values are unused by terminal count and the callback is pure/non-failing/non-divergent",
                    "dead-map");
            eliminated = true;
        }
        if (eliminated) {
            pipeline.optimizationNotes.add("map stage eliminated; reason: output unused and callback is pure/non-failing/non-divergent");
        }
        return eliminated;
    }

    private static void eliminateSemanticStep(FunctionalPipeline pipeline, int stepIndex, String reason, String rewriteName) {
        FunctionalSemanticStep step = pipeline.semanticSteps.get(stepIndex);
        for (int nodeIndex : step.sourceNodeIndices) {
            FunctionalNode node = pipeline.nodes.get(nodeIndex);
            node.deadStageEliminated = true;
            node.semanticWorkEliminated = true;
            node.semanticEliminationReason = reason;
        }
        recordSemanticRewrite(pipeline, rewriteName, reason, step.provenance);
        pipeline.semanticSteps.remove(stepIndex);
    }

    private static void disableCountElimination(FunctionalPipeline pipeline, String reason, boolean mappedWork) {
        pipeline.countUsesExactLength = false;
        pipeline.countUsesKnownSize = false;
        pipeline.knownSourceSize = 0;
        eraseNotesWithPrefix(pipeline, "count -> len");
        eraseNotesWithPrefix(pipeline, "count -> constant");
        eraseNotesWithPrefix(pipeline, "map elimination:");
        pipeline.semanticRewrites.removeIf(r -> "terminal-count".equals(r.name) || "known-size-count".equals(r.name));
        pipeline.optimizationNotes.add("count -> len disabled; reason: " + reason);
        boolean mapWasEliminated = false;
        for (FunctionalNode node : pipeline.nodes) {
            if (node.kind == FunctionalNodeKind.Map && node.deadStageEliminated) {
                mapWasEliminated = true;
                break;
            }
        }
        if (mappedWork && !mapWasEliminated) {
            pipeline.optimizationNotes.add("map elimination: disabled; reason: " + reason);
        }
    }

    private static void disableShortCircuit(FunctionalPipeline pipeline, String reason) {
        pipeline.shortCircuitTerminal = false;
        eraseNotesWithPrefix(pipeline, "short-circuit ");
        pipeline.optimizationNotes.add("short-circuit " + terminalName(pipeline) + " disabled; reason: " + reason);
    }

    private static String terminalName(FunctionalPipeline pipeline) {
        if (pipeline.nodes.isEmpty()) return "terminal";
        return pipeline.nodes.get(pipeline.nodes.size() - 1).kind.name().toLowerCase();
    }

    private static void eraseNotesWithPrefix(FunctionalPipeline pipeline, String prefix) {
        pipeline.optimizationNotes.removeIf(note -> note.startsWith(prefix));
    }

    private static String barrierReason(FunctionalPipeline pipeline) {
        for (FunctionalNode node : pipeline.nodes) {
            ObservableEffects effects = node.effects;
            if (effects.domainWrite) return "observable domain WRITE";
            if (effects.domainRead) return "observable domain READ";
            if (effects.message) return "message send";
            if (effects.externalIo) return "observable callback effect";
            if (effects.localMutation) return "observable local mutation";
            if (effects.mayPanic) return "callback may panic";
            if (!effects.raiseSet.isEmpty()) return "callback may raise";
            if (effects.unresolved) return "unresolved callback effect";
        }
        return "eager semantics required";
    }

    private static String skippedCallbackBarrierReason(FunctionalPipeline pipeline) {
        if (pipeline.nodes.size() >= 2) {
            for (int i = 1; i < pipeline.nodes.size(); i++) {
                ObservableEffects effects = pipeline.nodes.get(i).effects;
                if (effects.domainWrite) return "observable domain WRITE";
                if (effects.domainRead) return "observable domain READ";
                if (effects.message) return "message send";
                if (effects.externalIo) return "observable callback effect";
                if (effects.localMutation) return "observable local mutation";
                if (effects.mayPanic) return "callback may panic";
                if (!effects.raiseSet.isEmpty()) return "callback may raise";
                if (effects.mayDiverge) return "skipped callback may diverge";
                if (effects.unresolved) return "unresolved callback effect";
            }
        }
        return "eager semantics required";
    }

    private static String countEliminationBarrierReason(FunctionalPipeline pipeline) {
        String reason = skippedCallbackBarrierReason(pipeline);
        if ("skipped callback may diverge".equals(reason)) return "callback may diverge";
        return reason;
    }

    private static String semanticStepSkipBarrier(FunctionalPipeline pipeline, FunctionalSemanticStep step) {
        for (int nodeIndex : step.sourceNodeIndices) {
            if (nodeIndex == 0 || nodeIndex >= pipeline.nodes.size()) return "invalid semantic stage";
            ObservableEffects effects = pipeline.nodes.get(nodeIndex).effects;
            if (effects.domainWrite) return "observable domain WRITE";
            if (effects.domainRead) return "observable domain READ";
            if (effects.message) return "message send";
            if (effects.externalIo) return "observable callback effect";
            if (effects.localMutation) return "observable local mutation";
            if (effects.mayPanic) return "callback may panic";
            if (!effects.raiseSet.isEmpty()) return "callback may raise";
            if (effects.mayDiverge) return "callback may diverge";
            if (effects.unresolved) return "unresolved callback effect";
        }
        return "ownership or eager-ordering barrier";
    }

    public static Integer inertLiteralVectorSize(String expression) {
        if (expression == null) return null;
        String value = expression.trim();
        if (value.length() < 2 || value.charAt(0) != '[' || value.charAt(value.length() - 1) != ']') {
            return null;
        }
        String inside = value.substring(1, value.length() - 1).trim();
        if (inside.isEmpty()) return 0;
        String[] parts = inside.split(",");
        for (String part : parts) {
            String p = part.trim();
            if (!isInertLiteral(p)) return null;
        }
        return parts.length;
    }

    private static boolean isInertLiteral(String s) {
        if ("true".equals(s) || "false".equals(s)) return true;
        try {
            Double.parseDouble(s);
            return true;
        } catch (NumberFormatException e) {
            return false;
        }
    }
}
