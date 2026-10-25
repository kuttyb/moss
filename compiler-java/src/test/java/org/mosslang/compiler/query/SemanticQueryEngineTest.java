package org.mosslang.compiler.query;

import org.mosslang.compiler.ast.Program;
import org.mosslang.compiler.parser.Parser;
import org.mosslang.compiler.semantics.SemanticChecker;

import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;

public final class SemanticQueryEngineTest {
    public static void main(String[] args) {
        effectsQueryUsesResolvedFunctionFacts();
        effectsQueryDoesNotInferPanicFromNames();
        effectsQueryCollectsDirectRaisesForArbitraryNames();
        effectsQueryFollowsHandlerHelperEffects();
        effectsQueryFollowsOrdinaryFunctionRaises();
        effectsQueryFollowsLocalObjectMethodRaises();
        effectsQueryFollowsSameObjectMethodRaises();
        effectsQueryExcludesRecoveredDirectRaises();
        effectsQueryIncludesExplicitBareReraises();
        effectsQueryIncludesCatchAllBareReraises();
        effectsQueryNarrowsGroupedBareReraises();
        typeQueryPreservesOpenGenericResultFacts();
        System.out.println("SemanticQueryEngineTest PASSED.");
    }

    private static void effectsQueryUsesResolvedFunctionFacts() {
        String source = ""
                + "fn mutate(values: Vector[Int]):\n"
                + "  values.push(2)\n"
                + "fn main():\n"
                + "  values = [1]\n"
                + "  mutate(values)\n";
        Program program = Parser.parseSource(source, "effects-query.moss");
        new SemanticChecker(program).run();
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        try (PrintStream stream = new PrintStream(output, true, StandardCharsets.UTF_8)) {
            SemanticQueryEngine.executeQuery("effects", "mutate", program,
                    "effects-query.moss", true, null, null, stream);
        }
        String json = output.toString(StandardCharsets.UTF_8);
        assertContains(json, "\"id\": \"entity-v1:function:mutate\"");
        assertContains(json, "\"semantic_identity\": \"fn:mutate@1\"");
        assertContains(json, "\"line\": 1");
        assertContains(json, "\"type\": \"unit\"");
    }

    private static void typeQueryPreservesOpenGenericResultFacts() {
        String source = ""
                + "fn plus_one(value):\n"
                + "  return value + 1\n"
                + "fn main():\n"
                + "  integer = plus_one(1)\n"
                + "  floating = plus_one(1.5)\n";
        Program program = Parser.parseSource(source, "open-result-query.moss");
        new SemanticChecker(program).run();
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        try (PrintStream stream = new PrintStream(output, true, StandardCharsets.UTF_8)) {
            SemanticQueryEngine.executeQuery("type", "plus_one", program,
                    "open-result-query.moss", true, null, null, stream);
        }
        String json = output.toString(StandardCharsets.UTF_8);
        assertContains(json, "\"resolved_type\": \"_specialized_result:plus_one\"");
    }

    private static void effectsQueryDoesNotInferPanicFromNames() {
        String source = ""
                + "fn panic_free() -> Int:\n"
                + "  return 7\n"
                + "fn checked_assertion():\n"
                + "  assert(false)\n"
                + "fn main():\n"
                + "  pass\n";
        Program program = Parser.parseSource(source, "effects-query-panic-name.moss");
        new SemanticChecker(program).run();

        String harmless = queryEffects(program, "panic_free");
        assertContains(harmless, "\"may_panic\": false");
        String panicking = queryEffects(program, "checked_assertion");
        assertContains(panicking, "\"may_panic\": true");
    }

    private static String queryEffects(Program program, String target) {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        try (PrintStream stream = new PrintStream(output, true, StandardCharsets.UTF_8)) {
            SemanticQueryEngine.executeQuery("effects", target, program,
                    "effects-query-panic-name.moss", true, null, null, stream);
        }
        return output.toString(StandardCharsets.UTF_8);
    }

    private static void effectsQueryCollectsDirectRaisesForArbitraryNames() {
        String source = ""
                + "enum Trouble:\n"
                + "  Bad\n"
                + "fn calculate() -> Int:\n"
                + "  raise Trouble.Bad\n"
                + "fn main():\n"
                + "  pass\n";
        Program program = Parser.parseSource(source, "effects-query-direct-raise.moss");
        new SemanticChecker(program).run();
        String result = queryEffects(program, "calculate");
        assertContains(result, "\"raise_set\": [\"enum:Trouble:Bad\"]");
    }

    private static void effectsQueryFollowsHandlerHelperEffects() {
        String source = ""
                + "fn emit():\n"
                + "  echo \"x\"\n"
                + "domain Worker:\n"
                + "  fn Run():\n"
                + "    emit()\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  message worker.Run()\n";
        Program program = Parser.parseSource(source, "effects-query-handler-helper.moss");
        new SemanticChecker(program).run();
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        try (PrintStream stream = new PrintStream(output, true, StandardCharsets.UTF_8)) {
            SemanticQueryEngine.executeQuery("effects", "handler:Worker.Run", program,
                    "effects-query-handler-helper.moss", true, null, null, stream);
        }
        assertContains(output.toString(StandardCharsets.UTF_8), "\"external_io\": true");
    }

