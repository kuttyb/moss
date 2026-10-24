package org.mosslang.compiler.optimizer;

import java.util.Arrays;
import java.util.List;

public class FunctionalOptimizerTest {
    public static void main(String[] args) {
        System.out.println("Running FunctionalOptimizerTest...");

        testMapFusion();
        testFilterFusion();
        testTerminalCountElimination();
        testTerminalCountInertLiteral();
        testTerminalSumAndReduce();
        testIdentityMapPushdown();
        testFusionInhibitedWhenIneligibleOrImpure();

        System.out.println("FunctionalOptimizerTest PASSED: All tests passed successfully.");
    }

    private static void assertEquals(Object expected, Object actual, String message) {
        if (expected == null && actual == null) return;
        if (expected != null && expected.equals(actual)) return;
        throw new AssertionError(message + " - expected: " + expected + ", actual: " + actual);
    }

    private static void assertTrue(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    private static void assertFalse(boolean condition, String message) {
        if (condition) {
            throw new AssertionError(message);
        }
    }

    private static void testMapFusion() {
        FunctionalPipeline pipeline = new FunctionalPipeline();
        pipeline.fusionEligible = true;

        FunctionalNode source = new FunctionalNode(FunctionalNodeKind.Source, "data", "");
        FunctionalNode map1 = new FunctionalNode(FunctionalNodeKind.Map, "map", "double_val");
        FunctionalNode map2 = new FunctionalNode(FunctionalNodeKind.Map, "map", "increment");

        pipeline.nodes.addAll(Arrays.asList(source, map1, map2));

        FunctionalOptimizer.optimizePipeline(pipeline, true);

        assertTrue(pipeline.fused, "Pipeline should be fused");
        assertEquals(1, pipeline.semanticSteps.size(), "Two adjacent maps should be fused into one step");

        FunctionalSemanticStep step = pipeline.semanticSteps.get(0);
        assertEquals(FunctionalNodeKind.Map, step.kind, "Fused step should be Map");
        assertEquals(Arrays.asList(1, 2), step.sourceNodeIndices, "Step should contain indices [1, 2]");
        assertEquals("increment(double_val(x))", step.composedExpression, "Composed map expression should be increment(double_val(x))");

        boolean hasComposeMapRewrite = pipeline.semanticRewrites.stream()
                .anyMatch(r -> "compose-map".equals(r.name));
        assertTrue(hasComposeMapRewrite, "Should record 'compose-map' rewrite");
    }

    private static void testFilterFusion() {
        FunctionalPipeline pipeline = new FunctionalPipeline();
        pipeline.fusionEligible = true;

        FunctionalNode source = new FunctionalNode(FunctionalNodeKind.Source, "data", "");
        FunctionalNode filter1 = new FunctionalNode(FunctionalNodeKind.Filter, "filter", "x > 0");
        FunctionalNode filter2 = new FunctionalNode(FunctionalNodeKind.Filter, "filter", "x < 100");

        pipeline.nodes.addAll(Arrays.asList(source, filter1, filter2));

        FunctionalOptimizer.optimizePipeline(pipeline, true);

        assertTrue(pipeline.fused, "Pipeline should be fused");
        assertEquals(1, pipeline.semanticSteps.size(), "Two adjacent filters should be fused into one step");

        FunctionalSemanticStep step = pipeline.semanticSteps.get(0);
        assertEquals(FunctionalNodeKind.Filter, step.kind, "Fused step should be Filter");
        assertEquals(Arrays.asList(1, 2), step.sourceNodeIndices, "Step should contain indices [1, 2]");
        assertEquals("x > 0 and x < 100", step.composedExpression, "Composed filter expression should be 'x > 0 and x < 100'");

        boolean hasComposeFilterRewrite = pipeline.semanticRewrites.stream()
                .anyMatch(r -> "compose-filter".equals(r.name));
        assertTrue(hasComposeFilterRewrite, "Should record 'compose-filter' rewrite");
    }

    private static void testTerminalCountElimination() {
        FunctionalPipeline pipeline = new FunctionalPipeline();
        pipeline.fusionEligible = true;

        FunctionalNode source = new FunctionalNode(FunctionalNodeKind.Source, "items", "");
        FunctionalNode map = new FunctionalNode(FunctionalNodeKind.Map, "map", "compute");
        FunctionalNode count = new FunctionalNode(FunctionalNodeKind.Count, "count", "");

        pipeline.nodes.addAll(Arrays.asList(source, map, count));

        FunctionalOptimizer.optimizePipeline(pipeline, true);

        assertTrue(pipeline.fused, "Pipeline should be fused");
        assertTrue(pipeline.countUsesExactLength, "Terminal count should use exact length when trailing map is dead");
        assertTrue(map.deadStageEliminated, "Dead map stage before count should be eliminated");
        assertTrue(map.semanticWorkEliminated, "Dead map work should be eliminated");

        boolean hasDeadMapRewrite = pipeline.semanticRewrites.stream()
                .anyMatch(r -> "dead-map".equals(r.name));
        assertTrue(hasDeadMapRewrite, "Should record dead-map rewrite");

        boolean hasTerminalCountRewrite = pipeline.semanticRewrites.stream()
                .anyMatch(r -> "terminal-count".equals(r.name));
        assertTrue(hasTerminalCountRewrite, "Should record terminal-count rewrite");
    }

    private static void testTerminalCountInertLiteral() {
        FunctionalPipeline pipeline = new FunctionalPipeline();
        pipeline.fusionEligible = true;
        pipeline.sourceExpression = "[10, 20, 30, 40]";

        FunctionalNode source = new FunctionalNode(FunctionalNodeKind.Source, pipeline.sourceExpression, "");
        FunctionalNode count = new FunctionalNode(FunctionalNodeKind.Count, "count", "");

        pipeline.nodes.addAll(Arrays.asList(source, count));

        FunctionalOptimizer.optimizePipeline(pipeline, true);

        assertTrue(pipeline.countUsesExactLength, "Count uses exact length");
        assertTrue(pipeline.countUsesKnownSize, "Count uses known size");
        assertEquals(4, pipeline.knownSourceSize, "Known size should be 4");

        boolean hasKnownSizeCountRewrite = pipeline.semanticRewrites.stream()
                .anyMatch(r -> "known-size-count".equals(r.name));
        assertTrue(hasKnownSizeCountRewrite, "Should record known-size-count rewrite");
    }

    private static void testTerminalSumAndReduce() {
        // Test Sum
        FunctionalPipeline sumPipeline = new FunctionalPipeline();
        sumPipeline.fusionEligible = true;

        FunctionalNode src1 = new FunctionalNode(FunctionalNodeKind.Source, "nums", "");
        FunctionalNode filter = new FunctionalNode(FunctionalNodeKind.Filter, "filter", "x > 0");
        FunctionalNode sum = new FunctionalNode(FunctionalNodeKind.Sum, "sum", "");

        sumPipeline.nodes.addAll(Arrays.asList(src1, filter, sum));

        FunctionalOptimizer.optimizePipeline(sumPipeline, true);

        boolean hasTerminalSumRewrite = sumPipeline.semanticRewrites.stream()
                .anyMatch(r -> "terminal-sum".equals(r.name));
        assertTrue(hasTerminalSumRewrite, "Should record terminal-sum rewrite");

        // Test Reduce
        FunctionalPipeline reducePipeline = new FunctionalPipeline();
        reducePipeline.fusionEligible = true;

        FunctionalNode src2 = new FunctionalNode(FunctionalNodeKind.Source, "nums", "");
        FunctionalNode map = new FunctionalNode(FunctionalNodeKind.Map, "map", "f");
        FunctionalNode reduce = new FunctionalNode(FunctionalNodeKind.Reduce, "reduce", "0, add");

        reducePipeline.nodes.addAll(Arrays.asList(src2, map, reduce));

        FunctionalOptimizer.optimizePipeline(reducePipeline, true);

        boolean hasTerminalReduceRewrite = reducePipeline.semanticRewrites.stream()
                .anyMatch(r -> "terminal-reduce".equals(r.name));
        assertTrue(hasTerminalReduceRewrite, "Should record terminal-reduce rewrite");
    }

    private static void testIdentityMapPushdown() {
        FunctionalPipeline pipeline = new FunctionalPipeline();
        pipeline.fusionEligible = true;

        FunctionalNode source = new FunctionalNode(FunctionalNodeKind.Source, "data", "");
        FunctionalNode idMap = new FunctionalNode(FunctionalNodeKind.Map, "map", "_");
        idMap.inputType = "int";
        idMap.outputType = "int";
        FunctionalNode filter = new FunctionalNode(FunctionalNodeKind.Filter, "filter", "x > 0");

        pipeline.nodes.addAll(Arrays.asList(source, idMap, filter));

        FunctionalOptimizer.optimizePipeline(pipeline, true);

        assertEquals(1, pipeline.semanticSteps.size(), "Identity map should be pushed down/eliminated before filter");
        assertEquals(FunctionalNodeKind.Filter, pipeline.semanticSteps.get(0).kind, "Remaining step should be Filter");
        assertTrue(idMap.semanticWorkEliminated, "Identity map should have semantic work eliminated");

        boolean hasPredicatePushdownRewrite = pipeline.semanticRewrites.stream()
                .anyMatch(r -> "predicate-pushdown".equals(r.name));
        assertTrue(hasPredicatePushdownRewrite, "Should record predicate-pushdown rewrite");
    }

    private static void testFusionInhibitedWhenIneligibleOrImpure() {
        FunctionalPipeline pipeline = new FunctionalPipeline();
        pipeline.fusionEligible = false;

        FunctionalNode source = new FunctionalNode(FunctionalNodeKind.Source, "data", "");
        FunctionalNode map1 = new FunctionalNode(FunctionalNodeKind.Map, "map", "f");
        FunctionalNode map2 = new FunctionalNode(FunctionalNodeKind.Map, "map", "g");

        pipeline.nodes.addAll(Arrays.asList(source, map1, map2));

        FunctionalOptimizer.optimizePipeline(pipeline, true);

        assertFalse(pipeline.fused, "Pipeline should NOT be fused when fusionEligible is false");
        assertEquals(2, pipeline.semanticSteps.size(), "Maps should not be composed when fusion is ineligible");

        // Impure callback inhibits fusion
        FunctionalPipeline impurePipeline = new FunctionalPipeline();
        impurePipeline.fusionEligible = true;

        FunctionalNode mapImpure = new FunctionalNode(FunctionalNodeKind.Map, "map", "f_impure");
        mapImpure.effects.domainWrite = true; // Impure side effect
        FunctionalNode mapNext = new FunctionalNode(FunctionalNodeKind.Map, "map", "g");

        impurePipeline.nodes.addAll(Arrays.asList(source, mapImpure, mapNext));

        FunctionalOptimizer.optimizePipeline(impurePipeline, true);

        assertEquals(2, impurePipeline.semanticSteps.size(), "Impure map should not be fused with next map");
    }
}
