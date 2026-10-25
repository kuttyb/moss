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

    private static void assertContains(String value, String expected) {
        if (!value.contains(expected)) {
            throw new AssertionError("missing <" + expected + "> from query result: " + value);
        }
    }
}
