package org.mosslang.compiler.semantics;

import org.mosslang.compiler.ast.Program;
import org.mosslang.compiler.ast.Function;
import org.mosslang.compiler.ast.Domain;
import org.mosslang.compiler.ast.Handler;
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
        testInferredGenericConsumeSummaryUsesCheckedParameterType();
        testFunctionConsumeSummaryIncludesNestedFieldMoves();
        testMovingOwnedFieldConsumesOwnerAcrossBranches();
        testFunctionWriteSummaryPropagatesThroughHelperCalls();
        testFunctionWriteModePropagatesThroughNestedMethodReceiver();
        testInferredFunctionReceiverRetainsWriteMode();
        testInferredFunctionReceiverRetainsConsumeMode();
        testInferredReceiverMethodArgumentOwnershipAtCallsite();
        testFunctionWriteModePropagatesThroughMethodArguments();
        testFunctionConsumeSummaryIncludesObjectConstructors();
        testFunctionRetainsResolvedOpenParameterTypes();
        testGenericNumericOperationRetainsOpenTypeUntilSpecialization();
        testMixedIntFloatComparisons();
        testObjectMethodAccessModesAreRetained();
        testImmutableLocalRejectsDirectAndForwardedWriteMethods();
        testNestedFieldReadInBranchConditionKeepsReceiverReadOnly();
        testBranchLocalTypesDoNotLeakFromOneSidedPaths();
        testMovingObjectFieldConsumesMethodReceiver();
        testSiblingMutatorReceiverModeAndUnitCodegen();
        testForLoopValuesCanFlowToObjectMethodArguments();
        testForLoopBindingDoesNotEscapeItsLoop();
        testCustomIteratorNamedCounterWithoutRaisesIsAccepted();
        testObjectMethodArityIsChecked();
        testObjectReceiverWriteEffectPropagatesToFunctionParameter();
        testConsumedObjectParameterLowersByValue();
        testObjectMethodWriteParameterLowersMutably();
        testNestedObjectFieldWriteLowersMutableReceiver();
        testUniformDomainHandlerInferenceFromMainCalls();
        testPerInstanceDomainLayoutSpecialization();
        testConflictingDomainInstanceTypesAreRejected();
        testDomainHandlersRequireMessageSyntax();
        testDomainRoutesCannotBeReassigned();
        testTransitiveMessagePayloadWritesAreRejected();
        testDomainHandlesCannotAppearInDataSignatures();
        testExecutorCannotBeStoredInDomainState();
        testExecutorLifecycleAndConfigurationChecks();
        testRecursionThroughFunctionalCallbacksIsRejected();
        testFilterRequiresBooleanPredicate();
        testReduceCallbackResultMatchesAccumulator();
        testPipelineMethodCallbackRequiresConcreteReceiver();
        testEnumValuesAreRejectedAsMapKeys();
        testOverlappingConsumeArgumentsAreRejected();
        testFunctionalCallbacksRejectMutableCaptures();
        testFunctionalMapRejectsNoncopySourceAliases();
        testFunctionalPipelinesMustBeMaterializedAtDomainBoundary();
        testCollectionTypeArgumentsRequireExactArity();
        testUnknownFunctionalCallbackIsRejected();
        testMapRejectsTraitConstraintAsCallbackResult();
        testForLoopRequiresIteratorProtocol();
        testDirectVectorSumTypeAndLowering();
        testValueHandlerRepliesOnEveryNormalPath();
        testUnsupportedCollectionsAreRejectedAsForIterablesAtLoopSite();
        testRangeForRequiresPositiveNonzeroLiteralStep();
        testSortSemanticContractAndWriteEffects();
        testCharCodepointMethod();
        testScalarProtocolMethods();
        testTypedOnFailLocalCaptureLowering();
        testOnFailRejectsLocalInitializedAfterRaise();
        testOnFailCaptureUsesMatchingRaisedVariant();
        testOwnedMatchPayloadCanBeOnFailCapture();
        testBorrowedMatchPayloadCannotBeOnFailCapture();
        testIncomingMessageObjectPayloadCannotBeMovedIntoState();
        testConsumedCaseBinderCanShadowIncomingMessagePayload();
        testBorrowedCaseBinderCannotShadowIncomingMessagePayloadToMove();
        testSameCrateRaisingCallPreservesErrorCarrier();
        testOnFailCaptureUsesArgumentEvaluationRaiseEdge();
        testOnFailCannotConsumeDomainState();
        testOnFailRejectsDistinctBranchBindings();
        testConstantFalseBranchDoesNotConsumeReachableLocal();
        testWhileZeroIterationDoesNotInitializeCapture();
        testOnFailReplyMustCoverEveryBranch();
        testOnFailRejectsBranchEscapingRaise();
        testUnreachableRecoverDoesNotConsumeReachableOwnership();
        testBareRaiseOutsideRecoverIsRejected();
        testMainRejectsUncaughtTypedRaise();
        testExecutorRootRequiresRaiseHandling();
        testRootIngressTracksMethodPipelineRaises();
        testSynchronousRootMessagePropagatesHelperRaises();
        testRecoverUsesCallArgumentRaiseOrder();
        testRecoverUsesVectorElementRaiseOrder();
        testRecoverUsesPipelineStageRaiseOrder();
        testRecoverBindingInfersRaisesThroughScalarCalls();
        testRangeIndexHasByteType();
        testFileIoReadMakesIoRecoveryReachable();
        testFileIoBuiltinArityIsChecked();
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
                + "  try:\n"
                + "    echo caller()\n"
                + "  recover Failure.Bad:\n"
                + "    pass\n", "raise-carrier-propagation.moss");
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

    private static void testIncomingMessageObjectPayloadCannotBeMovedIntoState() {
        assertCompileError(""
                + "type Payload:\n"
                + "  value: Int\n"
                + "domain Worker:\n"
                + "  saved: Payload\n"
                + "  fn Store(payload: Payload):\n"
                + "    saved = payload\n"
                + "fn main():\n"
                + "  worker = Worker(saved: Payload(value: 0))\n"
                + "  message worker.Store(Payload(value: 1))\n",
                "MOSS_COMPILE_ERROR");
    }

    private static void testConsumedCaseBinderCanShadowIncomingMessagePayload() {
        Program program = Parser.parseSource(""
                + "type Payload:\n"
                + "  value: Int\n"
                + "enum Wrapped:\n"
                + "  Ok(incoming: Payload)\n"
                + "domain Worker:\n"
                + "  saved: Payload\n"
                + "  fn Store(incoming: Payload):\n"
                + "    wrapped = Wrapped.Ok(incoming: Payload(value: 2))\n"
                + "    match consume wrapped:\n"
                + "      case Ok(incoming):\n"
                + "        saved = incoming\n"
                + "fn main():\n"
                + "  worker = Worker(saved: Payload(value: 0))\n"
                + "  message worker.Store(Payload(value: 1))\n",
                "message-payload-shadowed-by-consumed-case.moss");
        new SemanticChecker(program).run();
    }

    private static void testBorrowedCaseBinderCannotShadowIncomingMessagePayloadToMove() {
        assertCompileError(""
                + "type Payload:\n"
                + "  value: Int\n"
                + "enum Wrapped:\n"
                + "  Ok(incoming: Payload)\n"
                + "domain Worker:\n"
                + "  saved: Payload\n"
                + "  fn Store(incoming: Payload):\n"
                + "    wrapped = Wrapped.Ok(incoming: Payload(value: 2))\n"
                + "    match wrapped:\n"
                + "      case Ok(incoming):\n"
                + "        saved = incoming\n"
                + "fn main():\n"
                + "  worker = Worker(saved: Payload(value: 0))\n"
                + "  message worker.Store(Payload(value: 1))\n",
                "BORROWED_ENUM_PAYLOAD_CONSUME");
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

    private static void testConstantFalseBranchDoesNotConsumeReachableLocal() {
        Program program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "  Other\n"
                + "fn take(data: String) -> String:\n"
                + "  return data\n"
                + "domain Worker:\n"
                + "  fn Run() -> String:\n"
                + "    var item = \"before\"\n"
                + "    try:\n"
                + "      if false:\n"
                + "        discarded = take(item)\n"
                + "        raise Failure.Bad\n"
                + "      else:\n"
                + "        raise Failure.Other\n"
                + "    recover Failure.Other:\n"
                + "      echo item.length()\n"
                + "    reply \"ok\"\n"
                + "  on_fail Failure.Bad:\n"
                + "    reply \"bad\"\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  echo message worker.Run()\n", "constant-branch-ownership.moss");
        new SemanticChecker(program).run();
    }

    private static void testWhileZeroIterationDoesNotInitializeCapture() {
        Program program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "domain Worker:\n"
                + "  fn Run() -> String:\n"
                + "    while false:\n"
                + "      let item = \"inside\"\n"
                + "    raise Failure.Bad\n"
                + "  on_fail Failure.Bad:\n"
                + "    reply item\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  echo message worker.Run()\n", "while-zero-iteration-capture.moss");
        try {
            new SemanticChecker(program).run();
            throw new AssertionError("a zero-iteration loop cannot initialize an on_fail capture");
        } catch (CompileError error) {
            if (!"ON_FAIL_CAPTURE_NOT_INITIALIZED".equals(error.getCode())) {
                throw new AssertionError("expected ON_FAIL_CAPTURE_NOT_INITIALIZED, got "
                        + error.getCode(), error);
            }
        }
    }

    private static void testOnFailReplyMustCoverEveryBranch() {
        Program program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "domain Worker:\n"
                + "  fn Run() -> Int:\n"
                + "    raise Failure.Bad\n"
                + "  on_fail Failure.Bad:\n"
                + "    if true:\n"
                + "      reply 1\n"
                + "    else:\n"
                + "      pass\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  echo message worker.Run()\n", "on-fail-branch-reply.moss");
        expectCompileError(program, "ON_FAIL_REPLY_REQUIRED",
                "on_fail must reply on every value-returning path");
    }

    private static void testOnFailRejectsBranchEscapingRaise() {
        Program program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "  Other\n"
                + "domain Worker:\n"
                + "  fn Run(flag: Bool):\n"
                + "    raise Failure.Bad\n"
                + "  on_fail Failure.Bad:\n"
                + "    if flag:\n"
                + "      raise Failure.Other\n"
                + "    else:\n"
                + "      pass\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  message worker.Run(true)\n", "on-fail-branch-raise.moss");
        expectCompileError(program, "ON_FAIL_MUST_HANDLE_ERRORS",
                "on_fail must handle errors raised from all branches");
    }

    private static void expectCompileError(Program program, String code, String message) {
        try {
            new SemanticChecker(program).run();
            throw new AssertionError(message);
        } catch (CompileError error) {
            if (!code.equals(error.getCode()))
                throw new AssertionError("expected " + code + ", got " + error.getCode(), error);
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
                + "  fn evaluate(value: Float, time: Float) -> Float\n"
                + "type Linear:\n"
                + "  scale: Float\n"
                + "  fn evaluate(value: Float, time: Float) -> Float:\n"
                + "    return scale * value\n"
                + "fn combine(value: Evaluator, x: Float, time: Float) -> Float:\n"
                + "  left = value.evaluate(x, time)\n"
                + "  right = value.evaluate(x + 1.0, time + 0.5)\n"
                + "  return left + right\n"
                + "fn main():\n"
                + "  linear = Linear(scale: 2.0)\n"
                + "  result = combine(linear, 1.0, 0.0)\n", "trait-method-repeated-calls.moss");
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

    private static void testFunctionConsumeSummaryIncludesObjectConstructors() {
        assertCompileError(""
                + "type Payload:\n"
                + "  value: String\n"
                + "type Box:\n"
                + "  payload: Payload\n"
                + "fn wrap(value: Payload) -> Box:\n"
                + "  return Box(payload=value)\n"
                + "fn main():\n"
                + "  value = Payload(value=\"x\")\n"
                + "  box = wrap(value)\n"
                + "  echo value.value\n", "OWNERSHIP_USE_AFTER_CONSUME");
    }

    private static void testInferredGenericConsumeSummaryUsesCheckedParameterType() {
        assertCompileError(""
                + "type Payload:\n"
                + "  value: String\n"
                + "fn take(value):\n"
                + "  return value\n"
                + "fn relay(value):\n"
                + "  return take(value)\n"
                + "fn main():\n"
                + "  value = Payload(value=\"x\")\n"
                + "  moved = relay(value)\n"
                + "  echo value.value\n", "OWNERSHIP_USE_AFTER_CONSUME");

        Program copyProgram = Parser.parseSource(""
                + "fn identity(value):\n"
                + "  return value\n"
                + "fn main():\n"
                + "  value = identity(1)\n"
                + "  echo value\n", "inferred-generic-copy-value.moss");
        new SemanticChecker(copyProgram).run();
    }

    private static void testFunctionConsumeSummaryIncludesNestedFieldMoves() {
        assertCompileError(""
                + "type Sink:\n"
                + "  value: Int\n"
                + "  fn bump():\n"
                + "    self.value = self.value + 1\n"
                + "type Box:\n"
                + "  sink: Sink\n"
                + "fn relay(box: Box):\n"
                + "  target = box.sink\n"
                + "  target.bump()\n"
                + "fn main():\n"
                + "  box = Box(sink=Sink(value=0))\n"
                + "  relay(box)\n"
                + "  echo box.sink.value\n", "OWNERSHIP_USE_AFTER_CONSUME");

        Program copyProgram = Parser.parseSource(""
                + "type Box:\n"
                + "  count: Int\n"
                + "fn observe(box: Box):\n"
                + "  count = box.count\n"
                + "fn main():\n"
                + "  box = Box(count=1)\n"
                + "  observe(box)\n"
                + "  echo box.count\n", "function-copy-field-read.moss");
        new SemanticChecker(copyProgram).run();
    }

    private static void testMovingOwnedFieldConsumesOwnerAcrossBranches() {
        Program invalid = Parser.parseSource(""
                + "type Payload:\n"
                + "  text: String\n"
                + "type Box:\n"
                + "  sink: Payload\n"
                + "  count: Int\n"
                + "fn inspect(box: Box, take: Bool) -> Int:\n"
                + "  if take:\n"
                + "    moved = box.sink\n"
                + "  else:\n"
                + "    pass\n"
                + "  return box.count\n"
                + "fn main():\n"
                + "  pass\n", "branch-owned-field-move.moss");
        expectCompileError(invalid, "OWNERSHIP_USE_AFTER_CONSUME",
                "moving an owned field in one branch must consume its whole owner after the join");

        Program copyField = Parser.parseSource(""
                + "type Payload:\n"
                + "  text: String\n"
                + "type Box:\n"
                + "  sink: Payload\n"
                + "  count: Int\n"
                + "fn inspect(box: Box) -> Int:\n"
                + "  copied = box.count\n"
                + "  return box.sink.text.length() + copied\n"
                + "fn main():\n"
                + "  pass\n", "copy-field-keeps-owner.moss");
        new SemanticChecker(copyField).run();
    }

    private static void testFunctionWriteSummaryPropagatesThroughHelperCalls() {
        Program program = Parser.parseSource(""
                + "fn relay(values: Vector[Int]):\n"
                + "  push_one(values)\n"
                + "fn push_one(values: Vector[Int]):\n"
                + "  values.push(1)\n"
                + "fn main():\n"
                + "  var values = Vector[Int]()\n"
                + "  relay(values)\n", "function-write-summary.moss");
        new SemanticChecker(program).run();
        Function relay = program.functions.stream().filter(function -> function.name.equals("relay"))
                .findFirst().orElseThrow();
        assertEquals(AccessMode.Write, relay.parameterAccessModes.get(0));
    }

    private static void testFunctionWriteModePropagatesThroughNestedMethodReceiver() {
        Program program = Parser.parseSource(""
                + "type Sink:\n"
                + "  value: Int\n"
                + "  fn bump():\n"
                + "    self.value = self.value + 1\n"
                + "type Box:\n"
                + "  sink: Sink\n"
                + "fn relay(box: Box):\n"
                + "  box.sink.bump()\n"
                + "fn main():\n"
                + "  box = Box(sink=Sink(value=0))\n"
                + "  relay(box)\n"
                + "  echo box.sink.value\n", "function-write-nested-receiver.moss");
        new SemanticChecker(program).run();
        Function relay = program.functions.stream().filter(function -> function.name.equals("relay"))
                .findFirst().orElseThrow();
        assertEquals(AccessMode.Write, relay.parameterAccessModes.get(0));
    }

    private static void testInferredFunctionReceiverRetainsWriteMode() {
        Program program = Parser.parseSource(""
                + "type Counter:\n"
                + "  value: Int\n"
                + "  fn bump():\n"
                + "    self.value = self.value + 1\n"
                + "fn relay(counter):\n"
                + "  counter.bump()\n"
                + "fn main():\n"
                + "  counter = Counter(value=0)\n"
                + "  relay(counter)\n"
                + "  echo counter.value\n", "inferred-function-receiver-write.moss");
        new SemanticChecker(program).run();
        Function relay = program.functions.stream()
                .filter(function -> function.name.equals("relay"))
                .findFirst().orElseThrow();
        assertEquals(AccessMode.Write, relay.parameterAccessModes.get(0));
    }

    private static void testInferredFunctionReceiverRetainsConsumeMode() {
        assertCompileError(""
                + "type Box:\n"
                + "  payload: String\n"
                + "  fn take() -> String:\n"
                + "    return self.payload\n"
                + "fn relay(box):\n"
                + "  discarded = box.take()\n"
                + "fn main():\n"
                + "  box = Box(payload=\"move\")\n"
                + "  relay(box)\n"
                + "  echo box.payload\n", "OWNERSHIP_USE_AFTER_CONSUME");

        Program program = Parser.parseSource(""
                + "type Box:\n"
                + "  payload: String\n"
                + "  fn take() -> String:\n"
                + "    return self.payload\n"
                + "fn relay(box):\n"
                + "  discarded = box.take()\n"
                + "fn main():\n"
                + "  box = Box(payload=\"move\")\n"
                + "  relay(box)\n", "inferred-function-receiver-consume.moss");
        new SemanticChecker(program).run();
        Function relay = program.functions.stream()
                .filter(function -> function.name.equals("relay"))
                .findFirst().orElseThrow();
        assertEquals(AccessMode.Consume, relay.parameterAccessModes.get(0));

        Program collisionProgram = Parser.parseSource(""
                + "type Mover:\n"
                + "  payload: String\n"
                + "  fn touch() -> String:\n"
                + "    return self.payload\n"
                + "type Reader:\n"
                + "  payload: String\n"
                + "  fn touch():\n"
                + "    echo self.payload\n"
                + "fn relay(item):\n"
                + "  item.touch()\n"
                + "fn main():\n"
                + "  item = Reader(payload=\"keep\")\n"
                + "  relay(item)\n"
                + "  echo item.payload\n", "inferred-function-receiver-collision.moss");
        new SemanticChecker(collisionProgram).run();

        assertCompileError(""
                + "type Mover:\n"
                + "  payload: String\n"
                + "  fn touch() -> String:\n"
                + "    return self.payload\n"
                + "type Reader:\n"
                + "  payload: String\n"
                + "  fn touch():\n"
                + "    echo self.payload\n"
                + "fn relay(item):\n"
                + "  item.touch()\n"
                + "fn main():\n"
                + "  item = Mover(payload=\"move\")\n"
                + "  relay(item)\n"
                + "  echo item.payload\n", "OWNERSHIP_USE_AFTER_CONSUME");
    }

    private static void testInferredReceiverMethodArgumentOwnershipAtCallsite() {
        String declarations = ""
                + "type Payload:\n"
                + "  value: String\n"
                + "type Mover:\n"
                + "  fn accept(value: Payload) -> Payload:\n"
                + "    return value\n"
                + "type Reader:\n"
                + "  fn accept(value: Payload):\n"
                + "    echo value.value\n"
                + "fn relay(value, item):\n"
                + "  item.accept(value)\n";
        assertCompileError(declarations
                + "fn main():\n"
                + "  payload = Payload(value=\"move\")\n"
                + "  relay(payload, Mover())\n"
                + "  echo payload.value\n", "OWNERSHIP_USE_AFTER_CONSUME");
        String aliasDeclarations = declarations.replace("  item.accept(value)\n",
                "  alias = item\n  alias.accept(value)\n");
        assertCompileError(aliasDeclarations
                + "fn main():\n"
                + "  payload = Payload(value=\"move\")\n"
                + "  relay(payload, Mover())\n"
                + "  echo payload.value\n", "OWNERSHIP_USE_AFTER_CONSUME");
        assertCompileError(""
                + "type Payload:\n"
                + "  value: String\n"
                + "type Mover:\n"
                + "  fn accept(value: Payload) -> Payload:\n"
                + "    return value\n"
                + "fn relay(value, item, flag: Bool):\n"
                + "  if flag:\n"
                + "    alias = item\n"
                + "  else:\n"
                + "    alias = item\n"
                + "  alias.accept(value)\n"
                + "fn main():\n"
                + "  payload = Payload(value=\"move\")\n"
                + "  relay(payload, Mover(), true)\n"
                + "  echo payload.value\n", "OWNERSHIP_USE_AFTER_CONSUME");

        Program readOnly = Parser.parseSource(declarations
                + "fn main():\n"
                + "  payload = Payload(value=\"keep\")\n"
                + "  relay(payload, Reader())\n"
                + "  echo payload.value\n", "inferred-receiver-read-only-argument.moss");
        new SemanticChecker(readOnly).run();
        Function relay = readOnly.functions.stream().filter(function -> function.name.equals("relay"))
                .findFirst().orElseThrow();
        assertEquals(AccessMode.Read, relay.parameterAccessModes.get(0));

        Program aliasReadOnly = Parser.parseSource(aliasDeclarations
                + "fn main():\n"
                + "  payload = Payload(value=\"keep\")\n"
                + "  relay(payload, Reader())\n"
                + "  echo payload.value\n", "inferred-receiver-alias-read-only-argument.moss");
        new SemanticChecker(aliasReadOnly).run();

        String branchAliasDeclarations = declarations
                .replace("fn relay(value, item):\n", "fn relay(value, item, flag: Bool):\n")
                .replace("  item.accept(value)\n", "  if flag:\n"
                        + "    alias = item\n"
                        + "  else:\n"
                        + "    alias = item\n"
                        + "  alias.accept(value)\n");
        Program branchAliasReadOnly = Parser.parseSource(branchAliasDeclarations
                + "fn main():\n"
                + "  payload = Payload(value=\"keep\")\n"
                + "  relay(payload, Reader(), true)\n"
                + "  echo payload.value\n", "inferred-receiver-branch-alias-read-only-argument.moss");
        new SemanticChecker(branchAliasReadOnly).run();
    }

    private static void testFunctionWriteModePropagatesThroughMethodArguments() {
        Program program = Parser.parseSource(""
                + "type Box:\n"
                + "  fn append_to(values: Vector[Int]):\n"
                + "    values.push(1)\n"
                + "fn relay(values: Vector[Int], box: Box):\n"
                + "  box.append_to(values)\n"
                + "fn main():\n"
                + "  var values = Vector[Int]()\n"
                + "  box = Box()\n"
                + "  relay(values, box)\n", "function-write-method-argument.moss");
        new SemanticChecker(program).run();
        Function relay = program.functions.stream().filter(function -> function.name.equals("relay"))
                .findFirst().orElseThrow();
        assertEquals(AccessMode.Write, relay.parameterAccessModes.get(0));
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

    private static void testImmutableLocalRejectsDirectAndForwardedWriteMethods() {
        String object = ""
                + "type Cell:\n"
                + "  value: Int\n"
                + "  fn update():\n"
                + "    value = value + 1\n"
                + "  fn read() -> Int:\n"
                + "    return value\n";
        assertCompileError(object
                + "fn main():\n"
                + "  let cell = Cell(value: 1)\n"
                + "  cell.update()\n", "IMMUTABLE_LOCAL_MUTATION");
        assertCompileError(object
                + "fn update_cell(cell: Cell):\n"
                + "  cell.update()\n"
                + "fn main():\n"
                + "  let cell = Cell(value: 1)\n"
                + "  update_cell(cell)\n", "IMMUTABLE_LOCAL_MUTATION");

        assertCompileError(""
                + "fn main():\n"
                + "  values = [1]\n"
                + "  let frozen = values\n"
                + "  frozen.push(2)\n", "IMMUTABLE_LOCAL_MUTATION");
        assertCompileError(""
                + "fn main():\n"
                + "  values = [1]\n"
                + "  let frozen = values\n"
                + "  frozen[0] = 2\n", "IMMUTABLE_LOCAL_MUTATION");
        assertCompileError(""
                + "fn main():\n"
                + "  values = Queue()\n"
                + "  values.push(1)\n"
                + "  let frozen = values\n"
                + "  frozen.pop()\n", "IMMUTABLE_LOCAL_MUTATION");
        assertCompileError(""
                + "fn main():\n"
                + "  values = Map()\n"
                + "  values[\"x\"] = 1\n"
                + "  let frozen = values\n"
                + "  var found = false\n"
                + "  frozen.delete(\"x\", 0, found)\n", "IMMUTABLE_LOCAL_MUTATION");

        Program readOnly = Parser.parseSource(object
                + "fn main():\n"
                + "  let cell = Cell(value: 1)\n"
                + "  echo cell.read()\n", "immutable-read-method.moss");
        new SemanticChecker(readOnly).run();
    }

    private static void testValueHandlerRepliesOnEveryNormalPath() {
        Program missingReply = Parser.parseSource(""
                + "domain Worker:\n"
                + "  fn Maybe(should_reply: Bool) -> Bool:\n"
                + "    if should_reply:\n"
                + "      reply true\n"
                + "    echo \"falling through\"\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  echo message worker.Maybe(false)\n", "handler-fallthrough.moss");
        expectCompileError(missingReply, "MOSS_COMPILE_ERROR",
                "value-returning handler must reply on every normal path");

        Program complete = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "domain Worker:\n"
                + "  fn Maybe(should_reply: Bool) -> Bool:\n"
                + "    if should_reply:\n"
                + "      reply true\n"
                + "    else:\n"
                + "      raise Failure.Bad\n", "handler-reply-or-raise.moss");
        new SemanticChecker(complete).run();
    }

    private static void testUniformDomainHandlerInferenceFromMainCalls() {
        Program program = Parser.parseSource(""
                + "fn normalize(value):\n"
                + "  return value\n"
                + "domain Box:\n"
                + "  value\n"
                + "  fn Set(item):\n"
                + "    value = normalize(item)\n"
                + "  fn Get():\n"
                + "    reply value\n"
                + "fn main():\n"
                + "  box = Box()\n"
                + "  message box.Set(41)\n"
                + "  echo message box.Get()\n", "uniform-domain-specialization.moss");
        new SemanticChecker(program).run();
        Domain box = program.domains.stream().filter(domain -> domain.name.equals("Box"))
                .findFirst().orElseThrow();
        Handler set = box.handlers.stream().filter(handler -> handler.name.equals("Set"))
                .findFirst().orElseThrow();
        Handler get = box.handlers.stream().filter(handler -> handler.name.equals("Get"))
                .findFirst().orElseThrow();
        String fieldType = box.state.get(0).typeRef == null ? null : box.state.get(0).typeRef.toCanonicalString();
        String parameterType = set.params.get(0).typeRef == null ? null : set.params.get(0).typeRef.toCanonicalString();
        String replyType = get.replyTypeRef == null ? null : get.replyTypeRef.toCanonicalString();
        if (!"int".equals(fieldType) || !"int".equals(parameterType) || !"int".equals(replyType)) {
            throw new AssertionError("main call types did not close the uniform domain layout: field="
                    + fieldType + ", parameter=" + parameterType + ", reply=" + replyType);
        }
    }

    private static void testPerInstanceDomainLayoutSpecialization() {
        Program program = Parser.parseSource(""
                + "fn normalize(value):\n"
                + "  return value\n"
                + "domain Box:\n"
                + "  value\n"
                + "  fn Set(item):\n"
                + "    value = normalize(item)\n"
                + "  fn Get():\n"
                + "    reply value\n"
                + "fn main():\n"
                + "  intBox = Box()\n"
                + "  floatBox = Box()\n"
                + "  message intBox.Set(10)\n"
                + "  message floatBox.Set(3.5)\n"
                + "  intValue = message intBox.Get()\n"
                + "  floatValue = message floatBox.Get()\n"
                + "  echo intValue\n"
                + "  echo floatValue\n", "per-instance-domain-specialization.moss");
        new SemanticChecker(program).run();
        Domain integer = program.domains.stream().filter(domain -> domain.name.equals("Box"))
                .findFirst().orElseThrow();
        Domain floating = program.domains.stream().filter(domain -> domain.name.equals("Box__floatBox"))
                .findFirst().orElseThrow();
        Handler integerGet = integer.handlers.stream().filter(handler -> handler.name.equals("Get"))
                .findFirst().orElseThrow();
        Handler floatSet = floating.handlers.stream().filter(handler -> handler.name.equals("Set"))
                .findFirst().orElseThrow();
        Handler floatGet = floating.handlers.stream().filter(handler -> handler.name.equals("Get"))
                .findFirst().orElseThrow();
        if (!"int".equals(integer.state.get(0).type)
                || !"float".equals(floating.state.get(0).type)
                || !"float".equals(floatSet.params.get(0).type)
                || !"int".equals(integerGet.replyType)
                || !"float".equals(floatGet.replyType)) {
            throw new AssertionError("domain instances did not retain their concrete layouts");
        }
        String rust = NativeGenerator.generate(program, false);
        if (!rust.contains("construct_Box__floatBox")
                || !rust.contains("__moss_body_Box__floatBox_Set")
                || !rust.contains("x: f64") && !rust.contains("item: f64")) {
            throw new AssertionError("native generation omitted the float domain specialization");
        }
    }

    private static void testConflictingDomainInstanceTypesAreRejected() {
        Program program = Parser.parseSource(""
                + "domain Pair:\n"
                + "  fn Set(a, b):\n"
                + "    echo a\n"
                + "fn main():\n"
                + "  pair = Pair()\n"
                + "  message pair.Set(10, 3.5)\n"
                + "  message pair.Set(20, 7)\n", "conflicting-domain-instance.moss");
        expectCompileError(program, "MOSS_COMPILE_ERROR",
                "one domain instance cannot receive conflicting inferred handler parameter types");
    }

    private static void testDirectVectorSumTypeAndLowering() {
        Program program = Parser.parseSource(""
                + "fn main():\n"
                + "  values = [9223372036854775807, 1]\n"
                + "  total = sum(values)\n"
                + "  floats = [0.5, 1.25]\n"
                + "  floatTotal = sum(floats)\n"
                + "  echo total\n", "direct-vector-sum.moss");
        new SemanticChecker(program).run();
        String totalType = program.localBindingTypes.get("main").get("total").toCanonicalString();
        String floatType = program.localBindingTypes.get("main").get("floatTotal").toCanonicalString();
        String rust = NativeGenerator.generate(program, false);
        if (!"int".equals(totalType) || !"float".equals(floatType)
                || !rust.contains("wrapping_add(*__moss_value)")
                || !rust.contains("sum::<f64>()")) {
            throw new AssertionError("direct sum must infer element types and preserve Moss numeric lowering");
        }
    }

    private static void testDomainHandlersRequireMessageSyntax() {
        Program invalid = Parser.parseSource(""
                + "domain Worker:\n"
                + "  fn Ping():\n"
                + "    echo \"ping\"\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  worker.Ping()\n", "naked-domain-handler.moss");
        expectCompileError(invalid, "DOMAIN_HANDLER_REQUIRES_MESSAGE",
                "domain handlers must be entered with message syntax");

        Program valid = Parser.parseSource(""
                + "domain Worker:\n"
                + "  fn Ping():\n"
                + "    echo \"ping\"\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  message worker.Ping()\n", "messaged-domain-handler.moss");
        new SemanticChecker(valid).run();

        Program missing = Parser.parseSource(""
                + "domain Worker:\n"
                + "  fn Read() -> Int:\n"
                + "    reply 42\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  echo worker.DoesNotExist()\n", "missing-domain-handler.moss");
        expectCompileError(missing, "UNKNOWN_SYMBOL_OR_TYPE",
                "an unknown domain handler in an expression must be rejected");

        Program sameDomain = Parser.parseSource(""
                + "domain D:\n"
                + "  domainroutes(other: D)\n"
                + "  fn First():\n"
                + "    message other.Second()\n"
                + "  fn Second():\n"
                + "    echo \"second\"\n"
                + "fn main():\n"
                + "  pass\n", "same-domain-handler-message.moss");
        expectCompileError(sameDomain, "MOSS_COMPILE_ERROR",
                "same-domain handlers must use ordinary helper calls");

        Program sameDomainReply = Parser.parseSource(""
                + "domain Worker:\n"
                + "  domainroutes(peer: Worker)\n"
                + "  fn Read() -> Int:\n"
                + "    reply 42\n"
                + "  fn Forward() -> Int:\n"
                + "    reply message peer.Read()\n"
                + "fn main():\n"
                + "  pass\n", "same-domain-reply-message.moss");
        expectCompileError(sameDomainReply, "MOSS_COMPILE_ERROR",
                "a reply expression cannot hide a same-domain handler message");
    }

    private static void testDomainRoutesCannotBeReassigned() {
        Program invalid = Parser.parseSource(""
                + "domain Worker:\n"
                + "  fn Run():\n"
                + "    pass\n"
                + "domain Router:\n"
                + "  domainroutes(worker: Worker)\n"
                + "  fn Reset():\n"
                + "    worker = worker\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  router = Router(worker: worker)\n"
                + "  message router.Reset()\n", "domain-route-reassignment.moss");
        expectCompileError(invalid, "IMMUTABLE_LOCAL_MUTATION",
                "a statically bound domain route cannot be reassigned");
    }

    private static void testTransitiveMessagePayloadWritesAreRejected() {
        Program invalid = Parser.parseSource(""
                + "type Buffer:\n"
                + "  value: Int\n"
                + "  fn clear():\n"
                + "    value = 0\n"
                + "fn mutate(buffer: Buffer):\n"
                + "  buffer.clear()\n"
                + "domain Worker:\n"
                + "  fn Process(payload: Buffer):\n"
                + "    mutate(payload)\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  message worker.Process(Buffer(value = 1))\n", "transitive-message-payload-write.moss");
        expectCompileError(invalid, "MOSS_COMPILE_ERROR",
                "a helper that writes an incoming payload must be rejected");
    }

    private static void testDomainHandlesCannotAppearInDataSignatures() {
        Program invalid = Parser.parseSource(""
                + "domain Worker:\n"
                + "  fn Run():\n"
                + "    pass\n"
                + "fn take(worker: Worker):\n"
                + "  pass\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  take(worker)\n", "domain-handle-data-parameter.moss");
        expectCompileError(invalid, "MOSS_COMPILE_ERROR",
                "a domain handle cannot be used as a function data parameter");
    }

    private static void testExecutorCannotBeStoredInDomainState() {
        Program invalid = Parser.parseSource(""
                + "domain Scheduler:\n"
                + "  pool: Executor\n"
                + "  fn Run():\n"
                + "    pass\n"
                + "fn main():\n"
                + "  scheduler = Scheduler()\n"
                + "  message scheduler.Run()\n", "executor-domain-state.moss");
        expectCompileError(invalid, "MOSS_COMPILE_ERROR",
                "Executor handles cannot be stored in domain state");
    }

    private static void testExecutorLifecycleAndConfigurationChecks() {
        Program missingJoin = Parser.parseSource(""
                + "domain Worker:\n"
                + "  fn Process():\n"
                + "    pass\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  executor = Executor().threads(1).start()\n"
                + "  executor.invoke(worker.Process())\n", "executor-missing-join.moss");
        expectCompileError(missingJoin, "EXECUTOR_JOIN_MISSING",
                "an active Executor must be joined before main exits");

        Program wrongConfig = Parser.parseSource(""
                + "fn main():\n"
                + "  executor = Executor().threads(\"two\").start()\n"
                + "  executor.join()\n", "executor-config-type.moss");
        expectCompileError(wrongConfig, "EXECUTOR_CONFIG_TYPE",
                "Executor thread count must be an Int");

        Program zeroQueueCapacity = Parser.parseSource(""
                + "fn main():\n"
                + "  executor = Executor().queue_capacity(0).start()\n"
                + "  executor.join()\n", "executor-queue-capacity-range.moss");
        expectCompileError(zeroQueueCapacity, "EXECUTOR_CONFIG_RANGE",
                "Executor.queue_capacity must be positive");

        Program valueReturning = Parser.parseSource(""
                + "domain Worker:\n"
                + "  fn Read() -> Int:\n"
                + "    reply 1\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  executor = Executor().start()\n"
                + "  executor.invoke(worker.Read())\n"
                + "  executor.join()\n", "executor-value-return.moss");
        expectCompileError(valueReturning, "EXECUTOR_INVOKE_VALUE_RETURNING",
                "Executor.invoke cannot submit a reply-bearing handler");

        Program importedRaise = Parser.parseSource(""
                + "domain Worker:\n"
                + "  fn Process():\n"
                + "    pass\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  message worker.Process()\n", "source-free-handler-raise-root.moss");
        Handler importedHandler = importedRaise.domains.get(0).handlers.get(0);
        importedHandler.body.clear();
        importedHandler.sourceFreeMayRaise = true;
        expectCompileError(importedRaise, "ROOT_UNHANDLED_RAISE",
                "source-free raising handler contracts must fail closed without on_fail");

        Program valid = Parser.parseSource(""
                + "domain Worker:\n"
                + "  fn Process(amount: Int):\n"
                + "    pass\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  executor = Executor().threads(1).queue_capacity(8).start()\n"
                + "  executor.invoke(worker.Process(1))\n"
                + "  executor.join()\n", "executor-valid-lifecycle.moss");
        new SemanticChecker(valid).run();
        String rust = NativeGenerator.generate(valid, false);
        if (!rust.contains("__MossExecutor::new().threads(1_i64).queue_capacity(8_i64).start()")
                || !rust.contains("pub fn enqueue<F>(&mut self, work: F)")
                || !rust.contains("shared.space_available.wait(state)")
                || !rust.contains("std::thread::Builder::new()")
                || !rust.contains("__moss_executor_worker(worker_shared)")
                || !rust.contains("state.shutdown = true")
                || !rust.contains("worker.join().is_err()")
                || !rust.contains("executor.enqueue(Box::new(move ||")
                || !rust.contains("worker.clone()")
                || !rust.contains("Process_shared(__moss_executor_arg_")
                || !rust.contains("1_i64.clone()")) {
            throw new AssertionError("Executor lowering must defer an owned one-way handler call until join");
        }

        Program handledFailure = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "domain Worker:\n"
                + "  value = 0\n"
                + "  fn Process(amount: Int):\n"
                + "    value = value + amount\n"
                + "    raise Failure.Bad\n"
                + "  on_fail Failure.Bad:\n"
                + "    value = value + 1\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  executor = Executor().threads(2).start()\n"
                + "  executor.invoke(worker.Process(3))\n"
                + "  executor.join()\n", "executor-on-fail-root.moss");
        new SemanticChecker(handledFailure).run();
        String handledRust = NativeGenerator.generate(handledFailure, false);
        if (!handledRust.contains("Process_root(__moss_executor_arg_")) {
            throw new AssertionError("Executor roots with on_fail arms must use the handler root wrapper");
        }
    }

    private static void testRecursionThroughFunctionalCallbacksIsRejected() {
        Program direct = Parser.parseSource(""
                + "fn recurse(value: Int) -> Int:\n"
                + "  values = [value]\n"
                + "  return values |> map(recurse) |> sum\n"
                + "fn main():\n"
                + "  echo recurse(1)\n", "recursive-map-callback.moss");
        expectCompileError(direct, "RECURSION_CYCLE",
                "direct functional callback recursion must be rejected");

        Program forwarded = Parser.parseSource(""
                + "fn apply(values, callable):\n"
                + "  return values |> map(callable)\n"
                + "fn recurse(value: Int) -> Int:\n"
                + "  values = [value]\n"
                + "  transformed = apply(values, recurse)\n"
                + "  return transformed[0]\n"
                + "fn main():\n"
                + "  echo recurse(1)\n", "recursive-hof-callback.moss");
        expectCompileError(forwarded, "RECURSION_CYCLE",
                "forwarded functional callback recursion must be rejected");
    }

    private static void testFilterRequiresBooleanPredicate() {
        Program invalid = Parser.parseSource(""
                + "fn main():\n"
                + "  values = [1, 2, 3]\n"
                + "  invalid = values |> filter(_ + 1)\n"
                + "  echo invalid[0]\n", "filter-non-bool-predicate.moss");
        expectCompileError(invalid, "TYPE_MISMATCH",
                "filter predicates must return bool");

        Program valid = Parser.parseSource(""
                + "fn is_positive(value: Int) -> Bool:\n"
                + "  return value > 0\n"
                + "fn main():\n"
                + "  values = [1, 2, 3]\n"
                + "  positives = values |> filter(is_positive)\n"
                + "  echo positives[0]\n", "filter-bool-predicate.moss");
        new SemanticChecker(valid).run();
    }

    private static void testReduceCallbackResultMatchesAccumulator() {
        Program invalid = Parser.parseSource(""
                + "fn wrong_accumulator(accumulator: Int, value: Int) -> Bool:\n"
                + "  return value > accumulator\n"
                + "fn main():\n"
                + "  values = [1, 2, 3]\n"
                + "  invalid = values |> reduce(0, wrong_accumulator)\n"
                + "  echo invalid\n", "reduce-result-mismatch.moss");
        expectCompileError(invalid, "TYPE_MISMATCH",
                "reduce callback results must match the accumulator type");

        Program valid = Parser.parseSource(""
                + "fn add(accumulator: Int, value: Int) -> Int:\n"
                + "  return accumulator + value\n"
                + "fn main():\n"
                + "  values = [1, 2, 3]\n"
                + "  total = values |> reduce(0, add)\n"
                + "  echo total\n", "reduce-result-match.moss");
        new SemanticChecker(valid).run();
    }

    private static void testPipelineMethodCallbackRequiresConcreteReceiver() {
        String declarations = ""
                + "type Box:\n"
                + "  fn apply(value: Int) -> Int:\n"
                + "    return value\n";
        assertCompileError(declarations
                + "fn mapped(box, values: Vector[Int]) -> Vector[Int]:\n"
                + "  return values |> map(box.apply)\n"
                + "fn main():\n"
                + "  box = Box()\n"
                + "  mapped(box, [1])\n", "UNTYPED_FIELD_ACCESS");
        Program concreteReceiver = Parser.parseSource(declarations
                + "fn mapped(box: Box, values: Vector[Int]) -> Vector[Int]:\n"
                + "  return values |> map(box.apply)\n"
                + "fn main():\n"
                + "  box = Box()\n"
                + "  mapped(box, [1])\n", "pipeline-method-callback-concrete-receiver.moss");
        new SemanticChecker(concreteReceiver).run();
    }

    private static void testEnumValuesAreRejectedAsMapKeys() {
        Program invalid = Parser.parseSource(""
                + "enum Color:\n"
                + "  Red\n"
                + "  Blue\n"
                + "fn main():\n"
                + "  var colors = Map()\n"
                + "  colors[Color.Red] = 1\n"
                + "  echo colors[Color.Red]\n", "enum-map-key.moss");
        expectCompileError(invalid, "ENUM_MAP_KEY_UNSUPPORTED",
                "enum values cannot be Map keys");

        Program valid = Parser.parseSource(""
                + "fn main():\n"
                + "  var colors = Map()\n"
                + "  colors[\"red\"] = 1\n"
                + "  echo colors[\"red\"]\n", "string-map-key.moss");
        new SemanticChecker(valid).run();
    }

    private static void testOverlappingConsumeArgumentsAreRejected() {
        Program consumeRead = Parser.parseSource(""
                + "type Box:\n"
                + "  value: Int\n"
                + "fn conflict(taken: Box, observed: Box):\n"
                + "  let moved = taken\n"
                + "  echo moved.value, observed.value\n"
                + "fn main():\n"
                + "  item = Box(value = 7)\n"
                + "  conflict(item, item)\n", "consume-read-alias.moss");
        expectCompileError(consumeRead, "OWNERSHIP_CONFLICTING_ACCESS",
                "a consumed argument must not overlap a read argument");

        Program doubleConsume = Parser.parseSource(""
                + "type Box:\n"
                + "  value: Int\n"
                + "fn conflict(first: Box, second: Box):\n"
                + "  let moved_first = first\n"
                + "  let moved_second = second\n"
                + "  echo moved_first.value, moved_second.value\n"
                + "fn main():\n"
                + "  item = Box(value = 7)\n"
                + "  conflict(item, item)\n", "double-consume-alias.moss");
        expectCompileError(doubleConsume, "OWNERSHIP_CONFLICTING_ACCESS",
                "two consumed arguments must not overlap");

        Program readRead = Parser.parseSource(""
                + "type Box:\n"
                + "  value: Int\n"
                + "fn inspect(first: Box, second: Box) -> Int:\n"
                + "  return first.value + second.value\n"
                + "fn main():\n"
                + "  item = Box(value = 7)\n"
                + "  echo inspect(item, item)\n", "read-read-alias.moss");
        new SemanticChecker(readRead).run();

        Program callbackAlias = Parser.parseSource(""
                + "type Box:\n"
                + "  value: Int\n"
                + "fn conflict(changed: Box, observed: Box) -> Int:\n"
                + "  changed.value = changed.value + 1\n"
                + "  return observed.value\n"
                + "fn invalid_callback(item: Box) -> Int:\n"
                + "  return conflict(item, item)\n"
                + "fn main():\n"
                + "  values = [Box(value = 1)]\n"
                + "  result = values |> map(invalid_callback)\n"
                + "  echo result[0]\n", "return-call-ownership-alias.moss");
        expectCompileError(callbackAlias, "OWNERSHIP_CONFLICTING_ACCESS",
                "return calls must validate overlapping write and read arguments");
    }

    private static void testFunctionalCallbacksRejectMutableCaptures() {
        Program direct = Parser.parseSource(""
                + "fn main():\n"
                + "  values = [1, 2, 3]\n"
                + "  captured = [0]\n"
                + "  invalid = values |> map(captured.push(_))\n"
                + "  echo invalid[0]\n", "functional-mutable-capture.moss");
        expectCompileError(direct, "FUNCTIONAL_CAPTURE_MUTATION",
                "functional callbacks cannot mutate captured collections");

        Program bound = Parser.parseSource(""
                + "type Accumulator:\n"
                + "  total: Int\n"
                + "  fn add(value: Int) -> Int:\n"
                + "    total = total + value\n"
                + "    return total\n"
                + "fn main():\n"
                + "  accumulator = Accumulator(total = 0)\n"
                + "  values = [1, 2, 3]\n"
                + "  invalid = values |> map(accumulator.add)\n"
                + "  echo invalid[0]\n", "functional-bound-method-capture.moss");
        expectCompileError(bound, "FUNCTIONAL_CAPTURE_MUTATION",
                "functional callbacks cannot mutate bound receiver captures");

        Program forwarded = Parser.parseSource(""
                + "type Accumulator:\n"
                + "  total: Int\n"
                + "  fn add(value: Int) -> Int:\n"
                + "    total = total + value\n"
                + "    return total\n"
                + "fn update(accumulator: Accumulator, value: Int) -> Int:\n"
                + "  return accumulator.add(value)\n"
                + "fn update_through_helper(accumulator: Accumulator, value: Int) -> Int:\n"
                + "  return update(accumulator, value)\n"
                + "fn main():\n"
                + "  values = [1, 2, 3]\n"
                + "  captured = Accumulator(total = 0)\n"
                + "  invalid = values |> map(update_through_helper(captured, _))\n"
                + "  echo invalid[0]\n", "functional-helper-mutable-capture.moss");
        expectCompileError(forwarded, "FUNCTIONAL_CAPTURE_MUTATION",
                "functional callbacks cannot mutate captures through helper calls");

        Program readCapture = Parser.parseSource(""
                + "fn main():\n"
                + "  threshold = 2\n"
                + "  values = [1, 2, 3]\n"
                + "  result = values |> map(_ + threshold)\n"
                + "  echo result[0]\n", "functional-read-capture.moss");
        new SemanticChecker(readCapture).run();
    }

    private static void testFunctionalPipelinesMustBeMaterializedAtDomainBoundary() {
        Program invalid = Parser.parseSource(""
                + "domain Sink:\n"
                + "  fn Store(values: Vector):\n"
                + "    echo \"stored\"\n"
                + "fn double(value: Int) -> Int:\n"
                + "  return value * 2\n"
                + "fn main():\n"
                + "  sink = Sink()\n"
                + "  values = [1, 2, 3]\n"
                + "  message sink.Store(values |> map(double))\n", "pipeline-domain-inline.moss");
        expectCompileError(invalid, "MOSS_COMPILE_ERROR",
                "a collection-producing pipeline must be materialized before a domain send");

        Program valid = Parser.parseSource(""
                + "domain Sink:\n"
                + "  fn Store(values: Vector):\n"
                + "    echo \"stored\"\n"
                + "fn double(value: Int) -> Int:\n"
                + "  return value * 2\n"
                + "fn main():\n"
                + "  sink = Sink()\n"
                + "  values = [1, 2, 3]\n"
                + "  mapped = values |> map(double)\n"
                + "  message sink.Store(mapped)\n", "pipeline-domain-materialized.moss");
        new SemanticChecker(valid).run();
    }

    private static void testFunctionalMapRejectsNoncopySourceAliases() {
        Program identity = Parser.parseSource(""
                + "type Payload:\n"
                + "  label: String\n"
                + "fn main():\n"
                + "  values = [Payload(label = \"first\")]\n"
                + "  invalid = values |> map(_)\n"
                + "  echo invalid[0]\n", "functional-map-alias-identity.moss");
        expectCompileError(identity, "FUNCTIONAL_SEMANTIC_ERROR",
                "map cannot return noncopy source elements without a copy");

        Program field = Parser.parseSource(""
                + "type Payload:\n"
                + "  label: String\n"
                + "type Envelope:\n"
                + "  payload: Payload\n"
                + "fn main():\n"
                + "  values = [Envelope(payload = Payload(label = \"first\"))]\n"
                + "  invalid = values |> map(_.payload)\n"
                + "  echo invalid[0].label\n", "functional-map-alias-field.moss");
        expectCompileError(field, "FUNCTIONAL_SEMANTIC_ERROR",
                "map cannot return a noncopy source field without a copy");
    }

    private static void testCollectionTypeArgumentsRequireExactArity() {
        Program partialMap = Parser.parseSource(""
                + "type Index:\n"
                + "  entries: Map[String]\n"
                + "fn main():\n"
                + "  echo 1\n", "partial-map-arity.moss");
        expectCompileError(partialMap, "MALFORMED_TYPE_EXPRESSION",
                "Map requires exactly two type arguments");

        Program extraVector = Parser.parseSource(""
                + "fn total(values: Vector[Int, Int]) -> Int:\n"
                + "  return 0\n"
                + "fn main():\n"
                + "  echo 1\n", "extra-vector-arity.moss");
        expectCompileError(extraVector, "MALFORMED_TYPE_EXPRESSION",
                "Vector requires exactly one type argument");

        Program valid = Parser.parseSource(""
                + "fn total(values: Map[String, Vector[Int]]) -> Int:\n"
                + "  return 0\n"
                + "fn main():\n"
                + "  echo 1\n", "nested-collection-arity.moss");
        new SemanticChecker(valid).run();
    }

    private static void testUnknownFunctionalCallbackIsRejected() {
        for (String stage : java.util.List.of("map(missing)", "filter(missing)",
                "any(missing)", "all(missing)")) {
            Program invalid = Parser.parseSource(""
                    + "fn main():\n"
                    + "  values = [20, 22]\n"
                    + "  result = values |> " + stage + "\n"
                    + "  echo result\n", "unknown-functional-callback.moss");
            expectCompileError(invalid, "FUNCTIONAL_SEMANTIC_ERROR",
                    "functional stages require a statically known callback: " + stage);
        }
    }

    private static void testMapRejectsTraitConstraintAsCallbackResult() {
        Program invalid = Parser.parseSource(""
                + "trait ValueSource:\n"
                + "  fn Value() -> Int\n"
                + "fn missing_result(value: Int) -> ValueSource:\n"
                + "  echo value\n"
                + "fn main():\n"
                + "  values = [1, 2]\n"
                + "  result = values |> map(missing_result)\n"
                + "  echo result |> count\n", "trait-valued-map-result.moss");
        expectCompileError(invalid, "TYPE_INFERENCE_FAILED",
                "map callbacks cannot return a compile-time trait constraint as a value");
    }

    private static void testForLoopRequiresIteratorProtocol() {
        Program invalid = Parser.parseSource(""
                + "type NotAnIterator:\n"
                + "  value: Int\n"
                + "fn main():\n"
                + "  source = NotAnIterator(value = 1)\n"
                + "  for item in source:\n"
                + "    echo item\n", "missing-iterator-protocol.moss");
        expectCompileError(invalid, "TYPE_MISMATCH",
                "for loops require a vector, range, or Iterator protocol");

        Program valid = Parser.parseSource(""
                + "type Counter:\n"
                + "  current: Int\n"
                + "  fn next() -> option[Int]:\n"
                + "    return None\n"
                + "fn main():\n"
                + "  counter = Counter(current = 0)\n"
                + "  for item in counter:\n"
                + "    echo item\n", "custom-iterator-protocol.moss");
        new SemanticChecker(valid).run();

        Program mutation = Parser.parseSource(""
                + "fn main():\n"
                + "  values = [1, 2]\n"
                + "  for item in values:\n"
                + "    values.push(item)\n", "mutation-during-for.moss");
        expectCompileError(mutation, "MOSS_COMPILE_ERROR",
                "a collection cannot be structurally mutated during a read traversal");
    }

    private static void testNestedFieldReadInBranchConditionKeepsReceiverReadOnly() {
        Program program = Parser.parseSource(""
                + "type Profile:\n"
                + "  name: String\n"
                + "  fn score() -> Int:\n"
                + "    if name == name:\n"
                + "      return 1\n"
                + "    return 0\n"
                + "type Record:\n"
                + "  profile: Profile\n"
                + "  fn score() -> Int:\n"
                + "    if profile.name == profile.name:\n"
                + "      return 1\n"
                + "    return 0\n"
                + "fn inspect_record(value: Record) -> Int:\n"
                + "  return value.score()\n"
                + "fn main():\n"
                + "  pass\n", "nested-field-read-branch-effects.moss");
        new SemanticChecker(program).run();
        var recordScore = program.objects.stream().filter(object -> object.name.equals("Record"))
                .flatMap(object -> object.methods.stream())
                .filter(method -> method.name.equals("score")).findFirst().orElseThrow();
        Function inspectRecord = program.functions.stream()
                .filter(function -> function.name.equals("inspect_record")).findFirst().orElseThrow();
        if (recordScore.receiverAccessMode != AccessMode.Read
                || inspectRecord.parameterAccessModes.size() != 1
                || inspectRecord.parameterAccessModes.get(0) != AccessMode.Read) {
            throw new AssertionError("nested field reads in branch conditions must keep receiver READ: method="
                    + recordScore.receiverAccessMode + " function=" + inspectRecord.parameterAccessModes);
        }
    }

    private static void testBranchLocalTypesDoNotLeakFromOneSidedPaths() {
        Program confinedOpenTypes = Parser.parseSource(""
                + "fn main():\n"
                + "  if true:\n"
                + "    values = []\n"
                + "    values.push(1)\n"
                + "    echo values[0]\n"
                + "  values = []\n"
                + "  values.push(\"outer\")\n"
                + "  echo values[0]\n", "branch-confined-open-type.moss");
        new SemanticChecker(confinedOpenTypes).run();

        Program leakedLocal = Parser.parseSource(""
                + "fn main():\n"
                + "  if true:\n"
                + "    branch_value = 1\n"
                + "  echo branch_value\n", "if-local-leak.moss");
        try {
            new SemanticChecker(leakedLocal).run();
            throw new AssertionError("one-sided branch local escaped its branch");
        } catch (CompileError error) {
            if (!"UNKNOWN_SYMBOL_OR_TYPE".equals(error.getCode())) throw error;
        }

        Program definitelyAssigned = Parser.parseSource(""
                + "fn main():\n"
                + "  if true:\n"
                + "    branch_value = 1\n"
                + "  else:\n"
                + "    branch_value = 2\n"
                + "  echo branch_value\n", "if-local-definitely-assigned.moss");
        new SemanticChecker(definitelyAssigned).run();

        Program loopScopedTypes = Parser.parseSource(""
                + "fn main():\n"
                + "  for item in [1]:\n"
                + "    result = []\n"
                + "    result.push(item)\n"
                + "  result = []\n"
                + "  result.push(\"done\")\n", "for-local-open-type.moss");
        new SemanticChecker(loopScopedTypes).run();

        Program leakedLoopLocal = Parser.parseSource(""
                + "fn main():\n"
                + "  for item in [1]:\n"
                + "    loop_value = item\n"
                + "  echo loop_value\n", "for-local-leak.moss");
        try {
            new SemanticChecker(leakedLoopLocal).run();
            throw new AssertionError("for-loop local escaped its loop scope");
        } catch (CompileError error) {
            if (!"UNKNOWN_SYMBOL_OR_TYPE".equals(error.getCode())) throw error;
        }
    }

    private static void testSiblingMutatorReceiverModeAndUnitCodegen() {
        Program program = Parser.parseSource(""
                + "type Box:\n"
                + "  value: Int\n"
                + "  fn bump():\n"
                + "    self.value = self.value + 1\n"
                + "  fn run():\n"
                + "    self.bump()\n"
                + "fn main():\n"
                + "  box = Box(value=0)\n"
                + "  box.run()\n", "sibling-mutator-receiver-mode.moss");
        new SemanticChecker(program).run();
        org.mosslang.compiler.ast.Method run = program.objects.get(0).methods.stream()
                .filter(method -> method.name.equals("run")).findFirst().orElseThrow();
        assertEquals(AccessMode.Write, run.receiverAccessMode);
        String rust = NativeGenerator.generate(program, false);
        if (!rust.contains("pub fn run(&mut self) -> ()")
                || rust.contains("pub fn run(&mut self) -> i64")) {
            throw new AssertionError("unit sibling-mutator method must retain WRITE receiver and unit result: " + rust);
        }
    }

    private static void testMovingObjectFieldConsumesMethodReceiver() {
        Program program = Parser.parseSource(""
                + "type Sink:\n"
                + "  value: Int\n"
                + "  fn bump():\n"
                + "    self.value = self.value + 1\n"
                + "type Box:\n"
                + "  sink: Sink\n"
                + "  fn take_sink():\n"
                + "    target = self.sink\n"
                + "    target.bump()\n"
                + "fn main():\n"
                + "  box = Box(sink=Sink(value=0))\n"
                + "  box.take_sink()\n", "method-consumes-field.moss");
        new SemanticChecker(program).run();
        var takeSink = program.objects.stream().filter(object -> object.name.equals("Box"))
                .findFirst().orElseThrow().methods.stream()
                .filter(method -> method.name.equals("take_sink")).findFirst().orElseThrow();
        assertEquals(AccessMode.Consume, takeSink.receiverAccessMode);
        String rust = NativeGenerator.generate(program, false);
        if (!rust.contains("pub fn take_sink(self) -> ()")) {
            throw new AssertionError("method moving an owned field must lower with a by-value receiver");
        }

        assertCompileError(""
                + "type Sink:\n"
                + "  value: Int\n"
                + "  fn bump():\n"
                + "    self.value = self.value + 1\n"
                + "type Box:\n"
                + "  sink: Sink\n"
                + "  fn take_sink():\n"
                + "    target = self.sink\n"
                + "    target.bump()\n"
                + "fn main():\n"
                + "  box = Box(sink=Sink(value=0))\n"
                + "  box.take_sink()\n"
                + "  echo box.sink.value\n", "OWNERSHIP_USE_AFTER_CONSUME");

        assertCompileError(""
                + "type Sink:\n"
                + "  value: Int\n"
                + "  fn bump():\n"
                + "    self.value = self.value + 1\n"
                + "type Box:\n"
                + "  sink: Sink\n"
                + "  fn take_sink():\n"
                + "    target = self.sink\n"
                + "    target.bump()\n"
                + "fn consume(box: Box):\n"
                + "  box.take_sink()\n"
                + "fn main():\n"
                + "  box = Box(sink=Sink(value=0))\n"
                + "  consume(box)\n"
                + "  echo box.sink.value\n", "OWNERSHIP_USE_AFTER_CONSUME");

        assertCompileError(""
                + "type Payload:\n"
                + "  value: String\n"
                + "  fn read_value() -> String:\n"
                + "    return self.value\n"
                + "type Sink:\n"
                + "  payload: Payload\n"
                + "  fn take_payload() -> String:\n"
                + "    target = self.payload\n"
                + "    return target.read_value()\n"
                + "type Box:\n"
                + "  sink: Sink\n"
                + "fn consume(box: Box):\n"
                + "  value = box.sink.take_payload()\n"
                + "fn main():\n"
                + "  box = Box(sink=Sink(payload=Payload(value=\"x\")))\n"
                + "  consume(box)\n"
                + "  echo box.sink.payload.value\n", "OWNERSHIP_USE_AFTER_CONSUME");

        assertCompileError(""
                + "type Payload:\n"
                + "  value: String\n"
                + "type Sink:\n"
                + "  payload: Payload\n"
                + "type Box:\n"
                + "  sink: Sink\n"
                + "  fn take_payload():\n"
                + "    target = self.sink.payload\n"
                + "fn main():\n"
                + "  box = Box(sink=Sink(payload=Payload(value=\"x\")))\n"
                + "  box.take_payload()\n"
                + "  echo box.sink.payload.value\n", "OWNERSHIP_USE_AFTER_CONSUME");

        assertCompileError(""
                + "type Payload:\n"
                + "  value: String\n"
                + "type Box:\n"
                + "  payload: Payload\n"
                + "  fn take_payload() -> Payload:\n"
                + "    return self.payload\n"
                + "fn main():\n"
                + "  box = Box(payload=Payload(value=\"x\"))\n"
                + "  payload = box.take_payload()\n"
                + "  echo box.payload.value\n", "OWNERSHIP_USE_AFTER_CONSUME");

        assertCompileError(""
                + "type Payload:\n"
                + "  value: String\n"
                + "fn take(value: Payload) -> Payload:\n"
                + "  return value\n"
                + "type Box:\n"
                + "  payload: Payload\n"
                + "  fn take_payload() -> Payload:\n"
                + "    return take(self.payload)\n"
                + "fn main():\n"
                + "  box = Box(payload=Payload(value=\"x\"))\n"
                + "  payload = box.take_payload()\n"
                + "  echo box.payload.value\n", "OWNERSHIP_USE_AFTER_CONSUME");

        assertCompileError(""
                + "type Payload:\n"
                + "  value: String\n"
                + "fn take(value: Payload) -> Payload:\n"
                + "  return value\n"
                + "fn relay(value: Payload) -> Payload:\n"
                + "  return take(value)\n"
                + "type Box:\n"
                + "  payload: Payload\n"
                + "  fn take_payload() -> Payload:\n"
                + "    return relay(self.payload)\n"
                + "fn main():\n"
                + "  box = Box(payload=Payload(value=\"x\"))\n"
                + "  payload = box.take_payload()\n"
                + "  echo box.payload.value\n", "OWNERSHIP_USE_AFTER_CONSUME");

        assertCompileError(""
                + "type Payload:\n"
                + "  value: String\n"
                + "fn relay(value: Payload) -> Payload:\n"
                + "  return take(value)\n"
                + "fn take(value: Payload) -> Payload:\n"
                + "  return value\n"
                + "type Box:\n"
                + "  payload: Payload\n"
                + "  fn take_payload() -> Payload:\n"
                + "    return relay(self.payload)\n"
                + "fn main():\n"
                + "  box = Box(payload=Payload(value=\"x\"))\n"
                + "  payload = box.take_payload()\n"
                + "  echo box.payload.value\n", "OWNERSHIP_USE_AFTER_CONSUME");

        assertCompileError(""
                + "type Payload:\n"
                + "  value: String\n"
                + "type Box:\n"
                + "  fn accept(value: Payload) -> Payload:\n"
                + "    return value\n"
                + "fn relay(value: Payload, box: Box) -> Payload:\n"
                + "  return box.accept(value)\n"
                + "fn main():\n"
                + "  box = Box()\n"
                + "  value = Payload(value=\"x\")\n"
                + "  moved = relay(value, box)\n"
                + "  echo value.value\n", "OWNERSHIP_USE_AFTER_CONSUME");
    }

    private static void testForLoopValuesCanFlowToObjectMethodArguments() {
        Program program = Parser.parseSource(""
                + "type Accumulator:\n"
                + "  data: Map [String, Int]\n"
                + "  fn get(key: String) -> Int:\n"
                + "    return data.get(key, 0)\n"
                + "  fn keys() -> Vector [String]:\n"
                + "    return data.keys()\n"
                + "  fn increment_by(key: String, amount: Int) -> Int:\n"
                + "    next = data.get(key, 0) + amount\n"
                + "    data [key] = next\n"
                + "    return next\n"
                + "  fn merge(other: Accumulator):\n"
                + "    for key in other.keys():\n"
                + "      self.increment_by(key, other.get(key))\n"
                + "fn main():\n"
                + "  pass\n", "method-loop-argument.moss");
        new SemanticChecker(program).run();
        assertCompileError(""
                + "type Accumulator:\n"
                + "  data: Map [String, Int]\n"
                + "  fn get(key: String) -> Int:\n"
                + "    return data.get(key, 0)\n"
                + "  fn keys() -> Vector [String]:\n"
                + "    return data.keys()\n"
                + "  fn increment_by(key: String, amount: Int) -> Int:\n"
                + "    next = data.get(key, 0) + amount\n"
                + "    data [key] = next\n"
                + "    return next\n"
                + "  fn merge(other: Accumulator):\n"
                + "    for key in other.keys():\n"
                + "      self.increment_by(key, true)\n"
                + "fn main():\n"
                + "  pass\n", "TYPE_MISMATCH");
    }

    private static void testForLoopBindingDoesNotEscapeItsLoop() {
        assertCompileError(""
                + "fn leak() -> Int:\n"
                + "  for item in [1]:\n"
                + "    pass\n"
                + "  return item\n"
                + "fn main():\n"
                + "  pass\n", "UNKNOWN_SYMBOL_OR_TYPE");

        Program program = Parser.parseSource(""
                + "type Box:\n"
                + "  fn get(key: String) -> Int:\n"
                + "    return 1\n"
                + "  fn keys() -> Vector[String]:\n"
                + "    return [\"key\"]\n"
                + "  fn visit(other: Box):\n"
                + "    for key in other.keys():\n"
                + "      self.get(key)\n"
                + "fn main():\n"
                + "  pass\n", "loop-binder-method-call.moss");
        new SemanticChecker(program).run();
    }

    private static void testCustomIteratorNamedCounterWithoutRaisesIsAccepted() {
        Program program = Parser.parseSource(""
                + "type Counter:\n"
                + "  fn next() -> option[Int]:\n"
                + "    return None\n"
                + "fn consume(counter: Counter):\n"
                + "  for value in counter:\n"
                + "    pass\n"
                + "fn main():\n"
                + "  pass\n", "custom-counter-iterator.moss");
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

    private static void testUnreachableRecoverDoesNotConsumeReachableOwnership() {
        Program program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  A\n"
                + "  B\n"
                + "fn take(value: String) -> String:\n"
                + "  return value\n"
                + "fn only_a():\n"
                + "  data = \"ok\"\n"
                + "  try:\n"
                + "    raise Failure.A\n"
                + "  recover Failure.B:\n"
                + "    ignored = take(data)\n"
                + "  recover Failure.A:\n"
                + "    pass\n"
                + "  echo data\n"
                + "fn main():\n"
                + "  only_a()\n", "unreachable-recover-ownership.moss");
        new SemanticChecker(program).run();
    }

    private static void testBareRaiseOutsideRecoverIsRejected() {
        assertCompileError(""
                + "fn invalid():\n"
                + "  raise\n", "BARE_RAISE_OUTSIDE_RECOVER");
        Program valid = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "fn main():\n"
                + "  try:\n"
                + "    raise Failure.Bad\n"
                + "  recover Failure.Bad:\n"
                + "    raise\n", "bare-raise-recover-scope.moss");
        new SemanticChecker(valid).run();
    }

    private static void testMainRejectsUncaughtTypedRaise() {
        String prefix = ""
                + "enum Trouble:\n"
                + "  Bad\n";
        assertCompileError(prefix
                + "fn main():\n"
                + "  raise Trouble.Bad\n", "MOSS_COMPILE_ERROR");
        assertCompileError(prefix
                + "fn main():\n"
                + "  try:\n"
                + "    pass\n"
                + "  recover:\n"
                + "    pass\n"
                + "  raise Trouble.Bad\n", "MOSS_COMPILE_ERROR");

        String callbackPrefix = prefix
                + "fn risky(value: Int) -> Int:\n"
                + "  raise Trouble.Bad\n"
                + "fn mapped(values: Vector[Int]) -> Vector[Int]:\n"
                + "  return values |> map(risky)\n";
        assertCompileError(callbackPrefix
                + "fn main():\n"
                + "  mapped([1])\n", "MOSS_COMPILE_ERROR");
        Program callbackHandled = Parser.parseSource(callbackPrefix
                + "fn main():\n"
                + "  try:\n"
                + "    mapped([1])\n"
                + "  recover Trouble.Bad:\n"
                + "    pass\n", "main-recovers-pipeline-callback-raise.moss");
        new SemanticChecker(callbackHandled).run();

        String methodCallbackPrefix = prefix
                + "enum CallbackFault:\n"
                + "  Other\n"
                + "fn risky(value: Int) -> Int:\n"
                + "  raise CallbackFault.Other\n"
                + "type Box:\n"
                + "  fn risky(value: Int) -> Int:\n"
                + "    raise Trouble.Bad\n"
                + "fn mapped(box: Box, values: Vector[Int]) -> Vector[Int]:\n"
                + "  return values |> map(box.risky)\n";
        assertCompileError(methodCallbackPrefix
                + "fn main():\n"
                + "  box = Box()\n"
                + "  mapped(box, [1])\n", "MOSS_COMPILE_ERROR");
        Program methodCallbackHandled = Parser.parseSource(methodCallbackPrefix
                + "fn main():\n"
                + "  box = Box()\n"
                + "  try:\n"
                + "    mapped(box, [1])\n"
                + "  recover Trouble.Bad:\n"
                + "    pass\n", "main-recovers-method-pipeline-callback-raise.moss");
        new SemanticChecker(methodCallbackHandled).run();
        assertCompileError(methodCallbackPrefix
                + "fn main():\n"
                + "  box = Box()\n"
                + "  mapped_values = [1] |> map(box.risky)\n", "MOSS_COMPILE_ERROR");
        Program directMethodCallbackHandled = Parser.parseSource(methodCallbackPrefix
                + "fn main():\n"
                + "  box = Box()\n"
                + "  try:\n"
                + "    mapped_values = [1] |> map(box.risky)\n"
                + "  recover Trouble.Bad:\n"
                + "    pass\n", "main-recovers-direct-method-pipeline-callback.moss");
        new SemanticChecker(directMethodCallbackHandled).run();

        Program handled = Parser.parseSource(prefix
                + "fn main():\n"
                + "  try:\n"
                + "    raise Trouble.Bad\n"
                + "  recover Trouble.Bad:\n"
                + "    pass\n", "main-handles-raised-variant.moss");
        new SemanticChecker(handled).run();

        Program catchAll = Parser.parseSource(prefix
                + "fn main():\n"
                + "  try:\n"
                + "    raise Trouble.Bad\n"
                + "  recover err:\n"
                + "    pass\n", "main-catchall-handles-raised-variant.moss");
        new SemanticChecker(catchAll).run();

        String methodPrefix = prefix
                + "type Sink:\n"
                + "  fn fail():\n"
                + "    raise Trouble.Bad\n"
                + "fn fail_helper():\n"
                + "  raise Trouble.Bad\n"
                + "type Box:\n"
                + "  sink: Sink\n"
                + "  fn relay():\n"
                + "    target = self.sink\n"
                + "    target.fail()\n"
                + "  fn relay_function() -> unit:\n"
                + "    fail_helper()\n";
        assertCompileError(methodPrefix
                + "fn main():\n"
                + "  box = Box(sink=Sink())\n"
                + "  box.sink.fail()\n", "MOSS_COMPILE_ERROR");
        Program methodHandled = Parser.parseSource(methodPrefix
                + "fn main():\n"
                + "  box = Box(sink=Sink())\n"
                + "  try:\n"
                + "    box.sink.fail()\n"
                + "  recover Trouble.Bad:\n"
                + "    pass\n", "main-handles-method-raised-variant.moss");
        new SemanticChecker(methodHandled).run();
        assertCompileError(methodPrefix
                + "fn main():\n"
                + "  box = Box(sink=Sink())\n"
                + "  box.relay()\n", "MOSS_COMPILE_ERROR");
        Program nestedMethodHandled = Parser.parseSource(methodPrefix
                + "fn main():\n"
                + "  box = Box(sink=Sink())\n"
                + "  try:\n"
                + "    box.relay()\n"
                + "  recover Trouble.Bad:\n"
                + "    pass\n", "main-handles-nested-method-raised-variant.moss");
        new SemanticChecker(nestedMethodHandled).run();
        assertCompileError(methodPrefix
                + "fn main():\n"
                + "  box = Box(sink=Sink())\n"
                + "  box.relay_function()\n", "MOSS_COMPILE_ERROR");
        Program methodFunctionHandled = Parser.parseSource(methodPrefix
                + "fn main():\n"
                + "  box = Box(sink=Sink())\n"
                + "  try:\n"
                + "    box.relay_function()\n"
                + "  recover Trouble.Bad:\n"
                + "    pass\n", "main-handles-method-function-raise.moss");
        new SemanticChecker(methodFunctionHandled).run();
        String functionMethodPrefix = methodPrefix
                + "fn invoke_method():\n"
                + "  box = Box(sink=Sink())\n"
                + "  box.relay()\n";
        assertCompileError(functionMethodPrefix
                + "fn main():\n"
                + "  invoke_method()\n", "MOSS_COMPILE_ERROR");
        Program functionMethodHandled = Parser.parseSource(functionMethodPrefix
                + "fn main():\n"
                + "  try:\n"
                + "    invoke_method()\n"
                + "  recover Trouble.Bad:\n"
                + "    pass\n", "main-handles-function-method-raise.moss");
        new SemanticChecker(functionMethodHandled).run();

        String functionPrefix = prefix
                + "fn fail():\n"
                + "  raise Trouble.Bad\n"
                + "fn relay():\n"
                + "  fail()\n";
        assertCompileError(functionPrefix
                + "fn main():\n"
                + "  relay()\n", "MOSS_COMPILE_ERROR");
        Program functionHandled = Parser.parseSource(functionPrefix
                + "fn main():\n"
                + "  try:\n"
                + "    relay()\n"
                + "  recover Trouble.Bad:\n"
                + "    pass\n", "main-handles-function-chain-raised-variant.moss");
        new SemanticChecker(functionHandled).run();
    }

    private static void testExecutorRootRequiresRaiseHandling() {
        assertCompileError(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "domain Worker:\n"
                + "  fn Run():\n"
                + "    raise Failure.Bad\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  executor = Executor().threads(1).start()\n"
                + "  executor.invoke(worker.Run())\n"
                + "  executor.join()\n", "ROOT_UNHANDLED_RAISE");
        Program handled = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "domain Worker:\n"
                + "  fn Run():\n"
                + "    raise Failure.Bad\n"
                + "  on_fail Failure.Bad:\n"
                + "    pass\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  executor = Executor().threads(1).start()\n"
                + "  executor.invoke(worker.Run())\n"
                + "  executor.join()\n", "executor-root-handled.moss");
        new SemanticChecker(handled).run();
    }

    private static void testRootIngressTracksMethodPipelineRaises() {
        String prefix = ""
                + "enum Failure:\n"
                + "  Bad\n"
                + "type Box:\n"
                + "  fn risky(value: Int) -> Int:\n"
                + "    raise Failure.Bad\n";
        String handler = ""
                + "domain Worker:\n"
                + "  fn Run():\n"
                + "    box = Box()\n"
                + "    values = [1] |> map(box.risky)\n";
        assertCompileError(prefix + handler
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  message worker.Run()\n", "ROOT_UNHANDLED_RAISE");
        Program handled = Parser.parseSource(prefix + handler
                + "  on_fail Failure.Bad:\n"
                + "    pass\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  message worker.Run()\n", "root-ingress-method-pipeline-raise-handled.moss");
        new SemanticChecker(handled).run();
    }

    private static void testSynchronousRootMessagePropagatesHelperRaises() {
        assertCompileError(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "fn fail() -> Int:\n"
                + "  raise Failure.Bad\n"
                + "fn projected() -> Int:\n"
                + "  return fail() + 1\n"
                + "domain Worker:\n"
                + "  fn Run():\n"
                + "    echo projected()\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  message worker.Run()\n", "ROOT_UNHANDLED_RAISE");
    }

    private static void testRecoverUsesCallArgumentRaiseOrder() {
        String prefix = ""
                + "enum Failure:\n"
                + "  Bad\n"
                + "fn fail() -> Int:\n"
                + "  raise Failure.Bad\n"
                + "fn consume(number: Int, data: String) -> String:\n"
                + "  return data\n"
                + "fn consume_before(data: String, number: Int) -> String:\n"
                + "  return data\n"
                + "domain Worker:\n"
                + "  fn Run() -> Int:\n"
                + "    var data = \"abc\"\n"
                + "    try:\n";
        String suffix = ""
                + "    recover Failure.Bad:\n"
                + "      reply data.length()\n"
                + "    reply 0\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  echo message worker.Run()\n";
        Program raisesBeforeMove = Parser.parseSource(prefix
                + "      result = consume((fail() + 1), data)\n" + suffix,
                "recover-call-raise-before-move.moss");
        new SemanticChecker(raisesBeforeMove).run();
        assertCompileError(prefix + "      result = consume_before(data, fail())\n" + suffix,
                "OWNERSHIP_USE_AFTER_CONSUME");
        String shared = ""
                + "enum Failure:\n"
                + "  Bad\n"
                + "fn maybe_fail(flag: Bool) -> Int:\n"
                + "  if flag:\n"
                + "    raise Failure.Bad\n"
                + "  return 1\n"
                + "fn consume_and_fail(number: Int, data: String) -> Int:\n"
                + "  discarded = data\n"
                + "  raise Failure.Bad\n"
                + "domain Worker:\n"
                + "  fn Run(flag: Bool) -> Int:\n"
                + "    var data = \"abc\"\n"
                + "    try:\n"
                + "      result = consume_and_fail(maybe_fail(flag), data)\n"
                + "    recover Failure.Bad:\n"
                + "      reply data.length()\n"
                + "    reply 0\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  echo message worker.Run(true)\n";
        assertCompileError(shared, "OWNERSHIP_USE_AFTER_CONSUME");
    }

    private static void testRecoverUsesVectorElementRaiseOrder() {
        String prefix = ""
                + "enum Failure:\n"
                + "  Bad\n"
                + "fn fail() -> String:\n"
                + "  raise Failure.Bad\n"
                + "domain Worker:\n"
                + "  fn Run() -> Int:\n"
                + "    var data = \"abc\"\n"
                + "    try:\n";
        String suffix = ""
                + "    recover Failure.Bad:\n"
                + "      reply data.length()\n"
                + "    reply 0\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  echo message worker.Run()\n";
        Program raisesBeforeMove = Parser.parseSource(prefix
                + "      values = [fail(), data]\n" + suffix,
                "recover-vector-raise-before-move.moss");
        new SemanticChecker(raisesBeforeMove).run();
        assertCompileError(prefix + "      values = [data, fail()]\n" + suffix,
                "OWNERSHIP_USE_AFTER_CONSUME");
    }

    private static void testRecoverUsesPipelineStageRaiseOrder() {
        String prefix = ""
                + "enum Failure:\n"
                + "  Bad\n"
                + "fn source(flag: Bool) -> Vector[Int]:\n"
                + "  if flag:\n"
                + "    raise Failure.Bad\n"
                + "  return [1]\n"
                + "fn fold(accumulator: String, item: Int) -> String:\n";
        String suffix = ""
                + "domain Worker:\n"
                + "  fn Run(flag: Bool) -> Int:\n"
                + "    var data = \"abc\"\n"
                + "    try:\n"
                + "      result = source(flag) |> reduce(data, fold)\n"
                + "    recover Failure.Bad:\n"
                + "      reply data.length()\n"
                + "    reply 0\n"
                + "fn main():\n"
                + "  worker = Worker()\n"
                + "  echo message worker.Run(true)\n";
        Program sourceRaisesFirst = Parser.parseSource(prefix
                + "  return accumulator\n" + suffix, "recover-pipeline-source-edge.moss");
        new SemanticChecker(sourceRaisesFirst).run();
        assertCompileError(prefix + "  raise Failure.Bad\n" + suffix,
                "OWNERSHIP_USE_AFTER_CONSUME");
    }

    private static void testRecoverBindingInfersRaisesThroughScalarCalls() {
        Program program = Parser.parseSource(""
                + "enum Failure:\n"
                + "  Bad\n"
                + "fn number(fail: Bool) -> Int:\n"
                + "  if fail:\n"
                + "    raise Failure.Bad\n"
                + "  return 7\n"
                + "fn forward(fail: Bool):\n"
                + "  raise number(fail)\n"
                + "fn route(fail: Bool):\n"
                + "  try:\n"
                + "    forward(fail)\n"
                + "  recover Failure.Bad:\n"
                + "    echo \"bad\"\n"
                + "  recover err:\n"
                + "    echo err\n"
                + "fn main():\n"
                + "  route(true)\n"
                + "  route(false)\n", "recover-scalar-call-raise.moss");
        SemanticChecker checker = new SemanticChecker(program);
        checker.run();
        if (checker.getWarnings().stream().anyMatch(warning ->
                "RECOVER_UNREACHABLE_VARIANT".equals(warning.code()))) {
            throw new AssertionError("raised enum from scalar call was omitted from recovery reachability");
        }
    }

    private static void testRangeIndexHasByteType() {
        Program program = Parser.parseSource(""
                + "fn first_byte(chunk: Range) -> Int:\n"
                + "  return chunk[0]\n"
                + "fn main():\n"
                + "  pass\n", "range-byte-index-type.moss");
        new SemanticChecker(program).run();
    }

    private static void testFileIoReadMakesIoRecoveryReachable() {
        Program program = Parser.parseSource(""
                + "enum FileError:\n"
                + "  IO\n"
                + "fn read_one(path: String) -> Int:\n"
                + "  try:\n"
                + "    source = FileIO.open(path, ro)\n"
                + "    bytes = source.read(0, 1)\n"
                + "    source.close()\n"
                + "    return bytes.length()\n"
                + "  recover FileError.IO:\n"
                + "    return 0\n"
                + "fn main():\n"
                + "  pass\n", "fileio-read-recovery-warning.moss");
        SemanticChecker checker = new SemanticChecker(program);
        checker.run();
        if (checker.getWarnings().stream().anyMatch(warning ->
                "RECOVER_UNREACHABLE_VARIANT".equals(warning.code()))) {
            throw new AssertionError("FileIO read must make FileError.IO recovery reachable");
        }

        Program unrelatedReader = Parser.parseSource(""
                + "enum FileError:\n"
                + "  IO\n"
                + "type Reader:\n"
                + "  fn read() -> Int:\n"
                + "    return 1\n"
                + "fn use(reader: Reader):\n"
                + "  try:\n"
                + "    value = reader.read()\n"
                + "  recover FileError.IO:\n"
                + "    pass\n"
                + "fn main():\n"
                + "  pass\n", "unrelated-read-recovery-warning.moss");
        SemanticChecker unrelatedReaderChecker = new SemanticChecker(unrelatedReader);
        unrelatedReaderChecker.run();
        if (unrelatedReaderChecker.getWarnings().stream().noneMatch(warning ->
                "RECOVER_UNREACHABLE_VARIANT".equals(warning.code()))) {
            throw new AssertionError("unrelated read method must not imply FileError.IO");
        }
    }

    private static void testFileIoBuiltinArityIsChecked() {
        assertCompileError(""
                + "fn main():\n"
                + "  source = FileIO.open(\"input\")\n", "TYPE_MISMATCH");
        assertCompileError(""
                + "fn main():\n"
                + "  source = FileIO.open(\"input\", ro)\n"
                + "  source.close(1)\n", "TYPE_MISMATCH");
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

    private static void testRangeForRequiresPositiveNonzeroLiteralStep() {
        String prefix = "fn main():\n  for index in ";
        String suffix = ":\n    pass\n";
        assertCompileError(prefix + "range(0, 3, 0)" + suffix, "MOSS_COMPILE_ERROR");
        assertCompileError(prefix + "range(0, 3, 00)" + suffix, "MOSS_COMPILE_ERROR");
        assertCompileError(prefix + "range(0, 3, -1)" + suffix, "MOSS_COMPILE_ERROR");
        assertCompileError("fn main():\n  step = 1\n  for index in range(0, 3, step):\n    pass\n",
                "MOSS_COMPILE_ERROR");
        assertCompileError(prefix + "range(0, 3, 1, 2)" + suffix, "TYPE_MISMATCH");
        assertCompileError(prefix + "range(0, true)" + suffix, "MOSS_COMPILE_ERROR");
        assertCompileError("fn main():\n  values = range(0, 3)\n",
                "RANGE_OUTSIDE_FOR");

        Program valid = Parser.parseSource(""
                + "fn main():\n"
                + "  for index in range(0, 3):\n"
                + "    pass\n"
                + "  for index in range(0, 3, +2):\n"
                + "    pass\n", "range-for-valid.moss");
        new SemanticChecker(valid).run();
    }

    private static void testSortSemanticContractAndWriteEffects() {
        Program valid = Parser.parseSource(""
                + "fn identity(value: Int) -> Int:\n"
                + "  return value\n"
                + "fn main():\n"
                + "  var values = [3, 1, 2]\n"
                + "  sort(values)\n"
                + "  sort(values, by: identity)\n"
                + "  sort(values, by: Reverse(identity))\n", "sort-semantic-valid.moss");
        new SemanticChecker(valid).run();

        assertCompileError("fn main():\n  let values = [3, 1, 2]\n  sort(values)\n",
                "IMMUTABLE_LOCAL_MUTATION");
        assertCompileError("fn main():\n  value = sort([3, 1, 2])\n",
                "SORT_REQUIRES_PLACE");
        assertCompileError("fn main():\n  var values = [3, 1, 2]\n  sort(values, identity)\n",
                "SORT_SIGNATURE");
        assertCompileError("fn main():\n  var values = [[3], [1]]\n  sort(values)\n",
                "SORT_KEY_UNORDERED");
        assertCompileError(""
                + "fn noisy(value: Int) -> Int:\n"
                + "  echo value\n"
                + "  return value\n"
                + "fn main():\n"
                + "  var values = [3, 1, 2]\n"
                + "  sort(values, by: noisy)\n", "SORT_KEY_IMPURE");

        Program forwardedWrite = Parser.parseSource(""
                + "fn reorder(values: Vector[Int]):\n"
                + "  sort(values)\n"
                + "fn main():\n"
                + "  let values = [3, 1, 2]\n"
                + "  reorder(values)\n", "sort-semantic-forwarded-write.moss");
        try {
            new SemanticChecker(forwardedWrite).run();
            throw new AssertionError("sort's inferred WRITE effect was not propagated");
        } catch (CompileError error) {
            if (!"IMMUTABLE_LOCAL_MUTATION".equals(error.getCode())) throw error;
        }
    }

    private static void testCharCodepointMethod() {
        Program valid = Parser.parseSource(""
                + "fn main():\n"
                + "  let letter = '\\x41'\n"
                + "  echo letter.codepoint()\n"
                + "  echo '\\x2C'.codepoint()\n", "char-codepoint-valid.moss");
        new SemanticChecker(valid).run();
        assertCompileError("fn main():\n  let letter = 'A'\n  echo letter.codepoint(1)\n",
                "TYPE_MISMATCH");
    }

    private static void testScalarProtocolMethods() {
        Program valid = Parser.parseSource(""
                + "fn main():\n"
                + "  echo 2.less(3)\n"
                + "  echo 2.isequal(2)\n"
                + "  echo 4.hash()\n"
                + "  echo 4.to_string()\n"
                + "  echo 1.5.less(2.0)\n"
                + "  echo 'A'.less('B')\n"
                + "  echo \"a\".less(\"b\")\n"
                + "  echo true.isequal(true)\n", "scalar-protocol-valid.moss");
        new SemanticChecker(valid).run();
        assertCompileError("fn main():\n  echo 1.less(2.0)\n", "TYPE_MISMATCH");
        assertCompileError("fn main():\n  echo 1.hash(2)\n", "TYPE_MISMATCH");
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
