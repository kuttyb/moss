package org.mosslang.compiler.project;

import org.mosslang.compiler.ast.EnumCase;
import org.mosslang.compiler.ast.EnumType;
import org.mosslang.compiler.ast.Field;
import org.mosslang.compiler.ast.Function;
import org.mosslang.compiler.ast.Param;
import org.mosslang.compiler.ast.Program;
import org.mosslang.compiler.abi.CheckedBodyCodec;
import org.mosslang.compiler.abi.MossiInterface;
import org.mosslang.compiler.abi.MossiRecord;
import org.mosslang.compiler.abi.AbiV10Records;
import org.mosslang.compiler.diagnostics.CompileError;
import org.mosslang.compiler.types.AccessMode;
import org.mosslang.compiler.parser.Parser;

import java.nio.file.Path;
import java.nio.file.Files;
import java.util.List;

public final class ProjectBuilderMetadataTest {
    public static void main(String[] args) throws Exception {
        EnumCase payload = new EnumCase("Rgb", 1, "metadata.moss");
        payload.fields.add(new Field("red", "Int", "", "", 1, "metadata.moss", false, true));
        payload.fields.add(new Field("green", "Float", "", "", 1, "metadata.moss", false, true));
        payload.fields.add(new Field("blue", "String", "", "", 1, "metadata.moss", false, true));

        StringBuilder contract = new StringBuilder();
        ProjectBuilder.appendEnumCaseContract(contract, payload);
        assertEquals("case Rgb red:int green:float blue:string\n", contract.toString());

        StringBuilder semantic = new StringBuilder();
        ProjectBuilder.appendEnumCaseSemantic(semantic, payload);
        assertEquals("  enum_case \"Rgb\" 3 \"red\" \"int\" \"green\" \"float\" \"blue\" \"string\"\n",
                semantic.toString());
        EnumType exportedEnum = new EnumType("provider__Color", "enum Color", 1,
                "metadata.moss", true);
        exportedEnum.cases.add(payload);
        MossiRecord enumRecord = ProjectBuilder.v10ExportedEnumRecord(exportedEnum,
                "provider", "module-v1:sample@1.0.0::provider", 0);
        assertEquals("00000000 export Color", enumRecord.id);
        if (!enumRecord.payload.contains("nominal-enum-v1:module-v1:sample@1.0.0::provider:enum:Color")
                || !enumRecord.payload.contains("enum_case \"Rgb\" 3")) {
            throw new AssertionError("ABI-v10 exported enum semantic record is incomplete: "
                    + enumRecord.payload);
        }
        testV10ImportRecordRequiresModuleBoundHashes();
        testV10SectionHashValidation();
        testV10Utf8InterfaceValidation();
        testV10RecordFramingUsesUtf8Bytes();
        testCanonicalNominalTemplateWirePayload();
        testCheckedOnFailCaptureAbiRows();
        testExplicitModuleBuildChecksSemantics();
        testV9FunctionParameterAccessSerialization();
        testExportedObjectFieldSerialization();
        testExportedObjectMethodSerialization();
        if ("v10".equals(System.getenv("MOSS_STAGED_ABI"))) {
            testV10ProjectProducerRoundTrip();
        }
        System.out.println("ProjectBuilderMetadataTest PASSED.");
    }

    private static void testCheckedOnFailCaptureAbiRows() {
        Program program = Parser.parseSource(""
                + "module provider\n"
                + "enum Failure:\n"
                + "  Bad\n"
                + "domain Worker:\n"
                + "  fn Work() -> Int:\n"
                + "    var attempt = 3\n"
                + "    raise Failure.Bad\n"
                + "  on_fail Failure.Bad:\n"
                + "    reply attempt\n", "capture-abi.moss");
        program.domains.get(0).exported = true;
        new org.mosslang.compiler.semantics.CheckedProgram(program).run();
        ProjectManifest manifest = new ProjectManifest(Path.of("tmp"), Path.of("tmp", "moss.toml"),
                "sample", "1.0.0", Path.of("src"));
        BackendToolchain toolchain = new BackendToolchain("rustc", "rustc 1.80.0 (test)\n",
                "dev", java.util.List.of(), "capture-abi-test");
        byte[] generated = ProjectBuilder.generateMossiInterface(manifest, "provider",
                program, toolchain, java.util.List.of(), Path.of("tmp"), java.util.Map.of());
        String text = new String(generated, java.nio.charset.StandardCharsets.UTF_8);
        if (!text.contains("handler_owned_type_shapes \"Work\" 1\n")
                || !text.contains("handler_owned_type_shape \"Work\" \"int\" \"0:3:int:0:\"\n")
                || !text.contains("handler_failure_arm \"Work\" 0 \"Failure.Bad\" ")
                || !text.contains("handler_failure_capture \"Work\" 0 \"attempt\" \"int\" \"attempt\" 1 1 \"0:3:int:0:\"\n")) {
            throw new AssertionError("project producer did not serialize checker-proved on_fail captures: " + text);
        }
    }

    private static void testExplicitModuleBuildChecksSemantics() throws Exception {
        Path root = Files.createTempDirectory(Path.of("tmp"), "java-module-check-");
        Path source = root.resolve("src");
        Files.createDirectories(source);
        Files.writeString(source.resolve("provider.moss"), ""
                + "module provider\n"
                + "export fn value() -> Int:\n"
                + "  return 1\n");
        Files.writeString(source.resolve("app.moss"), ""
                + "module app\n"
                + "import provider\n"
                + "fn bad() -> Int:\n"
                + "  return \"wrong\"\n"
                + "fn main():\n"
                + "  pass\n");
        ProjectManifest manifest = new ProjectManifest(root, root.resolve("moss.toml"),
                "sample", "1.0.0", Path.of("src"));
        try {
            ProjectBuilder.build(manifest, false, false);
            throw new AssertionError("explicit module project must be semantically checked before codegen");
        } catch (CompileError error) {
            if (!"TYPE_MISMATCH".equals(error.getCode())
                    && !"MOSS_COMPILE_ERROR".equals(error.getCode())) {
                throw new AssertionError("expected semantic type diagnostic, got " + error.getCode(), error);
            }
        }
    }

