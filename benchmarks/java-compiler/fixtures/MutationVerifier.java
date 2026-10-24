package org.mosslang.compiler.fixtures;

import java.lang.reflect.Method;
import java.util.Arrays;

/**
 * Deterministic Mutation Verifier (Phase 25.3 J0-R1)
 * Validates actual behavioral changes and absence of stale signatures in compiled bytecode.
 * Uses reflection so it loads bytecode strictly from the active classpath without invoking javac.
 */
public class MutationVerifier {
    public static void main(String[] args) {
        if (args.length < 2) {
            System.err.println("Usage: MutationVerifier <scenario> <trial_index>");
            System.exit(1);
        }

        String scenario = args[0].toUpperCase();
        int trial = Integer.parseInt(args[1]);

        try {
            switch (scenario) {
                case "B4" -> verifyB4(trial);
                case "B5" -> verifyB5(trial);
                case "B6" -> verifyB6(trial);
                case "B7" -> verifyB7(trial);
                case "B8" -> verifyB8(trial);
                case "B9" -> verifyB9(trial);
                case "B10" -> verifyB10(trial);
                case "DEP_INVALIDATION" -> verifyDepInvalidation(trial);
                default -> throw new IllegalArgumentException("Unknown scenario: " + scenario);
            }
            System.out.println("MUTATION_VERIFIED: " + scenario + " trial=" + trial);
        } catch (Throwable t) {
            System.err.println("MUTATION_VERIFICATION_FAILED: " + scenario + " trial=" + trial + " reason: " + t);
            t.printStackTrace(System.err);
            System.exit(2);
        }
    }

    private static void verifyB4(int trial) throws Exception {
        Class<?> cls = Class.forName("org.mosslang.compiler.types.TypeUnifier");
        Method m = cls.getMethod("getExecutionProfile");
        int res = (int) m.invoke(null);
        int expected = 1000 + (trial + 1);
        if (res != expected) {
            throw new AssertionError("B4 verification failed: expected " + expected + ", got " + res);
        }
    }

    private static void verifyB5(int trial) throws Exception {
        Class<?> cls = Class.forName("org.mosslang.compiler.types.TypeSolver");
        Object instance = cls.getDeclaredConstructor().newInstance();
        Method m = cls.getMethod("getConstraintPassCapacity");
        int res = (int) m.invoke(instance);
        int expected = 256 + (trial + 1);
        if (res != expected) {
            throw new AssertionError("B5 verification failed: expected " + expected + ", got " + res);
        }
    }

    private static void verifyB6(int trial) throws Exception {
        Class<?> cls = Class.forName("org.mosslang.compiler.symbols.SymbolTable");
        
        // Assert old single-argument signature is completely gone
        try {
            cls.getMethod("lookup", String.class);
            throw new AssertionError("B6 verification failed: old lookup(String) method still exists in SymbolTable.class!");
        } catch (NoSuchMethodException expected) {
            // Success: old signature was eliminated
        }

        // Assert new signature exists and works
        Method mNew = cls.getMethod("lookup", String.class, int.class);
        Object instance = cls.getDeclaredConstructor().newInstance();
        Object res = mNew.invoke(instance, "dummySymbol", 50 + (trial + 1));
        // Return can be null since symbol isn't declared, but method invocation must succeed without exception
    }

    private static void verifyB7(int trial) throws Exception {
        Class<?> trCls = Class.forName("org.mosslang.compiler.types.TypeRef");
        
        // Assert old no-arg typePrecedence is gone
        try {
            trCls.getMethod("typePrecedence");
            throw new AssertionError("B7 verification failed: old typePrecedence() still exists on TypeRef.class!");
        } catch (NoSuchMethodException expected) {
            // Success
        }

        Method mNew = trCls.getMethod("typePrecedence", int.class);

        // Verify PrimitiveType implementor
        Class<?> primCls = Class.forName("org.mosslang.compiler.types.PrimitiveType");
        Object intType = primCls.getField("INT").get(null);
        int resPrim = (int) mNew.invoke(intType, 10);
        int expected = 10 + 10 + (trial + 1);
        if (resPrim != expected) {
            throw new AssertionError("B7 verification failed on PrimitiveType: expected " + expected + ", got " + resPrim);
        }

        // Verify SpecializedType implementor
        Class<?> specCls = Class.forName("org.mosslang.compiler.types.SpecializedType_001");
        Object specInst = specCls.getDeclaredConstructor(int.class, String.class, trCls).newInstance(901, "Spec1", intType);
        int resSpec = (int) mNew.invoke(specInst, 10);
        if (resSpec != expected) {
            throw new AssertionError("B7 verification failed on SpecializedType_001: expected " + expected + ", got " + resSpec);
        }
    }

