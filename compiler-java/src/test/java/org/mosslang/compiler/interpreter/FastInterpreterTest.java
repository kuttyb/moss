package org.mosslang.compiler.interpreter;

import org.mosslang.compiler.parser.Parser;
import org.mosslang.compiler.semantics.CheckedProgram;

import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;

public final class FastInterpreterTest {
    public static void main(String[] args) {
        testFractionalAndMixedNumericComparisons();
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
}