    private static void testV9FunctionParameterAccessSerialization() {
        Function function = new Function("provider__f", "fn f(a: Int, b: Vector[Int], c: Vector[Int])", 3,
                "provider.moss", true);
        function.params.add(new Param("a", "Int"));
        function.params.add(new Param("b", "Vector[Int]"));
        function.params.add(new Param("c", "Vector[Int]"));
        function.parameterAccessModes.add(AccessMode.Read);
        function.parameterAccessModes.add(AccessMode.Write);
        function.parameterAccessModes.add(AccessMode.Consume);
        StringBuilder record = new StringBuilder();
        ProjectBuilder.appendV9FunctionRecord(record, function, "f", "effects=may_panic=0", false);
        assertContains(record.toString(), "param 0 Int mode=READ name=\"a\"");
        assertContains(record.toString(), "param 1 Vector[Int] mode=WRITE name=\"b\"");
        assertContains(record.toString(), "param 2 Vector[Int] mode=CONSUME name=\"c\"");
    }

    private static void testExportedObjectFieldSerialization() {
        org.mosslang.compiler.ast.ObjectType object =
                new org.mosslang.compiler.ast.ObjectType("Point", "type Point", 1,
                        "provider.moss", true);
        object.fields.add(new Field("x", "float", "", "", 2,
                "provider.moss", false, true));
        object.fields.add(new Field("label", "string", "", "", 3,
                "provider.moss", false, true));
        StringBuilder fields = new StringBuilder();
        ProjectBuilder.appendExportedObjectFields(fields, object);
        assertEquals("  public_representation field x float\n"
                        + "  public_representation field label string\n",
                fields.toString());
    }

    private static void testExportedObjectMethodSerialization() {
        org.mosslang.compiler.ast.ObjectType object =
                new org.mosslang.compiler.ast.ObjectType("Point", "type Point", 1,
                        "provider.moss", true);
        object.fields.add(new Field("x", "float", "", "", 2,
                "provider.moss", false, true));
        org.mosslang.compiler.ast.Method method = new org.mosslang.compiler.ast.Method(
                "Point", "shift", 4, "provider.moss");
        method.setReturnType("float");
        method.receiverAccessMode = AccessMode.Write;
        method.params.add(new Param("amount", "float"));
        method.parameterAccessModes.add(AccessMode.Consume);
        method.body.add(new org.mosslang.compiler.ast.Stmt(
                org.mosslang.compiler.ast.Stmt.Kind.Assign, 5, 0,
                "x = x + amount", "x", "x + amount"));
        StringBuilder record = new StringBuilder();
        ProjectBuilder.appendV9ObjectMethodRecord(record, object, method, "provider");
        assertContains(record.toString(), "object_method shift return=float receiver=WRITE");
        assertContains(record.toString(), "params=1 \"amount\" \"float\" CONSUME");
    }

    private static void testV10ImportRecordRequiresModuleBoundHashes() {
        SourceFreeAbi.MossiInfo dependency = new SourceFreeAbi.MossiInfo();
        dependency.moduleId = "module-v1:provider@1.0.0::service";
        dependency.binaryHash = "sha256:" + "1".repeat(64);
        dependency.semanticHash = "sha256:" + "2".repeat(64);
        assertEquals(dependency.moduleId + " " + dependency.binaryHash + " "
                        + dependency.semanticHash + "\n",
                ProjectBuilder.v10ImportRecord("service", dependency, Path.of("service.mossi")));

        dependency.binaryHash = null;
        try {
            ProjectBuilder.v10ImportRecord("service", dependency, Path.of("service.mossi"));
            throw new AssertionError("ABI-v10 imports require both provider contract hashes");
        } catch (CompileError error) {
            if (!"SOURCE_FREE_ABI_REBUILD_REQUIRED".equals(error.getCode())) {
                throw new AssertionError("expected SOURCE_FREE_ABI_REBUILD_REQUIRED, got " + error.getCode(), error);
            }
        }
    }

    private static void testV10SectionHashValidation() {
        MossiInterface valid = new MossiInterface("module-v1:sample@1.0.0::provider");
        String encoded = CheckedBodyCodec.encodeMossi(valid);
        String corrupted = encoded.replaceFirst("semantic_contract_hash sha256:[0-9a-f]{64}",
                "semantic_contract_hash sha256:" + "0".repeat(64));
        int digestLineEnd = corrupted.indexOf('\n', corrupted.indexOf("provider_contract_digest "));
        String material = "moss-module-interface-v3\n" + corrupted.substring(digestLineEnd + 1);
        String digest = CheckedBodyCodec.sha256Hex(material);
        corrupted = "moss-module-interface-v3\nprovider_contract_digest sha256:" + digest
                + corrupted.substring(digestLineEnd);
        try {
            SourceFreeAbi.validateMossiText(Path.of("provider.mossi"), corrupted, true);
            throw new AssertionError("an invalid ABI-v10 section hash must be rejected");
        } catch (CompileError error) {
            if (!"SOURCE_FREE_CONTRACT_INVALID".equals(error.getCode())) {
                throw new AssertionError("expected SOURCE_FREE_CONTRACT_INVALID, got " + error.getCode(), error);
            }
        }
    }

