package org.mosslang.compiler.semantics;

import org.mosslang.compiler.ast.Program;
import org.mosslang.compiler.ast.Function;
import org.mosslang.compiler.backend.NativeGenerator;
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
        testFunctionRetainsResolvedOpenParameterTypes();
        testGenericNumericOperationRetainsOpenTypeUntilSpecialization();
        testMixedIntFloatComparisons();
        testObjectMethodAccessModesAreRetained();
        testForLoopValuesCanFlowToObjectMethodArguments();
        testObjectMethodArityIsChecked();
        testObjectReceiverWriteEffectPropagatesToFunctionParameter();
        testConsumedObjectParameterLowersByValue();
        testObjectMethodWriteParameterLowersMutably();
        testNestedObjectFieldWriteLowersMutableReceiver();
        testUnsupportedCollectionsAreRejectedAsForIterablesAtLoopSite();
        testTypedOnFailLocalCaptureLowering();
        testOnFailRejectsLocalInitializedAfterRaise();
        testOnFailCaptureUsesMatchingRaisedVariant();
        testOwnedMatchPayloadCanBeOnFailCapture();
        testBorrowedMatchPayloadCannotBeOnFailCapture();
        testSameCrateRaisingCallPreservesErrorCarrier();
        testOnFailCaptureUsesArgumentEvaluationRaiseEdge();
        testOnFailCannotConsumeDomainState();
        testOnFailRejectsDistinctBranchBindings();
        System.out.println("SemanticCheckerTest PASSED.");
    }

    private static void testFunctionRetainsResolvedOpenParameterTypes() {
        Program program = Parser.parseSource(""
                + "fn identity(value):\n"
                + "  return value\n"
                + "fn main():\n"
                + "  number = identity(1)\n"
                + "  text = identity(\"x\")\n", "open-parameter-facts.moss");
        new SemanticChecker(program).run();
        Function identity = program.functions.stream()
                .filter(function -> function.name.equals("identity"))
                .findFirst().orElseThrow();
        if (identity.params.size() != 1 || identity.params.get(0).checkedTypeRef == null
                || identity.params.get(0).checkedTypeRef.kind()
                        != org.mosslang.compiler.types.TypeKind.InferenceVar
                || identity.returnTypeRef == null
                || identity.returnTypeRef.kind() != org.mosslang.compiler.types.TypeKind.InferenceVar
                || !identity.params.get(0).checkedTypeRef.varId().equals(identity.returnTypeRef.varId())) {
            throw new AssertionError("checker did not retain the shared open type of identity's parameter/result: "
                    + identity.params.get(0).checkedTypeRef + " -> " + identity.returnTypeRef);
        }
        if (identity.params.get(0).typeRef != null
                || identity.params.get(0).type == null || !identity.params.get(0).type.isEmpty()) {
            throw new AssertionError("checker facts must not masquerade as a source annotation");
        }
        if (!identity.specializations.stream().anyMatch(s -> s.parameterTypes.contains("int"))
                || !identity.specializations.stream().anyMatch(s -> s.parameterTypes.contains("string"))
                || identity.specializations.stream().map(s -> s.stableTypeKey).distinct().count() != 2
                || !identity.specializations.stream().anyMatch(s -> s.returnTypeRef != null
                        && "int".equals(s.returnTypeRef.toCanonicalString()))
                || !identity.specializations.stream().anyMatch(s -> s.returnTypeRef != null
                        && "string".equals(s.returnTypeRef.toCanonicalString()))) {
            throw new AssertionError("generic identity calls did not retain both concrete argument/result types: "
                    + identity.specializations.stream().map(s -> s.parameterTypes.toString()).toList());
        }
    }

    private static void testGenericNumericOperationRetainsOpenTypeUntilSpecialization() {
        Program program = Parser.parseSource(""
                + "fn plus_one(value):\n"
                + "  return value + 1\n"
                + "fn main():\n"
                + "  integer = plus_one(1)\n"
                + "  floating = plus_one(1.5)\n", "open-numeric-requirement.moss");
        new SemanticChecker(program).run();
        Function plusOne = program.functions.stream()
                .filter(function -> function.name.equals("plus_one"))
                .findFirst().orElseThrow();
        if (plusOne.params.get(0).checkedTypeRef == null
                || plusOne.params.get(0).checkedTypeRef.kind()
                        != org.mosslang.compiler.types.TypeKind.InferenceVar
                || plusOne.returnTypeRef == null
                || plusOne.returnTypeRef.kind() != org.mosslang.compiler.types.TypeKind.InferenceVar) {
            throw new AssertionError("generic arithmetic closed its open parameter/result to a literal type: "
                    + plusOne.params.get(0).checkedTypeRef + " -> " + plusOne.returnTypeRef);
        }
        if (!plusOne.specializations.stream().anyMatch(s -> s.parameterTypes.contains("int")
                    && s.returnTypeRef != null && "int".equals(s.returnTypeRef.toCanonicalString()))
                || !plusOne.specializations.stream().anyMatch(s -> s.parameterTypes.contains("float")
                    && s.returnTypeRef != null && "float".equals(s.returnTypeRef.toCanonicalString()))) {
            throw new AssertionError("generic arithmetic did not specialize for both Int and Float: "
                    + plusOne.specializations.stream().map(s -> s.parameterTypes + " -> " + s.returnType).toList());
        }

        Program invalid = Parser.parseSource(""
                + "fn plus_one(value):\n"
                + "  return value + 1\n"
                + "fn main():\n"
                + "  invalid = plus_one(\"x\")\n", "invalid-generic-numeric-operation.moss");
        try {
            new SemanticChecker(invalid).run();
            throw new AssertionError("generic numeric operation accepted a String specialization");
        } catch (CompileError error) {
            if (!"TYPE_AMBIGUOUS_OPEN_RESULT".equals(error.getCode()) || error.getLine() != 4) throw error;
        }
    }

    private static void testMixedIntFloatComparisons() {
        Program program = Parser.parseSource(""
                + "fn main():\n"
                + "  greater = 2.0 > 1\n"
                + "  less = 1 < 1.5\n"
                + "  equal = 2 == 2.0\n", "mixed-numeric-comparisons.moss");
        new SemanticChecker(program).run();

        Program invalid = Parser.parseSource(""
                + "fn main():\n"
                + "  invalid = true < 1.0\n", "invalid-mixed-comparison.moss");
        try {
            new SemanticChecker(invalid).run();
            throw new AssertionError("mixed Bool/Float comparison was accepted");
        } catch (CompileError error) {
            if (!"TYPE_MISMATCH".equals(error.getCode())) throw error;
        }
    }

    private static void testTypedOnFailLocalCaptureLowering() {
        Program program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "domain Worker:\n"
                + "  fn Run() -> String:\n"
                + "    var payload = \"kept\"\n"
                + "    raise Failure.Bad\n"
                + "  on_fail Failure.Bad:\n"
                + "    reply payload\n"
                + "  fn Count() -> Int:\n"
                + "    var count = 17\n"
                + "    raise Failure.Bad\n"
                + "  on_fail Failure.Bad:\n"
                + "    reply count\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  echo message worker.Run()\n"
                + "  echo message worker.Count()\n", "handler-capture.moss");
        new SemanticChecker(program).run();
        String rust = NativeGenerator.generate(program, false);
        if (!rust.contains("captures.insert(\"payload\".to_string(), Box::new(payload.clone()))")
                || !rust.contains("get_capture::<MossStr>(\"payload\")")
                || !rust.contains("captures.insert(\"count\".to_string(), Box::new(count.clone()))")
                || !rust.contains("get_capture::<i64>(\"count\")")
                || !rust.contains("pub trait __MossCloneCapture")) {
            throw new AssertionError("on_fail did not preserve and restore a typed local capture: " + rust);
        }
        if (rust.contains("payload.to_string()")) {
            throw new AssertionError("on_fail local capture was serialized as display text");
        }
    }

    private static void testOnFailRejectsLocalInitializedAfterRaise() {
        Program program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "domain Worker:\n"
                + "  fn Run() -> String:\n"
                + "    raise Failure.Bad\n"
                + "    var payload = \"late\"\n"
                + "  on_fail Failure.Bad:\n"
                + "    reply payload\n"
                + "fn main():\n"
                + "  pass\n", "uninitialized-handler-capture.moss");
        try {
            new SemanticChecker(program).run();
            throw new AssertionError("on_fail must reject a local initialized after the raise");
        } catch (CompileError error) {
            if (!"ON_FAIL_CAPTURE_NOT_INITIALIZED".equals(error.getCode())) {
                throw new AssertionError("expected ON_FAIL_CAPTURE_NOT_INITIALIZED, got "
                        + error.getCode(), error);
            }
        }
    }

    private static void testOnFailCaptureUsesMatchingRaisedVariant() {
        Program program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "  Other\n"
                + "domain Worker:\n"
                + "  fn Run(flag: Bool) -> String:\n"
                + "    if flag:\n"
                + "      var payload = \"bad\"\n"
                + "      raise Failure.Bad\n"
                + "    else:\n"
                + "      raise Failure.Other\n"
                + "  on_fail Failure.Bad:\n"
                + "    reply payload\n"
                + "  on_fail Failure.Other:\n"
                + "    reply \"other\"\n"
                + "fn main():\n"
                + "  pass\n", "variant-specific-handler-capture.moss");
        new SemanticChecker(program).run();
    }

    private static void testOwnedMatchPayloadCanBeOnFailCapture() {
        Program program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "enum Data:\n"
                + "  Have(value: String)\n"
                + "domain Worker:\n"
                + "  fn Run() -> String:\n"
                + "    data = Data.Have(value: \"hello\")\n"
                + "    match consume data:\n"
                + "      case Have(value):\n"
                + "        raise Failure.Bad\n"
                + "  on_fail Failure.Bad:\n"
                + "    reply value\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  echo message worker.Run()\n", "owned-match-on-fail-capture.moss");
        new SemanticChecker(program).run();
        if (!program.domains.get(0).handlers.get(0).failureArms.get(0)
                .checkedCaptures.containsKey("value")) {
            throw new AssertionError("owned match payload was not captured on the raise edge");
        }
    }

    private static void testBorrowedMatchPayloadCannotBeOnFailCapture() {
        Program program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "enum Data:\n"
                + "  Have(value: String)\n"
                + "domain Worker:\n"
                + "  fn Run() -> String:\n"
                + "    data = Data.Have(value: \"hello\")\n"
                + "    match data:\n"
                + "      case Have(value):\n"
                + "        raise Failure.Bad\n"
                + "  on_fail Failure.Bad:\n"
                + "    reply value\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  echo message worker.Run()\n", "borrowed-match-on-fail-capture.moss");
        try {
            new SemanticChecker(program).run();
            throw new AssertionError("borrowed match payload cannot escape into on_fail");
        } catch (CompileError error) {
            if (!"ON_FAIL_CAPTURE_NOT_OWNED".equals(error.getCode())) {
                throw new AssertionError("expected ON_FAIL_CAPTURE_NOT_OWNED, got "
                        + error.getCode(), error);
            }
        }
    }

    private static void testSameCrateRaisingCallPreservesErrorCarrier() {
        Program program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "fn failer() -> Int:\n"
                + "  raise Failure.Bad\n"
                + "fn middle() -> Int:\n"
                + "  return failer()\n"
                + "fn caller() -> Int:\n"
                + "  let value = middle()\n"
                + "  return value\n"
                + "fn literal() -> String:\n"
                + "  return \"failer()\"\n"
                + "fn main():\n"
                + "  echo caller()\n", "raise-carrier-propagation.moss");
        new SemanticChecker(program).run();
        String rust = NativeGenerator.generate(program, false);
        if (!rust.contains("fn middle() -> Result<i64, __MossRaised>")
                || !rust.contains("fn caller() -> Result<i64, __MossRaised>")
                || !rust.contains("middle()?")
                || !rust.contains("failer()?")
                || !rust.contains("fn literal() -> MossStr")
                || rust.contains("fn literal() -> Result<MossStr, __MossRaised>")) {
            throw new AssertionError("same-crate raising call must preserve the existing error carrier: " + rust);
        }
    }

    private static void testOnFailCaptureUsesArgumentEvaluationRaiseEdge() {
        Program program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "fn fail() -> Int:\n"
                + "  raise Failure.Bad\n"
                + "fn consume_after(number: Int, data: String) -> String:\n"
                + "  return data\n"
                + "domain Worker:\n"
                + "  fn Run() -> Int:\n"
                + "    var data = \"abc\"\n"
                + "    result = consume_after(fail(), data)\n"
                + "    reply 0\n"
                + "  on_fail Failure.Bad:\n"
                + "    reply data.length()\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  echo message worker.Run()\n", "raise-before-consume-capture.moss");
        new SemanticChecker(program).run();
        if (!program.domains.get(0).handlers.get(0).failureArms.get(0)
                .checkedCaptures.containsKey("data")) {
            throw new AssertionError("argument raise edge did not preserve the pre-transfer local");
        }
    }

    private static void testOnFailCannotConsumeDomainState() {
        Program program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "domain Worker:\n"
                + "  value = \"hello\"\n"
                + "  fn Run() -> String:\n"
                + "    local = value\n"
                + "    raise Failure.Bad\n"
                + "  on_fail Failure.Bad:\n"
                + "    reply local\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  echo message worker.Run()\n", "on-fail-domain-state-consume.moss");
        try {
            new SemanticChecker(program).run();
            throw new AssertionError("domain state cannot be consumed into a handler local");
        } catch (CompileError error) {
            if (!"MOSS_COMPILE_ERROR".equals(error.getCode())) {
                throw new AssertionError("expected MOSS_COMPILE_ERROR, got " + error.getCode(), error);
            }
        }
    }

    private static void testOnFailRejectsDistinctBranchBindings() {
        Program program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "domain Worker:\n"
                + "  fn Run(flag: Bool) -> String:\n"
                + "    if flag:\n"
                + "      let item = \"yes\"\n"
                + "      raise Failure.Bad\n"
                + "    else:\n"
                + "      let item = \"no\"\n"
                + "      raise Failure.Bad\n"
                + "  on_fail Failure.Bad:\n"
                + "    reply item\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  echo message worker.Run(true)\n", "on-fail-branch-binding-mismatch.moss");
        try {
            new SemanticChecker(program).run();
            throw new AssertionError("on_fail cannot capture distinct branch-local bindings by name");
        } catch (CompileError error) {
            if (!"ON_FAIL_CAPTURE_BINDING_MISMATCH".equals(error.getCode())) {
                throw new AssertionError("expected ON_FAIL_CAPTURE_BINDING_MISMATCH, got "
                        + error.getCode(), error);
            }
        }
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

    private static void testObjectMethodAccessModesAreRetained() {
        Program program = Parser.parseSource(""
                + "type Box:\n"
                + "  values: Vector[Int]\n"
                + "  fn replace(next: Vector[Int]):\n"
                + "    values = next\n"
                + "fn main():\n"
                + "  pass\n", "method-access-modes.moss");
        new SemanticChecker(program).run();
        var method = program.objects.get(0).methods.get(0);
        if (method.receiverAccessMode != AccessMode.Write
                || method.parameterAccessModes.size() != 1
                || method.parameterAccessModes.get(0) != AccessMode.Consume) {
            throw new AssertionError("object method receiver/parameter access modes were not retained");
        }
    }

    private static void testForLoopValuesCanFlowToObjectMethodArguments() {
        Program program = Parser.parseSource(""
                + "type Accumulator:\n"
                + "  data: Map [String, Int]\n"
                + "  fn keys() -> Vector [String]:\n"
                + "    return data.keys()\n"
                + "  fn increment_by(key: String, amount: Int) -> Int:\n"
                + "    next = data.get(key, 0) + amount\n"
                + "    data [key] = next\n"
                + "    return next\n"
                + "  fn merge(other: Accumulator):\n"
                + "    for key in other.keys():\n"
                + "      self.increment_by(key, 1)\n"
                + "fn main():\n"
                + "  pass\n", "method-loop-argument.moss");
        new SemanticChecker(program).run();
    }

    private static void testObjectMethodArityIsChecked() {
        String prefix = ""
                + "type Box:\n"
                + "  fn get(value: Int) -> Int:\n"
                + "    return value\n";
        assertCompileError(prefix
                + "fn use(box: Box) -> Int:\n"
                + "  return box.get()\n"
                + "fn main():\n"
                + "  pass\n", "MOSS_COMPILE_ERROR");
        assertCompileError(prefix
                + "fn use(box: Box) -> Int:\n"
                + "  return box.get(1, 2)\n"
                + "fn main():\n"
                + "  pass\n", "MOSS_COMPILE_ERROR");
    }

    private static void testObjectReceiverWriteEffectPropagatesToFunctionParameter() {
        Program program = Parser.parseSource(""
                + "type Box:\n"
                + "  value: Int\n"
                + "  fn set(next: Int):\n"
                + "    value = next\n"
                + "fn update(box: Box):\n"
                + "  box.set(1)\n"
                + "fn main():\n"
                + "  pass\n", "method-receiver-write.moss");
        new SemanticChecker(program).run();
        Function update = program.functions.stream()
                .filter(function -> function.name.equals("update"))
                .findFirst().orElseThrow();
        assertEquals(AccessMode.Write, update.parameterAccessModes.get(0));
        String rust = NativeGenerator.generate(program, false);
        if (!rust.contains("fn update(box: &mut Box)")) {
            throw new AssertionError("object receiver WRITE must lower to a mutable Rust parameter");
        }
    }

    private static void testConsumedObjectParameterLowersByValue() {
        Program program = Parser.parseSource(""
                + "type Box:\n"
                + "  value: Int\n"
                + "fn take(box: Box) -> Box:\n"
                + "  return box\n"
                + "fn main():\n"
                + "  pass\n", "method-consume-lowering.moss");
        new SemanticChecker(program).run();
        Function take = program.functions.stream()
                .filter(function -> function.name.equals("take"))
                .findFirst().orElseThrow();
        assertEquals(AccessMode.Consume, take.parameterAccessModes.get(0));
        String rust = NativeGenerator.generate(program, false);
        if (!rust.contains("fn take(box: Box) -> Box")) {
            throw new AssertionError("consumed object parameter must lower by value");
        }
    }

    private static void testObjectMethodWriteParameterLowersMutably() {
        Program program = Parser.parseSource(""
                + "type Inner:\n"
                + "  value: Int\n"
                + "  fn set(next: Int):\n"
                + "    value = next\n"
                + "type Outer:\n"
                + "  fn update(inner: Inner):\n"
                + "    inner.set(1)\n"
                + "fn main():\n"
                + "  pass\n", "method-parameter-write.moss");
        new SemanticChecker(program).run();
        var update = program.objects.stream()
                .filter(object -> object.name.equals("Outer"))
                .findFirst().orElseThrow().methods.get(0);
        assertEquals(AccessMode.Write, update.parameterAccessModes.get(0));
        String rust = NativeGenerator.generate(program, false);
        if (!rust.contains("pub fn update(&self, inner: &mut Inner)")) {
            throw new AssertionError("object method WRITE parameter must lower to a mutable Rust borrow");
        }
    }

    private static void testNestedObjectFieldWriteLowersMutableReceiver() {
        Program program = Parser.parseSource(""
                + "type Inner:\n"
                + "  value: Int\n"
                + "  fn set(next: Int):\n"
                + "    value = next\n"
                + "type Outer:\n"
                + "  inner: Inner\n"
                + "  fn update(next: Int):\n"
                + "    inner.set(next)\n"
                + "fn main():\n"
                + "  pass\n", "nested-method-receiver-write.moss");
        new SemanticChecker(program).run();
        var update = program.objects.stream()
                .filter(object -> object.name.equals("Outer"))
                .findFirst().orElseThrow().methods.get(0);
        assertEquals(AccessMode.Write, update.receiverAccessMode);
        String rust = NativeGenerator.generate(program, false);
        if (!rust.contains("pub fn update(&mut self, next: i64)")) {
            throw new AssertionError("nested object field write must lower with mutable self");
        }
    }

    private static void assertCompileError(String source, String code) {
        try {
            new SemanticChecker(Parser.parseSource(source, "object-method-arity.moss")).run();
            throw new AssertionError("expected " + code + "");
        } catch (CompileError error) {
            if (!code.equals(error.getCode())) {
                throw new AssertionError("expected " + code + ", got " + error.getCode(), error);
            }
        }
    }

    private static void testUnsupportedCollectionsAreRejectedAsForIterablesAtLoopSite() {
        assertNotIterable(""
                + "fn main():\n"
                + "  var values = Map()\n"
                + "  values[\"a\"] = 1\n"
                + "  values[\"b\"] = 2\n"
                + "  for key in values:\n"
                + "    echo key\n");
        assertNotIterable(""
                + "fn main():\n"
                + "  var values = Queue()\n"
                + "  values.push(1)\n"
                + "  values.push(2)\n"
                + "  for item in values:\n"
                + "    echo item\n");
    }

    private static void assertNotIterable(String source) {
        Program program = Parser.parseSource(source, "collection-not-iterable.moss");
        try {
            new SemanticChecker(program).run();
            throw new AssertionError("Map/Queue must not be accepted as a for-loop iterable");
        } catch (CompileError error) {
            if (!"TYPE_MISMATCH".equals(error.getCode()) || error.getLine() != 5
                    || !error.getMessage().contains("not statically iterable")) {
                throw new AssertionError("collection iterability diagnostic was imprecise: " + error.getMessage(), error);
            }
        }
    }

    private static void assertEquals(Object expected, Object actual) {
        if (!expected.equals(actual)) {
            throw new AssertionError("expected " + expected + ", got " + actual);
        }
    }
}
