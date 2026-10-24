package org.mosslang.compiler.types;

import java.util.List;

public class TypeSolverTest {
    public static void main(String[] args) {
        System.out.println("Running TypeSolverTest...");
        testPrimitives();
        testVariableBinding();
        testTransitiveUnification();
        testOccursCheck();
        testTransactionalRollback();
        testCanonicalParser();
        System.out.println("TypeSolverTest PASSED.");
    }

    private static void testPrimitives() {
        TypeSolver solver = new TypeSolver(1);
        TypeRef intType = TypeRef.primitive("int");
        TypeRef floatType = TypeRef.primitive("float");

        if (!solver.unify(intType, intType)) {
            throw new AssertionError("int should unify with int");
        }
        if (solver.unify(intType, floatType)) {
            throw new AssertionError("int should not unify with float");
        }
    }

    private static void testVariableBinding() {
        TypeSolver solver = new TypeSolver(1);
        TypeRef v1 = solver.fresh(10);
        TypeRef intType = TypeRef.primitive("int");

        if (!solver.unify(v1, intType)) {
            throw new AssertionError("v1 should unify with int");
        }
        TypeRef resolved = solver.resolve(v1);
        if (!resolved.equals(intType)) {
            throw new AssertionError("resolved v1 should be int, got " + resolved);
        }
    }

    private static void testTransitiveUnification() {
        TypeSolver solver = new TypeSolver(1);
        TypeRef v1 = solver.fresh(10);
        TypeRef v2 = solver.fresh(10);
        TypeRef stringType = TypeRef.primitive("string");

        if (!solver.unify(v1, v2)) {
            throw new AssertionError("v1 should unify with v2");
        }
        if (!solver.unify(v2, stringType)) {
            throw new AssertionError("v2 should unify with string");
        }
        if (!solver.resolve(v1).equals(stringType)) {
            throw new AssertionError("v1 should transitively resolve to string");
        }
        if (!solver.resolve(v2).equals(stringType)) {
            throw new AssertionError("v2 should transitively resolve to string");
        }
    }

    private static void testOccursCheck() {
        TypeSolver solver = new TypeSolver(1);
        TypeRef v1 = solver.fresh(10);
        TypeRef vecOfV1 = TypeRef.builtin("vector", List.of(v1));

        if (solver.unify(v1, vecOfV1)) {
            throw new AssertionError("Occurs check should reject v1 = vector[v1]");
        }
        // Ensure v1 was not bound
        if (solver.resolve(v1).kind() != TypeKind.InferenceVar) {
            throw new AssertionError("v1 should remain unbound after failed occurs check");
        }
    }

    private static void testTransactionalRollback() {
        TypeSolver solver = new TypeSolver(1);
        TypeRef t = solver.fresh(10);
        TypeRef u = solver.fresh(10);
        TypeRef intType = TypeRef.primitive("int");
        TypeRef floatType = TypeRef.primitive("float");
        TypeRef stringType = TypeRef.primitive("string");

        // Transaction: t = int, u = float, t = string (contradiction on 3rd equality)
        List<TypeSolver.Equality> equalities = List.of(
            new TypeSolver.Equality(t, intType),
            new TypeSolver.Equality(u, floatType),
            new TypeSolver.Equality(t, stringType)
        );

        boolean ok = solver.unifyAll(equalities);
        if (ok) {
            throw new AssertionError("unifyAll should fail due to contradiction");
        }

        // Verify total rollback: both t and u must be completely restored as unbound inference variables!
        TypeRef resolvedT = solver.resolve(t);
        TypeRef resolvedU = solver.resolve(u);
        if (resolvedT.kind() != TypeKind.InferenceVar) {
            throw new AssertionError("t should be rolled back to inference var, got: " + resolvedT);
        }
        if (resolvedU.kind() != TypeKind.InferenceVar) {
            throw new AssertionError("u should be rolled back to inference var, got: " + resolvedU);
        }

        // Subsequent inference must succeed cleanly
        if (!solver.unify(t, stringType)) {
            throw new AssertionError("t should now successfully unify with string");
        }
        if (!solver.resolve(t).equals(stringType)) {
            throw new AssertionError("t should resolve to string");
        }
    }

    private static void testCanonicalParser() {
        TypeRef parsed1 = TypeParser.parseCanonical("int");
        if (!parsed1.equals(TypeRef.primitive("int"))) {
            throw new AssertionError("expected int, got " + parsed1);
        }
        TypeRef parsedVec = TypeParser.parseCanonical("vector[string]");
        if (parsedVec.kind() != TypeKind.Builtin || !parsedVec.name().equals("vector") ||
            parsedVec.typeArgs().size() != 1 || !parsedVec.typeArgs().get(0).equals(TypeRef.primitive("string"))) {
            throw new AssertionError("expected vector[string], got " + parsedVec);
        }
        TypeRef parsedMap = TypeParser.parseCanonical("map[string, int]");
        if (parsedMap.kind() != TypeKind.Builtin || !parsedMap.name().equals("map") ||
            parsedMap.typeArgs().size() != 2) {
            throw new AssertionError("expected map[string, int], got " + parsedMap);
        }
    }
}
