package org.mosslang.compiler.abi;

import org.mosslang.compiler.types.TypeRef;
import java.util.List;

public class CheckedBodyCodecTest {
    public static void main(String[] args) {
        System.out.println("Running CheckedBodyCodecTest...");
        testSimpleReturnEncode();
        testArithmeticHashDeterminism();
        System.out.println("CheckedBodyCodecTest PASSED.");
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
}
