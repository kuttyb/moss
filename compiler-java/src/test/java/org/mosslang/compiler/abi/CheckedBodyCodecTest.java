package org.mosslang.compiler.abi;

import org.mosslang.compiler.diagnostics.CompileError;
import org.mosslang.compiler.types.TypeRef;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

public class CheckedBodyCodecTest {
    public static void main(String[] args) throws IOException {
        System.out.println("Running CheckedBodyCodecTest...");
        testSimpleReturnEncode();
        testArithmeticHashDeterminism();
        testSimpleReturnEncodeDecodeRoundTrip();
        testComplexStatementsRoundTrip();
        testMossiSerializationAndParsing();
        testMossiUtf8ByteFraming();
        testMossiBinaryPayloadRoundTrip();
        testMossiEncoderRejectsDuplicateIds();
        testNominalRecordsRequireValidProvenanceReferences();
        testMethodTemplateNominalReferences();
        testCanonicalOpenSummaryRecord();
        testExportedEnumIdentityValidation();
        testMossiRejectsUnboundedCounts();
        testCheckedBodyReaderBoundsAndTrailingData();
        testSourceFreeVerificationSuccess();
        testSourceFreeVerificationTamperDetection();
        System.out.println("CheckedBodyCodecTest PASSED: All tests passed successfully.");
    }

    private static void testSimpleReturnEncode() {
        TypeRef intType = TypeRef.primitive("int");
        CheckedExpr param0 = CheckedExpr.parameter(0, intType);
        CheckedExpr lit42 = CheckedExpr.integerLiteral(42, intType);
        CheckedExpr add = CheckedExpr.binary(CheckedBinaryOp.Add, param0, lit42, intType);
        CheckedStmt ret = CheckedStmt.returnStmt(add);

        String encoded = CheckedBodyCodec.encodeCheckedBodyV1(List.of(ret));
        if (!encoded.startsWith("moss-checked-body-v1\n")) {
            throw new AssertionError("Invalid header in encoded checked body");
        }
        if (!encoded.contains("42")) {
            throw new AssertionError("Encoded body should contain literal 42");
        }
    }

    private static void testArithmeticHashDeterminism() {
        TypeRef intType = TypeRef.primitive("int");
        CheckedExpr param0 = CheckedExpr.parameter(0, intType);
        CheckedStmt ret = CheckedStmt.returnStmt(param0);

        String hash1 = CheckedBodyCodec.computeCheckedBodyHashV1(List.of(ret));
        String hash2 = CheckedBodyCodec.computeCheckedBodyHashV1(List.of(ret));

        if (!hash1.startsWith("body-semantic-v1:sha256:")) {
            throw new AssertionError("Hash must start with body-semantic-v1:sha256:");
        }
        if (!hash1.equals(hash2)) {
            throw new AssertionError("Hash computation must be strictly deterministic");
        }
    }

    private static void testSimpleReturnEncodeDecodeRoundTrip() {
        TypeRef intType = TypeRef.primitive("int");
        CheckedExpr param0 = CheckedExpr.parameter(0, intType);
        CheckedExpr lit42 = CheckedExpr.integerLiteral(42, intType);
        CheckedExpr add = CheckedExpr.binary(CheckedBinaryOp.Add, param0, lit42, intType);
        CheckedStmt ret = CheckedStmt.returnStmt(add);
        List<CheckedStmt> original = List.of(ret);

        String encoded = CheckedBodyCodec.encodeCheckedBodyV1(original);
        List<CheckedStmt> decoded = CheckedBodyCodec.decodeCheckedBody(encoded);

        if (decoded.size() != 1) {
            throw new AssertionError("Expected 1 statement, got " + decoded.size());
        }
        if (!decoded.equals(original)) {
            throw new AssertionError("Decoded statement does not match original: expected " + original + ", got " + decoded);
        }

        String reencoded = CheckedBodyCodec.encodeCheckedBodyV1(decoded);
        if (!encoded.equals(reencoded)) {
            throw new AssertionError("Re-encoded body differs from original encoding:\nOriginal:  " + encoded + "\nReencoded: " + reencoded);
        }

        String hash1 = CheckedBodyCodec.computeCheckedBodyHashV1(original);
        String hash2 = CheckedBodyCodec.computeCheckedBodyHashV1(decoded);
        if (!hash1.equals(hash2)) {
            throw new AssertionError("Hash mismatch after decode: " + hash1 + " vs " + hash2);
        }
    }