    private static void verifyB8(int trial) throws Exception {
        Class<?> cls = Class.forName("org.mosslang.compiler.semantics.SemanticChecker");
        Class<?> diagCls = Class.forName("org.mosslang.compiler.driver.DiagnosticSink");
        Class<?> solverCls = Class.forName("org.mosslang.compiler.types.TypeSolver");
        Class<?> srcCls = Class.forName("org.mosslang.compiler.ast.SourceFile");

        Object solverInst = solverCls.getDeclaredConstructor().newInstance();
        Object checker = cls.getDeclaredConstructor(diagCls, solverCls).newInstance(null, solverInst);

        Class<?> spanCls = Class.forName("org.mosslang.compiler.driver.SourceSpan");
        Object noneSpan = spanCls.getField("NONE").get(null);
        Object emptyList = org.eclipse.collections.impl.list.mutable.FastList.newList();
        Object sf = srcCls.getDeclaredConstructor(String.class, org.eclipse.collections.api.list.MutableList.class, org.eclipse.collections.api.list.MutableList.class, spanCls)
                          .newInstance("test.moss", emptyList, emptyList, noneSpan);

        Method m = cls.getMethod("analyzeComplexityBudget", srcCls);
        int res = (int) m.invoke(checker, sf);
        int expected = 1000 + (trial + 1);
        if (res != expected) {
            throw new AssertionError("B8 verification failed: expected " + expected + ", got " + res);
        }
    }

    private static void verifyB9(int trial) throws Exception {
        Class<?> codecCls = Class.forName("org.mosslang.compiler.abi.CheckedBodyCodec");
        Method m = codecCls.getMethod("getEpoch");
        int res = (int) m.invoke(null);
        int expected = 100 + (trial + 1);
        if (res != expected) {
            throw new AssertionError("B9 verification failed: expected epoch " + expected + ", got " + res);
        }
    }

    private static void verifyB10(int trial) throws Exception {
        Class<?> ledgerCls = Class.forName("org.mosslang.compiler.abi.OccurrenceLedger");
        Class<?> roleCls = Class.forName("org.mosslang.compiler.abi.OccurrenceRole");
        Object ledger = ledgerCls.getDeclaredConstructor().newInstance();

        Object readRole = roleCls.getField("READ").get(null);
        Method recM = ledgerCls.getMethod("record", roleCls, int.class, int.class);
        recM.invoke(ledger, readRole, 1, 1);
        recM.invoke(ledger, readRole, 2, 2);

        Method sumM = ledgerCls.getMethod("computePrimitiveChecksum");
        long res = (long) sumM.invoke(ledger);
        if (res == 0) {
            throw new AssertionError("B10 verification failed: expected non-zero primitive checksum");
        }
    }

    private static void verifyDepInvalidation(int trial) throws Exception {
        Class<?> trCls = Class.forName("org.mosslang.compiler.types.TypeRef");
        Class<?> solverCls = Class.forName("org.mosslang.compiler.types.TypeSolver");
        Class<?> primCls = Class.forName("org.mosslang.compiler.types.PrimitiveType");

        // Assert old no-arg tagValue is gone on TypeRef
        try {
            trCls.getMethod("tagValue");
            throw new AssertionError("DEP_INVALIDATION failed: old tagValue() still exists on TypeRef.class!");
        } catch (NoSuchMethodException expected) {
            // Success
        }

        Object solver = solverCls.getDeclaredConstructor().newInstance();
        Object intType = primCls.getField("INT").get(null);
        Method m = solverCls.getMethod("computeTag", trCls);
        long res = (long) m.invoke(solver, intType);
        long expected = 42L + 100;
        if (res != expected) {
            throw new AssertionError("DEP_INVALIDATION failed: expected " + expected + ", got " + res);
        }
    }
}