    private static void effectsQueryExcludesRecoveredDirectRaises() {
        String source = ""
                + "enum Trouble:\n"
                + "  Bad\n"
                + "fn handled() -> Int:\n"
                + "  try:\n"
                + "    raise Trouble.Bad\n"
                + "  recover Trouble.Bad:\n"
                + "    pass\n"
                + "  return 1\n"
                + "fn source_fault():\n"
                + "  raise Trouble.Bad\n"
                + "fn handled_call():\n"
                + "  try:\n"
                + "    source_fault()\n"
                + "  recover Trouble.Bad:\n"
                + "    pass\n"
                + "fn main():\n"
                + "  pass\n";
        Program program = Parser.parseSource(source, "effects-query-recovered-raise.moss");
        new SemanticChecker(program).run();
        String result = queryEffects(program, "handled");
        assertContains(result, "\"raise_set\": []");
        String calledResult = queryEffects(program, "handled_call");
        assertContains(calledResult, "\"raise_set\": []");
    }

    private static void effectsQueryIncludesExplicitBareReraises() {
        String source = ""
                + "enum Trouble:\n"
                + "  Bad\n"
                + "fn source_fault():\n"
                + "  raise Trouble.Bad\n"
                + "fn reraiser():\n"
                + "  try:\n"
                + "    source_fault()\n"
                + "  recover Trouble.Bad:\n"
                + "    raise\n"
                + "fn main():\n"
                + "  pass\n";
        Program program = Parser.parseSource(source, "effects-query-bare-reraise.moss");
        new SemanticChecker(program).run();
        String result = queryEffects(program, "reraiser");
        assertContains(result, "\"raise_set\": [\"enum:Trouble:Bad\"]");
    }

    private static void effectsQueryIncludesCatchAllBareReraises() {
        String source = ""
                + "enum Trouble:\n"
                + "  Bad\n"
                + "fn source_fault():\n"
                + "  raise Trouble.Bad\n"
                + "fn reraiser():\n"
                + "  try:\n"
                + "    source_fault()\n"
                + "  recover:\n"
                + "    raise\n"
                + "fn main():\n"
                + "  pass\n";
        Program program = Parser.parseSource(source, "effects-query-catchall-reraise.moss");
        new SemanticChecker(program).run();
        String result = queryEffects(program, "reraiser");
        assertContains(result, "\"raise_set\": [\"enum:Trouble:Bad\"]");
    }

    private static void effectsQueryNarrowsGroupedBareReraises() {
        String source = ""
                + "enum Trouble:\n"
                + "  Bad\n"
                + "  Other\n"
                + "fn source_fault():\n"
                + "  raise Trouble.Bad\n"
                + "fn reraiser():\n"
                + "  try:\n"
                + "    source_fault()\n"
                + "  recover Trouble.Bad, Trouble.Other:\n"
                + "    raise\n"
                + "fn main():\n"
                + "  pass\n";
        Program program = Parser.parseSource(source, "effects-query-grouped-reraise.moss");
        new SemanticChecker(program).run();
        String result = queryEffects(program, "reraiser");
        assertContains(result, "\"raise_set\": [\"enum:Trouble:Bad\"]");
        if (result.contains("enum:Trouble:Other"))
            throw new AssertionError("unreachable grouped recover variant leaked into query: " + result);
    }

    private static void effectsQueryFollowsOrdinaryFunctionRaises() {
        String source = ""
                + "enum Trouble:\n"
                + "  Bad\n"
                + "fn source_fault():\n"
                + "  raise Trouble.Bad\n"
                + "fn calculate():\n"
                + "  source_fault()\n"
                + "fn main():\n"
                + "  pass\n";
        Program program = Parser.parseSource(source, "effects-query-called-raise.moss");
        new SemanticChecker(program).run();
        String result = queryEffects(program, "calculate");
        assertContains(result, "\"raise_set\": [\"enum:Trouble:Bad\"]");
    }

    private static void effectsQueryFollowsLocalObjectMethodRaises() {
        String source = ""
                + "enum Trouble:\n"
                + "  Bad\n"
                + "type Box:\n"
                + "  fn source_fault():\n"
                + "    raise Trouble.Bad\n"
                + "fn calculate():\n"
                + "  box = Box()\n"
                + "  box.source_fault()\n"
                + "fn main():\n"
                + "  pass\n";
        Program program = Parser.parseSource(source, "effects-query-local-method-raise.moss");
        new SemanticChecker(program).run();
        String result = queryEffects(program, "calculate");
        assertContains(result, "\"raise_set\": [\"enum:Trouble:Bad\"]");
    }

    private static void effectsQueryFollowsSameObjectMethodRaises() {
        String source = ""
                + "enum Trouble:\n"
                + "  Bad\n"
                + "type Box:\n"
                + "  fn source_fault():\n"
                + "    raise Trouble.Bad\n"
                + "  fn calculate():\n"
                + "    self.source_fault()\n"
                + "fn main():\n"
                + "  pass\n";
        Program program = Parser.parseSource(source, "effects-query-method-raise.moss");
        new SemanticChecker(program).run();
        String result = queryEffects(program, "method:Box.calculate");
        assertContains(result, "\"raise_set\": [\"enum:Trouble:Bad\"]");
    }

    private static void assertContains(String value, String expected) {
        if (!value.contains(expected)) {
            throw new AssertionError("missing <" + expected + "> from query result: " + value);
        }
    }
}
