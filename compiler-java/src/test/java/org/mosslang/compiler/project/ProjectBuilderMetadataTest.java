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
import org.mosslang.compiler.types.TypeRef;
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
        if ("v10".equals(System.getenv("MOSS_STAGED_ABI"))) {
            testSourceFreeProviderNameIsNotSpecialCased();
            testUnsupportedSourceFreeCallableFailsClosed();
        }
        testCanonicalNominalTemplateWirePayload();
        testCheckedOnFailCaptureAbiRows();
        testFunctionRaiseSetExportsEveryVariant();
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
                + "    var captured_value = 3\n"
                + "    raise Failure.Bad\n"
                + "  on_fail Failure.Bad:\n"
                + "    reply captured_value\n", "capture-abi.moss");
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
                || !text.contains("handler_failure_capture \"Work\" 0 \"captured_value\" \"int\" \"captured_value\" 1 1 \"0:3:int:0:\"\n")) {
            throw new AssertionError("project producer did not serialize checker-proved on_fail captures: " + text);
        }
    }

    private static void testFunctionRaiseSetExportsEveryVariant() throws Exception {
        Program program = Parser.parseSource(""
                + "module provider\n"
                + "enum Failure:\n"
                + "  Bad\n"
                + "  Other\n"
                + "export fn signal(enabled: Bool):\n"
                + "  if enabled:\n"
                + "    raise Failure.Bad\n"
                + "  else:\n"
                + "    raise Failure.Other\n"
                + "fn main():\n"
                + "  pass\n", "multi-raise-export.moss");
        new org.mosslang.compiler.semantics.CheckedProgram(program).run();
        ProjectManifest manifest = new ProjectManifest(Path.of("tmp"), Path.of("tmp", "moss.toml"),
                "sample", "1.0.0", Path.of("src"));
        BackendToolchain toolchain = new BackendToolchain("rustc", "rustc 1.80.0 (test)\n",
                "dev", java.util.List.of(), "multi-raise-test");
        byte[] interfaceBytes = ProjectBuilder.generateMossiInterface(manifest, "provider",
                program, toolchain, java.util.List.of(), Path.of("tmp"), java.util.Map.of());
        String text = new String(interfaceBytes, java.nio.charset.StandardCharsets.UTF_8);
        String raiseSet = "raise_set=enum:provider__Failure:Bad,enum:provider__Failure:Other";
        if (!text.contains(raiseSet)) {
            throw new AssertionError("source-free function metadata omitted a raised variant: " + text);
        }
        Path scratch = Path.of("tmp");
        Files.createDirectories(scratch);
        Path interfaceFile = Files.createTempFile(scratch, "java-multi-raise-", ".mossi");
        try {
            Files.write(interfaceFile, interfaceBytes);
            Program consumer = new Program();
            org.mosslang.compiler.cli.Main.loadMossiFunctions(interfaceFile, "provider", consumer);
            org.mosslang.compiler.ast.Function imported = consumer.functions.stream()
                    .filter(function -> function.name.equals("provider.signal"))
                    .findFirst().orElseThrow(() -> new AssertionError("multi-raise function was not imported"));
            if (!imported.sourceFreeRaiseVariants.contains("provider__Failure.Bad")
                    || !imported.sourceFreeRaiseVariants.contains("provider__Failure.Other")) {
                throw new AssertionError("source-free loader did not restore every raise variant: "
                        + imported.sourceFreeRaiseVariants);
            }
        } finally {
            Files.deleteIfExists(interfaceFile);
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

    private static void testSourceFreeProviderNameIsNotSpecialCased() throws Exception {
        Path root = Files.createTempDirectory(Path.of("tmp"), "java-sourcefree-outer-");
        Path providerRoot = root.resolve("provider");
        Path providerSource = providerRoot.resolve("src/provider.moss");
        Path consumerRoot = root.resolve("consumer");
        Path consumerSource = consumerRoot.resolve("src/main.moss");
        Path interfaceDirectory = consumerRoot.resolve("build/debug");
        try {
            Files.createDirectories(providerSource.getParent());
            Files.createDirectories(consumerSource.getParent());
            Files.createDirectories(interfaceDirectory);
            Files.writeString(providerRoot.resolve("Moss.toml"), ""
                    + "[package]\nname = \"outer-provider\"\nversion = \"1.0.0\"\n\n"
                    + "[build]\nsource = \"src\"\n");
            Files.writeString(consumerRoot.resolve("Moss.toml"), ""
                    + "[package]\nname = \"outer-consumer\"\nversion = \"1.0.0\"\n\n"
                    + "[build]\nsource = \"src\"\n");
            Program provider = Parser.parseSource(""
                    + "module Provider\n"
                    + "export fn outer(value: Int) -> Int:\n"
                    + "  return value + 1\n", providerSource.toString());
            new org.mosslang.compiler.semantics.CheckedProgram(provider).run();
            provider.functions.get(0).semanticCallableId =
                    "callable-v1:module-v1:outer-provider@1.0.0::Provider:fn:outer";
            ProjectManifest manifest = new ProjectManifest(providerRoot,
                    providerRoot.resolve("Moss.toml"), "outer-provider", "1.0.0",
                    providerRoot.resolve("src"));
            BackendToolchain toolchain = new BackendToolchain("rustc", "rustc 1.80.0 (test)\n",
                    "dev", java.util.List.of(), "source-free-outer-test");
            byte[] artifact = ProjectBuilder.generateMossiInterface(manifest, "Provider",
                    provider, toolchain, java.util.List.of(), providerRoot.resolve("build"),
                    java.util.Map.of());
            Files.write(interfaceDirectory.resolve("Provider.mossi"), artifact);
            MossiInterface verified = CheckedBodyCodec.verifySourceFree(artifact);
            Files.writeString(interfaceDirectory.resolve("Provider.rlib"),
                    "MOSS_ABI_V10_CONTRACT::Provider::" + verified.digest + "::END",
                    java.nio.charset.StandardCharsets.US_ASCII);
            Files.writeString(consumerSource, ""
                    + "module Consumer\n"
                    + "import Provider\n"
                    + "fn main():\n"
                    + "  echo Provider.outer(5)\n");

            java.io.PrintStream previous = System.out;
            java.io.ByteArrayOutputStream output = new java.io.ByteArrayOutputStream();
            int exitCode;
            try (java.io.PrintStream captured = new java.io.PrintStream(output,
                    true, java.nio.charset.StandardCharsets.UTF_8)) {
                System.setOut(captured);
                exitCode = org.mosslang.compiler.cli.Main.run(new String[] {
                        "check", consumerSource.toString(), "--json"});
            } finally {
                System.setOut(previous);
            }
            if (exitCode != 0) {
                throw new AssertionError("valid source-free Provider.outer was rejected: " + output);
            }
            assertContains(output.toString(java.nio.charset.StandardCharsets.UTF_8), "\"ok\": true");
        } finally {
            try (var paths = Files.walk(root)) {
                paths.sorted(java.util.Comparator.reverseOrder()).forEach(path -> {
                    try { Files.deleteIfExists(path); }
                    catch (java.io.IOException error) {
                        throw new IllegalStateException("failed to clean source-free provider test", error);
                    }
                });
            }
        }
    }

    private static void testUnsupportedSourceFreeCallableFailsClosed() throws Exception {
        Path root = Files.createTempDirectory(Path.of("tmp"), "java-sourcefree-unavailable-");
        Path providerRoot = root.resolve("provider");
        Path providerSource = providerRoot.resolve("src/provider.moss");
        Path consumerRoot = root.resolve("consumer");
        Path consumerSource = consumerRoot.resolve("src/main.moss");
        Path interfaceDirectory = consumerRoot.resolve("build/debug");
        try {
            Files.createDirectories(providerSource.getParent());
            Files.createDirectories(consumerSource.getParent());
            Files.createDirectories(interfaceDirectory);
            Files.writeString(providerRoot.resolve("Moss.toml"), ""
                    + "[package]\nname = \"relay-provider\"\nversion = \"1.0.0\"\n\n"
                    + "[build]\nsource = \"src\"\n");
            Files.writeString(consumerRoot.resolve("Moss.toml"), ""
                    + "[package]\nname = \"relay-consumer\"\nversion = \"1.0.0\"\n\n"
                    + "[build]\nsource = \"src\"\n");
            Program provider = Parser.parseSource(""
                    + "module Provider\n"
                    + "export fn fresh():\n"
                    + "  return []\n"
                    + "export fn relay():\n"
                    + "  return fresh()\n", providerSource.toString());
            new org.mosslang.compiler.semantics.CheckedProgram(provider).run();
            String moduleId = "module-v1:relay-provider@1.0.0::Provider";
            provider.functions.forEach(function -> function.semanticCallableId =
                    "callable-v1:" + moduleId + ":fn:" + function.name);
            ProjectManifest manifest = new ProjectManifest(providerRoot,
                    providerRoot.resolve("Moss.toml"), "relay-provider", "1.0.0",
                    providerRoot.resolve("src"));
            BackendToolchain toolchain = new BackendToolchain("rustc", "rustc 1.80.0 (test)\n",
                    "dev", java.util.List.of(), "source-free-relay-test");
            byte[] artifact = ProjectBuilder.generateMossiInterface(manifest, "Provider",
                    provider, toolchain, java.util.List.of(), providerRoot.resolve("build"),
                    java.util.Map.of());
            Files.write(interfaceDirectory.resolve("Provider.mossi"), artifact);
            MossiInterface verified = CheckedBodyCodec.verifySourceFree(artifact);
            MossiRecord freshRecord = verified.semanticContract.stream()
                    .filter(record -> record.id.endsWith("fn:fresh"))
                    .findFirst().orElseThrow(() -> new AssertionError(
                            "ABI-v10 producer omitted the inferred fresh callable"));
            if (!freshRecord.payload.contains("return=vector")) {
                throw new AssertionError("inferred open collection result was not retained in ABI metadata: "
                        + freshRecord.payload);
            }
            Files.writeString(interfaceDirectory.resolve("Provider.rlib"),
                    "MOSS_ABI_V10_CONTRACT::Provider::" + verified.digest + "::END",
                    java.nio.charset.StandardCharsets.US_ASCII);
            Files.writeString(consumerSource, ""
                    + "module Consumer\n"
                    + "import Provider\n"
                    + "fn main():\n"
                    + "  echo Provider.relay()\n");

            java.io.PrintStream previous = System.out;
            java.io.ByteArrayOutputStream output = new java.io.ByteArrayOutputStream();
            int exitCode;
            try (java.io.PrintStream captured = new java.io.PrintStream(output,
                    true, java.nio.charset.StandardCharsets.UTF_8)) {
                System.setOut(captured);
                exitCode = org.mosslang.compiler.cli.Main.run(new String[] {
                        "check", consumerSource.toString(), "--json"});
            } finally {
                System.setOut(previous);
            }
            String diagnostic = output.toString(java.nio.charset.StandardCharsets.UTF_8);
            if (exitCode == 0 || !diagnostic.contains("\"ok\": false")) {
                throw new AssertionError("unavailable source-free relay was not rejected: " + diagnostic);
            }
            if (!diagnostic.contains("\"code\": \"SOURCE_FREE_CHECKED_BODY_UNAVAILABLE\"")) {
                throw new AssertionError("unavailable source-free relay lost its ABI diagnostic: " + diagnostic);
            }
        } finally {
            try (var paths = Files.walk(root)) {
                paths.sorted(java.util.Comparator.reverseOrder()).forEach(path -> {
                    try { Files.deleteIfExists(path); }
                    catch (java.io.IOException error) {
                        throw new IllegalStateException("failed to clean unavailable source-free test", error);
                    }
                });
            }
        }
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
                + "  fn ready() -> Int:\n"
                + "    return 7\n"
                + "  fn enabled() -> Bool:\n"
                + "    return true\n"
                + "  fn label() -> String:\n"
                + "    return \"point\"\n"
                + "  fn measure() -> Float:\n"
                + "    return 1.5\n"
                + "  fn ready_after(delay: Int) -> Int:\n"
                + "    return 8\n"
                + "  fn identity(value: Int) -> Int:\n"
                + "    return value\n"
                + "  fn identity_collection(values: Vector[Int]) -> Vector[Int]:\n"
                + "    return values\n"
                + "  fn select_right(left: Int, middle: Bool, right: String) -> String:\n"
                + "    return right\n"
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
        program.enums.get(0).exported = true;
        program.domains.get(0).exported = true;
        program.domains.get(1).exported = true;
        new org.mosslang.compiler.semantics.CheckedProgram(program).run();
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
        if (pointTemplate.methodTemplateIds.size() != 8) {
            throw new AssertionError("ABI-v10 nominal template omitted its method reference");
        }
        AbiV10Records.MethodTemplate methodTemplate = decoded.binaryContract.stream()
                .filter(record -> pointTemplate.methodTemplateIds.contains(record.id))
                .map(AbiV10Records.MethodTemplate::decode)
                .filter(method -> "ready".equals(method.methodName)).findFirst().orElseThrow();
        if (!"ready".equals(methodTemplate.methodName)
                || methodTemplate.receiverAccess != AccessMode.Read.ordinal() + 1
                || !methodTemplate.parameterAccess.isEmpty()
                || !"int".equals(methodTemplate.result.canonicalName())) {
            throw new AssertionError("ABI-v10 method template omitted its pure literal-return signature");
        }
        AbiV10Records.MethodTemplate parameterizedMethodTemplate = decoded.binaryContract.stream()
                .filter(record -> pointTemplate.methodTemplateIds.contains(record.id))
                .map(AbiV10Records.MethodTemplate::decode)
                .filter(method -> "ready_after".equals(method.methodName)).findFirst().orElseThrow();
        if (parameterizedMethodTemplate.parameters.size() != 1
                || !"int".equals(parameterizedMethodTemplate.parameters.get(0).canonicalName())
                || !parameterizedMethodTemplate.parameterAccess.equals(List.of(AccessMode.Read.ordinal() + 1))) {
            throw new AssertionError("ABI-v10 pure method template lost its typed READ parameter");
        }
        AbiV10Records.MethodTemplate identityMethodTemplate = decoded.binaryContract.stream()
                .filter(record -> pointTemplate.methodTemplateIds.contains(record.id))
                .map(AbiV10Records.MethodTemplate::decode)
                .filter(method -> "identity".equals(method.methodName)).findFirst().orElseThrow();
        if (identityMethodTemplate.parameters.size() != 1
                || !"int".equals(identityMethodTemplate.parameters.get(0).canonicalName())
                || !"int".equals(identityMethodTemplate.result.canonicalName())
                || !identityMethodTemplate.parameterAccess.equals(List.of(AccessMode.Read.ordinal() + 1))) {
            throw new AssertionError("ABI-v10 method template lost its pure parameter/result relation");
        }
        AbiV10Records.MethodTemplate consumingIdentityTemplate = decoded.binaryContract.stream()
                .filter(record -> pointTemplate.methodTemplateIds.contains(record.id))
                .map(AbiV10Records.MethodTemplate::decode)
                .filter(method -> "identity_collection".equals(method.methodName)).findFirst().orElseThrow();
        if (consumingIdentityTemplate.parameters.size() != 1
                || !"vector[int]".equalsIgnoreCase(consumingIdentityTemplate.parameters.get(0).canonicalName())
                || !"vector[int]".equalsIgnoreCase(consumingIdentityTemplate.result.canonicalName())
                || !consumingIdentityTemplate.parameterAccess.equals(List.of(AccessMode.Consume.ordinal() + 1))) {
            throw new AssertionError("ABI-v10 method template lost its consumed collection identity relation: "
                    + consumingIdentityTemplate.parameters.stream().map(AbiV10Records.WireType::canonicalName).toList()
                    + " -> " + consumingIdentityTemplate.result.canonicalName() + " / "
                    + consumingIdentityTemplate.parameterAccess);
        }
        AbiV10Records.MethodTemplate selectRightTemplate = decoded.binaryContract.stream()
                .filter(record -> pointTemplate.methodTemplateIds.contains(record.id))
                .map(AbiV10Records.MethodTemplate::decode)
                .filter(method -> "select_right".equals(method.methodName)).findFirst().orElseThrow();
        if (selectRightTemplate.parameters.size() != 3
                || !"int".equalsIgnoreCase(selectRightTemplate.parameters.get(0).canonicalName())
                || !"bool".equalsIgnoreCase(selectRightTemplate.parameters.get(1).canonicalName())
                || !"string".equalsIgnoreCase(selectRightTemplate.parameters.get(2).canonicalName())
                || !"string".equalsIgnoreCase(selectRightTemplate.result.canonicalName())
                || !selectRightTemplate.parameterAccess.equals(List.of(
                        AccessMode.Read.ordinal() + 1, AccessMode.Read.ordinal() + 1,
                        AccessMode.Consume.ordinal() + 1))) {
            throw new AssertionError("ABI-v10 method template lost its selected parameter ownership relation");
        }
        MossiInterface methodTemplateOnly = CheckedBodyCodec.verifySourceFree(generated);
        methodTemplateOnly.semanticContract.clear();
        methodTemplateOnly.binaryContract.removeIf(record ->
                record.id.startsWith("enum:") || record.id.startsWith("domain:")
                        || record.id.startsWith("type:"));
        Path methodTemplateOnlyFile = Path.of("tmp", "java-method-template-only.mossi");
        try {
            Files.write(methodTemplateOnlyFile, CheckedBodyCodec.encodeMossiBytes(methodTemplateOnly));
            Program methodConsumer = Parser.parseSource(""
                    + "fn call(point: provider__Point) -> Int:\n"
                    + "  return point.ready()\n"
                    + "fn enabled(point: provider__Point) -> Bool:\n"
                    + "  return point.enabled()\n"
                    + "fn label(point: provider__Point) -> String:\n"
                    + "  return point.label()\n"
                    + "fn measure(point: provider__Point) -> Float:\n"
                    + "  return point.measure()\n"
                    + "fn delayed(point: provider__Point, delay: Int) -> Int:\n"
                    + "  return point.ready_after(delay)\n"
                    + "fn identity(point: provider__Point, value: Int) -> Int:\n"
                    + "  return point.identity(value)\n"
                    + "fn identity_collection(point: provider__Point, values: Vector[Int]) -> Vector[Int]:\n"
                    + "  return point.identity_collection(values)\n"
                    + "fn select_right(point: provider__Point, left: Int, middle: Bool, right: String) -> String:\n"
                    + "  return point.select_right(left, middle, right)\n"
                    + "fn main():\n"
                    + "  pass\n", "method-template-consumer.moss");
            org.mosslang.compiler.cli.Main.loadMossiFunctions(
                    methodTemplateOnlyFile, "provider", methodConsumer);
            new org.mosslang.compiler.semantics.SemanticChecker(methodConsumer).run();
            if (methodConsumer.objects.stream().noneMatch(object -> object.name.equals("provider__Point")
                    && object.methods.stream().anyMatch(method -> method.name.equals("ready")
                            && "int".equalsIgnoreCase(method.returnType)))
                    || methodConsumer.objects.stream().noneMatch(object -> object.name.equals("provider__Point")
                            && object.methods.stream().anyMatch(method -> method.name.equals("enabled")
                                    && "bool".equalsIgnoreCase(method.returnType)))
                    || methodConsumer.objects.stream().noneMatch(object -> object.name.equals("provider__Point")
                            && object.methods.stream().anyMatch(method -> method.name.equals("label")
                                    && "string".equalsIgnoreCase(method.returnType)))
                    || methodConsumer.objects.stream().noneMatch(object -> object.name.equals("provider__Point")
                            && object.methods.stream().anyMatch(method -> method.name.equals("measure")
                                    && "float".equalsIgnoreCase(method.returnType)))
                    || methodConsumer.objects.stream().noneMatch(object -> object.name.equals("provider__Point")
                            && object.methods.stream().anyMatch(method -> method.name.equals("ready_after")
                                    && method.params.size() == 1
                                    && "int".equalsIgnoreCase(method.params.get(0).type)))
                    || methodConsumer.objects.stream().noneMatch(object -> object.name.equals("provider__Point")
                            && object.methods.stream().anyMatch(method -> method.name.equals("identity")
                                    && method.params.size() == 1
                                    && "int".equalsIgnoreCase(method.params.get(0).type)
                                    && "int".equalsIgnoreCase(method.returnType)))
                    || methodConsumer.objects.stream().noneMatch(object -> object.name.equals("provider__Point")
                            && object.methods.stream().anyMatch(method -> method.name.equals("identity_collection")
                                    && method.params.size() == 1
                                    && "vector[int]".equalsIgnoreCase(method.params.get(0).type)
                                    && "vector[int]".equalsIgnoreCase(method.returnType)))
                    || methodConsumer.objects.stream().noneMatch(object -> object.name.equals("provider__Point")
                            && object.methods.stream().anyMatch(method -> method.name.equals("select_right")
                                    && method.params.size() == 3
                                    && "string".equalsIgnoreCase(method.params.get(2).type)
                                    && "string".equalsIgnoreCase(method.returnType)))) {
                throw new AssertionError("ABI-v10 method template did not supply source-free method typing");
            }
            Program consumingMethodConsumer = Parser.parseSource(""
                    + "fn invalid(point: provider__Point, values: Vector[Int]) -> Vector[Int]:\n"
                    + "  result = point.identity_collection(values)\n"
                    + "  return values\n"
                    + "fn main():\n"
                    + "  pass\n", "method-template-consume-consumer.moss");
            org.mosslang.compiler.cli.Main.loadMossiFunctions(
                    methodTemplateOnlyFile, "provider", consumingMethodConsumer);
            try {
                new org.mosslang.compiler.semantics.SemanticChecker(consumingMethodConsumer).run();
                throw new AssertionError("source-free Consume method must consume its collection argument");
            } catch (CompileError error) {
                if (!"OWNERSHIP_USE_AFTER_CONSUME".equals(error.getCode())) {
                    throw new AssertionError("expected source-free method consume diagnostic, got "
                            + error.getCode(), error);
                }
            }
            Program consumingStringConsumer = Parser.parseSource(""
                    + "fn invalid(point: provider__Point, left: Int, middle: Bool, right: String) -> String:\n"
                    + "  result = point.select_right(left, middle, right)\n"
                    + "  return right\n"
                    + "fn main():\n"
                    + "  pass\n", "method-template-select-consume-consumer.moss");
            org.mosslang.compiler.cli.Main.loadMossiFunctions(
                    methodTemplateOnlyFile, "provider", consumingStringConsumer);
            try {
                new org.mosslang.compiler.semantics.SemanticChecker(consumingStringConsumer).run();
                throw new AssertionError("source-free selected method must consume its returned String argument");
            } catch (CompileError error) {
                if (!"OWNERSHIP_USE_AFTER_CONSUME".equals(error.getCode())) {
                    throw new AssertionError("expected source-free selected-parameter consume diagnostic, got "
                            + error.getCode(), error);
                }
            }
        } catch (java.io.IOException error) {
            throw new AssertionError("could not create method-template-only source-free interface", error);
        } finally {
            try { Files.deleteIfExists(methodTemplateOnlyFile); }
            catch (java.io.IOException error) {
                throw new AssertionError("could not clean up method-template-only interface", error);
            }
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
                + "export fn with_local(value: Int) -> Int:\n"
                + "  let bumped = value + 1\n"
                + "  return bumped * 2\n"
                + "export fn with_mutable_local(value: Int) -> Int:\n"
                + "  count = value\n"
                + "  count = count + 1\n"
                + "  return count\n"
                + "export fn with_float_local(value: Float) -> Float:\n"
                + "  let adjusted = value + .5\n"
                + "  return adjusted * 2.0\n"
                + "export fn with_mutable_float_local(value: Float) -> Float:\n"
                + "  result = value\n"
                + "  result = result + 1.0\n"
                + "  return result\n"
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
                + "export fn is_in_range(value: Int) -> Bool:\n"
                + "  return value >= 0 and value < 10\n"
                + "export fn is_nonnegative(value: Int) -> Bool:\n"
                + "  return not (value < 0)\n"
                + "export fn abs_value(value: Int) -> Int:\n"
                + "  if value >= 0:\n"
                + "    return value\n"
                + "  else:\n"
                + "    return -value\n"
                + "export fn is_nonnegative_branch(value: Int) -> Bool:\n"
                + "  if value >= 0:\n"
                + "    return true\n"
                + "  else:\n"
                + "    return false\n"
                + "export fn abs_ratio(value: Float) -> Float:\n"
                + "  if value >= 0.0:\n"
                + "    return value\n"
                + "  else:\n"
                + "    return -value\n"
                + "export fn is_warm(value: Float) -> Bool:\n"
                + "  return value > 0.0 and value < 100.0\n"
                + "export fn greeting() -> String:\n"
                + "  return \"hello\"\n"
                + "export fn fresh():\n"
                + "  return []\n"
                + "export fn empty_int() -> Vector[Int]:\n"
                + "  return []\n"
                + "export fn numbers() -> Vector[Int]:\n"
                + "  return [1, 2, -3 + 5]\n"
                + "export fn flags() -> Vector[Bool]:\n"
                + "  return [true, false, true]\n"
                + "export fn ratios() -> Vector[Float]:\n"
                + "  return [1.5, -2.0, 3.0 / 2.0]\n"
                + "export fn names() -> Vector[String]:\n"
                + "  return [\"one\", \"two\", \"three\"]\n"
                + "export fn matrix() -> Vector[Vector[Int]]:\n"
                + "  return [[1, 2], [3], []]\n"
                + "export fn escaped_names() -> Vector[String]:\n"
                + "  return [\"line\\nbreak\", \"quote: \\\"value\\\"\"]\n", "identity-provider.moss");
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
        AbiV10Records.Summary freshSummary = identityInterface.binaryContract.stream()
                .filter(record -> record.payloadBytes().length > 0
                        && (record.payloadBytes()[0] == 3 || record.payloadBytes()[0] == 4))
                .map(AbiV10Records.Summary::decode)
                .filter(summary -> summary.callableId.endsWith(":fn:fresh"))
                .findFirst().orElseThrow(() -> new AssertionError(
                        "ABI-v10 producer omitted the inferred empty-vector summary"));
        AbiV10Records.CheckedBodyRecord freshBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:fresh"))
                .findFirst().orElseThrow(() -> new AssertionError(
                        "ABI-v10 producer omitted the inferred empty-vector body"));
        List<org.mosslang.compiler.abi.CheckedStmt> freshStatements =
                CheckedBodyCodec.decodeCheckedBody(freshBody.checkedIrBytes);
        if (freshSummary.variableCount != 1 || !freshSummary.parameters.isEmpty()
                || freshSummary.result.kind != AbiV10Records.WireTypeKind.Builtin
                || !"vector".equals(freshSummary.result.identity)
                || freshSummary.result.arguments.size() != 1
                || freshSummary.result.arguments.get(0).kind != AbiV10Records.WireTypeKind.BoundVar
                || freshSummary.result.arguments.get(0).ordinal != 0
                || freshStatements.size() != 1
                || freshStatements.get(0).expr.kind != org.mosslang.compiler.abi.CheckedExprKind.VectorLiteral
                || !freshSummary.bodySemanticHash.equals(freshBody.semanticHash)) {
            throw new AssertionError("ABI-v10 inferred empty-vector summary/body is incomplete");
        }
        AbiV10Records.Summary emptyIntSummary = identityInterface.binaryContract.stream()
                .filter(record -> record.payloadBytes().length > 0
                        && (record.payloadBytes()[0] == 3 || record.payloadBytes()[0] == 4))
                .map(AbiV10Records.Summary::decode)
                .filter(summary -> summary.callableId.endsWith(":fn:empty_int"))
                .findFirst().orElseThrow(() -> new AssertionError(
                        "ABI-v10 producer omitted the typed empty-vector summary"));
        if (emptyIntSummary.variableCount != 0 || !emptyIntSummary.parameters.isEmpty()
                || emptyIntSummary.result.kind != AbiV10Records.WireTypeKind.Builtin
                || !"vector".equals(emptyIntSummary.result.identity)
                || emptyIntSummary.result.arguments.size() != 1
                || emptyIntSummary.result.arguments.get(0).kind != AbiV10Records.WireTypeKind.Primitive
                || !"int".equals(emptyIntSummary.result.arguments.get(0).identity)) {
            throw new AssertionError("ABI-v10 typed empty-vector summary lost its concrete element type");
        }
        AbiV10Records.CheckedBodyRecord numbersBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:numbers"))
                .findFirst().orElseThrow(() -> new AssertionError(
                        "ABI-v10 producer omitted the closed Int vector literal body"));
        java.util.List<org.mosslang.compiler.abi.CheckedStmt> numbersStatements =
                CheckedBodyCodec.decodeCheckedBody(numbersBody.checkedIrBytes);
        if (numbersStatements.size() != 1
                || numbersStatements.get(0).expr.kind != org.mosslang.compiler.abi.CheckedExprKind.VectorLiteral
                || numbersStatements.get(0).expr.operands.size() != 3
                || numbersStatements.get(0).expr.operands.get(2).kind
                        != org.mosslang.compiler.abi.CheckedExprKind.Binary) {
            throw new AssertionError("ABI-v10 closed Int vector literal body is incomplete");
        }
        for (String callable : List.of("flags", "ratios", "names")) {
            AbiV10Records.CheckedBodyRecord vectorBody = identityInterface.privateSemanticDependencies.stream()
                    .filter(record -> record.id.startsWith("checked-body-v1:"))
                    .map(AbiV10Records.CheckedBodyRecord::decode)
                    .filter(body -> body.callableId.endsWith(":fn:" + callable))
                    .findFirst().orElseThrow(() -> new AssertionError(
                            "ABI-v10 producer omitted the closed primitive vector body for " + callable));
            java.util.List<org.mosslang.compiler.abi.CheckedStmt> vectorStatements =
                    CheckedBodyCodec.decodeCheckedBody(vectorBody.checkedIrBytes);
            if (vectorStatements.size() != 1
                    || vectorStatements.get(0).expr.kind
                            != org.mosslang.compiler.abi.CheckedExprKind.VectorLiteral
                    || vectorStatements.get(0).expr.operands.size() != 3) {
                throw new AssertionError("ABI-v10 closed primitive vector body is incomplete for " + callable);
            }
        }
        AbiV10Records.CheckedBodyRecord matrixBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:matrix"))
                .findFirst().orElseThrow(() -> new AssertionError(
                        "ABI-v10 producer omitted the closed nested-vector body"));
        java.util.List<org.mosslang.compiler.abi.CheckedStmt> matrixStatements =
                CheckedBodyCodec.decodeCheckedBody(matrixBody.checkedIrBytes);
        if (matrixStatements.size() != 1
                || matrixStatements.get(0).expr.operands.size() != 3
                || matrixStatements.get(0).expr.operands.get(0).kind
                        != org.mosslang.compiler.abi.CheckedExprKind.VectorLiteral
                || matrixStatements.get(0).expr.operands.get(1).operands.size() != 1
                || !matrixStatements.get(0).expr.operands.get(2).operands.isEmpty()) {
            throw new AssertionError("ABI-v10 nested-vector checked body is incomplete");
        }
        AbiV10Records.CheckedBodyRecord escapedNamesBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:escaped_names"))
                .findFirst().orElseThrow(() -> new AssertionError(
                        "ABI-v10 producer omitted escaped String vector body"));
        if (CheckedBodyCodec.decodeCheckedBody(escapedNamesBody.checkedIrBytes).get(0).expr.operands.size() != 2) {
            throw new AssertionError("ABI-v10 escaped String vector body is incomplete");
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
        for (String localCallable : List.of("with_local", "with_mutable_local",
                "with_float_local", "with_mutable_float_local")) {
            AbiV10Records.CheckedBodyRecord localBody = identityInterface.privateSemanticDependencies.stream()
                    .filter(record -> record.id.startsWith("checked-body-v1:"))
                    .map(AbiV10Records.CheckedBodyRecord::decode)
                    .filter(body -> body.callableId.endsWith(":fn:" + localCallable))
                    .findFirst().orElseThrow(() -> new AssertionError(
                            "ABI-v10 producer omitted local-body IR for " + localCallable));
            List<org.mosslang.compiler.abi.CheckedStmt> localStatements =
                    CheckedBodyCodec.decodeCheckedBody(localBody.checkedIrBytes);
            boolean floatLocal = localCallable.contains("float");
            boolean mutableLocal = localCallable.contains("mutable");
            org.mosslang.compiler.abi.CheckedStmtKind expectedDeclaration = !mutableLocal
                    ? org.mosslang.compiler.abi.CheckedStmtKind.Let
                    : org.mosslang.compiler.abi.CheckedStmtKind.Var;
            int expectedSize = mutableLocal ? 3 : 2;
            int returnIndex = expectedSize - 1;
            if (localStatements.size() != expectedSize
                    || localStatements.get(0).kind != expectedDeclaration
                    || (mutableLocal
                            && localStatements.get(1).kind != org.mosslang.compiler.abi.CheckedStmtKind.Assign)
                    || localStatements.get(returnIndex).kind != org.mosslang.compiler.abi.CheckedStmtKind.Return
                    || !localStatements.get(0).type.equals(TypeRef.primitive(floatLocal ? "float" : "int"))) {
                throw new AssertionError("ABI-v10 local statement body is incomplete for " + localCallable);
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
        for (String callable : List.of("is_in_range", "is_nonnegative", "is_warm")) {
            AbiV10Records.CheckedBodyRecord body = identityInterface.privateSemanticDependencies.stream()
                    .filter(record -> record.id.startsWith("checked-body-v1:"))
                    .map(AbiV10Records.CheckedBodyRecord::decode)
                    .filter(record -> record.callableId.endsWith(":fn:" + callable))
                    .findFirst().orElseThrow(() -> new AssertionError(
                            "ABI-v10 producer omitted the Boolean expression body for " + callable));
            List<org.mosslang.compiler.abi.CheckedStmt> statements =
                    CheckedBodyCodec.decodeCheckedBody(body.checkedIrBytes);
            org.mosslang.compiler.abi.CheckedExpr expr = statements.get(0).expr;
            boolean expected = callable.equals("is_in_range") || callable.equals("is_warm")
                    ? expr.kind == org.mosslang.compiler.abi.CheckedExprKind.Binary
                            && expr.binaryOp == org.mosslang.compiler.abi.CheckedBinaryOp.And
                    : expr.kind == org.mosslang.compiler.abi.CheckedExprKind.Unary
                            && expr.unaryOp == org.mosslang.compiler.abi.CheckedUnaryOp.Not;
            if (statements.size() != 1 || !expected) {
                throw new AssertionError("ABI-v10 Boolean expression tree is incomplete for " + callable);
            }
        }
        AbiV10Records.CheckedBodyRecord branchBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:abs_value"))
                .findFirst().orElseThrow(() -> new AssertionError(
                        "ABI-v10 producer omitted the Int branch-return body"));
        List<org.mosslang.compiler.abi.CheckedStmt> branchStatements =
                CheckedBodyCodec.decodeCheckedBody(branchBody.checkedIrBytes);
        if (branchStatements.size() != 1
                || branchStatements.get(0).kind != org.mosslang.compiler.abi.CheckedStmtKind.If
                || branchStatements.get(0).thenBranch.size() != 1
                || branchStatements.get(0).elseBranch.size() != 1
                || branchStatements.get(0).thenBranch.get(0).kind
                        != org.mosslang.compiler.abi.CheckedStmtKind.Return
                || branchStatements.get(0).elseBranch.get(0).kind
                        != org.mosslang.compiler.abi.CheckedStmtKind.Return) {
            throw new AssertionError("ABI-v10 Int branch-return checked body is incomplete");
        }
        AbiV10Records.CheckedBodyRecord floatBranchBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:abs_ratio"))
                .findFirst().orElseThrow(() -> new AssertionError(
                        "ABI-v10 producer omitted the Float branch-return body"));
        List<org.mosslang.compiler.abi.CheckedStmt> floatBranchStatements =
                CheckedBodyCodec.decodeCheckedBody(floatBranchBody.checkedIrBytes);
        if (floatBranchStatements.size() != 1
                || floatBranchStatements.get(0).kind != org.mosslang.compiler.abi.CheckedStmtKind.If
                || !"float".equals(floatBranchStatements.get(0).expr.operands.get(0).type.name())) {
            throw new AssertionError("ABI-v10 Float branch-return checked body is incomplete");
        }
        AbiV10Records.CheckedBodyRecord boolBranchBody = identityInterface.privateSemanticDependencies.stream()
                .filter(record -> record.id.startsWith("checked-body-v1:"))
                .map(AbiV10Records.CheckedBodyRecord::decode)
                .filter(body -> body.callableId.endsWith(":fn:is_nonnegative_branch"))
                .findFirst().orElseThrow(() -> new AssertionError(
                        "ABI-v10 producer omitted the Bool branch-return body"));
        List<org.mosslang.compiler.abi.CheckedStmt> boolBranchStatements =
                CheckedBodyCodec.decodeCheckedBody(boolBranchBody.checkedIrBytes);
        if (boolBranchStatements.size() != 1
                || boolBranchStatements.get(0).kind != org.mosslang.compiler.abi.CheckedStmtKind.If
                || boolBranchStatements.get(0).thenBranch.get(0).expr.kind
                        != org.mosslang.compiler.abi.CheckedExprKind.BoolLiteral) {
            throw new AssertionError("ABI-v10 Bool branch-return checked body is incomplete");
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
            List<MossiRecord> answerRows = identityInterface.semanticContract.stream()
                    .filter(record -> record.id.contains("answer") || record.payload.contains("fn answer")
                            || record.id.contains("increment") || record.payload.contains("fn increment")
                            || record.id.contains("enabled") || record.payload.contains("fn enabled")
                            || record.id.contains("names") || record.payload.contains("fn names")
                            || record.id.contains("identity") || record.payload.contains("fn identity")
                            || record.id.contains("second") || record.payload.contains("fn second")
                            || record.id.contains("fresh") || record.payload.contains("fn fresh")
                            || record.id.contains("matrix") || record.payload.contains("fn matrix")
                            || record.id.contains("escaped_names") || record.payload.contains("fn escaped_names")
                            || record.id.contains("ratio") || record.payload.contains("fn ratio")
                            || record.id.contains("greeting") || record.payload.contains("fn greeting")
                            || record.id.contains("combine") || record.payload.contains("fn combine")
                            || record.id.contains("negative") || record.payload.contains("fn negative")
                            || record.id.contains("weighted") || record.payload.contains("fn weighted")
                            || record.id.contains("is_in_range") || record.payload.contains("fn is_in_range"))
                    .toList();
            if (answerRows.isEmpty()) {
                throw new AssertionError("could not isolate legacy callable declarations for ABI-v10-only import");
            }
            List<MossiRecord> binaryLegacyRows = identityInterface.binaryContract.stream()
                    .filter(record -> record.id.equals("fn:answer") || record.id.startsWith("fn:answer::")
                            || record.id.equals("fn:increment") || record.id.startsWith("fn:increment::")
                            || record.id.equals("fn:enabled") || record.id.startsWith("fn:enabled::")
                            || record.id.equals("fn:names") || record.id.startsWith("fn:names::")
                            || record.id.equals("fn:identity") || record.id.startsWith("fn:identity::")
                            || record.id.equals("fn:second") || record.id.startsWith("fn:second::")
                            || record.id.equals("fn:fresh") || record.id.startsWith("fn:fresh::")
                            || record.id.equals("fn:matrix") || record.id.startsWith("fn:matrix::")
                            || record.id.equals("fn:escaped_names") || record.id.startsWith("fn:escaped_names::")
                            || record.id.equals("fn:ratio") || record.id.startsWith("fn:ratio::")
                            || record.id.equals("fn:greeting") || record.id.startsWith("fn:greeting::")
                            || record.id.equals("fn:combine") || record.id.startsWith("fn:combine::")
                            || record.id.equals("fn:negative") || record.id.startsWith("fn:negative::")
                            || record.id.equals("fn:weighted") || record.id.startsWith("fn:weighted::")
                            || record.id.equals("fn:is_in_range") || record.id.startsWith("fn:is_in_range::"))
                    .toList();
            for (String functionName : List.of("answer", "increment", "enabled", "names", "identity", "second", "fresh", "matrix", "escaped_names", "ratio", "greeting", "combine", "negative", "weighted", "is_in_range")) {
                String legacyPrefix = "fn:" + functionName;
                if (answerRows.stream().noneMatch(record -> record.id.equals(legacyPrefix)
                                || record.id.startsWith(legacyPrefix + "::"))
                        || binaryLegacyRows.stream().noneMatch(record -> record.id.equals(legacyPrefix)
                                || record.id.startsWith(legacyPrefix + "::"))) {
                    throw new AssertionError("could not remove both legacy declaration copies for " + functionName);
                }
            }
            identityInterface.semanticContract.removeAll(answerRows);
            identityInterface.binaryContract.removeAll(binaryLegacyRows);
            for (String functionName : List.of("answer", "increment", "enabled", "names", "identity",
                    "second", "fresh", "matrix", "escaped_names", "ratio", "greeting", "combine", "negative",
                    "weighted", "is_in_range")) {
                String legacyPrefix = "fn:" + functionName;
                if (identityInterface.semanticContract.stream().anyMatch(record ->
                                record.id.equals(legacyPrefix) || record.id.startsWith(legacyPrefix + "::"))
                        || identityInterface.binaryContract.stream().anyMatch(record ->
                                record.id.equals(legacyPrefix) || record.id.startsWith(legacyPrefix + "::"))) {
                    throw new AssertionError("legacy callable declaration rows remain in the v10-only fixture: "
                            + functionName);
                }
            }
            Files.write(identityMossi, CheckedBodyCodec.encodeMossiBytes(identityInterface));
            Program answerConsumer = Parser.parseSource(""
                    + "fn use_fresh_int() -> Vector[Int]:\n"
                    + "  return provider__fresh()\n"
                    + "fn use_fresh_bool() -> Vector[Bool]:\n"
                    + "  return provider__fresh()\n"
                    + "fn main():\n"
                    + "  echo provider__answer()\n"
                    + "  echo provider__increment(41)\n"
                    + "  echo provider__enabled()\n"
                    + "  echo provider__names()[1]\n"
                    + "  echo provider__identity(7)\n"
                    + "  echo provider__identity(\"seven\")\n"
                    + "  echo provider__second(7, \"seven\")\n"
                    + "  echo provider__matrix()[1][0]\n"
                    + "  echo provider__escaped_names()[0].length()\n"
                    + "  echo provider__escaped_names()[1].length()\n"
                    + "  echo provider__ratio()\n"
                    + "  echo provider__greeting()\n"
                    + "  echo provider__combine(20, 8)\n"
                    + "  echo provider__negative()\n"
                    + "  echo provider__weighted(2.0, 4.0)\n"
                    + "  echo provider__is_in_range(7)\n"
                    + "  echo use_fresh_int().len()\n"
                    + "  echo use_fresh_bool().len()\n", "answer-consumer.moss");
            org.mosslang.compiler.cli.Main.loadMossiFunctions(identityMossi, "provider", answerConsumer);
            Function seededIdentity = answerConsumer.functions.stream()
                    .filter(function -> function.name.equals("provider.identity"))
                    .findFirst().orElseThrow(() -> new AssertionError("ABI-v10 identity declaration was not seeded"));
            if (!seededIdentity.generic || seededIdentity.params.size() != 1
                    || seededIdentity.params.get(0).typeRef != null
                    || seededIdentity.parameterAccessModes.size() != 1
                    || seededIdentity.body.size() != 1
                    || seededIdentity.body.get(0).kind != org.mosslang.compiler.ast.Stmt.Kind.Return
                    || !seededIdentity.body.get(0).a.equals(seededIdentity.params.get(0).name)) {
                throw new AssertionError("ABI-v10 generic identity seed did not retain the open call-local profile");
            }
            new org.mosslang.compiler.semantics.SemanticChecker(answerConsumer).run();
            java.io.ByteArrayOutputStream answerOutput = new java.io.ByteArrayOutputStream();
            new org.mosslang.compiler.interpreter.FastInterpreter(answerConsumer, false,
                    new java.io.PrintStream(answerOutput),
                    new java.io.PrintStream(new java.io.ByteArrayOutputStream())).run();
            if (!"42\n42\ntrue\ntwo\n7\nseven\nseven\n3\n10\n14\n1.5\nhello\n64\n-42\n7.0\ntrue\n0\n0\n".equals(
                    answerOutput.toString(java.nio.charset.StandardCharsets.UTF_8))) {
                throw new AssertionError("ABI-v10 summary bodies did not import without their v9 declarations: "
                        + answerOutput);
            }
            identityInterface.semanticContract.addAll(answerRows);
            identityInterface.binaryContract.addAll(binaryLegacyRows);
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
                    + "fn use_fresh_int() -> Vector[Int]:\n"
                    + "  return provider__fresh()\n"
                    + "fn use_fresh_bool() -> Vector[Bool]:\n"
                    + "  return provider__fresh()\n"
                    + "fn use_empty_int() -> Vector[Int]:\n"
                    + "  return provider__empty_int()\n"
                    + "fn use_numbers() -> Vector[Int]:\n"
                    + "  return provider__numbers()\n"
                    + "fn use_flags() -> Vector[Bool]:\n"
                    + "  return provider__flags()\n"
                    + "fn use_ratios() -> Vector[Float]:\n"
                    + "  return provider__ratios()\n"
                    + "fn use_names() -> Vector[String]:\n"
                    + "  return provider__names()\n"
                    + "fn use_matrix_element() -> Int:\n"
                    + "  matrix = provider__matrix()\n"
                    + "  return matrix[1][0]\n"
                    + "fn use_escaped_names_length() -> Int:\n"
                    + "  names = provider__escaped_names()\n"
                    + "  return names[0].length() + names[1].length()\n"
                    + "fn use_weighted() -> Float:\n"
                    + "  return provider__weighted(2.0, 4.0)\n"
                    + "fn use_adjust_ratio() -> Float:\n"
                    + "  return provider__adjust_ratio(3.0)\n"
                    + "fn use_is_positive() -> Bool:\n"
                    + "  return provider__is_positive(7)\n"
                    + "fn use_is_in_range() -> Bool:\n"
                    + "  return provider__is_in_range(7)\n"
                    + "fn use_is_nonnegative() -> Bool:\n"
                    + "  return provider__is_nonnegative(7)\n"
                    + "fn use_abs_value() -> Int:\n"
                    + "  return provider__abs_value(-5)\n"
                    + "fn use_abs_ratio() -> Float:\n"
                    + "  return provider__abs_ratio(-5.0)\n"
                    + "fn use_is_nonnegative_branch() -> Bool:\n"
                    + "  return provider__is_nonnegative_branch(5)\n"
                    + "fn use_is_warm() -> Bool:\n"
                    + "  return provider__is_warm(22.5)\n"
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
                    + "fn use_with_local() -> Int:\n"
                    + "  return provider__with_local(20)\n"
                    + "fn use_with_mutable_local() -> Int:\n"
                    + "  return provider__with_mutable_local(20)\n"
                    + "fn use_with_float_local() -> Float:\n"
                    + "  return provider__with_float_local(2.0)\n"
                    + "fn use_with_mutable_float_local() -> Float:\n"
                    + "  return provider__with_mutable_float_local(2.0)\n"
                    + "fn use_negated() -> Int:\n"
                    + "  return provider__negated(3)\n"
                    + "fn main():\n"
                    + "  echo use_matrix_element()\n"
                    + "  echo use_escaped_names_length()\n", "identity-consumer.moss");
            org.mosslang.compiler.cli.Main.loadMossiFunctions(identityMossi, "provider", consumer);
            Function importedBranch = consumer.functions.stream()
                    .filter(function -> function.name.equals("provider.abs_value"))
                    .findFirst().orElseThrow(() -> new AssertionError("source-free branch callable was not loaded"));
            if (importedBranch.body.size() != 4
                    || importedBranch.body.get(0).kind != org.mosslang.compiler.ast.Stmt.Kind.If
                    || importedBranch.body.get(1).kind != org.mosslang.compiler.ast.Stmt.Kind.Return
                    || importedBranch.body.get(2).kind != org.mosslang.compiler.ast.Stmt.Kind.Else
                    || importedBranch.body.get(3).kind != org.mosslang.compiler.ast.Stmt.Kind.Return) {
                throw new AssertionError("ABI-v10 Int branch-return facts were not projected into the source-free callable");
            }
            Function importedFloatBranch = consumer.functions.stream()
                    .filter(function -> function.name.equals("provider.abs_ratio"))
                    .findFirst().orElseThrow(() -> new AssertionError("source-free Float branch callable was not loaded"));
            if (importedFloatBranch.body.size() != 4
                    || importedFloatBranch.body.get(0).kind != org.mosslang.compiler.ast.Stmt.Kind.If
                    || importedFloatBranch.body.get(3).a == null
                    || !importedFloatBranch.body.get(3).a.contains("-")) {
                throw new AssertionError("ABI-v10 Float branch-return facts were not projected into the source-free callable");
            }
            Function importedBoolBranch = consumer.functions.stream()
                    .filter(function -> function.name.equals("provider.is_nonnegative_branch"))
                    .findFirst().orElseThrow(() -> new AssertionError("source-free Bool branch callable was not loaded"));
            if (importedBoolBranch.body.size() != 4
                    || importedBoolBranch.body.get(0).kind != org.mosslang.compiler.ast.Stmt.Kind.If
                    || importedBoolBranch.body.get(1).a == null
                    || !importedBoolBranch.body.get(1).a.contains("true")) {
                throw new AssertionError("ABI-v10 Bool branch-return facts were not projected into the source-free callable");
            }
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
            Function importedMatrix = consumer.functions.stream()
                    .filter(function -> function.name.equals("provider.matrix"))
                    .findFirst().orElseThrow(() -> new AssertionError("source-free nested-vector function was not loaded"));
            if (importedMatrix.body.size() != 1
                    || importedMatrix.body.get(0).kind != org.mosslang.compiler.ast.Stmt.Kind.Return
                    || importedMatrix.body.get(0).a == null
                    || !importedMatrix.body.get(0).a.contains("[[1, 2], [3], []]")) {
                throw new AssertionError("source-free nested-vector literal was not projected: "
                        + importedMatrix.body);
            }
            java.io.ByteArrayOutputStream output = new java.io.ByteArrayOutputStream();
            new org.mosslang.compiler.interpreter.FastInterpreter(consumer, false,
                    new java.io.PrintStream(output), new java.io.PrintStream(new java.io.ByteArrayOutputStream())).run();
            if (!"3\n24\n".equals(output.toString(java.nio.charset.StandardCharsets.UTF_8))) {
                throw new AssertionError("source-free nested vector did not execute through indexed access: " + output);
            }
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