    private static void testV10Utf8InterfaceValidation() throws Exception {
        Path dir = Path.of("tmp", "java-v10-utf8-validation");
        Files.createDirectories(dir);
        Path interfaceFile = dir.resolve("provider.mossi");
        Path rlib = dir.resolve("provider.rlib");
        MossiInterface iface = new MossiInterface("module-v1:sample@1.0.0::provider");
        iface.compiler = "moss-java-é";
        iface.binaryContract.add(MossiRecord.fromBytes("wire-record",
                new byte[] {0, 1, (byte) 0x80, (byte) 0xff}));
        byte[] encoded = CheckedBodyCodec.encodeMossiBytes(iface);
        Files.write(interfaceFile, encoded);
        Files.writeString(rlib, "MOSS_ABI_V10_CONTRACT::provider::" + iface.digest + "::END",
                java.nio.charset.StandardCharsets.US_ASCII);
        try {
            SourceFreeAbi.validateMossiInterface(interfaceFile);
        } finally {
            Files.deleteIfExists(rlib);
            Files.deleteIfExists(interfaceFile);
            Files.deleteIfExists(dir);
        }
    }

    private static void testV10RecordFramingUsesUtf8Bytes() {
        StringBuilder frame = new StringBuilder();
        ProjectBuilder.appendV10FramedRecord(frame, "fn:é", "café\n");
        assertEquals("record 5 6\nfn:écafé\n\n", frame.toString());
    }

    private static void testCanonicalNominalTemplateWirePayload() {
        String module = "module-v1:sample@1.0.0::provider";
        String provenance = "sha256:" + "1".repeat(64);
        AbiV10Records.NominalTemplate template = AbiV10Records.NominalTemplate.create(
                module, "Point",
                java.util.List.of(new AbiV10Records.NominalField("x",
                        AbiV10Records.WireType.scalar(
                                AbiV10Records.WireTypeKind.Primitive, "float"),
                        true, provenance)), provenance);
        String goldenHex = "010000003f6e6f6d696e616c2d74656d706c6174652d76313a6d6f64756c652d76313a73616d706c6540312e302e303a3a70726f76696465723a747970653a506f696e74000000206d6f64756c652d76313a73616d706c6540312e302e303a3a70726f766964657200000005506f696e74000000000000000100000001780100000005666c6f61740000000001000000477368613235363a31313131313131313131313131313131313131313131313131313131313131313131313131313131313131313131313131313131313131313131313131313131000000477368613235363a3461383861316639643161376261626238646538643762643765323436336265346361313765323263353631343166616437356532393262333965623761356100000000000000477368613235363a31313131313131313131313131313131313131313131313131313131313131313131313131313131313131313131313131313131313131313131313131313131";
        String actualHex = java.util.HexFormat.of().formatHex(template.record().payloadBytes());
        assertEquals(goldenHex, actualHex);
        AbiV10Records.NominalTemplate decoded = AbiV10Records.NominalTemplate.decode(template.record());
        assertEquals(template.representationHash, decoded.representationHash);
        assertEquals(template.templateId, decoded.templateId);
    }