    private static void testComplexStatementsRoundTrip() {
        TypeRef intType = TypeRef.primitive("int");
        CheckedExpr param0 = CheckedExpr.parameter(0, intType);
        CheckedExpr lit10 = CheckedExpr.integerLiteral(10, intType);
        CheckedExpr sum = CheckedExpr.binary(CheckedBinaryOp.Add, param0, lit10, intType);
        CheckedStmt stmt1 = CheckedStmt.letStmt(0, sum);

        CheckedExpr read0 = CheckedExpr.localRead(0, intType);
        CheckedExpr lit2 = CheckedExpr.integerLiteral(2, intType);
        CheckedExpr mul = CheckedExpr.binary(CheckedBinaryOp.Mul, read0, lit2, intType);
        CheckedStmt stmt2 = CheckedStmt.varStmt(1, mul);

        CheckedExpr read1 = CheckedExpr.localRead(1, intType);
        CheckedStmt stmt3 = CheckedStmt.returnStmt(read1);

        List<CheckedStmt> original = List.of(stmt1, stmt2, stmt3);

        String encoded = CheckedBodyCodec.encodeCheckedBodyV1(original);
        List<CheckedStmt> decoded = CheckedBodyCodec.decodeCheckedBody(encoded);

        if (!decoded.equals(original)) {
            throw new AssertionError("Decoded multi-statement body does not match original");
        }

        String reencoded = CheckedBodyCodec.encodeCheckedBodyV1(decoded);
        if (!encoded.equals(reencoded)) {
            throw new AssertionError("Re-encoded multi-statement body differs");
        }

        String hash1 = CheckedBodyCodec.computeCheckedBodyHashV1(original);
        String hash2 = CheckedBodyCodec.computeCheckedBodyHashV1(decoded);
        if (!hash1.equals(hash2)) {
            throw new AssertionError("Hash mismatch on complex statement body");
        }
    }

