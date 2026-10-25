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
        testRaiseRecoverTraceCarriesErrorIdentity();
        testObjectMethodTraceCarriesCallContext();
        testBuiltinMethodReceiverReadIsTraced();
        testEnumLocalWriteUsesCompactTypeSummary();
        testExceptionalFunctionTraceUnwindsCallContext();
        testDomainConstructionTraceIsStructured();
        testDomainMessageTraceCarriesHandlerContext();
        testLocalArgumentTraceOccursInsideCallee();
        testNamedDomainStateAndNestedRoutes();
        testPipelineStageUsesFunctionCallTrace();
        testFunctionTraceIncludesSelectedSpecialization();
        testNestedDomainFailurePropagatesToCallerRecovery();
        testCatchAllRecoveryBindsRaisedEnum();
        testOnFailDispatchUsesQualifiedErrorType();
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
                + "    echo value\n"
                + "  if value < 0:\n"
                + "    echo 0\n", "fast-interpreter-branch-trace.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(new ByteArrayOutputStream()),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        if (events.split("\\\"event\\\":\\\"BranchTaken\\\"", -1).length != 4
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

    private static void testRaiseRecoverTraceCarriesErrorIdentity() {
        var program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad(code: Int)\n"
                + "fn main():\n"
                + "  try:\n"
                + "    raise Failure.Bad(code: 2)\n"
                + "  recover Failure.Bad(code):\n"
                + "    echo code\n",
                "fast-interpreter-recover-trace.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        String expected = ""
                + "{\"event_id\":1,\"event\":\"raise\",\"function\":\"main\",\"source_file\":\"fast-interpreter-recover-trace.moss\",\"semantic_identity\":\"main@3\",\"line\":5,\"depth\":0,\"detail\":\"enum:Failure:Bad\"}\n"
                + "{\"event_id\":2,\"event\":\"recover\",\"function\":\"main\",\"source_file\":\"fast-interpreter-recover-trace.moss\",\"semantic_identity\":\"main@3\",\"line\":6,\"depth\":0,\"detail\":\"enum:Failure:Bad\"}\n"
                + "{\"event_id\":3,\"event\":\"LocalRead\",\"function\":\"main\",\"source_file\":\"fast-interpreter-recover-trace.moss\",\"semantic_identity\":\"main@3\",\"line\":7,\"depth\":0,\"detail\":\"code\"}\n";
        if (!expected.equals(events) || !"2\n".equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("raise/recover trace omitted error identity: " + events);
        }
    }

    private static void testObjectMethodTraceCarriesCallContext() {
        var program = Parser.parseSource(""
                + "type Box:\n"
                + "  value: Int\n"
                + "  fn get() -> Int:\n"
                + "    return value\n"
                + "fn main():\n"
                + "  let box = Box(value: 7)\n"
                + "  echo box.get()\n",
                "fast-interpreter-method-trace.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        String expected = ""
                + "{\"event_id\":1,\"event\":\"LocalWrite\",\"function\":\"main\",\"source_file\":\"fast-interpreter-method-trace.moss\",\"semantic_identity\":\"main@5\",\"line\":6,\"depth\":0,\"detail\":\"box\",\"after\":\"Box\"}\n"
                + "{\"event_id\":2,\"event\":\"LocalRead\",\"function\":\"main\",\"source_file\":\"fast-interpreter-method-trace.moss\",\"semantic_identity\":\"main@5\",\"line\":7,\"depth\":0,\"detail\":\"box\"}\n"
                + "{\"event_id\":3,\"event\":\"MethodEnter\",\"function\":\"Box.get\",\"source_file\":\"fast-interpreter-method-trace.moss\",\"semantic_identity\":\"method:Box.get@3\",\"line\":3,\"depth\":0}\n"
                + "{\"event_id\":4,\"event\":\"LocalRead\",\"function\":\"Box.get\",\"source_file\":\"fast-interpreter-method-trace.moss\",\"semantic_identity\":\"method:Box.get@3\",\"line\":4,\"parent_event_id\":3,\"call_event_id\":3,\"depth\":1,\"detail\":\"value\"}\n"
                + "{\"event_id\":5,\"event\":\"Return\",\"function\":\"Box.get\",\"source_file\":\"fast-interpreter-method-trace.moss\",\"semantic_identity\":\"method:Box.get@3\",\"line\":4,\"parent_event_id\":3,\"call_event_id\":3,\"depth\":1,\"detail\":\"7\"}\n"
                + "{\"event_id\":6,\"event\":\"MethodExit\",\"function\":\"Box.get\",\"source_file\":\"fast-interpreter-method-trace.moss\",\"semantic_identity\":\"method:Box.get@3\",\"line\":3,\"parent_event_id\":3,\"call_event_id\":3,\"depth\":1}\n";
        if (!expected.equals(events) || !"7\n".equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("method trace omitted call context: " + events);
        }
    }

    private static void testBuiltinMethodReceiverReadIsTraced() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  text = \"abc\"\n"
                + "  echo text.length()\n", "fast-interpreter-builtin-method-read.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        String[] lines = events.split("\\R");
        if (lines.length != 2 || !lines[1].contains("\"event\":\"LocalRead\"")
                || !lines[1].contains("\"detail\":\"text\"")
                || !"3\n".equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("builtin method receiver read was not traced: " + events);
        }
    }

    private static void testEnumLocalWriteUsesCompactTypeSummary() {
        var program = Parser.parseSource(""
                + "enum Data:\n"
                + "  Have(value: String)\n"
                + "fn main():\n"
                + "  data = Data.Have(value: \"hello\")\n",
                "fast-interpreter-enum-write-summary.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(new ByteArrayOutputStream(), true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        if (!events.contains("\"event\":\"LocalWrite\"")
                || !events.contains("\"after\":\"Data\"")) {
            throw new AssertionError("enum LocalWrite should use its compact type summary: " + events);
        }
    }

    private static void testExceptionalFunctionTraceUnwindsCallContext() {
        var program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad(code: Int)\n"
                + "fn fail():\n"
                + "  raise Failure.Bad(code: 2)\n"
                + "fn main():\n"
                + "  try:\n"
                + "    fail()\n"
                + "  recover Failure.Bad(code):\n"
                + "    echo code\n",
                "fast-interpreter-exception-call-trace.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String expected = ""
                + "{\"event_id\":1,\"event\":\"FunctionEnter\",\"function\":\"fail\",\"source_file\":\"fast-interpreter-exception-call-trace.moss\",\"semantic_identity\":\"fn:fail@3\",\"line\":3,\"depth\":0}\n"
                + "{\"event_id\":2,\"event\":\"raise\",\"function\":\"fail\",\"source_file\":\"fast-interpreter-exception-call-trace.moss\",\"semantic_identity\":\"fn:fail@3\",\"line\":4,\"parent_event_id\":1,\"call_event_id\":1,\"depth\":1,\"detail\":\"enum:Failure:Bad\"}\n"
                + "{\"event_id\":3,\"event\":\"recover\",\"function\":\"main\",\"source_file\":\"fast-interpreter-exception-call-trace.moss\",\"semantic_identity\":\"main@5\",\"line\":8,\"depth\":0,\"detail\":\"enum:Failure:Bad\"}\n"
                + "{\"event_id\":4,\"event\":\"LocalRead\",\"function\":\"main\",\"source_file\":\"fast-interpreter-exception-call-trace.moss\",\"semantic_identity\":\"main@5\",\"line\":9,\"depth\":0,\"detail\":\"code\"}\n";
        if (!expected.equals(trace.toString(StandardCharsets.UTF_8))
                || !"2\n".equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("exceptional function trace did not unwind call context: " + trace);
        }
    }

    private static void testDomainConstructionTraceIsStructured() {
        var program = Parser.parseSource(""
                + "domain Worker:\n"
                + "  count: Int\n"
                + "fn main():\n"
                + "  let worker = Worker()\n",
                "fast-interpreter-domain-instance.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(new ByteArrayOutputStream()),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String expected = "{\"event_id\":1,\"event\":\"domain_instance\",\"function\":\"main\","
                + "\"source_file\":\"fast-interpreter-domain-instance.moss\",\"semantic_identity\":\"main@3\","
                + "\"line\":4,\"depth\":0,\"instance\":\"main::worker\","
                + "\"specialization\":\"domain-specialization:Worker:worker\"}\n";
        if (!expected.equals(trace.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("domain construction trace was incomplete: " + trace);
        }
    }

    private static void testDomainMessageTraceCarriesHandlerContext() {
        var program = Parser.parseSource(""
                + "domain Worker:\n"
                + "  total = 0\n"
                + "\n"
                + "  fn Run(value: Int):\n"
                + "    total = total + value\n"
                + "    echo \"worker:\", total\n"
                + "\n"
                + "  fn Get():\n"
                + "    reply total\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  message worker.Run(10)\n"
                + "  current = message worker.Get()\n"
                + "  echo \"current:\", current\n",
                "fast-interpreter-domain-message-trace.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        String[] kinds = {"domain_instance", "LocalRead", "message_call", "handler_enter",
                "state_read", "LocalRead", "state_write", "state_read", "handler_exit",
                "message_return", "LocalRead", "message_call", "handler_enter", "state_read",
                "reply", "handler_exit", "message_return", "LocalWrite", "LocalRead"};
        int previous = -1;
        for (String kind : kinds) {
            int position = events.indexOf("\"event\":\"" + kind + "\"", previous + 1);
            if (position < 0) throw new AssertionError("domain message trace omitted " + kind + ": " + events);
            previous = position;
        }
        if (events.contains("FastInterpreter$DomainInstance@")
                || !events.contains("\"semantic_identity\":\"handler:Worker.Run@4\"")
                || !events.contains("\"message_event_id\":3,\"handler_event_id\":4")
                || !"worker: 10\ncurrent: 10\n".equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("domain message trace lost handler context: " + events);
        }
    }

    private static void testLocalArgumentTraceOccursInsideCallee() {
        var program = Parser.parseSource(""
                + "fn identity(value: Int) -> Int:\n"
                + "  return value\n"
                + "fn main():\n"
                + "  let number = 3\n"
                + "  echo identity(number)\n",
                "fast-interpreter-call-argument-trace.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String expected = ""
                + "{\"event_id\":1,\"event\":\"LocalWrite\",\"function\":\"main\",\"source_file\":\"fast-interpreter-call-argument-trace.moss\",\"semantic_identity\":\"main@3\",\"line\":4,\"depth\":0,\"detail\":\"number\",\"after\":\"3\"}\n"
                + "{\"event_id\":2,\"event\":\"FunctionEnter\",\"function\":\"identity\",\"source_file\":\"fast-interpreter-call-argument-trace.moss\",\"semantic_identity\":\"fn:identity@1\",\"line\":1,\"depth\":0}\n"
                + "{\"event_id\":3,\"event\":\"LocalRead\",\"function\":\"identity\",\"source_file\":\"fast-interpreter-call-argument-trace.moss\",\"semantic_identity\":\"fn:identity@1\",\"line\":2,\"parent_event_id\":2,\"call_event_id\":2,\"depth\":1,\"detail\":\"value\"}\n"
                + "{\"event_id\":4,\"event\":\"Return\",\"function\":\"identity\",\"source_file\":\"fast-interpreter-call-argument-trace.moss\",\"semantic_identity\":\"fn:identity@1\",\"line\":2,\"parent_event_id\":2,\"call_event_id\":2,\"depth\":1,\"detail\":\"3\"}\n"
                + "{\"event_id\":5,\"event\":\"FunctionExit\",\"function\":\"identity\",\"source_file\":\"fast-interpreter-call-argument-trace.moss\",\"semantic_identity\":\"fn:identity@1\",\"line\":1,\"parent_event_id\":2,\"call_event_id\":2,\"depth\":1}\n";
        if (!expected.equals(trace.toString(StandardCharsets.UTF_8))
                || !"3\n".equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("read-only direct call argument produced an extra trace read: " + trace);
        }
    }

    private static void testNamedDomainStateAndNestedRoutes() {
        var program = Parser.parseSource(""
                + "domain Ledger:\n"
                + "  balance: Int = 10\n"
                + "\n"
                + "  fn Read() -> Int:\n"
                + "    reply balance\n"
                + "domain Inventory:\n"
                + "  domainroutes(ledger: Ledger)\n"
                + "\n"
                + "  fn Available() -> Int:\n"
                + "    reply message ledger.Read()\n"
                + "fn main():\n"
                + "  ledger = Ledger(balance: 42)\n"
                + "  inventory = Inventory(ledger: ledger)\n"
                + "  echo message inventory.Available()\n",
                "fast-interpreter-nested-domain-route.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        if (!"42\n".equals(stdout.toString(StandardCharsets.UTF_8))
                || !events.contains("\"path\":\"balance\"")
                || !events.contains("\"event\":\"message_call\",\"function\":\"Inventory.Available\"")
                || !events.contains("\"call_event_id\":5,\"message_event_id\":6,\"handler_event_id\":5")
                || !events.contains("\"detail\":\"42\"")) {
            throw new AssertionError("named domain state or nested route was not preserved: " + events
                    + " stdout=" + stdout);
        }
    }

    private static void testPipelineStageUsesFunctionCallTrace() {
        var program = Parser.parseSource(""
                + "fn twice(value: Int) -> Int:\n"
                + "  return value * 2\n"
                + "fn main():\n"
                + "  let number = 3\n"
                + "  echo number |> twice\n",
                "fast-interpreter-pipeline-call-trace.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        if (!"6\n".equals(stdout.toString(StandardCharsets.UTF_8))
                || !events.contains("\"event\":\"FunctionEnter\",\"function\":\"twice\"")
                || !events.contains("\"semantic_identity\":\"fn:twice@1\"")
                || !events.contains("\"event\":\"FunctionExit\",\"function\":\"twice\"")) {
            throw new AssertionError("pipeline callback bypassed normal call tracing: " + events);
        }
    }

    private static void testFunctionTraceIncludesSelectedSpecialization() {
        var program = Parser.parseSource(""
                + "fn twice(value):\n"
                + "  return value * 2\n"
                + "fn main():\n"
                + "  echo twice(3)\n",
                "fast-interpreter-specialization-trace.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        if (!"6\n".equals(stdout.toString(StandardCharsets.UTF_8))
                || !events.contains("\"specialization\":\"fn:twice<int>@1\"")) {
            throw new AssertionError("selected function specialization was absent from trace: " + events);
        }
    }

    private static void testNestedDomainFailurePropagatesToCallerRecovery() {
        var program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad(code: Int)\n"
                + "domain Inner:\n"
                + "  fn Run() -> Int:\n"
                + "    raise Failure.Bad(code: 7)\n"
                + "  on_fail Failure.Bad(code):\n"
                + "    reply code + 1\n"
                + "domain Outer:\n"
                + "  domainroutes(inner: Inner)\n"
                + "  fn Run() -> Int:\n"
                + "    try:\n"
                + "      reply message inner.Run()\n"
                + "    recover Failure.Bad(code):\n"
                + "      reply code + 2\n"
                + "fn main():\n"
                + "  inner = Inner()\n"
                + "  outer = Outer(inner: inner)\n"
                + "  echo message inner.Run()\n"
                + "  echo message outer.Run()\n", "fast-interpreter-nested-on-fail.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        if (!"8\n9\n".equals(stdout.toString(StandardCharsets.UTF_8))
                || !events.contains("\"event\":\"recover\",\"function\":\"Outer.Run\"")
                || !events.contains("\"semantic_identity\":\"handler:Outer.Run@")
                || !events.contains("\"detail\":\"9\"")) {
            throw new AssertionError("nested domain failure did not reach caller recovery: "
                    + events + " stdout=" + stdout);
        }
    }

    private static void testCatchAllRecoveryBindsRaisedEnum() {
        var program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "fn fail():\n"
                + "  raise Failure.Bad\n"
                + "fn main():\n"
                + "  try:\n"
                + "    fail()\n"
                + "  recover err:\n"
                + "    match err:\n"
                + "      case Bad:\n"
                + "        echo \"caught\"\n", "fast-interpreter-catchall-recover.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        if (!"caught\n".equals(stdout.toString(StandardCharsets.UTF_8))
                || !trace.toString(StandardCharsets.UTF_8).contains("\"event\":\"BranchTaken\"")) {
            throw new AssertionError("catch-all recover or its match trace was incorrect: "
                    + stdout + " trace=" + trace);
        }
    }

    private static void testOnFailDispatchUsesQualifiedErrorType() {
        var program = Parser.parseSource(""
                + "enum FirstError:\n"
                + "  Bad\n"
                + "enum SecondError:\n"
                + "  Bad\n"
                + "domain Worker:\n"
                + "  fn Run(first: Bool):\n"
                + "    if first:\n"
                + "      raise FirstError.Bad\n"
                + "    else:\n"
                + "      raise SecondError.Bad\n"
                + "  on_fail FirstError.Bad:\n"
                + "    echo \"first\"\n"
                + "  on_fail SecondError.Bad:\n"
                + "    echo \"second\"\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  message worker.Run(true)\n"
                + "  message worker.Run(false)\n", "fast-interpreter-qualified-on-fail.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        new FastInterpreter(program, false,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(new ByteArrayOutputStream())).run();
        if (!"first\nsecond\n".equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("on_fail dispatch used only the short variant name: " + stdout);
        }
    }
}
