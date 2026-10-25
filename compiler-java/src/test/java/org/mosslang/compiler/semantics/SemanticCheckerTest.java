package org.mosslang.compiler.semantics;

import org.mosslang.compiler.ast.Program;
import org.mosslang.compiler.diagnostics.CompileError;
import org.mosslang.compiler.parser.Parser;

public class SemanticCheckerTest {
    public static void main(String[] args) {
        testSameTraitConstraintCanBeForwarded();
        testDifferentTraitConstraintIsNotAssumed();
        testStructuralMethodSignatureIsChecked();
        System.out.println("SemanticCheckerTest PASSED.");
    }

    private static void testSameTraitConstraintCanBeForwarded() {
        Program program = Parser.parseSource(""
                + "trait Readable:\n"
                + "  fn read() -> Int\n"
                + "fn inner(value: Readable) -> Int:\n"
                + "  return value.read()\n"
                + "fn outer(value: Readable) -> Int:\n"
                + "  return inner(value)\n"
                + "fn main():\n"
                + "  pass\n", "trait-forwarding.moss");
        new SemanticChecker(program).run();
    }

    private static void testDifferentTraitConstraintIsNotAssumed() {
        Program program = Parser.parseSource(""
                + "trait Readable:\n"
                + "  fn read() -> Int\n"
                + "trait Writable:\n"
                + "  fn write(value: Int)\n"
                + "fn forward(value: Readable):\n"
                + "  consume(value)\n"
                + "fn consume(value: Writable):\n"
                + "  value.write(1)\n"
                + "fn main():\n"
                + "  pass\n", "trait-mismatch.moss");
        try {
            new SemanticChecker(program).run();
            throw new AssertionError("a Readable constraint must not satisfy Writable");
        } catch (CompileError error) {
            if (!"TYPE_MISMATCH".equals(error.getCode())) {
                throw new AssertionError("expected TYPE_MISMATCH, got " + error.getCode(), error);
            }
        }
    }

    private static void testStructuralMethodSignatureIsChecked() {
        Program program = Parser.parseSource(""
                + "trait Readable:\n"
                + "  fn read(value: Int) -> Int\n"
                + "type Wrong:\n"
                + "  fn read(value: String) -> Int:\n"
                + "    return 1\n"
                + "fn accept(value: Readable):\n"
                + "  pass\n"
                + "fn main():\n"
                + "  wrong = Wrong()\n"
                + "  accept(wrong)\n", "trait-signature.moss");
        try {
            new SemanticChecker(program).run();
            throw new AssertionError("a method name alone must not satisfy a structural trait");
        } catch (CompileError error) {
            if (!"TYPE_MISMATCH".equals(error.getCode())) {
                throw new AssertionError("expected TYPE_MISMATCH, got " + error.getCode(), error);
            }
        }
    }
}
