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
                + "  return value\n", "identity-provider.moss");
        new org.mosslang.compiler.semantics.CheckedProgram(identityProgram).run();
        MossiInterface identityInterface = CheckedBodyCodec.verifySourceFree(
                ProjectBuilder.generateMossiInterface(manifest, "provider", identityProgram,
                        toolchain, java.util.List.of(), Path.of("tmp"), java.util.Map.of()));
        AbiV10Records.Summary identitySummary = identityInterface.binaryContract.stream()
                .filter(record -> record.payloadBytes().length > 0
                        && (record.payloadBytes()[0] == 3 || record.payloadBytes()[0] == 4))
                .map(AbiV10Records.Summary::decode).findFirst().orElseThrow(() ->
                        new AssertionError("ABI-v10 producer omitted the checked generic identity summary"));
        AbiV10Records.CheckedBodyRecord identityBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode).findFirst().orElseThrow(() ->
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
