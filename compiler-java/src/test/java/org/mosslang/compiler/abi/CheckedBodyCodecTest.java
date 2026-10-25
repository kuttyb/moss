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
        testMossiRejectsUnboundedCounts();
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
