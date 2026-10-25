package org.mosslang.compiler.interpreter;

import org.mosslang.compiler.parser.Parser;
import org.mosslang.compiler.semantics.CheckedProgram;

import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;

public final class FastInterpreterTest {
    public static void main(String[] args) {
        testFractionalAndMixedNumericComparisons();
        testAssertEqualUsesMossVectorEquality();
        testAssertionFailureEmitsStructuredTrace();
        testIeeeAndMixedNumericEquality();
        testWhileLoopRepeatsCheckedBody();
        testCustomIteratorForLoopUsesOptionProtocol();
        testLocalWriteTraceIsStructuredAndEscaped();
        testMossStringEscapeDecoding();
        testLexicalBlocksRestoreShadowedLocals();
        testRecoveryPayloadBindingIsScoped();
        testForLoopTraceMatchesIterationStructure();
        testVectorForLoopTraceMatchesIterationStructure();
        testBranchTraceCarriesControlContext();
        testFunctionCallTraceCarriesCallContext();
        testCollectionTraceUsesCompactSummary();
        testRaiseRecoverTraceCarriesErrorIdentity();
        testObjectMethodTraceCarriesCallContext();
        testBorrowedDomainObjectTraceKeepsStatePath();
        testNestedDomainStatePathTracesOnlyAtLeaf();
        testIndexedDomainStateTraceKeepsFullPath();
        testMatchCaseBinderShadowDoesNotEscape();
        testDomainReplyCopiesNestedObjectValue();
        testLocalObjectFieldAssignmentTrace();
        testObjectMethodUpdatesScalarField();
        testObjectMethodFieldShadowsSameNamedParameter();
        testSpacedGenericCollectionConstructors();
        testNestedMethodFieldReplacementPrecedesStaleLocalSnapshot();
        testIndexedWriteThroughDottedObjectField();
        testMapGetInsideObjectMethodReturnsStoredOrFallbackValue();
        testMapSizeAndMembershipMethods();
        testMapIndexedWritesTraceReceiverReads();
        testBuiltinMethodReceiverReadIsTraced();
        testEnumLocalWriteUsesCompactTypeSummary();
        testExceptionalFunctionTraceUnwindsCallContext();
        testDomainConstructionTraceIsStructured();
        testDomainConstructionTraceUsesBindingName();
        testDomainMessageTraceCarriesHandlerContext();
        testLocalArgumentTraceOccursInsideCallee();
        testIndexedStringMapReadTraceRetainsSourceLiteral();
        testNamedDomainStateAndNestedRoutes();
        testPipelineStageUsesFunctionCallTrace();
        testFilterPipelineUsesPlaceholder();
        testGeneralPipelineCallbacksAndTerminals();
        testDirectVectorSumUsesMossNumericSemantics();
        testSortAndReverseRuntimeSemantics();
        testCharCodepointMethod();
        testScalarProtocolMethods();
        testTypedVectorFunctionTraceIncludesElementSpecialization();
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

    private static void testAssertEqualUsesMossVectorEquality() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  assertEqual([1, 2, 3], [1, 2, 3])\n", "fast-interpreter-vector-assert-equal.moss");
        new CheckedProgram(program).run();
        new FastInterpreter(program, false,
                new PrintStream(new ByteArrayOutputStream()),
                new PrintStream(new ByteArrayOutputStream())).run();
    }