    private static void testMossiSerializationAndParsing() {
        MossiInterface mossi = new MossiInterface("module-v1:math@1.0.0::calc");
        mossi.compiler = "moss-java-0.1.0";
        mossi.backendFingerprint = "fingerprint-v10";
        mossi.backendRustc = "rustc-1.80.0";

        mossi.addImport("module-v1:core@1.0.0::foundations",
                "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                "sha256:fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210");

        TypeRef intType = TypeRef.primitive("int");
        CheckedExpr p0 = CheckedExpr.parameter(0, intType);
        CheckedExpr p1 = CheckedExpr.parameter(1, intType);
        CheckedExpr add = CheckedExpr.binary(CheckedBinaryOp.Add, p0, p1, intType);
        CheckedStmt ret = CheckedStmt.returnStmt(add);
        List<CheckedStmt> addBody = List.of(ret);
        String addBodyHash = CheckedBodyCodec.computeCheckedBodyHashV1(addBody);

        mossi.addFunction("add", "(a: int, b: int) -> int", addBodyHash);
        mossi.addDomain("Calculator", List.of("Worker", "Logger"));
        mossi.addCheckedBody("calc.add@1", addBody);

        String encoded = CheckedBodyCodec.encodeMossi(mossi);
        if (!encoded.startsWith("moss-module-interface-v3\nprovider_contract_digest sha256:")) {
            throw new AssertionError("Encoded .mossi has invalid header:\n" + encoded.substring(0, Math.min(200, encoded.length())));
        }

        MossiInterface decoded = CheckedBodyCodec.decodeMossi(encoded);
        if (!decoded.moduleId.equals(mossi.moduleId)) {
            throw new AssertionError("Module ID mismatch: expected " + mossi.moduleId + ", got " + decoded.moduleId);
        }
        if (decoded.imports.size() != 1 || !decoded.imports.get(0).equals(mossi.imports.get(0))) {
            throw new AssertionError("Imports mismatch");
        }
        if (decoded.semanticContract.size() != mossi.semanticContract.size()) {
            throw new AssertionError("Semantic contract size mismatch: expected " + mossi.semanticContract.size() + ", got " + decoded.semanticContract.size());
        }
        if (!decoded.digest.equals(mossi.digest)) {
            throw new AssertionError("Digest mismatch: expected " + mossi.digest + ", got " + decoded.digest);
        }
    }

    private static void testSourceFreeVerificationSuccess() throws IOException {
        MossiInterface mossi = new MossiInterface("module-v1:testpkg@2.1.0::service");
        mossi.addImport("module-v1:core@1.0.0::foundations",
                "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");

        TypeRef intType = TypeRef.primitive("int");
        CheckedStmt ret = CheckedStmt.returnStmt(CheckedExpr.integerLiteral(100, intType));
        mossi.addFunction("answer", "() -> int", CheckedBodyCodec.computeCheckedBodyHashV1(List.of(ret)));
        mossi.addCheckedBody("service.answer@1", List.of(ret));

        String mossiText = CheckedBodyCodec.encodeMossi(mossi);

        // Verify in-memory
        MossiInterface verified = CheckedBodyCodec.verifySourceFree(mossiText);
        if (verified == null || !verified.moduleId.equals(mossi.moduleId)) {
            throw new AssertionError("Verification failed for valid .mossi interface");
        }

        // Verify from disk file
        File tmpDir = new File("tmp");
        if (!tmpDir.exists()) tmpDir.mkdirs();
        Path tmpFile = Path.of("tmp", "service.mossi");
        Files.writeString(tmpFile, mossiText);

        MossiInterface loaded = CheckedBodyCodec.loadSourceFreeInterface(tmpFile);
        if (loaded == null || !loaded.moduleId.equals(mossi.moduleId)) {
            throw new AssertionError("loadSourceFreeInterface failed from disk");
        }
    }

    private static void testMossiRejectsUnboundedCounts() {
        String valid = CheckedBodyCodec.encodeMossi(
                new MossiInterface("module-v1:bounds@1.0.0::provider"));
        String oversized = valid.replaceFirst("imports\\ncount 0\\n", "imports\\ncount 2147483647\\n");
        try {
            CheckedBodyCodec.decodeMossi(oversized);
            throw new AssertionError("ABI-v10 decoder accepted an unbounded imports count");
        } catch (CompileError error) {
            if (!"SOURCE_FREE_CONTRACT_INVALID".equals(error.getCode())) throw error;
        }

        String negative = valid.replaceFirst("binary_contract\\ncount 0\\n", "binary_contract\\ncount -1\\n");
        try {
            CheckedBodyCodec.decodeMossi(negative);
            throw new AssertionError("ABI-v10 decoder accepted a negative record count");
        } catch (CompileError error) {
            if (!"SOURCE_FREE_CONTRACT_INVALID".equals(error.getCode())) throw error;
        }
    }

    private static void testCheckedBodyReaderBoundsAndTrailingData() {
        String hostileCount = CheckedBodyCodec.HEADER + "10:2147483647";
        assertCheckedBodyRejects(hostileCount, "unbounded statement count");
        String trailing = CheckedBodyCodec.encodeCheckedBodyV1(List.of()) + "garbage";
        assertCheckedBodyRejects(trailing, "trailing body data");
    }

    private static void assertCheckedBodyRejects(String body, String description) {
        try {
            CheckedBodyCodec.decodeCheckedBody(body);
            throw new AssertionError("checked body reader accepted " + description);
        } catch (CompileError error) {
            if (!"SOURCE_FREE_CONTRACT_INVALID".equals(error.getCode())) throw error;
        }
    }

    private static void testMossiUtf8ByteFraming() {
        MossiInterface original = new MossiInterface("module-v1:utf8@1.0.0::provider");
        original.semanticContract.add(new MossiRecord("unicode:é", "display café 😀\n"));
        String encoded = CheckedBodyCodec.encodeMossi(original);
        MossiInterface decoded = CheckedBodyCodec.decodeMossi(encoded);
        if (decoded.semanticContract.size() != 1
                || !original.semanticContract.get(0).equals(decoded.semanticContract.get(0))) {
            throw new AssertionError("ABI-v10 UTF-8 byte-framed record did not round-trip");
        }
        CheckedBodyCodec.verifySourceFree(decoded, encoded);
    }

    private static void testMossiBinaryPayloadRoundTrip() {
        MossiInterface original = new MossiInterface("module-v1:wire@1.0.0::provider");
        original.binaryContract.add(MossiRecord.fromBytes("wire-record",
                new byte[] {0, 1, 0x7f, (byte) 0x80, (byte) 0xff, 10}));
        byte[] encoded = CheckedBodyCodec.encodeMossiBytes(original);
        MossiInterface decoded = CheckedBodyCodec.verifySourceFree(encoded);
        if (decoded.binaryContract.size() != 1
                || !java.util.Arrays.equals(original.binaryContract.get(0).payloadBytes(),
                        decoded.binaryContract.get(0).payloadBytes())) {
            throw new AssertionError("ABI-v10 binary record payload was not preserved");
        }
        try {
            CheckedBodyCodec.encodeMossi(original);
            throw new AssertionError("text encoder accepted a non-UTF-8 binary ABI payload");
        } catch (CompileError expected) {
            if (!"SOURCE_FREE_CONTRACT_INVALID".equals(expected.getCode())) throw expected;
        }
    }

    private static void testMossiEncoderRejectsDuplicateIds() {
        MossiInterface duplicateRecord = new MossiInterface("module-v1:duplicate@1.0.0::provider");
        duplicateRecord.semanticContract.add(new MossiRecord("fn:one", "payload\n"));
        duplicateRecord.semanticContract.add(new MossiRecord("fn:one", "payload\n"));
        assertMossiEncodeRejects(duplicateRecord, "duplicate semantic record ID");

        MossiInterface duplicateImport = new MossiInterface("module-v1:duplicate@1.0.0::provider");
        duplicateImport.addImport("module-v1:dep@1.0.0::api", "1", "2");
        duplicateImport.addImport("module-v1:dep@1.0.0::api", "3", "4");
        assertMossiEncodeRejects(duplicateImport, "duplicate import module ID");
    }

    private static void testNominalRecordsRequireValidProvenanceReferences() {
        String module = "module-v1:records@1.0.0::provider";
        String templateId = "nominal-template-v1:" + module + ":type:Point";
        AbiV10Records.SourceProvenance provenance = AbiV10Records.SourceProvenance.create(
                module, templateId, "provider.moss", 1);
        AbiV10Records.NominalTemplate template = AbiV10Records.NominalTemplate.create(
                module, "Point", List.of(new AbiV10Records.NominalField("x",
                        AbiV10Records.WireType.scalar(
                                AbiV10Records.WireTypeKind.Primitive, "float"),
                        true, provenance.provenanceId())), provenance.provenanceId());

        MossiInterface valid = new MossiInterface(module);
        valid.binaryContract.add(template.record());
        valid.provenance.add(provenance.record());
        CheckedBodyCodec.verifySourceFree(CheckedBodyCodec.encodeMossiBytes(valid));

        MossiInterface missingProvenance = new MossiInterface(module);
        missingProvenance.binaryContract.add(template.record());
        try {
            CheckedBodyCodec.verifySourceFree(CheckedBodyCodec.encodeMossiBytes(missingProvenance));
            throw new AssertionError("ABI-v10 verifier accepted a nominal template with missing provenance");
        } catch (CompileError error) {
            if (!"SOURCE_FREE_TYPE_IDENTITY_MISMATCH".equals(error.getCode())) throw error;
        }
    }

    private static void testMethodTemplateNominalReferences() {
        String module = "module-v1:methods@1.0.0::provider";
        String templateId = "nominal-template-v1:" + module + ":type:Point";
        AbiV10Records.SourceProvenance provenance = AbiV10Records.SourceProvenance.create(
                module, templateId, "provider.moss", 1);
        String methodId = "method-template-v1:" + module + ":type:Point:method:ready";
        AbiV10Records.MethodTemplate method = AbiV10Records.MethodTemplate.create(
                methodId, templateId, "ready", List.of(), List.of(),
                AbiV10Records.WireType.unit(), 1, provenance.provenanceId());
        AbiV10Records.NominalTemplate template = AbiV10Records.NominalTemplate.create(
                module, "Point", List.of(), provenance.provenanceId());
        template.methodTemplateIds.add(methodId);

        MossiInterface valid = new MossiInterface(module);
        valid.binaryContract.add(template.record());
        valid.binaryContract.add(method.record());
        valid.provenance.add(provenance.record());
        CheckedBodyCodec.verifySourceFree(CheckedBodyCodec.encodeMossiBytes(valid));

        AbiV10Records.MethodTemplate orphan = AbiV10Records.MethodTemplate.create(
                "method-template-v1:" + module + ":type:Missing:method:ready",
                "nominal-template-v1:" + module + ":type:Missing", "ready", List.of(), List.of(),
                AbiV10Records.WireType.unit(), 1, provenance.provenanceId());
        MossiInterface invalid = new MossiInterface(module);
        invalid.binaryContract.add(template.record());
        invalid.binaryContract.add(orphan.record());
        invalid.provenance.add(provenance.record());
        try {
            CheckedBodyCodec.verifySourceFree(CheckedBodyCodec.encodeMossiBytes(invalid));
            throw new AssertionError("ABI-v10 verifier accepted a method with an unknown nominal template");
        } catch (CompileError error) {
            if (!"SOURCE_FREE_TYPE_IDENTITY_MISMATCH".equals(error.getCode())) throw error;
        }
    }

    private static void testCanonicalOpenSummaryRecord() {
        AbiV10Records.Summary summary = new AbiV10Records.Summary();
        summary.callableId = "module-v1:boxes@1.0.0::Boxes::fresh";
        summary.variableCount = 1;
        AbiV10Records.WireType variable = AbiV10Records.WireType.bound(0);
        AbiV10Records.WireType vector = AbiV10Records.WireType.scalar(
                AbiV10Records.WireTypeKind.Builtin, "Vector");
        vector.arguments.add(variable);
        summary.result = vector;
        summary.bodySemanticHash = "body-semantic-v1:sha256:" + "e".repeat(64);
        summary.provenanceId = "boxes:fn:3";
        AbiV10Records.Requirement requirement = new AbiV10Records.Requirement();
        requirement.receiver = variable;
        requirement.operation = "to_string";
        requirement.provenanceId = "boxes:fn:4";
        summary.requirements.add(requirement);

        MossiRecord record = summary.record();
        String expectedPayloadHex = "03000000236d6f64756c652d76313a626f78657340312e302e303a3a426f7865733a3a667265736800000001000000000200000006566563746f7200000001060000000000000001060000000000000009746f5f737472696e6701000000000000000a626f7865733a666e3a340000000000000000000000000000000000000000000000000000000058626f64792d73656d616e7469632d76313a7368613235363a656565656565656565656565656565656565656565656565656565656565656565656565656565656565656565656565656565656565656565656565656565650000000a626f7865733a666e3a33";
        if (!expectedPayloadHex.equals(java.util.HexFormat.of().formatHex(record.payloadBytes()))
                || !"sha256:048871e18e43f896c39ed73cbf0e63cef5a3eef0490f1041c153ee21c272e861"
                        .equals(record.id)) {
            throw new AssertionError("Java summary payload differs from canonical C++ ABI-v10 bytes");
        }
        AbiV10Records.Summary decoded = AbiV10Records.Summary.decode(record);
        if (!decoded.callableId.equals(summary.callableId)
                || decoded.variableCount != 1 || decoded.requirements.size() != 1
                || !decoded.result.canonicalName().equals("Vector[_0]")) {
            throw new AssertionError("canonical ABI-v10 summary did not round trip");
        }
        MossiInterface iface = new MossiInterface("module-v1:boxes@1.0.0::Boxes");
        iface.semanticContract.add(record);
        CheckedBodyCodec.verifySourceFree(CheckedBodyCodec.encodeMossiBytes(iface));

        MossiInterface forged = new MossiInterface(iface.moduleId);
        forged.semanticContract.add(MossiRecord.fromBytes("sha256:" + "0".repeat(64),
                record.payloadBytes()));
        try {
            CheckedBodyCodec.verifySourceFree(CheckedBodyCodec.encodeMossiBytes(forged));
            throw new AssertionError("source-free verifier accepted a forged summary identity");
        } catch (CompileError error) {
            if (!"SOURCE_FREE_SUMMARY_INVALID".equals(error.getCode())) throw error;
        }

        AbiV10Records.Summary effectful = new AbiV10Records.Summary();
        effectful.callableId = summary.callableId;
        effectful.variableCount = 1;
        effectful.result = variable;
        effectful.bodySemanticHash = summary.bodySemanticHash;
        effectful.provenanceId = summary.provenanceId;
        effectful.requirements.add(requirement);
        AbiV10Records.Requirement callable = new AbiV10Records.Requirement();
        callable.receiver = variable;
        callable.operation = "static callable";
        callable.provenanceId = "boxes:fn:5";
        effectful.requirements.add(callable);
        effectful.effectDependencies.add(2);
        MossiRecord effectRecord = effectful.record();
        AbiV10Records.Summary decodedEffect = AbiV10Records.Summary.decode(effectRecord);
        if (!"sha256:ee950bd5e71b48062bdeef224a8f72d52b8531ead2d74dde5863b97790905e0e"
                        .equals(effectRecord.id)
                || decodedEffect.effectDependencies.size() != 1
                || decodedEffect.effectDependencies.get(0) != 2) {
            throw new AssertionError("ABI-v10 v4 summary differs from C++ Callable-effect record");
        }
        MossiInterface effectInterface = new MossiInterface(iface.moduleId);
        effectInterface.semanticContract.add(effectRecord);
        CheckedBodyCodec.verifySourceFree(CheckedBodyCodec.encodeMossiBytes(effectInterface));
    }

    private static void testExportedEnumIdentityValidation() {
        String module = "module-v1:enum-pkg@1.0.0::provider";
        MossiInterface valid = new MossiInterface(module);
        valid.semanticContract.add(new MossiRecord("00000000 export Result", ""
                + "export enum Result\n"
                + "  declaration_id nominal-enum-v1:" + module + ":enum:Result\n"
                + "  enum_case \"Ok\" 0\n"));
        CheckedBodyCodec.verifySourceFree(CheckedBodyCodec.encodeMossiBytes(valid));

        MossiInterface foreignIdentity = new MossiInterface(module);
        foreignIdentity.semanticContract.add(new MossiRecord("00000000 export Result", ""
                + "export enum Result\n"
                + "  declaration_id nominal-enum-v1:module-v1:other@1.0.0::provider:enum:Result\n"
                + "  enum_case \"Ok\" 0\n"));
        try {
            CheckedBodyCodec.verifySourceFree(CheckedBodyCodec.encodeMossiBytes(foreignIdentity));
            throw new AssertionError("ABI-v10 verifier accepted a foreign enum declaration identity");
        } catch (CompileError error) {
            if (!"SOURCE_FREE_TYPE_IDENTITY_MISMATCH".equals(error.getCode())) throw error;
        }

        MossiInterface validDomain = new MossiInterface(module);
        validDomain.semanticContract.add(new MossiRecord("00000000 export Router", ""
                + "export domain Router opaque\n"
                + "  declaration_id nominal-domain-v1:" + module + ":domain:Router\n"));
        CheckedBodyCodec.verifySourceFree(CheckedBodyCodec.encodeMossiBytes(validDomain));
        MossiInterface foreignDomain = new MossiInterface(module);
        foreignDomain.semanticContract.add(new MossiRecord("00000000 export Router", ""
                + "export domain Router opaque\n"
                + "  declaration_id nominal-domain-v1:module-v1:foreign@1.0.0::provider:domain:Router\n"));
        try {
            CheckedBodyCodec.verifySourceFree(CheckedBodyCodec.encodeMossiBytes(foreignDomain));
            throw new AssertionError("ABI-v10 verifier accepted a foreign domain declaration identity");
        } catch (CompileError error) {
            if (!"SOURCE_FREE_TYPE_IDENTITY_MISMATCH".equals(error.getCode())) throw error;
        }
    }

    private static void assertMossiEncodeRejects(MossiInterface mossi, String description) {
        try {
            CheckedBodyCodec.encodeMossi(mossi);
            throw new AssertionError("ABI-v10 encoder accepted " + description);
        } catch (CompileError error) {
            if (!"SOURCE_FREE_CONTRACT_INVALID".equals(error.getCode())) throw error;
        }
    }

    private static void testSourceFreeVerificationTamperDetection() {
        MossiInterface mossi = new MossiInterface("module-v1:secpkg@1.0.0::mod");
        TypeRef intType = TypeRef.primitive("int");
        mossi.addFunction("foo", "() -> int", "");
        String text = CheckedBodyCodec.encodeMossi(mossi);

        // 1. Tamper with digest
        String tamperedDigest = text.replace("provider_contract_digest sha256:" + mossi.digest,
                                             "provider_contract_digest sha256:0000000000000000000000000000000000000000000000000000000000000000");
        boolean caughtDigest = false;
        try {
            CheckedBodyCodec.verifySourceFree(tamperedDigest);
        } catch (CompileError e) {
            if ("SOURCE_FREE_CONTRACT_MISMATCH".equals(e.getCode())) {
                caughtDigest = true;
            }
        }
        if (!caughtDigest) {
            throw new AssertionError("Failed to detect tampered digest with SOURCE_FREE_CONTRACT_MISMATCH");
        }

        // 2. Tamper with payload content without updating digest
        String tamperedPayload = text.replace("export fn foo () -> int", "export fn hacked () -> int");
        boolean caughtPayload = false;
        try {
            CheckedBodyCodec.verifySourceFree(tamperedPayload);
        } catch (CompileError e) {
            caughtPayload = true;
        }
        if (!caughtPayload) {
            throw new AssertionError("Failed to detect tampered payload");
        }

        // 3. Malformed module ID
        MossiInterface badId = new MossiInterface("malformed_id");
        boolean caughtBadId = false;
        try {
            CheckedBodyCodec.encodeMossi(badId);
        } catch (CompileError e) {
            if ("SOURCE_FREE_CONTRACT_INVALID".equals(e.getCode())) {
                caughtBadId = true;
            }
        }
        if (!caughtBadId) {
            throw new AssertionError("Failed to reject malformed module ID during encode");
        }
    }
}
