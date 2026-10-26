package org.mosslang.compiler.cli;

import org.mosslang.compiler.ast.ObjectType;
import org.mosslang.compiler.ast.EnumType;
import org.mosslang.compiler.ast.Program;
import org.mosslang.compiler.ast.Function;
import org.mosslang.compiler.ast.Param;
import org.mosslang.compiler.backend.NativeGenerator;
import org.mosslang.compiler.abi.CheckedBodyCodec;
import org.mosslang.compiler.abi.MossiInterface;
import org.mosslang.compiler.abi.MossiRecord;
import org.mosslang.compiler.abi.AbiV10Records;
import org.mosslang.compiler.diagnostics.CompileError;
import org.mosslang.compiler.parser.Parser;
import org.mosslang.compiler.semantics.SemanticChecker;

import java.nio.file.Files;
import java.nio.file.Path;

public final class SourceFreeObjectMetadataTest {
    public static void main(String[] args) throws Exception {
        Path scratch = Path.of("tmp", "java-source-free-object-metadata");
        Files.createDirectories(scratch);
        Path interfaceFile = scratch.resolve("provider.mossi");
        String exportedFields = "  public_representation field x float\n"
                + "  public_representation field label string\n";

        String content = "moss-module-interface-v2\n"
                + "provider_contract_digest sha256:" + "0".repeat(64) + "\n"
                + "module_id sample::provider\ncontract\nobject Point\ncomment Café\n"
                + "domain Worker opaque\n"
                + "  handler_failure_arms \"Work\" 1\n"
                + "  handler_failure_arm \"Work\" 0 \"Fault.Bad\" \"__moss_on_fail_provider__Worker_Work_0\" 1 1 0 0 0\n"
                + "  handler_failure_capture \"Work\" 0 \"attempt\" \"int\" \"attempt\" 1 1 \"0:3:int:0:\"\n"
                + "semantic_exports\n"
                + "export domain Worker opaque\n"
                + "  handler Work reply=unit effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=enum:provider__Fault:Bad;may_diverge=0;unresolved=0\n"
                + "export enum LegacyResult\n"
                + "  declaration_id nominal-enum-v1:module-v1:sample@0.1.0::provider:enum:LegacyResult\n"
                + "  enum_case \"Failed\" 2 \"code\" \"int\" \"message\" \"string\"\n"
                + "export type Point\n" + exportedFields
                + "  object_method shift return=float receiver=WRITE "
                + "effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=-;may_diverge=0;unresolved=0 "
                + "params=1 \"amount\" \"float\" READ\n"
                + "  object_method replace return=unit receiver=WRITE "
                + "effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=-;may_diverge=0;unresolved=0 "
                + "params=1 \"next\" \"Vector[Int]\" CONSUME\n"
                + "  object_method signal return=unit receiver=READ "
                + "effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=enum:Fault:Bad;may_diverge=0;unresolved=0 "
                + "params=0\n";
        Files.writeString(interfaceFile, content);
        if (!Main.readMossiText(interfaceFile).contains("comment CafÃ©")) {
            throw new AssertionError("ABI-v9 interface decoding no longer preserves its legacy byte mapping");
        }
        Program program = new Program();
        Main.loadMossiFunctions(interfaceFile, "provider", program);
        org.mosslang.compiler.ast.Domain importedWorker = program.domains.stream()
                .filter(domain -> domain.name.equals("provider__Worker")).findFirst()
                .orElseThrow(() -> new AssertionError("source-free domain declaration was not imported"));
        if (importedWorker.handlers.size() != 1 || importedWorker.handlers.get(0).failureArms.size() != 1
                || !importedWorker.handlers.get(0).failureArms.get(0).patterns.contains("Fault.Bad")
                || !"int".equals(importedWorker.handlers.get(0).failureArms.get(0)
                        .checkedCaptures.get("attempt"))) {
            throw new AssertionError("ABI-v9 failure-arm capture metadata was not restored");
        }
        String withoutFailureArm = content.replace(
                "  handler_failure_arms \"Work\" 1\n"
                        + "  handler_failure_arm \"Work\" 0 \"Fault.Bad\" \"__moss_on_fail_provider__Worker_Work_0\" 1 1 0 0 0\n"
                        + "  handler_failure_capture \"Work\" 0 \"attempt\" \"int\" \"attempt\" 1 1 \"0:3:int:0:\"\n",
                "  handler_failure_arms \"Work\" 0\n");
        if (withoutFailureArm.equals(content)) {
            throw new AssertionError("source-free raise fixture failed to remove the imported on_fail arm");
        }
        Program handledImportedHandler = Parser.parseSource(""
                + "import provider\n"
                + "fn main():\n"
                + "  worker = provider.Worker()\n"
                + "  executor = Executor().threads(1).start()\n"
                + "  executor.invoke(worker.Work())\n"
                + "  executor.join()\n", "source-free-handler-root-handled.moss");
        Main.loadMossiFunctions(interfaceFile, "provider", handledImportedHandler);
        new SemanticChecker(handledImportedHandler).run();
        String handledImportedRust = NativeGenerator.generate(handledImportedHandler, false);
        if (!handledImportedRust.contains("__moss_executor_domain_")
                || !handledImportedRust.contains(".Work_root()))")) {
            throw new AssertionError("source-free handled Executor root did not use the provider root wrapper");
        }
        Program importedSynchronousRoot = Parser.parseSource(""
                + "import provider\n"
                + "fn main():\n"
                + "  worker = provider.Worker()\n"
                + "  executor = Executor().threads(1).start()\n"
                + "  message worker.Work()\n"
                + "  executor.join()\n", "source-free-synchronous-root.moss");
        Main.loadMossiFunctions(interfaceFile, "provider", importedSynchronousRoot);
        new SemanticChecker(importedSynchronousRoot).run();
        String importedSynchronousRust = NativeGenerator.generate(importedSynchronousRoot, false);
        int importedStart = importedSynchronousRust.indexOf("__MossExecutor::new().threads(1_i64).start()");
        int importedRoot = importedSynchronousRust.indexOf("runtime_invoke(move || __moss_root_domain.__moss_message_Work())");
        int importedJoin = importedSynchronousRust.indexOf("executor.join();");
        if (importedStart < 0 || importedRoot <= importedStart || importedJoin <= importedRoot) {
            throw new AssertionError("source-free synchronous root must use its ABI bridge while Executor is active");
        }
        Path raisingInterface = scratch.resolve("provider-raising-no-failure.mossi");
        Files.writeString(raisingInterface, withoutFailureArm);
        Program unhandledImportedHandler = Parser.parseSource(""
                + "import provider\n"
                + "fn main():\n"
                + "  worker = provider.Worker()\n"
                + "  message worker.Work()\n", "source-free-handler-root-raise.moss");
        Main.loadMossiFunctions(raisingInterface, "provider", unhandledImportedHandler);
        try {
            new SemanticChecker(unhandledImportedHandler).run();
            throw new AssertionError("source-free raising domain handler must require on_fail at root ingress");
        } catch (CompileError error) {
            if (!"ROOT_UNHANDLED_RAISE".equals(error.getCode())) {
                throw new AssertionError("expected ROOT_UNHANDLED_RAISE for source-free handler, got "
                        + error.getCode(), error);
            }
        }
        String twoVariantContract = content.replace(
                "raise_set=enum:provider__Fault:Bad;",
                "raise_set=enum:provider__Fault:Bad,enum:provider__Fault:Other;");
        Path partialFailureInterface = scratch.resolve("provider-partial-failure.mossi");
        Files.writeString(partialFailureInterface, twoVariantContract);
        Program partialImportedFailure = Parser.parseSource(""
                + "import provider\n"
                + "fn main():\n"
                + "  worker = provider.Worker()\n"
                + "  message worker.Work()\n", "source-free-handler-partial-failure.moss");
        Main.loadMossiFunctions(partialFailureInterface, "provider", partialImportedFailure);
        try {
            new SemanticChecker(partialImportedFailure).run();
            throw new AssertionError("source-free on_fail arms must cover every imported raise variant");
        } catch (CompileError error) {
            if (!"ROOT_UNHANDLED_RAISE".equals(error.getCode())) {
                throw new AssertionError("expected ROOT_UNHANDLED_RAISE for partial source-free coverage, got "
                        + error.getCode(), error);
            }
        }
        ObjectType imported = program.objects.stream()
                .filter(object -> object.name.equals("provider__Point"))
                .findFirst().orElseThrow(() -> new AssertionError(
                        "source-free object declaration was not imported"));
        if (imported.fields.size() != 2
                || !"float".equals(imported.fields.get(0).type)
                || !"string".equals(imported.fields.get(1).type)) {
            throw new AssertionError("source-free fields were not preserved: "
                    + imported.fields);
        }
        if (program.objects.stream().noneMatch(object -> object.name.equals("provider.Point"))) {
            throw new AssertionError("qualified source type alias was not imported");
        }
        EnumType legacyResult = program.enums.stream()
                .filter(enumeration -> enumeration.name.equals("provider__LegacyResult"))
                .findFirst().orElseThrow(() -> new AssertionError(
                        "ABI-v9 source-free enum was not imported"));
        if (!legacyResult.sourceFree || legacyResult.cases.size() != 1
                || legacyResult.cases.get(0).fields.size() != 2) {
            throw new AssertionError("ABI-v9 enum payload facts were not imported");
        }
        Program legacyEnumConsumer = Parser.parseSource(""
                + "fn failure() -> provider__LegacyResult:\n"
                + "  return provider__LegacyResult.Failed(code=3, message=\"bad\")\n"
                + "fn main():\n"
                + "  pass\n", "legacy-enum-consumer.moss");
        Main.loadMossiFunctions(interfaceFile, "provider", legacyEnumConsumer);
        new SemanticChecker(legacyEnumConsumer).run();
        String legacyEnumRust = NativeGenerator.generate(legacyEnumConsumer, false);
        if (!legacyEnumRust.contains("moss_provider::LegacyResult::Failed")
                || legacyEnumRust.contains("pub enum provider__LegacyResult")) {
            throw new AssertionError("ABI-v9 source-free enum did not use its provider Rust path: "
                    + legacyEnumRust);
        }
        if (imported.methods.size() != 3
                || !"shift".equals(imported.methods.get(0).name)
                || !"float".equals(imported.methods.get(0).returnType)
                || imported.methods.get(0).params.size() != 1
                || !"amount".equals(imported.methods.get(0).params.get(0).name)
                || !"float".equals(imported.methods.get(0).params.get(0).type)
                || imported.methods.get(0).receiverAccessMode
                        != org.mosslang.compiler.types.AccessMode.Write
                || imported.methods.get(0).parameterAccessModes.size() != 1
                || imported.methods.get(0).parameterAccessModes.get(0)
                        != org.mosslang.compiler.types.AccessMode.Read
                || imported.methods.get(1).parameterAccessModes.size() != 1
                || imported.methods.get(1).parameterAccessModes.get(0)
                        != org.mosslang.compiler.types.AccessMode.Consume) {
            throw new AssertionError("source-free method signature was not restored");
        }
        if (!"signal".equals(imported.methods.get(2).name)
                || !imported.methods.get(2).sourceFreeRaiseVariants.contains("Fault.Bad")) {
            throw new AssertionError("source-free method raise facts were not restored");
        }
        if (!"unit".equals(imported.methods.get(1).returnType)) {
            throw new AssertionError("source-free unit method return type was not restored");
        }
        Function consumer = new Function("consume", "fn consume(value: provider__Point)",
                1, "consumer.moss", false);
        consumer.params.add(new Param("value", "provider__Point"));
        consumer.setReturnType("unit");
        program.functions.add(consumer);
        String rust = NativeGenerator.generate(program, false);
        if (!rust.contains("value: &moss_provider::Point")) {
            throw new AssertionError("source-free nominal type did not lower to its provider crate path: " + rust);
        }
        if (rust.contains("pub struct provider__Point")
                || rust.contains("impl provider__Point")) {
            throw new AssertionError("source-free nominal was incorrectly redefined locally");
        }
        Program checkedConsumer = Parser.parseSource(""
                + "fn use_point(point: provider__Point) -> Float:\n"
                + "  return point.shift(2.0)\n"
                + "fn replace_values(point: provider__Point, values: Vector[Int]):\n"
                + "  point.replace(values)\n"
                + "fn main():\n"
                + "  pass\n", "consumer.moss");
        Main.loadMossiFunctions(interfaceFile, "provider", checkedConsumer);
        new SemanticChecker(checkedConsumer).run();
        String checkedRust = NativeGenerator.generate(checkedConsumer, false);
        if (!checkedRust.contains("fn replace_values(point: &mut moss_provider::Point, values: std::vec::Vec<i64>)")) {
            throw new AssertionError("source-free WRITE/CONSUME modes did not reach Rust parameter lowering: "
                    + checkedRust);
        }
        Program forwardedImportedConsume = Parser.parseSource(""
                + "fn relay(values, point):\n"
                + "  point.replace(values)\n"
                + "fn main():\n"
                + "  point = provider__Point(x=0.0, label=\"x\")\n"
                + "  values = [1]\n"
                + "  relay(values, point)\n"
                + "  echo values |> count\n", "source-free-method-consume-forwarded.moss");
        Main.loadMossiFunctions(interfaceFile, "provider", forwardedImportedConsume);
        try {
            new SemanticChecker(forwardedImportedConsume).run();
            Function relay = forwardedImportedConsume.functions.stream()
                    .filter(function -> function.name.equals("relay")).findFirst().orElseThrow();
            throw new AssertionError("imported CONSUME method mode was not propagated through relay: "
                    + relay.parameterAccessModes + " checked=" + relay.params.stream()
                            .map(parameter -> parameter.name + ":" + parameter.checkedTypeRef).toList());
        } catch (CompileError error) {
            if (!"OWNERSHIP_USE_AFTER_CONSUME".equals(error.getCode())) throw error;
        }
        Program unhandledMethodRaise = Parser.parseSource(""
                + "enum Fault:\n"
                + "  Bad\n"
                + "fn main():\n"
                + "  point = provider__Point(x=0.0, label=\"x\")\n"
                + "  point.signal()\n", "source-free-method-raise.moss");
        Main.loadMossiFunctions(interfaceFile, "provider", unhandledMethodRaise);
        try {
            new SemanticChecker(unhandledMethodRaise).run();
            throw new AssertionError("source-free method raise was allowed to escape main");
        } catch (CompileError error) {
            if (!"MOSS_COMPILE_ERROR".equals(error.getCode())) throw error;
        }
        Program handledMethodRaise = Parser.parseSource(""
                + "enum Fault:\n"
                + "  Bad\n"
                + "fn main():\n"
                + "  point = provider__Point(x=0.0, label=\"x\")\n"
                + "  try:\n"
                + "    point.signal()\n"
                + "  recover Fault.Bad:\n"
                + "    pass\n", "source-free-method-raise-handled.moss");
        Main.loadMossiFunctions(interfaceFile, "provider", handledMethodRaise);
        new SemanticChecker(handledMethodRaise).run();
        String handledMethodRust = NativeGenerator.generate(handledMethodRaise, false);
        if (!handledMethodRust.contains("point.signal().map_err(|e| __moss_raise(format!(\"{:?}\", e)))?")) {
            throw new AssertionError("source-free raising method error was not bridged into recover: "
                    + handledMethodRust);
        }
        Program helperMethodRaise = Parser.parseSource(""
                + "import provider\n"
                + "enum Fault:\n"
                + "  Bad\n"
                + "fn relay(point: provider__Point):\n"
                + "  point.signal()\n"
                + "fn main():\n"
                + "  point = provider__Point(x=0.0, label=\"x\")\n"
                + "  try:\n"
                + "    relay(point)\n"
                + "  recover Fault.Bad:\n"
                + "    pass\n", "source-free-method-raise-helper.moss");
        Main.loadMossiFunctions(interfaceFile, "provider", helperMethodRaise);
        new SemanticChecker(helperMethodRaise).run();
        String helperMethodRust = NativeGenerator.generate(helperMethodRaise, false);
        if (!helperMethodRust.contains("fn relay(")
                || !helperMethodRust.contains("point.signal().map_err(|e| __moss_raise(format!(\"{:?}\", e)))?")) {
            throw new AssertionError("helper did not propagate source-free method error: " + helperMethodRust);
        }
        Program methodLikeText = Parser.parseSource(""
                + "import provider\n"
                + "enum Fault:\n"
                + "  Bad\n"
                + "fn note(point: provider__Point):\n"
                + "  echo \"point.signal()\"\n"
                + "fn main():\n"
                + "  point = provider__Point(x=0.0, label=\"x\")\n"
                + "  note(point)\n", "source-free-method-name-in-string.moss");
        Main.loadMossiFunctions(interfaceFile, "provider", methodLikeText);
        new SemanticChecker(methodLikeText).run();
        String methodLikeTextRust = NativeGenerator.generate(methodLikeText, false);
        int noteDeclaration = methodLikeTextRust.indexOf("fn note(");
        int noteDeclarationEnd = noteDeclaration < 0 ? -1 : methodLikeTextRust.indexOf('\n', noteDeclaration);
        String noteSignature = noteDeclarationEnd < 0 ? ""
                : methodLikeTextRust.substring(noteDeclaration, noteDeclarationEnd);
        if (noteSignature.isEmpty() || noteSignature.contains("Result<")) {
            throw new AssertionError("raise-like method text made note a raising function: "
                    + methodLikeTextRust);
        }
        Program localRaisingMethod = Parser.parseSource(""
                + "enum Fault:\n"
                + "  Bad\n"
                + "type Box:\n"
                + "  fn poke() -> Int:\n"
                + "    raise Fault.Bad\n"
                + "fn invoke(box: Box) -> Int:\n"
                + "  return box.poke()\n"
                + "fn main():\n"
                + "  pass\n", "local-raising-poke-method.moss");
        new SemanticChecker(localRaisingMethod).run();
        String localRaisingMethodRust = NativeGenerator.generate(localRaisingMethod, false);
        if (!localRaisingMethodRust.contains("box.poke()?")
                || localRaisingMethodRust.contains("box.poke().map_err(|e| __moss_raise")) {
            throw new AssertionError("locally checked raising method bypassed method metadata lowering: "
                    + localRaisingMethodRust);
        }
        Program importedMethodSummary = Parser.parseSource(""
                + "fn relay(point: provider__Point, values: Vector[Int]):\n"
                + "  point.replace(values)\n"
                + "fn main():\n"
                + "  pass\n", "source-free-method-summary.moss");
        Main.loadMossiFunctions(interfaceFile, "provider", importedMethodSummary);
        new SemanticChecker(importedMethodSummary).run();
        Function relaySummary = importedMethodSummary.functions.stream()
                .filter(function -> function.name.equals("relay")).findFirst().orElseThrow();
        if (relaySummary.parameterAccessModes.get(0)
                != org.mosslang.compiler.types.AccessMode.Write
                || relaySummary.parameterAccessModes.get(1)
                != org.mosslang.compiler.types.AccessMode.Consume) {
            throw new AssertionError("source-free method receiver/argument effects were not summarized: "
                    + relaySummary.parameterAccessModes);
        }
        Program consumingConsumer = Parser.parseSource(""
                + "fn use_point(point: provider__Point, values: Vector[Int]) -> Vector[Int]:\n"
                + "  point.replace(values)\n"
                + "  return values\n"
                + "fn main():\n"
                + "  pass\n", "consumer-ownership.moss");
        Main.loadMossiFunctions(interfaceFile, "provider", consumingConsumer);
        try {
            new SemanticChecker(consumingConsumer).run();
            throw new AssertionError("source-free method CONSUME mode must consume its argument");
        } catch (CompileError error) {
            if (!"OWNERSHIP_USE_AFTER_CONSUME".equals(error.getCode())) {
                throw new AssertionError("expected OWNERSHIP_USE_AFTER_CONSUME, got "
                        + error.getCode(), error);
            }
        }
        Program forwardedConsume = Parser.parseSource(""
                + "fn forward(point: provider__Point, values: Vector[Int]):\n"
                + "  point.replace(values)\n"
                + "fn caller(point: provider__Point, values: Vector[Int]) -> Vector[Int]:\n"
                + "  forward(point, values)\n"
                + "  return values\n"
                + "fn main():\n"
                + "  pass\n", "consumer-forwarded-ownership.moss");
        Main.loadMossiFunctions(interfaceFile, "provider", forwardedConsume);
        try {
            new SemanticChecker(forwardedConsume).run();
            throw new AssertionError("source-free method CONSUME must propagate through a function helper");
        } catch (CompileError error) {
            if (!"OWNERSHIP_USE_AFTER_CONSUME".equals(error.getCode())) {
                throw new AssertionError("expected OWNERSHIP_USE_AFTER_CONSUME, got "
                        + error.getCode(), error);
            }
        }
        Program wrongReturnConsumer = Parser.parseSource(""
                + "fn wrong_return(point: provider__Point, values: Vector[Int]) -> Int:\n"
                + "  return point.replace(values)\n"
                + "fn main():\n"
                + "  pass\n", "consumer-return-type.moss");
        Main.loadMossiFunctions(interfaceFile, "provider", wrongReturnConsumer);
        try {
            new SemanticChecker(wrongReturnConsumer).run();
            throw new AssertionError("unit-returning source-free method cannot satisfy Int result");
        } catch (CompileError error) {
            if (!"MOSS_COMPILE_ERROR".equals(error.getCode())) {
                throw new AssertionError("expected return-type rejection, got " + error.getCode(), error);
            }
        }
        MossiInterface v10 = new MossiInterface("module-v1:sample@0.1.0::provider");
        v10.compiler = "moss-java-é";
        v10.binaryContract.add(new MossiRecord("type:Point", "type Point nominal\n"));
        v10.binaryContract.add(new MossiRecord("domain:Worker", ""
                + "domain Worker opaque\n"
                + "  handler_failure_arms \"Work\" 2\n"
                + "  handler_failure_arm \"Work\" 0 \"Fault.Bad\" \"__moss_on_fail_provider__Worker_Work_0\" 0 1 0 0 0\n"
                + "  handler_failure_arm \"Work\" 1 \"Fault.Other\" \"__moss_on_fail_provider__Worker_Work_1\" 0 1 0 0 0\n"));
        AbiV10Records.SourceProvenance pointProvenance = AbiV10Records.SourceProvenance.create(
                v10.moduleId, "nominal-template-v1:" + v10.moduleId + ":type:Point",
                "provider.moss", 1);
        AbiV10Records.NominalTemplate pointTemplate = AbiV10Records.NominalTemplate.create(
                v10.moduleId, "Point", java.util.List.of(new AbiV10Records.NominalField(
                        "x", AbiV10Records.WireType.scalar(
                                AbiV10Records.WireTypeKind.Primitive, "float"),
                        true, pointProvenance.provenanceId())), pointProvenance.provenanceId());
        String replaceMethodId = "method-template-v1:" + v10.moduleId
                + ":type:Point:method:replace";
        AbiV10Records.WireType vectorInt = AbiV10Records.WireType.scalar(
                AbiV10Records.WireTypeKind.Builtin, "vector");
        vectorInt.arguments.add(AbiV10Records.WireType.scalar(
                AbiV10Records.WireTypeKind.Primitive, "int"));
        AbiV10Records.MethodTemplate replaceMethod = AbiV10Records.MethodTemplate.create(
                replaceMethodId, pointTemplate.templateId, "replace",
                java.util.List.of(vectorInt), java.util.List.of(3),
                AbiV10Records.WireType.unit(), 3, pointProvenance.provenanceId());
        pointTemplate.methodTemplateIds.add(replaceMethodId);
        v10.binaryContract.add(pointTemplate.record());
        v10.binaryContract.add(replaceMethod.record());
        v10.provenance.add(pointProvenance.record());
        v10.semanticContract.add(new MossiRecord("type:Point", ""
                + "export type Point nominal\n"
                + "  declaration_id nominal-template-v1:module-v1:sample@0.1.0::provider:type:Point\n"));
        v10.semanticContract.add(new MossiRecord("00000000 export Outcome", ""
                + "export enum Outcome\n"
                + "  declaration_id nominal-enum-v1:module-v1:sample@0.1.0::provider:enum:Outcome\n"
                + "  enum_case \"Ready\" 1 \"code\" \"int\"\n"
                + "  enum_case \"Done\" 0\n"));
        v10.semanticContract.add(new MossiRecord("00000001 export Recorder", ""
                + "export domain Recorder opaque\n"
                + "  declaration_id nominal-domain-v1:module-v1:sample@0.1.0::provider:domain:Recorder\n"
                + "  construction_state \"count\" \"int\" \"0\"\n"
                + "  handler Count reply=int effects=local_capture_read=0;local_mutation=0;domain_read=1;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=-;may_diverge=0;unresolved=0\n"
                + "  handler_param \"Count\" \"offset\" \"int\"\n"));
        v10.semanticContract.add(new MossiRecord("00000002 export Worker", ""
                + "export domain Worker opaque\n"
                + "  declaration_id nominal-domain-v1:module-v1:sample@0.1.0::provider:domain:Worker\n"
                + "  handler Work reply=unit effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=enum:provider__Fault:Bad,enum:provider__Fault:Other;may_diverge=0;unresolved=0\n"));
        Path v10File = scratch.resolve("provider-v10.mossi");
        String encodedV10 = CheckedBodyCodec.encodeMossi(v10);
        Files.writeString(v10File, encodedV10, java.nio.charset.StandardCharsets.UTF_8);
        if (!encodedV10.equals(Main.readMossiText(v10File))) {
            throw new AssertionError("ABI-v10 interface was not decoded as UTF-8");
        }
        Program v10Consumer = new Program();
        Main.loadMossiFunctions(v10File, "provider", v10Consumer);
        ObjectType v10Point = v10Consumer.objects.stream()
                .filter(object -> object.name.equals("provider__Point"))
                .findFirst().orElseThrow();
        if (v10Point.methods.size() != 1
                || v10Point.fields.size() != 1
                || !"x".equals(v10Point.fields.get(0).name)
                || !"float".equals(v10Point.fields.get(0).type)
                || !"unit".equals(v10Point.methods.get(0).returnType)
                || v10Point.methods.get(0).receiverAccessMode
                        != org.mosslang.compiler.types.AccessMode.Consume
                || v10Point.methods.get(0).parameterAccessModes.size() != 1
                || v10Point.methods.get(0).parameterAccessModes.get(0)
                        != org.mosslang.compiler.types.AccessMode.Consume) {
            throw new AssertionError("ABI-v10 source-free object method facts were not restored");
        }
        Program consumedMethodConsumer = Parser.parseSource(""
                + "fn consume(point: provider__Point, values: Vector[Int]):\n"
                + "  point.replace(values)\n"
                + "  echo point.x\n"
                + "fn main():\n"
                + "  pass\n", "source-free-consuming-receiver.moss");
        Main.loadMossiFunctions(v10File, "provider", consumedMethodConsumer);
        try {
            new SemanticChecker(consumedMethodConsumer).run();
            throw new AssertionError("ABI-v10 source-free CONSUME receiver must consume its caller");
        } catch (CompileError error) {
            if (!"OWNERSHIP_USE_AFTER_CONSUME".equals(error.getCode())) {
                throw new AssertionError("expected source-free receiver consumption, got "
                        + error.getCode(), error);
            }
        }
        Program v10ForwardedMethodConsume = Parser.parseSource(""
                + "fn relay(values, point):\n"
                + "  point.replace(values)\n"
                + "fn main():\n"
                + "  point = provider__Point(x=0.0)\n"
                + "  values = [1]\n"
                + "  relay(values, point)\n"
                + "  echo values |> count\n", "source-free-v10-method-consume-forwarded.moss");
        Main.loadMossiFunctions(v10File, "provider", v10ForwardedMethodConsume);
        try {
            new SemanticChecker(v10ForwardedMethodConsume).run();
            throw new AssertionError("ABI-v10 imported CONSUME method mode was not propagated through relay");
        } catch (CompileError error) {
            if (!"OWNERSHIP_USE_AFTER_CONSUME".equals(error.getCode())) throw error;
        }
        EnumType v10Outcome = v10Consumer.enums.stream()
                .filter(enumeration -> enumeration.name.equals("provider__Outcome"))
                .findFirst().orElseThrow(() -> new AssertionError(
                        "ABI-v10 source-free enum was not imported"));
        if (!v10Outcome.sourceFree || v10Outcome.cases.size() != 2
                || !"Ready".equals(v10Outcome.cases.get(0).name)
                || v10Outcome.cases.get(0).fields.size() != 1
                || !"code".equals(v10Outcome.cases.get(0).fields.get(0).name)) {
            throw new AssertionError("ABI-v10 source-free enum payload was not restored");
        }
        Program enumConsumer = Parser.parseSource(""
                + "fn choose() -> provider__Outcome:\n"
                + "  return provider__Outcome.Ready(code=1)\n"
                + "fn main():\n"
                + "  pass\n", "enum-consumer.moss");
        Main.loadMossiFunctions(v10File, "provider", enumConsumer);
        new SemanticChecker(enumConsumer).run();
        String enumRust = NativeGenerator.generate(enumConsumer, false);
        if (!enumRust.contains("moss_provider::Outcome::Ready")
                || enumRust.contains("pub enum provider__Outcome")) {
            throw new AssertionError("source-free enum did not lower to its provider crate: " + enumRust);
        }
        Program domainConsumer = Parser.parseSource(""
                + "domain Client:\n"
                + "  domainroutes(recorder: provider.Recorder)\n"
                + "  fn Read() -> Int:\n"
                + "    reply message recorder.Count(1)\n"
                + "fn main():\n"
                + "  pass\n", "domain-consumer.moss");
        Main.loadMossiFunctions(v10File, "provider", domainConsumer);
        new SemanticChecker(domainConsumer).run();
        org.mosslang.compiler.ast.Domain importedRecorder = domainConsumer.domains.stream()
                .filter(domain -> domain.name.equals("provider__Recorder"))
                .findFirst().orElseThrow(() -> new AssertionError(
                        "ABI-v10 source-free domain was not imported"));
        if (!importedRecorder.sourceFree || importedRecorder.handlers.size() != 1
                || !"int".equals(importedRecorder.handlers.get(0).replyType)
                || importedRecorder.handlers.get(0).params.size() != 1
                || !"offset".equals(importedRecorder.handlers.get(0).params.get(0).name)
                || importedRecorder.handlers.get(0).sourceFreeMayRaise
                || importedRecorder.state.size() != 1
                || !"count".equals(importedRecorder.state.get(0).name)) {
            throw new AssertionError("ABI-v10 source-free handler signature was not restored");
        }
        String domainRust = NativeGenerator.generate(domainConsumer, false);
        if (!domainRust.contains("moss_provider::RecorderRef")
                || domainRust.contains("pub struct provider__RecorderRuntime")) {
            throw new AssertionError("source-free domain did not use its provider crate type: "
                    + domainRust);
        }
        MossiInterface v10WithoutFailure = new MossiInterface(v10.moduleId);
        v10WithoutFailure.compiler = v10.compiler;
        v10WithoutFailure.semanticContract.add(new MossiRecord("00000000 export Worker", ""
                + "export domain Worker opaque\n"
                + "  declaration_id nominal-domain-v1:module-v1:sample@0.1.0::provider:domain:Worker\n"
                + "  handler Work reply=unit effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=enum:provider__Fault:Bad,enum:provider__Fault:Other;may_diverge=0;unresolved=0\n"));
        Path v10NoFailureFile = scratch.resolve("provider-v10-no-failure.mossi");
        Files.writeString(v10NoFailureFile, CheckedBodyCodec.encodeMossi(v10WithoutFailure));
        Program v10UnhandledHandler = Parser.parseSource(""
                + "import provider\n"
                + "fn main():\n"
                + "  worker = provider.Worker()\n"
                + "  message worker.Work()\n", "source-free-v10-handler-raise-root.moss");
        Main.loadMossiFunctions(v10NoFailureFile, "provider", v10UnhandledHandler);
        try {
            new SemanticChecker(v10UnhandledHandler).run();
            throw new AssertionError("ABI-v10 source-free raising handler must require on_fail at root ingress");
        } catch (CompileError error) {
            if (!"ROOT_UNHANDLED_RAISE".equals(error.getCode())) {
                throw new AssertionError("expected ABI-v10 ROOT_UNHANDLED_RAISE, got "
                        + error.getCode(), error);
            }
        }
        MossiInterface v10PartialFailure = new MossiInterface(v10.moduleId);
        v10PartialFailure.compiler = v10.compiler;
        v10PartialFailure.binaryContract.add(new MossiRecord("domain:Worker", ""
                + "domain Worker opaque\n"
                + "  handler_failure_arms \"Work\" 1\n"
                + "  handler_failure_arm \"Work\" 0 \"Fault.Bad\" \"__moss_on_fail_provider__Worker_Work_0\" 0 1 0 0 0\n"));
        v10PartialFailure.semanticContract.addAll(v10WithoutFailure.semanticContract);
        Path v10PartialFailureFile = scratch.resolve("provider-v10-partial-failure.mossi");
        Files.writeString(v10PartialFailureFile, CheckedBodyCodec.encodeMossi(v10PartialFailure));
        Program v10PartialHandler = Parser.parseSource(""
                + "import provider\n"
                + "fn main():\n"
                + "  worker = provider.Worker()\n"
                + "  message worker.Work()\n", "source-free-v10-handler-partial-failure.moss");
        Main.loadMossiFunctions(v10PartialFailureFile, "provider", v10PartialHandler);
        try {
            new SemanticChecker(v10PartialHandler).run();
            throw new AssertionError("ABI-v10 on_fail arms must cover every imported raise variant");
        } catch (CompileError error) {
            if (!"ROOT_UNHANDLED_RAISE".equals(error.getCode())) {
                throw new AssertionError("expected ABI-v10 ROOT_UNHANDLED_RAISE for partial coverage, got "
                        + error.getCode(), error);
            }
        }
        Program v10HandledHandler = Parser.parseSource(""
                + "import provider\n"
                + "fn main():\n"
                + "  worker = provider.Worker()\n"
                + "  executor = Executor().threads(1).start()\n"
                + "  executor.invoke(worker.Work())\n"
                + "  executor.join()\n", "source-free-v10-handler-root-handled.moss");
        Main.loadMossiFunctions(v10File, "provider", v10HandledHandler);
        new SemanticChecker(v10HandledHandler).run();
        org.mosslang.compiler.ast.Domain importedV10Worker = v10HandledHandler.domains.stream()
                .filter(domain -> domain.name.equals("provider__Worker")).findFirst()
                .orElseThrow(() -> new AssertionError("ABI-v10 source-free Worker was not imported"));
        if (importedV10Worker.handlers.size() != 1 || importedV10Worker.handlers.get(0).failureArms.size() != 2) {
            throw new AssertionError("ABI-v10 source-free failure-arm metadata was not restored");
        }
        String v10HandledRust = NativeGenerator.generate(v10HandledHandler, false);
        if (!v10HandledRust.contains(".Work_root()))")) {
            throw new AssertionError("ABI-v10 handled Executor root did not use Work_root");
        }
        Files.deleteIfExists(interfaceFile);
        Files.deleteIfExists(raisingInterface);
        Files.deleteIfExists(partialFailureInterface);
        Files.deleteIfExists(v10File);
        Files.deleteIfExists(v10NoFailureFile);
        Files.deleteIfExists(v10PartialFailureFile);
        Files.deleteIfExists(scratch);
        System.out.println("SourceFreeObjectMetadataTest PASSED.");
    }
}
