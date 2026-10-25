package org.mosslang.compiler.interpreter;

import org.mosslang.compiler.parser.Parser;
import org.mosslang.compiler.semantics.CheckedProgram;

import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;

public final class FastInterpreterTest {
    public static void main(String[] args) {
        testFractionalAndMixedNumericComparisons();
        testIeeeAndMixedNumericEquality();
        testWhileLoopRepeatsCheckedBody();
        testLocalWriteTraceIsStructuredAndEscaped();
        testMossStringEscapeDecoding();
        testLexicalBlocksRestoreShadowedLocals();
        testRecoveryPayloadBindingIsScoped();
        testForLoopTraceMatchesIterationStructure();
        testBranchTraceCarriesControlContext();
        testFunctionCallTraceCarriesCallContext();
        testCollectionTraceUsesCompactSummary();
        System.out.println("FastInterpreterTest PASSED.");
    }

    private static void testFractionalAndMixedNumericComparisons() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  echo 1.9 > 1.1\n"
                + "  echo 1.1 < 1.9\n"
                + "  echo 1.9 >= 1.9\n"
                + "  echo 1.1 <= 1.1\n"
                + "  echo 1 < 1.5\n"
                + "  echo 2.5 > 2\n", "fast-interpreter-float-comparison.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        try (PrintStream captured = new PrintStream(stdout, true, StandardCharsets.UTF_8)) {
            new FastInterpreter(program, false, captured,
                    new PrintStream(new ByteArrayOutputStream())).run();
        }
        String actual = stdout.toString(StandardCharsets.UTF_8);
        String expected = "true\ntrue\ntrue\ntrue\ntrue\ntrue\n";
        if (!expected.equals(actual)) {
            throw new AssertionError("fractional/mixed numeric comparisons were incorrect: " + actual);
        }
    }

    private static void testIeeeAndMixedNumericEquality() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  echo 2 == 2.0\n"
                + "  echo 2 != 2.0\n"
                + "  echo 0.0 == -0.0\n"
                + "  echo 0.0 != -0.0\n"
                + "  echo (0.0 / 0.0) == (0.0 / 0.0)\n"
                + "  echo (0.0 / 0.0) != (0.0 / 0.0)\n"
                + "  echo [1] == [1]\n"
                + "  echo [[1], [2]] == [[1], [2]]\n"
                + "  echo [1.0] == [1.0]\n",
                "fast-interpreter-float-equality.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        try (PrintStream captured = new PrintStream(stdout, true, StandardCharsets.UTF_8)) {
            new FastInterpreter(program, false, captured,
                    new PrintStream(new ByteArrayOutputStream())).run();
        }
        String actual = stdout.toString(StandardCharsets.UTF_8);
        String expected = "true\nfalse\ntrue\nfalse\nfalse\ntrue\ntrue\ntrue\ntrue\n";
        if (!expected.equals(actual)) {
            throw new AssertionError("IEEE/mixed numeric equality was incorrect: " + actual);
        }
    }

    private static void testWhileLoopRepeatsCheckedBody() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  count = 0\n"
                + "  while count < 3:\n"
                + "    echo count\n"
                + "    count = count + 1\n", "fast-interpreter-while.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        try (PrintStream captured = new PrintStream(stdout, true, StandardCharsets.UTF_8)) {
            new FastInterpreter(program, false, captured,
                    new PrintStream(new ByteArrayOutputStream())).run();
        }
        String actual = stdout.toString(StandardCharsets.UTF_8);
        String expected = "0\n1\n2\n";
        if (!expected.equals(actual)) {
            throw new AssertionError("while loop did not reevaluate its checked body: " + actual);
        }
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(new ByteArrayOutputStream()),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        if (events.split("\\\"event\\\":\\\"LoopIteration\\\"", -1).length != 4
                || !events.contains("\"detail\":\"1\"")
                || !events.contains("\"detail\":\"2\"")
                || !events.contains("\"detail\":\"3\"")
                || !events.contains("\"parent_event_id\":3,\"control_event_id\":3")
                || !events.contains("\"line\":4,\"parent_event_id\":3,\"control_event_id\":3,\"depth\":1")) {
            throw new AssertionError("while-loop trace omitted iteration context: " + events);
        }
    }

    private static void testLocalWriteTraceIsStructuredAndEscaped() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  text = \"say \\\"hello\\\"\"\n"
                + "  echo text\n", "fast-interpreter-trace.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String event = trace.toString(StandardCharsets.UTF_8);
        if (!event.contains("\"event\":\"LocalWrite\"")
                || !event.contains("\"semantic_identity\":\"main@1\"")
                || !event.contains("\"detail\":\"text\"")
                || !event.contains("\"after\":\"say \\\"hello\\\"\"")
                || !event.contains("\"event\":\"LocalRead\"")
                || !event.contains("\"line\":3")
                || !"say \"hello\"\n".equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("Fast Debug trace omitted or malformed the local write: " + event);
        }
    }

    private static void testMossStringEscapeDecoding() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  echo \"a\\nb\"\n"
                + "  echo \"\\x41\"\n"
                + "  echo \"\\u{1F642}\"\n"
                + "  echo \"\\\\\"\n", "fast-interpreter-string-escapes.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        new FastInterpreter(program, false,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(new ByteArrayOutputStream())).run();
        String expected = "a\nb\nA\n🙂\n\\\n";
        String actual = stdout.toString(StandardCharsets.UTF_8);
        if (!expected.equals(actual)) {
            throw new AssertionError("Moss string escapes were decoded incorrectly: " + actual);
        }
    }

    private static void testLexicalBlocksRestoreShadowedLocals() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  value = 1\n"
                + "  if true:\n"
                + "    let value = 2\n"
                + "    echo value\n"
                + "  echo value\n"
                + "  for value in [3, 4]:\n"
                + "    echo value\n"
                + "  echo value\n", "fast-interpreter-block-scopes.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        new FastInterpreter(program, false,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(new ByteArrayOutputStream())).run();
        String expected = "2\n1\n3\n4\n1\n";
        String actual = stdout.toString(StandardCharsets.UTF_8);
        if (!expected.equals(actual)) {
            throw new AssertionError("Fast Debug leaked block bindings: " + actual);
        }
    }

    private static void testRecoveryPayloadBindingIsScoped() {
        var program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad(code: Int)\n"
                + "fn main():\n"
                + "  code = 1\n"
                + "  try:\n"
                + "    raise Failure.Bad(code: 2)\n"
                + "  recover Failure.Bad(code):\n"
                + "    echo code\n"
                + "  echo code\n", "fast-interpreter-recover-scope.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        new FastInterpreter(program, false,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(new ByteArrayOutputStream())).run();
        String actual = stdout.toString(StandardCharsets.UTF_8);
        if (!"2\n1\n".equals(actual)) {
            throw new AssertionError("recovery payload binding escaped its scope: " + actual);
        }
    }

    private static void testForLoopTraceMatchesIterationStructure() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  total = 0\n"
                + "  for value in range(0, 2):\n"
                + "    echo value\n"
                + "    total = total + value\n", "fast-interpreter-for-trace.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        if (events.split("\\\"event\\\":\\\"LoopIteration\\\"", -1).length != 3
                || !events.contains("\"line\":4,\"parent_event_id\":2,\"control_event_id\":2,\"depth\":1")
                || !"0\n1\n".equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("for-loop trace omitted iteration context: " + events);
        }
    }

    private static void testBranchTraceCarriesControlContext() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  value = 1\n"
                + "  if value > 0:\n"
                + "    echo value\n"
                + "  else:\n"
                + "    echo 0\n"
                + "  if value < 0:\n"
                + "    echo 0\n"
                + "  else:\n"
                + "    echo value\n", "fast-interpreter-branch-trace.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(new ByteArrayOutputStream()),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        if (events.split("\\\"event\\\":\\\"BranchTaken\\\"", -1).length != 3
                || !events.contains("\"detail\":\"then\"")
                || !events.contains("\"detail\":\"else\"")
                || !events.contains("\"line\":4,\"parent_event_id\":3,\"control_event_id\":3,\"depth\":1")) {
            throw new AssertionError("branch trace omitted selected-arm context: " + events);
        }
    }

    private static void testFunctionCallTraceCarriesCallContext() {
        var program = Parser.parseSource(""
                + "fn add_one(x: Int) -> Int:\n"
                + "  return x + 1\n"
                + "\n"
                + "fn main():\n"
                + "  echo add_one(4)\n",
                "fast-interpreter-function-trace.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        String expected = ""
                + "{\"event_id\":1,\"event\":\"FunctionEnter\",\"function\":\"add_one\",\"source_file\":\"fast-interpreter-function-trace.moss\",\"semantic_identity\":\"fn:add_one@1\",\"line\":1,\"depth\":0}\n"
                + "{\"event_id\":2,\"event\":\"LocalRead\",\"function\":\"add_one\",\"source_file\":\"fast-interpreter-function-trace.moss\",\"semantic_identity\":\"fn:add_one@1\",\"line\":2,\"parent_event_id\":1,\"call_event_id\":1,\"depth\":1,\"detail\":\"x\"}\n"
                + "{\"event_id\":3,\"event\":\"Return\",\"function\":\"add_one\",\"source_file\":\"fast-interpreter-function-trace.moss\",\"semantic_identity\":\"fn:add_one@1\",\"line\":2,\"parent_event_id\":1,\"call_event_id\":1,\"depth\":1,\"detail\":\"5\"}\n"
                + "{\"event_id\":4,\"event\":\"FunctionExit\",\"function\":\"add_one\",\"source_file\":\"fast-interpreter-function-trace.moss\",\"semantic_identity\":\"fn:add_one@1\",\"line\":1,\"parent_event_id\":1,\"call_event_id\":1,\"depth\":1}\n";
        if (!expected.equals(events) || !"5\n".equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("function trace did not retain call context: " + events);
        }
    }

    private static void testCollectionTraceUsesCompactSummary() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  let xs = [3, 4]\n",
                "fast-interpreter-vector-trace.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(new ByteArrayOutputStream()),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String expected = "{\"event_id\":1,\"event\":\"LocalWrite\",\"function\":\"main\","
                + "\"source_file\":\"fast-interpreter-vector-trace.moss\",\"semantic_identity\":\"main@1\","
                + "\"line\":2,\"depth\":0,\"detail\":\"xs\",\"after\":\"Vector(size=2)\"}\n";
        if (!expected.equals(trace.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("collection trace summary differed: " + trace);
        }
    }
}