    private static void testAssertionFailureEmitsStructuredTrace() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  value = 7\n"
                + "  assertEqual(value, 6)\n", "fast-interpreter-assertion-trace.moss");
        if (!"fast-interpreter-assertion-trace.moss".equals(program.main.body.get(1).sourceFile))
            throw new AssertionError("parsed assertion lost its source file: " + program.main.body.get(1).sourceFile);
        new CheckedProgram(program).run();

        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        try {
            new FastInterpreter(program, true,
                    new PrintStream(new ByteArrayOutputStream()),
                    new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
            throw new AssertionError("assertEqual unexpectedly succeeded");
        } catch (RuntimeException expected) {
            if (!expected.getMessage().contains("actual=7, expected=6")) throw expected;
        }
        String expected = "{\"event_id\":3,\"event\":\"AssertionFailure\",\"function\":\"main\","
                + "\"source_file\":\"fast-interpreter-assertion-trace.moss\",\"semantic_identity\":\"main@1\","
                + "\"line\":3,\"depth\":0,\"detail\":\"assertEqual(value, 6)\"}";
        if (!trace.toString(StandardCharsets.UTF_8).contains(expected + "\n")) {
            throw new AssertionError("assertEqual failure trace was missing or malformed: " + trace);
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

    private static void testCustomIteratorForLoopUsesOptionProtocol() {
        var program = Parser.parseSource(""
                + "type Countdown:\n"
                + "  current: Int\n"
                + "  fn next() -> option[Int]:\n"
                + "    if current <= 0:\n"
                + "      return None\n"
                + "    value = current\n"
                + "    current = current - 1\n"
                + "    Some(value)\n"
                + "fn main():\n"
                + "  counter = Countdown(current = 2)\n"
                + "  total = 0\n"
                + "  for value in counter:\n"
                + "    echo value\n"
                + "    total = total + value\n"
                + "  assertEqual(total, 3)\n"
                + "  assertEqual(counter.current, 0)\n",
                "fast-interpreter-custom-iterator.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        String[] lines = events.split("\\R");
        int iterations = 0;
        for (String line : lines) {
            if (!line.contains("\"event\":\"LoopIteration\"")) {
                continue;
            }
            iterations++;
            String eventId = line.split("\"event_id\":", 2)[1].split(",", 2)[0];
            if (!events.contains("\"parent_event_id\":" + eventId
                    + ",\"control_event_id\":" + eventId
                    + ",\"depth\":1,\"detail\":\"value\"")) {
                throw new AssertionError("custom iterator body read lost loop context: " + events);
            }
        }
        if (iterations != 2 || !"2\n1\n".equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("custom iterator trace or output was incorrect: " + events);
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

    private static void testIndexedStringMapReadTraceRetainsSourceLiteral() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  prices = Map()\n"
                + "  prices[\"NVDA\"] = 1\n"
                + "  price = prices[\"NVDA\"]\n", "fast-interpreter-map-read-trace.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(new ByteArrayOutputStream(), true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        if (!trace.toString(StandardCharsets.UTF_8).contains("\"detail\":\"prices[\\\"NVDA\\\"]\""))
            throw new AssertionError("map index trace lost the source string literal: " + trace);
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

    private static void testVectorForLoopTraceMatchesIterationStructure() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  values = [3, 5]\n"
                + "  for value in values:\n"
                + "    echo value\n", "fast-interpreter-vector-for-trace.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        if (events.split("\"event\":\"LoopIteration\"", -1).length != 3
                || !events.contains("\"line\":4,\"parent_event_id\":3,\"control_event_id\":3,\"depth\":1")
                || !events.contains("\"line\":4,\"parent_event_id\":5,\"control_event_id\":5,\"depth\":1")
                || !"3\n5\n".equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("vector for-loop trace omitted iteration context: " + events);
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

    private static void testBorrowedDomainObjectTraceKeepsStatePath() {
        var program = Parser.parseSource(""
                + "type Box:\n"
                + "  title: String\n"
                + "  fn get() -> String:\n"
                + "    return title\n"
                + "fn inspect(value: Box) -> String:\n"
                + "  return value.get()\n"
                + "domain Store:\n"
                + "  value: Box\n"
                + "  fn Read() -> String:\n"
                + "    reply inspect(value)\n"
                + "fn main():\n"
                + "  store = Store(value: Box(title: \"borrowed\"))\n"
                + "  echo message store.Read()\n",
                "fast-interpreter-borrowed-domain-object-trace.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        int functionEnter = events.indexOf("\"event\":\"FunctionEnter\",\"function\":\"inspect\"");
        int borrowedRead = events.indexOf("\"event\":\"state_read\",\"function\":\"inspect\"");
        int methodFieldRead = events.indexOf("\"event\":\"state_read\",\"function\":\"Box.get\"");
        if (functionEnter < 0 || borrowedRead <= functionEnter || methodFieldRead <= borrowedRead
                || !events.substring(borrowedRead).contains("\"path\":\"value\"")
                || !events.substring(methodFieldRead).contains("\"path\":\"value.title\"")
                || !"borrowed\n".equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("borrowed domain object trace lost its state path: " + events);
        }
    }

    private static void testNestedDomainStatePathTracesOnlyAtLeaf() {
        var program = Parser.parseSource(""
                + "type Profile:\n"
                + "  name: String\n"
                + "type Record:\n"
                + "  profile: Profile\n"
                + "domain Store:\n"
                + "  record: Record\n"
                + "  fn Read() -> String:\n"
                + "    reply record.profile.name\n"
                + "fn main():\n"
                + "  store = Store(record: Record(profile: Profile(name: \"nested\")))\n"
                + "  echo message store.Read()\n",
                "fast-interpreter-nested-domain-state-path-trace.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        if (!events.contains("\"event\":\"state_read\"")
                || !events.contains("\"path\":\"record.profile.name\"")
                || events.contains("\"path\":\"record\"")
                || events.contains("\"path\":\"record.profile\"")
                || !"nested\n".equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("nested state path should emit one full-path read: " + events);
        }
    }

    private static void testIndexedDomainStateTraceKeepsFullPath() {
        var program = Parser.parseSource(""
                + "fn consume(value: String):\n"
                + "  echo value\n"
                + "domain Store:\n"
                + "  values: Vector[String]\n"
                + "  fn Read():\n"
                + "    consume(values[0])\n"
                + "    echo values[0]\n"
                + "  fn Write():\n"
                + "    values[0] = \"updated\"\n"
                + "fn main():\n"
                + "  store = Store(values: [\"initial\"])\n"
                + "  message store.Read()\n"
                + "  message store.Write()\n",
                "fast-interpreter-indexed-domain-state-trace.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        int enter = events.indexOf("\"event\":\"FunctionEnter\",\"function\":\"consume\"");
        int calleeRead = events.indexOf("\"event\":\"state_read\",\"function\":\"consume\"");
        int directRead = events.indexOf("\"event\":\"state_read\",\"function\":\"Store.Read\"");
        int indexedWrite = events.indexOf("\"event\":\"state_write\",\"function\":\"Store.Write\"");
        if (enter < 0 || calleeRead <= enter || directRead <= calleeRead || indexedWrite <= directRead
                || !events.substring(calleeRead).contains("\"path\":\"values[0]\"")
                || !events.substring(directRead).contains("\"path\":\"values[0]\"")
                || !events.substring(indexedWrite).contains("\"path\":\"values[0]\"")
                || !"initial\ninitial\n".equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("indexed domain state trace lost its full path: " + events);
        }
    }

    private static void testMatchCaseBinderShadowDoesNotEscape() {
        var program = Parser.parseSource(""
                + "enum Reply:\n"
                + "  Empty\n"
                + "  Pair(code: Int, value: String)\n"
                + "domain Relay:\n"
                + "  fn Bounce(value: Reply) -> Reply:\n"
                + "    match value:\n"
                + "      case Empty:\n"
                + "        pass\n"
                + "      case Pair(code, value):\n"
                + "        echo code\n"
                + "    reply value\n"
                + "fn main():\n"
                + "  relay = Relay()\n"
                + "  input = Reply.Pair(code: 3, value: \"message\")\n"
                + "  output = message relay.Bounce(input)\n"
                + "  match consume output:\n"
                + "    case Empty:\n"
                + "      pass\n"
                + "    case Pair(code, value):\n"
                + "      echo value\n",
                "fast-interpreter-match-binder-shadow.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        if (!"3\nmessage\n".equals(stdout.toString(StandardCharsets.UTF_8))
                || !events.contains("\"event\":\"reply\"")
                || !events.contains("\"event\":\"message_return\"")
                || !events.contains("\"detail\":\"Reply\"")
                || events.contains("\"detail\":\"message\"")) {
            throw new AssertionError("match case shadow escaped its lexical scope: stdout="
                    + stdout.toString(StandardCharsets.UTF_8) + " trace=" + events);
        }
    }

    private static void testDomainReplyCopiesNestedObjectValue() {
        var program = Parser.parseSource(""
                + "type Record:\n"
                + "  count: Int\n"
                + "  values: Vector[Int]\n"
                + "domain Store:\n"
                + "  record: Record\n"
                + "  fn Snapshot() -> Record:\n"
                + "    reply record\n"
                + "  fn Change():\n"
                + "    record.count = 2\n"
                + "    record.values[0] = 9\n"
                + "fn main():\n"
                + "  store = Store(record: Record(count: 1, values: [4]))\n"
                + "  snapshot = message store.Snapshot()\n"
                + "  message store.Change()\n"
                + "  echo snapshot.count, snapshot.values[0]\n",
                "fast-interpreter-domain-reply-copy.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        new FastInterpreter(program, false,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(new ByteArrayOutputStream(), true, StandardCharsets.UTF_8)).run();
        if (!"1 4\n".equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("domain reply aliased mutable state instead of copying it: "
                    + stdout.toString(StandardCharsets.UTF_8));
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

    private static void testObjectMethodUpdatesScalarField() {
        var program = Parser.parseSource(""
                + "type Counter:\n"
                + "  count: Int\n"
                + "  fn increment():\n"
                + "    count = count + 1\n"
                + "  fn value() -> Int:\n"
                + "    return count\n"
                + "fn main():\n"
                + "  counter = Counter(count: 4)\n"
                + "  counter.increment()\n"
                + "  echo counter.value()\n", "fast-interpreter-object-scalar-field-write.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        try (PrintStream captured = new PrintStream(stdout, true, StandardCharsets.UTF_8)) {
            new FastInterpreter(program, true, captured,
                    new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        }
        String actual = stdout.toString(StandardCharsets.UTF_8);
        String events = trace.toString(StandardCharsets.UTF_8);
        if (!"5\n".equals(actual) || !events.contains("\"event\":\"LocalWrite\"")
                || !events.contains("\"detail\":\"count\",\"before\":\"4\",\"after\":\"5\"")) {
            throw new AssertionError("object method scalar field write was lost: " + actual);
        }
    }

    private static void testLocalObjectFieldAssignmentTrace() {
        var program = Parser.parseSource(""
                + "type Envelope:\n"
                + "  counter: Int\n"
                + "fn main():\n"
                + "  wrapped = Envelope(counter: 0)\n"
                + "  wrapped.counter = 7\n"
                + "  echo wrapped.counter\n", "fast-interpreter-local-object-field-trace.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        if (!"7\n".equals(stdout.toString(StandardCharsets.UTF_8))
                || !events.contains("\"event\":\"LocalWrite\",\"function\":\"main\"")
                || !events.contains("\"detail\":\"wrapped.counter\",\"before\":\"0\",\"after\":\"7\"")) {
            throw new AssertionError("local object field assignment trace was missing: " + events);
        }
    }

    private static void testObjectMethodFieldShadowsSameNamedParameter() {
        var program = Parser.parseSource(""
                + "type Box:\n"
                + "  value: Int\n"
                + "  fn set(value: Int):\n"
                + "    value = value + 1\n"
                + "  fn get() -> Int:\n"
                + "    return value\n"
                + "fn main():\n"
                + "  box = Box(value: 10)\n"
                + "  box.set(2)\n"
                + "  echo box.get()\n", "fast-interpreter-object-parameter-shadow.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        try (PrintStream captured = new PrintStream(stdout, true, StandardCharsets.UTF_8)) {
            new FastInterpreter(program, false, captured,
                    new PrintStream(new ByteArrayOutputStream())).run();
        }
        String actual = stdout.toString(StandardCharsets.UTF_8);
        if (!"11\n".equals(actual)) {
            throw new AssertionError("object field binding did not win over a same-named parameter: " + actual);
        }
    }

    private static void testSpacedGenericCollectionConstructors() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  values = Vector [Int]()\n"
                + "  values.push(7)\n"
                + "  echo values[0]\n"
                + "  values [0] = 9\n"
                + "  echo values[0]\n", "fast-interpreter-spaced-vector-constructor.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        try (PrintStream captured = new PrintStream(stdout, true, StandardCharsets.UTF_8)) {
            new FastInterpreter(program, false, captured,
                    new PrintStream(new ByteArrayOutputStream())).run();
        }
        String actual = stdout.toString(StandardCharsets.UTF_8);
        if (!"7\n9\n".equals(actual)) {
            throw new AssertionError("spaced generic Vector constructor did not create a Vector: " + actual);
        }
    }

    private static void testNestedMethodFieldReplacementPrecedesStaleLocalSnapshot() {
        var program = Parser.parseSource(""
                + "type Buffer:\n"
                + "  data: Vector[Int]\n"
                + "  size: Int\n"
                + "  cap: Int\n"
                + "  fn grow():\n"
                + "    data = [1, 2, 3, 4, 5, 6, 7, 8]\n"
                + "    cap = 8\n"
                + "  fn append(value: Int):\n"
                + "    if size == cap:\n"
                + "      grow()\n"
                + "    data[size] = value\n"
                + "    size = size + 1\n"
                + "  fn at(index: Int) -> Int:\n"
                + "    return data[index]\n"
                + "fn main():\n"
                + "  buffer = Buffer(data: [1, 2, 3, 4], size: 4, cap: 4)\n"
                + "  buffer.append(5)\n"
                + "  echo buffer.at(4)\n", "fast-interpreter-nested-object-vector-replacement.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        try (PrintStream captured = new PrintStream(stdout, true, StandardCharsets.UTF_8)) {
            new FastInterpreter(program, false, captured,
                    new PrintStream(new ByteArrayOutputStream())).run();
        }
        String actual = stdout.toString(StandardCharsets.UTF_8);
        if (!"5\n".equals(actual)) {
            throw new AssertionError("indexed write used a stale object field after nested method replacement: " + actual);
        }
    }

    private static void testIndexedWriteThroughDottedObjectField() {
        var program = Parser.parseSource(""
                + "type Holder:\n"
                + "  values: Map[String, Int]\n"
                + "fn main():\n"
                + "  holder = Holder(values: Map())\n"
                + "  holder.values[\"key\"] = 12\n"
                + "  assertEqual(holder.values[\"key\"], 12)\n", "fast-interpreter-dotted-object-index-write.moss");
        new CheckedProgram(program).run();
        new FastInterpreter(program, false,
                new PrintStream(new ByteArrayOutputStream()),
                new PrintStream(new ByteArrayOutputStream())).run();
    }

    private static void testMapGetInsideObjectMethodReturnsStoredOrFallbackValue() {
        var program = Parser.parseSource(""
                + "type Accumulator:\n"
                + "  data: Map [String, Int]\n"
                + "  fn get(key: String) -> Int:\n"
                + "    return data.get(key, 0)\n"
                + "  fn increment_by(key: String, amount: Int) -> Int:\n"
                + "    next = data.get(key, 0) + amount\n"
                + "    data [key] = next\n"
                + "    return next\n"
                + "  fn increment(key: String) -> Int:\n"
                + "    return increment_by(key, 1)\n"
                + "  fn keys() -> Vector [String]:\n"
                + "    return data.keys()\n"
                + "  fn merge(other: Accumulator):\n"
                + "    for key in other.keys():\n"
                + "      self.increment_by(key, other.get(key))\n"
                + "fn main():\n"
                + "  data = Map()\n"
                + "  data [\"present\"] = 5\n"
                + "  accumulator = Accumulator(data: data)\n"
                + "  echo accumulator.increment(\"present\")\n"
                + "  echo accumulator.get(\"present\")\n"
                + "  echo accumulator.get(\"missing\")\n"
                + "  otherData = Map()\n"
                + "  otherData [\"present\"] = 2\n"
                + "  other = Accumulator(data: otherData)\n"
                + "  accumulator.merge(other)\n"
                + "  echo accumulator.get(\"present\")\n",
                "fast-interpreter-object-map-get.moss");
        new CheckedProgram(program).run();
        var getMethod = program.objects.get(0).methods.stream()
                .filter(method -> method.name.equals("get")).findFirst().orElseThrow();
        if (getMethod.parameterAccessModes.size() != 1
                || getMethod.parameterAccessModes.get(0) != org.mosslang.compiler.types.AccessMode.Read) {
            throw new AssertionError("Map.get key parameter should remain a read-only method argument");
        }

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        new FastInterpreter(program, false,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(new ByteArrayOutputStream())).run();
        String actual = stdout.toString(StandardCharsets.UTF_8);
        if (!"6\n6\n0\n8\n".equals(actual)) {
            throw new AssertionError("Map.get inside an object method returned incorrect values: " + actual);
        }
    }

    private static void testMapSizeAndMembershipMethods() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  values = Map()\n"
                + "  values[\"present\"] = 9\n"
                + "  echo values.len()\n"
                + "  echo values.size()\n"
                + "  echo values.count()\n"
                + "  echo values.contains(\"present\")\n"
                + "  echo values.hasKey(\"present\")\n"
                + "  echo values.contains(\"missing\")\n",
                "fast-interpreter-map-size-membership.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        new FastInterpreter(program, false,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(new ByteArrayOutputStream())).run();
        String actual = stdout.toString(StandardCharsets.UTF_8);
        if (!"1\n1\n1\ntrue\ntrue\nfalse\n".equals(actual)) {
            throw new AssertionError("Map size or membership methods returned incorrect results: " + actual);
        }
    }

    private static void testMapIndexedWritesTraceReceiverReads() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  values = Map()\n"
                + "  values[\"alpha\"] = 3\n"
                + "  values[\"beta\"] = 7\n"
                + "  for key in values.keys():\n"
                + "    echo key\n"
                + "  for value in values.values():\n"
                + "    echo value\n",
                "fast-interpreter-map-loop-trace.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String expectedTrace = ""
                + "{\"event_id\":1,\"event\":\"LocalWrite\",\"function\":\"main\",\"source_file\":\"fast-interpreter-map-loop-trace.moss\",\"semantic_identity\":\"main@1\",\"line\":2,\"depth\":0,\"detail\":\"values\",\"after\":\"Map(size=0)\"}\n"
                + "{\"event_id\":2,\"event\":\"LocalRead\",\"function\":\"main\",\"source_file\":\"fast-interpreter-map-loop-trace.moss\",\"semantic_identity\":\"main@1\",\"line\":3,\"depth\":0,\"detail\":\"values\"}\n"
                + "{\"event_id\":3,\"event\":\"LocalRead\",\"function\":\"main\",\"source_file\":\"fast-interpreter-map-loop-trace.moss\",\"semantic_identity\":\"main@1\",\"line\":4,\"depth\":0,\"detail\":\"values\"}\n"
                + "{\"event_id\":4,\"event\":\"LocalRead\",\"function\":\"main\",\"source_file\":\"fast-interpreter-map-loop-trace.moss\",\"semantic_identity\":\"main@1\",\"line\":5,\"depth\":0,\"detail\":\"values\"}\n"
                + "{\"event_id\":5,\"event\":\"LoopIteration\",\"function\":\"main\",\"source_file\":\"fast-interpreter-map-loop-trace.moss\",\"semantic_identity\":\"main@1\",\"line\":5,\"depth\":0,\"detail\":\"1\"}\n"
                + "{\"event_id\":6,\"event\":\"LocalRead\",\"function\":\"main\",\"source_file\":\"fast-interpreter-map-loop-trace.moss\",\"semantic_identity\":\"main@1\",\"line\":6,\"parent_event_id\":5,\"control_event_id\":5,\"depth\":1,\"detail\":\"key\"}\n"
                + "{\"event_id\":7,\"event\":\"LoopIteration\",\"function\":\"main\",\"source_file\":\"fast-interpreter-map-loop-trace.moss\",\"semantic_identity\":\"main@1\",\"line\":5,\"depth\":0,\"detail\":\"2\"}\n"
                + "{\"event_id\":8,\"event\":\"LocalRead\",\"function\":\"main\",\"source_file\":\"fast-interpreter-map-loop-trace.moss\",\"semantic_identity\":\"main@1\",\"line\":6,\"parent_event_id\":7,\"control_event_id\":7,\"depth\":1,\"detail\":\"key\"}\n"
                + "{\"event_id\":9,\"event\":\"LocalRead\",\"function\":\"main\",\"source_file\":\"fast-interpreter-map-loop-trace.moss\",\"semantic_identity\":\"main@1\",\"line\":7,\"depth\":0,\"detail\":\"values\"}\n"
                + "{\"event_id\":10,\"event\":\"LoopIteration\",\"function\":\"main\",\"source_file\":\"fast-interpreter-map-loop-trace.moss\",\"semantic_identity\":\"main@1\",\"line\":7,\"depth\":0,\"detail\":\"1\"}\n"
                + "{\"event_id\":11,\"event\":\"LocalRead\",\"function\":\"main\",\"source_file\":\"fast-interpreter-map-loop-trace.moss\",\"semantic_identity\":\"main@1\",\"line\":8,\"parent_event_id\":10,\"control_event_id\":10,\"depth\":1,\"detail\":\"value\"}\n"
                + "{\"event_id\":12,\"event\":\"LoopIteration\",\"function\":\"main\",\"source_file\":\"fast-interpreter-map-loop-trace.moss\",\"semantic_identity\":\"main@1\",\"line\":7,\"depth\":0,\"detail\":\"2\"}\n"
                + "{\"event_id\":13,\"event\":\"LocalRead\",\"function\":\"main\",\"source_file\":\"fast-interpreter-map-loop-trace.moss\",\"semantic_identity\":\"main@1\",\"line\":8,\"parent_event_id\":12,\"control_event_id\":12,\"depth\":1,\"detail\":\"value\"}\n";
        if (!"alpha\nbeta\n3\n7\n".equals(stdout.toString(StandardCharsets.UTF_8))
                || !expectedTrace.equals(trace.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("Map indexed-write receiver reads or key-loop trace were incorrect: "
                    + trace.toString(StandardCharsets.UTF_8));
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

    private static void testDomainConstructionTraceUsesBindingName() {
        var program = Parser.parseSource(""
                + "domain Reader:\n"
                + "  fn Read():\n"
                + "    echo \"read\"\n"
                + "fn main():\n"
                + "  child = Reader()\n"
                + "  message child.Read()\n",
                "fast-interpreter-domain-binding-trace.moss");
        new CheckedProgram(program).run();

        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(new ByteArrayOutputStream(), true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        if (!events.contains("\"event\":\"domain_instance\"")
                || !events.contains("\"instance\":\"main::child\"")
                || !events.contains("\"specialization\":\"domain-specialization:Reader:child\"")
                || !events.contains("\"event\":\"message_call\"")
                || !events.contains("\"handler\":\"Read\"")) {
            throw new AssertionError("domain instance trace ignored the source binding: " + events);
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

    private static void testFilterPipelineUsesPlaceholder() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  let values = [1, 2, 3]\n"
                + "  let selected = values |> filter(_ > 2)\n"
                + "  echo selected |> sum\n", "fast-interpreter-filter-pipeline.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        new FastInterpreter(program, false,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(new ByteArrayOutputStream())).run();
        if (!"3\n".equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("filter pipeline did not retain values matching its placeholder: " + stdout);
        }

        var namedProgram = Parser.parseSource(""
                + "fn above_two(value: Int) -> Bool:\n"
                + "  return value > 2\n"
                + "fn add(a: Int, b: Int) -> Int:\n"
                + "  return a + b\n"
                + "fn main():\n"
                + "  let threshold = 1\n"
                + "  echo [1, 2, 3] |> filter(_ > threshold) |> map(_ + threshold) |> reduce(0, add)\n",
                "fast-interpreter-filter-named-callback.moss");
        new CheckedProgram(namedProgram).run();
        ByteArrayOutputStream namedOutput = new ByteArrayOutputStream();
        new FastInterpreter(namedProgram, false,
                new PrintStream(namedOutput, true, StandardCharsets.UTF_8),
                new PrintStream(new ByteArrayOutputStream())).run();
        if (!"7\n".equals(namedOutput.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("map/filter pipeline mishandled named or inline callbacks: " + namedOutput);
        }
    }

    private static void testGeneralPipelineCallbacksAndTerminals() {
        var program = Parser.parseSource(""
                + "type Scaler:\n"
                + "  factor: Int\n"
                + "  fn apply(value: Int) -> Int:\n"
                + "    return value * factor\n"
                + "type Reading:\n"
                + "  sensor_id: Int\n"
                + "type VisitCounter:\n"
                + "  visited: Int\n"
                + "  fn positive(value: Int) -> Bool:\n"
                + "    return value > 0\n"
                + "fn double(value: Int) -> Int:\n"
                + "  return value * 2\n"
                + "fn add_bias(value: Int, bias: Int) -> Int:\n"
                + "  return value + bias\n"
                + "fn transform(values, operation):\n"
                + "  return values |> map(operation)\n"
                + "fn main():\n"
                + "  values = [1, 2, 3]\n"
                + "  doubled = transform(values, double)\n"
                + "  scaler = Scaler(factor: 4)\n"
                + "  scaled_total = values |> map(scaler.apply) |> sum\n"
                + "  biased = 3 |> add_bias(2)\n"
                + "  found = values |> any(_ == 2)\n"
                + "  all_positive = values |> all(_ > 0)\n"
                + "  counter = VisitCounter(visited: 0)\n"
                + "  bound_any = values |> any(counter.positive)\n"
                + "  flags = [true, false, true]\n"
                + "  any_flag = flags |> any\n"
                + "  all_flags = flags |> all\n"
                + "  readings = [Reading(sensor_id: 101), Reading(sensor_id: 103)]\n"
                + "  sensor_ids = readings |> map(_.sensor_id)\n"
                + "  echo doubled |> sum\n"
                + "  echo scaled_total\n"
                + "  echo biased\n"
                + "  echo found\n"
                + "  echo all_positive\n"
                + "  echo bound_any, counter.visited\n"
                + "  echo any_flag\n"
                + "  echo all_flags\n"
                + "  echo sensor_ids[0], sensor_ids[1]\n", "fast-interpreter-general-pipeline.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        new FastInterpreter(program, false,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(new ByteArrayOutputStream())).run();
        String expected = "12\n24\n5\ntrue\ntrue\ntrue 0\ntrue\nfalse\n101 103\n";
        if (!expected.equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("general pipeline callback or terminal result was wrong: " + stdout);
        }
    }

    private static void testDirectVectorSumUsesMossNumericSemantics() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  ints = [9223372036854775807, 1]\n"
                + "  floats = [0.5, 1.25]\n"
                + "  echo sum(ints)\n"
                + "  echo sum(floats)\n", "fast-interpreter-direct-sum.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        new FastInterpreter(program, false,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(new ByteArrayOutputStream())).run();
        if (!"-9223372036854775808\n1.75\n".equals(stdout.toString(StandardCharsets.UTF_8))) {
            throw new AssertionError("direct vector sum did not preserve wrapping Int and Float results: "
                    + stdout.toString(StandardCharsets.UTF_8));
        }
    }

    private static void testSortAndReverseRuntimeSemantics() {
        var program = Parser.parseSource(""
                + "fn negate(value: Int) -> Int:\n"
                + "  return 0 - value\n"
                + "fn parity(value: Int) -> Int:\n"
                + "  return value % 2\n"
                + "fn main():\n"
                + "  var direct = [5, 2, 8, 1, 9]\n"
                + "  sort(direct)\n"
                + "  echo direct[0], direct[1], direct[2], direct[3], direct[4]\n"
                + "  var keyed = [1, 2, 3, 4, 5]\n"
                + "  sort(keyed, by: negate)\n"
                + "  echo keyed[0], keyed[1], keyed[2], keyed[3], keyed[4]\n"
                + "  var reversed = [5, 1, 4, 2]\n"
                + "  sort(reversed, by: Reverse(negate))\n"
                + "  echo reversed[0], reversed[1], reversed[2], reversed[3]\n"
                + "  var stable = [3, 1, 5, 2, 4]\n"
                + "  sort(stable, by: parity)\n"
                + "  echo stable[0], stable[1], stable[2], stable[3], stable[4]\n"
                + "  nan = 0.0 / 0.0\n"
                + "  var floats = [nan, 1.0, 0.0, -0.0, -10.0]\n"
                + "  sort(floats)\n"
                + "  echo floats[0], floats[1], floats[2], floats[3], floats[4]\n",
                "fast-interpreter-sort.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        new FastInterpreter(program, false, new PrintStream(stdout),
                new PrintStream(new ByteArrayOutputStream())).run();
        String expected = "1 2 5 8 9\n5 4 3 2 1\n1 2 4 5\n2 4 3 1 5\n-10.0 -0.0 0.0 1.0 NaN\n";
        String actual = stdout.toString(StandardCharsets.UTF_8);
        if (!expected.equals(actual)) {
            throw new AssertionError("sort/reverse produced unexpected output: " + actual);
        }
    }

    private static void testCharCodepointMethod() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  let letter = '\\x41'\n"
                + "  echo letter.codepoint()\n"
                + "  echo '\\x2C'.codepoint()\n", "fast-interpreter-char-codepoint.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        new FastInterpreter(program, false, new PrintStream(stdout),
                new PrintStream(new ByteArrayOutputStream())).run();
        String expected = "65\n44\n";
        String actual = stdout.toString(StandardCharsets.UTF_8);
        if (!expected.equals(actual))
            throw new AssertionError("Char.codepoint() returned unexpected values: " + actual);
    }

    private static void testScalarProtocolMethods() {
        var program = Parser.parseSource(""
                + "fn main():\n"
                + "  echo 2.less(3)\n"
                + "  echo 2.isequal(2)\n"
                + "  echo 4.hash()\n"
                + "  echo 4.to_string()\n"
                + "  echo 1.5.less(2.0)\n"
                + "  echo 'A'.less('B')\n"
                + "  echo \"a\".less(\"b\")\n"
                + "  echo true.isequal(true)\n"
                + "  echo 0.0.isequal(-0.0)\n"
                + "  nan = 0.0 / 0.0\n"
                + "  echo nan.isequal(nan)\n", "fast-interpreter-scalar-protocol.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        new FastInterpreter(program, false, new PrintStream(stdout),
                new PrintStream(new ByteArrayOutputStream())).run();
        String expected = "true\ntrue\n4\n4\ntrue\ntrue\ntrue\ntrue\nfalse\ntrue\n";
        String actual = stdout.toString(StandardCharsets.UTF_8);
        if (!expected.equals(actual))
            throw new AssertionError("scalar protocol methods returned unexpected output: " + actual);
    }

    private static void testTypedVectorFunctionTraceIncludesElementSpecialization() {
        var program = Parser.parseSource(""
                + "fn summarize(values: Vector) -> Int:\n"
                + "  return values |> sum\n"
                + "fn main():\n"
                + "  echo summarize([1, 2])\n", "fast-interpreter-vector-specialization-trace.moss");
        new CheckedProgram(program).run();
        ByteArrayOutputStream stdout = new ByteArrayOutputStream();
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        new FastInterpreter(program, true,
                new PrintStream(stdout, true, StandardCharsets.UTF_8),
                new PrintStream(trace, true, StandardCharsets.UTF_8)).run();
        String events = trace.toString(StandardCharsets.UTF_8);
        if (!"3\n".equals(stdout.toString(StandardCharsets.UTF_8))
                || !events.contains("\"specialization\":\"fn:summarize<vector[int]>@1\"")) {
            throw new AssertionError("typed vector trace omitted its element specialization: " + events);
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