    private static void testV10ProjectProducerRoundTrip() {
        Program program = Parser.parseSource(""
                + "type Point:\n"
                + "  x: Float\n"
                + "  y: Int\n"
                + "enum Outcome:\n"
                + "  Ready(code: Int)\n"
                + "  Done\n"
                + "domain Recorder:\n"
                + "  count: Int\n"
                + "  fn Count() -> Int:\n"
                + "    reply count\n"
                + "enum Failure:\n"
                + "  Bad\n"
                + "domain Worker:\n"
                + "  retries: Int\n"
                + "  fn Work() -> Int:\n"
                + "    raise Failure.Bad\n"
                + "  on_fail Failure.Bad:\n"
                + "    retries = retries + 1\n"
                + "    reply retries\n", "provider.moss");
        program.objects.get(0).exported = true;
        org.mosslang.compiler.ast.Method shift = new org.mosslang.compiler.ast.Method(
                "Point", "ready", 4, "provider.moss");
        shift.setReturnType("unit");
        program.objects.get(0).methods.add(shift);
        program.enums.get(0).exported = true;
        program.domains.get(0).exported = true;
        program.domains.get(1).exported = true;
        ProjectManifest manifest = new ProjectManifest(Path.of("tmp"),
                Path.of("tmp", "moss.toml"), "sample", "1.0.0", Path.of("src"));
        BackendToolchain toolchain = new BackendToolchain("rustc", "rustc 1.80.0 (test)\n",
                "dev", java.util.List.of(), "fingerprint-v10-test");
        byte[] generated = ProjectBuilder.generateMossiInterface(manifest, "provider",
                program, toolchain, java.util.List.of(), Path.of("tmp"), java.util.Map.of());
        MossiInterface decoded = CheckedBodyCodec.verifySourceFree(generated);
        AbiV10Records.NominalTemplate pointTemplate = decoded.binaryContract.stream()
                .filter(record -> record.id.startsWith("nominal-template-v1:"))
                .map(AbiV10Records.NominalTemplate::decode).findFirst().orElseThrow();
        if (pointTemplate.methodTemplateIds.size() != 1) {
            throw new AssertionError("ABI-v10 nominal template omitted its method reference");
        }
        AbiV10Records.MethodTemplate methodTemplate = decoded.binaryContract.stream()
                .filter(record -> record.id.equals(pointTemplate.methodTemplateIds.get(0)))
                .map(AbiV10Records.MethodTemplate::decode).findFirst().orElseThrow();
        if (!"ready".equals(methodTemplate.methodName)
                || methodTemplate.receiverAccess != AccessMode.Read.ordinal() + 1
                || !methodTemplate.parameterAccess.isEmpty()
                || !"unit".equals(methodTemplate.result.canonicalName())) {
            throw new AssertionError("ABI-v10 method template omitted its representable no-op signature");
        }
        if (decoded.binaryContract.stream().noneMatch(record ->
                    record.id.startsWith("nominal-template-v1:") && record.payloadBytes().length > 0)
                || decoded.semanticContract.stream().noneMatch(record ->
                    record.id.equals("00000000 export Outcome")
                    && record.payload.contains("enum_case \"Ready\" 1 \"code\" \"int\""))) {
            throw new AssertionError("Margo ABI-v10 producer omitted canonical nominal/enum facts");
        }
        if (decoded.semanticContract.stream().noneMatch(record ->
                    record.id.equals("00000001 export Recorder")
                    && record.payload.contains("export domain Recorder opaque")
                    && record.payload.contains("handler Count reply=int"))) {
            throw new AssertionError("Margo ABI-v10 producer omitted exported domain facts");
        }
        if (decoded.semanticContract.stream().noneMatch(record ->
                    record.payload.contains("handler_failure_arm \"Work\" 0 \"Failure.Bad\" ")
                    && record.payload.contains(" 1 \"retries\" 1 \"retries\" 0\n"))) {
            throw new AssertionError("Margo ABI-v10 producer did not derive failure-arm state effects");
        }

        Program identityProgram = Parser.parseSource(""
                + "module provider\n"
                + "export fn identity(value):\n"
                + "  return value\n"
                + "export fn second(left, right):\n"
                + "  return right\n"
                + "export fn answer() -> Int:\n"
                + "  return 42\n"
                + "export fn same_vector(values: Vector[Int]) -> Vector[Int]:\n"
                + "  return values\n"
                + "export fn enabled() -> Bool:\n"
                + "  return true\n"
                + "export fn ratio() -> Float:\n"
                + "  return 1.5\n"
                + "export fn increment(value: Int) -> Int:\n"
                + "  return value + 1\n"
                + "export fn scale(value: Int) -> Int:\n"
                + "  return value * 2\n"
                + "export fn subtract(value: Int) -> Int:\n"
                + "  return value - 2\n"
                + "export fn divide(value: Int) -> Int:\n"
                + "  return value / 2\n"
                + "export fn modulo(value: Int) -> Int:\n"
                + "  return value % 2\n"
                + "export fn combine(left: Int, right: Int) -> Int:\n"
                + "  return left * 3 + right / 2\n"
                + "export fn negated(value: Int) -> Int:\n"
                + "  return -value + 4\n"
                + "export fn negative() -> Int:\n"
                + "  return -42\n"
                + "export fn negative_ratio() -> Float:\n"
                + "  return -1.5\n"
                + "export fn weighted(left: Float, right: Float) -> Float:\n"
                + "  return left * 2.5 + right / 2.0\n"
                + "export fn adjust_ratio(value: Float) -> Float:\n"
                + "  return -value + .5\n"
                + "export fn is_positive(value: Int) -> Bool:\n"
                + "  return value > 0\n"
                + "export fn greeting() -> String:\n"
                + "  return \"hello\"\n", "identity-provider.moss");
        new org.mosslang.compiler.semantics.CheckedProgram(identityProgram).run();
        identityProgram.functions.forEach(function -> {
            String name = function.name;
            int separator = Math.max(name.lastIndexOf('.'), name.lastIndexOf("__"));
            String functionName = name.substring(separator + (name.contains("__") ? 2 : 1));
            function.semanticCallableId = "callable-v1:module-v1:sample@1.0.0::provider:fn:" + functionName;
        });
        MossiInterface identityInterface = CheckedBodyCodec.verifySourceFree(
                ProjectBuilder.generateMossiInterface(manifest, "provider", identityProgram,
                        toolchain, java.util.List.of(), Path.of("tmp"), java.util.Map.of()));
        AbiV10Records.Summary identitySummary = identityInterface.binaryContract.stream()
                .filter(record -> record.payloadBytes().length > 0
                        && (record.payloadBytes()[0] == 3 || record.payloadBytes()[0] == 4))
                .map(AbiV10Records.Summary::decode)
                .filter(summary -> summary.callableId.endsWith(":fn:identity"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the checked generic identity summary"));
        AbiV10Records.CheckedBodyRecord identityBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:identity"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the checked generic identity body"));
        if (!identitySummary.callableId.equals(identityBody.callableId)
                || identitySummary.variableCount != 1
                || identitySummary.parameters.size() != 1
                || identitySummary.parameters.get(0).kind != AbiV10Records.WireTypeKind.BoundVar
                || identitySummary.parameters.get(0).ordinal != 0
                || identitySummary.result.kind != AbiV10Records.WireTypeKind.BoundVar
                || identitySummary.result.ordinal != 0
                || !identitySummary.bodySemanticHash.equals(identityBody.semanticHash)) {
            throw new AssertionError("ABI-v10 identity summary/body did not retain its checked type relation");
        }
        AbiV10Records.Summary secondSummary = identityInterface.binaryContract.stream()
                .filter(record -> record.payloadBytes().length > 0
                        && (record.payloadBytes()[0] == 3 || record.payloadBytes()[0] == 4))
                .map(AbiV10Records.Summary::decode)
                .filter(summary -> summary.callableId.endsWith(":fn:second"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the multi-parameter direct-return summary"));
        AbiV10Records.CheckedBodyRecord secondBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:second"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the multi-parameter direct-return body"));
        java.util.List<org.mosslang.compiler.abi.CheckedStmt> secondStatements =
                CheckedBodyCodec.decodeCheckedBody(secondBody.checkedIrBytes);
        if (secondSummary.variableCount != 2 || secondSummary.parameters.size() != 2
                || secondSummary.result.kind != AbiV10Records.WireTypeKind.BoundVar
                || secondSummary.result.ordinal != 1
                || secondStatements.size() != 1
                || secondStatements.get(0).expr.ordinal != 1
                || !secondSummary.bodySemanticHash.equals(secondBody.semanticHash)) {
            throw new AssertionError("ABI-v10 multi-parameter summary/body lost its second-parameter relation");
        }
        AbiV10Records.Summary answerSummary = identityInterface.binaryContract.stream()
                .filter(record -> record.payloadBytes().length > 0
                        && (record.payloadBytes()[0] == 3 || record.payloadBytes()[0] == 4))
                .map(AbiV10Records.Summary::decode)
                .filter(summary -> summary.callableId.endsWith(":fn:answer"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the constant-return summary"));
        AbiV10Records.CheckedBodyRecord answerBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:answer"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the constant-return body"));
        java.util.List<org.mosslang.compiler.abi.CheckedStmt> answerStatements =
                CheckedBodyCodec.decodeCheckedBody(answerBody.checkedIrBytes);
        if (!answerSummary.parameters.isEmpty()
                || answerSummary.result.kind != AbiV10Records.WireTypeKind.Primitive
                || !"int".equals(answerSummary.result.identity)
                || answerStatements.size() != 1
                || answerStatements.get(0).expr.kind != org.mosslang.compiler.abi.CheckedExprKind.IntegerLiteral
                || !"42".equals(answerStatements.get(0).expr.literalValue)
                || !answerSummary.bodySemanticHash.equals(answerBody.semanticHash)) {
            throw new AssertionError("ABI-v10 constant-return summary/body is incomplete");
        }
        AbiV10Records.Summary negativeSummary = identityInterface.binaryContract.stream()
                .filter(record -> record.payloadBytes().length > 0
                        && (record.payloadBytes()[0] == 3 || record.payloadBytes()[0] == 4))
                .map(AbiV10Records.Summary::decode)
                .filter(summary -> summary.callableId.endsWith(":fn:negative"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the negative integer constant summary"));
        AbiV10Records.CheckedBodyRecord negativeBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:negative"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the negative integer constant body"));
        java.util.List<org.mosslang.compiler.abi.CheckedStmt> negativeStatements =
                CheckedBodyCodec.decodeCheckedBody(negativeBody.checkedIrBytes);
        if (!negativeSummary.parameters.isEmpty()
                || negativeStatements.size() != 1
                || negativeStatements.get(0).expr.kind != org.mosslang.compiler.abi.CheckedExprKind.IntegerLiteral
                || !"-42".equals(negativeStatements.get(0).expr.literalValue)
                || !negativeSummary.bodySemanticHash.equals(negativeBody.semanticHash)) {
            throw new AssertionError("ABI-v10 negative integer constant body is incomplete");
        }
        AbiV10Records.Summary vectorSummary = identityInterface.binaryContract.stream()
                .filter(record -> record.payloadBytes().length > 0
                        && (record.payloadBytes()[0] == 3 || record.payloadBytes()[0] == 4))
                .map(AbiV10Records.Summary::decode)
                .filter(summary -> summary.callableId.endsWith(":fn:same_vector"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the concrete collection identity summary"));
        if (vectorSummary.parameters.size() != 1
                || vectorSummary.parameters.get(0).kind != AbiV10Records.WireTypeKind.Builtin
                || vectorSummary.parameters.get(0).arguments.size() != 1
                || vectorSummary.parameters.get(0).arguments.get(0).kind != AbiV10Records.WireTypeKind.Primitive
                || !"int".equals(vectorSummary.parameters.get(0).arguments.get(0).identity)
                || vectorSummary.result.kind != AbiV10Records.WireTypeKind.Builtin
                || !"vector".equals(vectorSummary.result.identity)) {
            throw new AssertionError("ABI-v10 collection identity signature lost its nested element type");
        }
        AbiV10Records.Summary enabledSummary = identityInterface.binaryContract.stream()
                .filter(record -> record.payloadBytes().length > 0
                        && (record.payloadBytes()[0] == 3 || record.payloadBytes()[0] == 4))
                .map(AbiV10Records.Summary::decode)
                .filter(summary -> summary.callableId.endsWith(":fn:enabled"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the boolean constant summary"));
        AbiV10Records.CheckedBodyRecord enabledBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:enabled"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the boolean constant body"));
        java.util.List<org.mosslang.compiler.abi.CheckedStmt> enabledStatements =
                CheckedBodyCodec.decodeCheckedBody(enabledBody.checkedIrBytes);
        if (!enabledSummary.parameters.isEmpty()
                || enabledSummary.result.kind != AbiV10Records.WireTypeKind.Primitive
                || !"bool".equals(enabledSummary.result.identity)
                || enabledStatements.size() != 1
                || enabledStatements.get(0).expr.kind != org.mosslang.compiler.abi.CheckedExprKind.BoolLiteral
                || !"true".equals(enabledStatements.get(0).expr.literalValue)
                || !enabledSummary.bodySemanticHash.equals(enabledBody.semanticHash)) {
            throw new AssertionError("ABI-v10 boolean constant summary/body is incomplete");
        }
        AbiV10Records.Summary ratioSummary = identityInterface.binaryContract.stream()
                .filter(record -> record.payloadBytes().length > 0
                        && (record.payloadBytes()[0] == 3 || record.payloadBytes()[0] == 4))
                .map(AbiV10Records.Summary::decode)
                .filter(summary -> summary.callableId.endsWith(":fn:ratio"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the float constant summary"));
        AbiV10Records.CheckedBodyRecord ratioBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:ratio"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the float constant body"));
        java.util.List<org.mosslang.compiler.abi.CheckedStmt> ratioStatements =
                CheckedBodyCodec.decodeCheckedBody(ratioBody.checkedIrBytes);
        if (!ratioSummary.parameters.isEmpty()
                || ratioSummary.result.kind != AbiV10Records.WireTypeKind.Primitive
                || !"float".equals(ratioSummary.result.identity)
                || ratioStatements.size() != 1
                || ratioStatements.get(0).expr.kind != org.mosslang.compiler.abi.CheckedExprKind.FloatLiteral
                || !"1.5".equals(ratioStatements.get(0).expr.literalValue)
                || !ratioSummary.bodySemanticHash.equals(ratioBody.semanticHash)) {
            throw new AssertionError("ABI-v10 float constant summary/body is incomplete");
        }
        AbiV10Records.CheckedBodyRecord negativeRatioBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:negative_ratio"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the signed float constant body"));
        java.util.List<org.mosslang.compiler.abi.CheckedStmt> negativeRatioStatements =
                CheckedBodyCodec.decodeCheckedBody(negativeRatioBody.checkedIrBytes);
        if (negativeRatioStatements.size() != 1
                || negativeRatioStatements.get(0).expr.kind != org.mosslang.compiler.abi.CheckedExprKind.FloatLiteral
                || !"-1.5".equals(negativeRatioStatements.get(0).expr.literalValue)) {
            throw new AssertionError("ABI-v10 signed float constant body is incomplete");
        }
        AbiV10Records.CheckedBodyRecord greetingBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:greeting"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the String constant body"));
        java.util.List<org.mosslang.compiler.abi.CheckedStmt> greetingStatements =
                CheckedBodyCodec.decodeCheckedBody(greetingBody.checkedIrBytes);
        if (greetingStatements.size() != 1
                || greetingStatements.get(0).expr.kind != org.mosslang.compiler.abi.CheckedExprKind.StringLiteral
                || !"hello".equals(greetingStatements.get(0).expr.literalValue)) {
            throw new AssertionError("ABI-v10 String constant body is incomplete");
        }
        AbiV10Records.Summary incrementSummary = identityInterface.binaryContract.stream()
                .filter(record -> record.payloadBytes().length > 0
                        && (record.payloadBytes()[0] == 3 || record.payloadBytes()[0] == 4))
                .map(AbiV10Records.Summary::decode)
                .filter(summary -> summary.callableId.endsWith(":fn:increment"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the integer-add summary"));
        AbiV10Records.CheckedBodyRecord incrementBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:increment"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the integer-add body"));
        java.util.List<org.mosslang.compiler.abi.CheckedStmt> incrementStatements =
                CheckedBodyCodec.decodeCheckedBody(incrementBody.checkedIrBytes);
        if (incrementSummary.parameters.size() != 1
                || incrementSummary.parameters.get(0).kind != AbiV10Records.WireTypeKind.Primitive
                || !"int".equals(incrementSummary.parameters.get(0).identity)
                || incrementSummary.result.kind != AbiV10Records.WireTypeKind.Primitive
                || !"int".equals(incrementSummary.result.identity)
                || incrementStatements.size() != 1
                || incrementStatements.get(0).expr.kind != org.mosslang.compiler.abi.CheckedExprKind.Binary
                || incrementStatements.get(0).expr.binaryOp != org.mosslang.compiler.abi.CheckedBinaryOp.Add
                || incrementStatements.get(0).expr.operands.get(0).kind
                        != org.mosslang.compiler.abi.CheckedExprKind.Parameter
                || !"1".equals(incrementStatements.get(0).expr.operands.get(1).literalValue)
                || !incrementSummary.bodySemanticHash.equals(incrementBody.semanticHash)) {
            throw new AssertionError("ABI-v10 integer-add summary/body is incomplete");
        }
        AbiV10Records.CheckedBodyRecord scaleBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:scale"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the integer-multiply body"));
        java.util.List<org.mosslang.compiler.abi.CheckedStmt> scaleStatements =
                CheckedBodyCodec.decodeCheckedBody(scaleBody.checkedIrBytes);
        if (scaleStatements.size() != 1
                || scaleStatements.get(0).expr.kind != org.mosslang.compiler.abi.CheckedExprKind.Binary
                || scaleStatements.get(0).expr.binaryOp != org.mosslang.compiler.abi.CheckedBinaryOp.Mul
                || !"2".equals(scaleStatements.get(0).expr.operands.get(1).literalValue)) {
            throw new AssertionError("ABI-v10 integer-multiply checked body is incomplete");
        }
        java.util.Map<String, org.mosslang.compiler.abi.CheckedBinaryOp> integerOperations =
                java.util.Map.of("subtract", org.mosslang.compiler.abi.CheckedBinaryOp.Sub,
                        "divide", org.mosslang.compiler.abi.CheckedBinaryOp.Div,
                        "modulo", org.mosslang.compiler.abi.CheckedBinaryOp.Mod);
        for (java.util.Map.Entry<String, org.mosslang.compiler.abi.CheckedBinaryOp> operation
                : integerOperations.entrySet()) {
            AbiV10Records.CheckedBodyRecord operationBody = identityInterface.privateSemanticDependencies.stream()
                    .filter(record -> record.id.startsWith("checked-body-v1:"))
                    .map(AbiV10Records.CheckedBodyRecord::decode)
                    .filter(body -> body.callableId.endsWith(":fn:" + operation.getKey()))
                    .findFirst().orElseThrow(() -> new AssertionError(
                            "ABI-v10 producer omitted integer-" + operation.getKey() + " body"));
            java.util.List<org.mosslang.compiler.abi.CheckedStmt> operationStatements =
                    CheckedBodyCodec.decodeCheckedBody(operationBody.checkedIrBytes);
            if (operationStatements.size() != 1
                    || operationStatements.get(0).expr.kind != org.mosslang.compiler.abi.CheckedExprKind.Binary
                    || operationStatements.get(0).expr.binaryOp != operation.getValue()) {
                throw new AssertionError("ABI-v10 integer-" + operation.getKey() + " checked body is incomplete");
            }
        }
        for (String floatCallable : List.of("weighted", "adjust_ratio")) {
            AbiV10Records.Summary floatSummary = identityInterface.binaryContract.stream()
                    .filter(record -> record.payloadBytes().length > 0
                            && (record.payloadBytes()[0] == 3 || record.payloadBytes()[0] == 4))
                    .map(AbiV10Records.Summary::decode)
                    .filter(summary -> summary.callableId.endsWith(":fn:" + floatCallable))
                    .findFirst().orElseThrow(() -> new AssertionError(
                            "ABI-v10 producer omitted the Float expression summary for " + floatCallable));
            AbiV10Records.CheckedBodyRecord floatBody = identityInterface.privateSemanticDependencies.stream()
                    .filter(record -> record.id.startsWith("checked-body-v1:"))
                    .map(AbiV10Records.CheckedBodyRecord::decode)
                    .filter(body -> body.callableId.endsWith(":fn:" + floatCallable))
                    .findFirst().orElseThrow(() -> new AssertionError(
                            "ABI-v10 producer omitted the Float expression body for " + floatCallable));
            List<org.mosslang.compiler.abi.CheckedStmt> floatStatements =
                    CheckedBodyCodec.decodeCheckedBody(floatBody.checkedIrBytes);
            if (floatSummary.result.kind != AbiV10Records.WireTypeKind.Primitive
                    || !"float".equals(floatSummary.result.identity)
                    || floatStatements.size() != 1
                    || floatStatements.get(0).kind != org.mosslang.compiler.abi.CheckedStmtKind.Return
                    || !floatSummary.bodySemanticHash.equals(floatBody.semanticHash)) {
                throw new AssertionError("ABI-v10 Float summary/body is incomplete for " + floatCallable);
            }
        }
        AbiV10Records.Summary predicateSummary = identityInterface.binaryContract.stream()
                .filter(record -> record.payloadBytes().length > 0
                        && (record.payloadBytes()[0] == 3 || record.payloadBytes()[0] == 4))
                .map(AbiV10Records.Summary::decode)
                .filter(summary -> summary.callableId.endsWith(":fn:is_positive"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the checked predicate summary"));
        AbiV10Records.CheckedBodyRecord predicateBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:is_positive"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the checked predicate body"));
        List<org.mosslang.compiler.abi.CheckedStmt> predicateStatements =
                CheckedBodyCodec.decodeCheckedBody(predicateBody.checkedIrBytes);
        if (predicateSummary.result.kind != AbiV10Records.WireTypeKind.Primitive
                || !"bool".equals(predicateSummary.result.identity)
                || predicateStatements.size() != 1
                || predicateStatements.get(0).expr.kind != org.mosslang.compiler.abi.CheckedExprKind.Binary
                || predicateStatements.get(0).expr.binaryOp != org.mosslang.compiler.abi.CheckedBinaryOp.Greater
                || !predicateSummary.bodySemanticHash.equals(predicateBody.semanticHash)) {
            throw new AssertionError("ABI-v10 checked predicate summary/body is incomplete");
        }
        AbiV10Records.Summary combineSummary = identityInterface.binaryContract.stream()
                .filter(record -> record.payloadBytes().length > 0
                        && (record.payloadBytes()[0] == 3 || record.payloadBytes()[0] == 4))
                .map(AbiV10Records.Summary::decode)
                .filter(summary -> summary.callableId.endsWith(":fn:combine"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the multi-parameter Int expression summary"));
        AbiV10Records.CheckedBodyRecord combineBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:combine"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the multi-parameter Int expression body"));
        java.util.List<org.mosslang.compiler.abi.CheckedStmt> combineStatements =
                CheckedBodyCodec.decodeCheckedBody(combineBody.checkedIrBytes);
        if (combineSummary.parameters.size() != 2
                || combineStatements.size() != 1
                || combineStatements.get(0).expr.kind != org.mosslang.compiler.abi.CheckedExprKind.Binary
                || combineStatements.get(0).expr.binaryOp != org.mosslang.compiler.abi.CheckedBinaryOp.Add
                || combineStatements.get(0).expr.operands.get(0).binaryOp
                        != org.mosslang.compiler.abi.CheckedBinaryOp.Mul
                || combineStatements.get(0).expr.operands.get(1).binaryOp
                        != org.mosslang.compiler.abi.CheckedBinaryOp.Div
                || !combineSummary.bodySemanticHash.equals(combineBody.semanticHash)) {
            throw new AssertionError("ABI-v10 multi-parameter nested Int expression is incomplete");
        }
        AbiV10Records.CheckedBodyRecord negatedBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:negated"))
                .findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the unary-negation expression body"));
        java.util.List<org.mosslang.compiler.abi.CheckedStmt> negatedStatements =
                CheckedBodyCodec.decodeCheckedBody(negatedBody.checkedIrBytes);
        if (negatedStatements.size() != 1
                || negatedStatements.get(0).expr.operands.get(0).kind
                        != org.mosslang.compiler.abi.CheckedExprKind.Unary
                || negatedStatements.get(0).expr.operands.get(0).operands.get(0).kind
                        != org.mosslang.compiler.abi.CheckedExprKind.Parameter) {
            throw new AssertionError("ABI-v10 unary-negation checked expression is incomplete: "
                    + negatedStatements.get(0).expr + " / "
                    + negatedStatements.get(0).expr.operands);
        }
        Path identityMossi = null;
        try {
            identityMossi = Files.createTempFile(Path.of("tmp"), "identity-provider-", ".mossi");
            for (int index = 0; index < identityInterface.semanticContract.size(); index++) {
                MossiRecord record = identityInterface.semanticContract.get(index);
                String withoutLegacyBody = record.payload.lines()
                        .filter(line -> !line.trim().startsWith("generic_stmt ")
                                && !line.trim().startsWith("generic_result ")
                                && !line.trim().startsWith("generic_ir_"))
                        .collect(java.util.stream.Collectors.joining("\n", "", "\n"));
                identityInterface.semanticContract.set(index,
                        new MossiRecord(record.id, withoutLegacyBody));
            }
            Files.write(identityMossi, CheckedBodyCodec.encodeMossiBytes(identityInterface));
            Program consumer = Parser.parseSource(""
                    + "fn use_int() -> Int:\n"
                    + "  return provider__identity(7)\n"
                    + "fn use_string() -> String:\n"
                    + "  return provider__identity(\"seven\")\n"
                    + "fn use_second() -> String:\n"
                    + "  return provider__second(7, \"seven\")\n"
                    + "fn use_answer() -> Int:\n"
                    + "  return provider__answer()\n"
                    + "fn use_negative() -> Int:\n"
                    + "  return provider__negative()\n"
                    + "fn use_vector(value: Vector[Int]) -> Vector[Int]:\n"
                    + "  return provider__same_vector(value)\n"
                    + "fn use_enabled() -> Bool:\n"
                    + "  return provider__enabled()\n"
                    + "fn use_ratio() -> Float:\n"
                    + "  return provider__ratio()\n"
                    + "fn use_negative_ratio() -> Float:\n"
                    + "  return provider__negative_ratio()\n"
                    + "fn use_greeting() -> String:\n"
                    + "  return provider__greeting()\n"
                    + "fn use_weighted() -> Float:\n"
                    + "  return provider__weighted(2.0, 4.0)\n"
                    + "fn use_adjust_ratio() -> Float:\n"
                    + "  return provider__adjust_ratio(3.0)\n"
                    + "fn use_is_positive() -> Bool:\n"
                    + "  return provider__is_positive(7)\n"
                    + "fn use_increment() -> Int:\n"
                    + "  return provider__increment(41)\n"
                    + "fn use_scale() -> Int:\n"
                    + "  return provider__scale(21)\n"
                    + "fn use_subtract() -> Int:\n"
                    + "  return provider__subtract(43)\n"
                    + "fn use_divide() -> Int:\n"
                    + "  return provider__divide(42)\n"
                    + "fn use_modulo() -> Int:\n"
                    + "  return provider__modulo(43)\n"
                    + "fn use_combine() -> Int:\n"
                    + "  return provider__combine(20, 8)\n"
                    + "fn use_negated() -> Int:\n"
                    + "  return provider__negated(3)\n"
                    + "fn main():\n"
                    + "  pass\n", "identity-consumer.moss");
            org.mosslang.compiler.cli.Main.loadMossiFunctions(identityMossi, "provider", consumer);
            Function importedIdentity = consumer.functions.stream()
                    .filter(function -> function.name.equals("provider.identity"))
                    .findFirst().orElseThrow(() -> new AssertionError("source-free identity callable was not loaded"));
            if (importedIdentity.params.get(0).typeRef == null
                    || importedIdentity.returnTypeRef == null
                    || importedIdentity.body.size() != 1
                    || importedIdentity.body.get(0).kind != org.mosslang.compiler.ast.Stmt.Kind.Return) {
                throw new AssertionError("ABI-v10 checked identity facts were not projected into the source-free callable: id="
                        + importedIdentity.semanticCallableId + ", paramType="
                        + importedIdentity.params.get(0).typeRef + ", returnType="
                        + importedIdentity.returnTypeRef + ", body=" + importedIdentity.body.size()
                        + ", summary=" + identitySummary.callableId + ", bodyId=" + identityBody.callableId);
            }
            new org.mosslang.compiler.semantics.SemanticChecker(consumer).run();
        } catch (java.io.IOException error) {
            throw new AssertionError("could not construct source-free identity consumer", error);
        } finally {
            if (identityMossi != null) {
                try { Files.deleteIfExists(identityMossi); }
                catch (java.io.IOException error) {
                    throw new AssertionError("could not clean up source-free identity fixture", error);
                }
            }
        }
    }

    private static void assertEquals(String expected, String actual) {
        if (!expected.equals(actual)) {
            throw new AssertionError("expected <" + expected + "> but got <" + actual + ">");
        }
    }

    private static void assertContains(String value, String expected) {
        if (!value.contains(expected)) throw new AssertionError("missing <" + expected + "> from <" + value + ">");
    }
}
