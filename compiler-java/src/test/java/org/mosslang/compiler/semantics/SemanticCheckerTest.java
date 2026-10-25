package org.mosslang.compiler.semantics;

import org.mosslang.compiler.ast.Program;
import org.mosslang.compiler.diagnostics.CompileError;
import org.mosslang.compiler.parser.Parser;
import org.mosslang.compiler.types.AccessMode;

public class SemanticCheckerTest {
    public static void main(String[] args) {
        testSameTraitConstraintCanBeForwarded();
        testDifferentTraitConstraintIsNotAssumed();
        testStructuralMethodSignatureIsChecked();
        testTraitMethodArgumentsAreCheckedWithoutInstantiation();
        testRepeatedTraitMethodCallsRemainWellTyped();
        testInferredMethodReturnSatisfiesStructuralTrait();
        testInferredMethodReturnMustMatchStructuralTrait();
        testFunctionParameterAccessModesAreRetained();
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

    private static void testTraitMethodArgumentsAreCheckedWithoutInstantiation() {
        Program program = Parser.parseSource(""
                + "trait Evaluator:\n"
                + "  fn evaluate(value: Float) -> Float\n"
                + "fn invalid(value: Evaluator) -> Float:\n"
                + "  return value.evaluate(true)\n"
                + "fn main():\n"
                + "  pass\n", "trait-method-argument.moss");
        try {
            new SemanticChecker(program).run();
            throw new AssertionError("trait method arguments must be checked in the generic body");
        } catch (CompileError error) {
            if (!"TYPE_MISMATCH".equals(error.getCode())) {
                throw new AssertionError("expected TYPE_MISMATCH, got " + error.getCode(), error);
            }
        }
    }

    private static void testRepeatedTraitMethodCallsRemainWellTyped() {
        Program program = Parser.parseSource(""
                + "trait Evaluator:\n"
                + "  fn evaluate(value: Float) -> Float\n"
                + "type Linear:\n"
                + "  scale: Float\n"
                + "  fn evaluate(value: Float) -> Float:\n"
                + "    return scale * value\n"
                + "fn combine(value: Evaluator, x: Float) -> Float:\n"
                + "  left = value.evaluate(x)\n"
                + "  right = value.evaluate(x + 1.0)\n"
                + "  return left + right\n"
                + "fn main():\n"
                + "  linear = Linear(scale: 2.0)\n"
                + "  result = combine(linear, 1.0)\n", "trait-method-repeated-calls.moss");
        new SemanticChecker(program).run();
    }

    private static void testInferredMethodReturnSatisfiesStructuralTrait() {
        Program program = Parser.parseSource(""
                + "trait Readable:\n"
                + "  fn read() -> Int\n"
                + "type Inferred:\n"
                + "  fn read():\n"
                + "    return 1\n"
                + "fn accept(value: Readable):\n"
                + "  pass\n"
                + "fn relay(value: Inferred):\n"
                + "  accept(value)\n"
                + "fn main():\n"
                + "  item = Inferred()\n"
                + "  relay(item)\n", "trait-inferred-return.moss");
        new SemanticChecker(program).run();
    }

    private static void testInferredMethodReturnMustMatchStructuralTrait() {
        Program program = Parser.parseSource(""
                + "trait Readable:\n"
                + "  fn read() -> Int\n"
                + "type Wrong:\n"
                + "  fn read():\n"
                + "    return \"not an int\"\n"
                + "fn accept(value: Readable):\n"
                + "  pass\n"
                + "fn relay(value: Wrong):\n"
                + "  accept(value)\n"
                + "fn main():\n"
                + "  item = Wrong()\n"
                + "  relay(item)\n", "trait-inferred-return-mismatch.moss");
        try {
            new SemanticChecker(program).run();
            throw new AssertionError("an inferred wrong return type must not satisfy a structural trait");
        } catch (CompileError error) {
            if (!"TYPE_MISMATCH".equals(error.getCode())) {
                throw new AssertionError("expected TYPE_MISMATCH, got " + error.getCode(), error);
            }
        }
    }

    private static void testFunctionParameterAccessModesAreRetained() {
        Program program = Parser.parseSource(""
                + "fn read(value: Int) -> Int:\n"
                + "  return value\n"
                + "fn write(values: Vector[Int]):\n"
                + "  values.push(1)\n"
                + "fn transfer(values: Vector[Int]) -> Vector[Int]:\n"
                + "  return values\n"
                + "fn main():\n"
                + "  pass\n", "parameter-access.moss");
        new SemanticChecker(program).run();
        assertEquals(AccessMode.Read, program.functions.get(0).parameterAccessModes.get(0));
        assertEquals(AccessMode.Write, program.functions.get(1).parameterAccessModes.get(0));
        assertEquals(AccessMode.Consume, program.functions.get(2).parameterAccessModes.get(0));
    }

    private static void assertEquals(Object expected, Object actual) {
        if (!expected.equals(actual)) {
            throw new AssertionError("expected " + expected + ", got " + actual);
        }
    }
}
