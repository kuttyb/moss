package org.mosslang.compiler.semantics;

import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.diagnostics.CompileError;
import org.mosslang.compiler.types.*;

import java.util.*;

public class ExpressionTyper {
    private final Program program;
    private final TypeContext typeContext;
    private final TypeSolver solver;
    private final Map<String, Function> functions;
    private final Map<String, Domain> domains;
    private final Map<String, ObjectType> objects;
    private final Map<String, EnumType> enums;
    private final Map<String, Trait> traits;
    private final Deque<String> activeSpecializations = new ArrayDeque<>();

    public ExpressionTyper(Program program,
                           TypeContext typeContext,
                           TypeSolver solver,
                           Map<String, Function> functions,
                           Map<String, Domain> domains,
                           Map<String, ObjectType> objects,
                           Map<String, EnumType> enums,
                           Map<String, Trait> traits) {
        this.program = program;
        this.typeContext = typeContext;
        this.solver = solver;
        this.functions = functions != null ? functions : Collections.emptyMap();
        this.domains = domains != null ? domains : Collections.emptyMap();
        this.objects = objects != null ? objects : Collections.emptyMap();
        this.enums = enums != null ? enums : Collections.emptyMap();
        this.traits = traits != null ? traits : Collections.emptyMap();
    }

    public record BinarySplit(String left, String op, String right) {}

    public TypeRef infer(String expression, int line, TypeEnv env) {
        if (expression == null) {
            return TypeRef.unit();
        }
        String e = stripRedundantParentheses(expression.trim());
        if (e.isEmpty()) {
            return TypeRef.unit();
        }

        // 1. String and Char literals
        if (e.startsWith("\"") && e.endsWith("\"") && e.length() >= 2) {
            return TypeRef.primitive("string");
        }
        if (e.startsWith("'") && e.endsWith("'") && e.length() >= 2) {
            return TypeRef.primitive("char");
        }

        // 2. Boolean literals
        if (e.equals("true") || e.equals("false")) {
            return TypeRef.primitive("bool");
        }

        // 3. Option None
        if (e.equals("None")) {
            return TypeRef.builtin("option", List.of(solver.fresh(0)));
        }

        // 4. Numeric literals
        if (e.matches("^[+-]?\\d+$")) {
            return TypeRef.primitive("int");
        }
        if (e.matches("^[+-]?(\\d+\\.\\d+|\\d+[eE][+-]?\\d+|\\d+\\.\\d+[eE][+-]?\\d+)$")) {
            return TypeRef.primitive("float");
        }

        // 5. Functional pipeline: source |> stage1 |> stage2
        BinarySplit pipe = splitBinary(e, List.of("|>"));
        if (pipe != null) {
            return inferPipeline(pipe.left, pipe.right, line, env);
        }

        // 6. Logical OR, AND, XOR
        BinarySplit logical = splitBinary(e, List.of(" or ", " and ", " xor "));
        if (logical != null) {
            TypeRef left = infer(logical.left, line, env);
            TypeRef right = infer(logical.right, line, env);
            TypeRef boolType = TypeRef.primitive("bool");
            if (!solver.unify(left, boolType) || !solver.unify(right, boolType)) {
                throw new CompileError(line, "operator '" + logical.op.trim() + "' requires bool operands, got '" +
                        left.toCanonicalString() + "' and '" + right.toCanonicalString() + "'", "TYPE_MISMATCH");
            }
            return boolType;
        }

        // 7. Logical NOT
        if (e.startsWith("not ") || e.equals("not")) {
            String operand = e.substring(3).trim();
            TypeRef opType = infer(operand, line, env);
            TypeRef boolType = TypeRef.primitive("bool");
            if (!solver.unify(opType, boolType)) {
                throw new CompileError(line, "operator 'not' requires bool operand, got '" +
                        opType.toCanonicalString() + "'", "TYPE_MISMATCH");
            }
            return boolType;
        }

        // 8. Comparisons (==, !=, <=, >=, <, >)
        BinarySplit comp = splitBinary(e, List.of("==", "!=", "<=", ">=", "<", ">"));
        if (comp != null) {
            TypeRef left = infer(comp.left, line, env);
            TypeRef right = infer(comp.right, line, env);
            if (!solver.unify(left, right)) {
                throw new CompileError(line, "comparison '" + comp.op + "' between incompatible types '" +
                        left.toCanonicalString() + "' and '" + right.toCanonicalString() + "'", "TYPE_MISMATCH");
            }
            return TypeRef.primitive("bool");
        }

        // 9. Bitwise operators (|, ^, &, <<, >>)
        BinarySplit bitwise = splitBinary(e, List.of("<<", ">>", "|", "^", "&"));
        if (bitwise != null) {
            TypeRef left = infer(bitwise.left, line, env);
            TypeRef right = infer(bitwise.right, line, env);
            TypeRef intType = TypeRef.primitive("int");
            if (!solver.unify(left, intType) || !solver.unify(right, intType)) {
                throw new CompileError(line, "bitwise operator '" + bitwise.op + "' requires int operands", "TYPE_MISMATCH");
            }
            return intType;
        }

        // 10. Bitwise NOT (~)
        if (e.startsWith("~")) {
            TypeRef opType = infer(e.substring(1).trim(), line, env);
            TypeRef intType = TypeRef.primitive("int");
            if (!solver.unify(opType, intType)) {
                throw new CompileError(line, "bitwise not '~' requires int operand", "TYPE_MISMATCH");
            }
            return intType;
        }

        // 11. Arithmetic (+, -, *, /, %)
        BinarySplit arith = splitBinary(e, List.of("+", "-"));
        if (arith == null) {
            arith = splitBinary(e, List.of("*", "/", "%"));
        }
        if (arith != null) {
            TypeRef left = infer(arith.left, line, env);
            TypeRef right = infer(arith.right, line, env);
            String op = arith.op;

            // String concatenation with '+'
            if (op.equals("+")) {
                TypeRef stringType = TypeRef.primitive("string");
                if (left.sameHead(stringType) && right.sameHead(stringType)) {
                    return stringType;
                }
            }

            // Modulo requires integers
            if (op.equals("%")) {
                TypeRef intType = TypeRef.primitive("int");
                if (!solver.unify(left, intType) || !solver.unify(right, intType)) {
                    throw new CompileError(line, "operator '%' requires int operands, got '" +
                            left.toCanonicalString() + "' and '" + right.toCanonicalString() + "'", "TYPE_MISMATCH");
                }
                return intType;
            }

            // Numeric arithmetic
            TypeRef intType = TypeRef.primitive("int");
            TypeRef floatType = TypeRef.primitive("float");

            // If operand is an unconstrained inference variable, arithmetic constrains it to numeric (int)
            if (left.kind() == TypeKind.InferenceVar) {
                solver.unify(left, intType);
                left = solver.resolve(left);
            }
            if (right.kind() == TypeKind.InferenceVar) {
                solver.unify(right, intType);
                right = solver.resolve(right);
            }

            boolean leftInt = left.sameHead(intType);
            boolean leftFloat = left.sameHead(floatType);
            boolean rightInt = right.sameHead(intType);
            boolean rightFloat = right.sameHead(floatType);

            if ((leftInt || leftFloat) && (rightInt || rightFloat)) {
                if (leftFloat || rightFloat) {
                    return floatType;
                }
                return intType;
            }

            // Try unification if one is inference var
            if (solver.unify(left, right)) {
                TypeRef resolved = solver.resolve(left);
                if (resolved.sameHead(intType) || resolved.sameHead(floatType)) {
                    return resolved;
                }
            }

            throw new CompileError(line, "operator '" + op + "' is not defined for '" +
                    left.toCanonicalString() + "' and '" + right.toCanonicalString() + "'", "TYPE_MISMATCH");
        }

        // 11b. Unary + and -
        if (e.startsWith("-") || e.startsWith("+")) {
            String operand = e.substring(1).trim();
            TypeRef opType = solver.resolve(infer(operand, line, env));
            TypeRef intType = TypeRef.primitive("int");
            TypeRef floatType = TypeRef.primitive("float");
            if (opType.kind() == TypeKind.InferenceVar) {
                solver.unify(opType, intType);
                return intType;
            }
            if (opType.sameHead(intType) || opType.sameHead(floatType)) {
                return opType;
            }
            throw new CompileError(line, "unary operator '" + e.charAt(0) + "' requires numeric operand, got '" +
                    opType.toCanonicalString() + "'", "TYPE_MISMATCH");
        }

        // 12. Domain message expression: message receiver.Handler(args...)
        if (e.startsWith("message ")) {
            return inferMessageCall(e.substring(8).trim(), line, env);
        }

        // 13. Vector literals: [item1, item2, ...]
        if (e.startsWith("[") && e.endsWith("]")) {
            return inferVectorLiteral(e, line, env);
        }

        // 14. Index access: expr[idx]
        if (e.endsWith("]") && !e.startsWith("[")) {
            int openBracket = findMatchingOpenBracket(e, e.length() - 1);
            if (openBracket > 0) {
                String baseExpr = e.substring(0, openBracket).trim();
                String indexExpr = e.substring(openBracket + 1, e.length() - 1).trim();
                TypeRef baseType = solver.resolve(infer(baseExpr, line, env));
                TypeRef indexType = infer(indexExpr, line, env);

                if (baseType.kind() == TypeKind.Builtin && "vector".equals(baseType.name())) {
                    if (!solver.unify(indexType, TypeRef.primitive("int"))) {
                        throw new CompileError(line, "vector index must be int", "TYPE_MISMATCH");
                    }
                    return baseType.typeArgs().isEmpty() ? TypeRef.primitive("int") : baseType.typeArgs().get(0);
                }
                if (baseType.kind() == TypeKind.Builtin && "map".equals(baseType.name())) {
                    if (baseType.typeArgs().size() >= 2) {
                        TypeRef keyType = baseType.typeArgs().get(0);
                        TypeRef valType = baseType.typeArgs().get(1);
                        if (!solver.unify(indexType, keyType)) {
                            throw new CompileError(line, "map key type mismatch", "TYPE_MISMATCH");
                        }
                        return valType;
                    }
                    return solver.fresh(0);
                }
                if (baseType.sameHead(TypeRef.primitive("string"))) {
                    if (!solver.unify(indexType, TypeRef.primitive("int"))) {
                        throw new CompileError(line, "string index must be int", "TYPE_MISMATCH");
                    }
                    return TypeRef.primitive("char");
                }
                // If baseType is open inference var, constrain to vector
                if (baseType.kind() == TypeKind.InferenceVar) {
                    TypeRef elem = solver.fresh(0);
                    solver.unify(baseType, TypeRef.builtin("vector", List.of(elem)));
                    solver.unify(indexType, TypeRef.primitive("int"));
                    return elem;
                }
                throw new CompileError(line, "cannot index into non-collection type '" + baseType.toCanonicalString() + "'", "TYPE_MISMATCH");
            }
        }

        // 15. Function / Object / Domain / Enum constructor or call: callee(args...)
        if (e.endsWith(")")) {
            int openParen = findMatchingOpenParen(e, e.length() - 1);
            if (openParen > 0) {
                String callee = e.substring(0, openParen).trim();
                String argsString = e.substring(openParen + 1, e.length() - 1).trim();
                List<String> argExprs = parseArguments(argsString);

                // Method or member call: target.member(args...)
                int dot = callee.lastIndexOf('.');
                if (dot > 0 && !callee.startsWith("\"")) {
                    String target = callee.substring(0, dot).trim();
                    String member = callee.substring(dot + 1).trim();
                    if (enums.containsKey(target)) {
                        return TypeRef.enumeration(target);
                    }
                    if (program != null && program.imports.anySatisfy(imp -> imp.name.equals(target))) {
                        for (String arg : argExprs) {
                            if (arg.contains(":")) {
                                arg = arg.substring(arg.indexOf(':') + 1).trim();
                            }
                            infer(arg, line, env);
                        }
                        if (!member.isEmpty() && Character.isUpperCase(member.charAt(0))) {
                            return TypeRef.domain(target + "." + member);
                        }
                        return TypeRef.primitive("int");
                    }
                    return inferMethodCall(target, member, argExprs, line, env);
                }

                // Builtin functions and collections constructors
                if ("sqrt".equals(callee)) {
                    for (String arg : argExprs) {
                        infer(arg, line, env);
                    }
                    return TypeRef.primitive("float");
                }
                if ("Executor".equals(callee)) {
                    return TypeRef.primitive("Executor");
                }
                if ("assert".equals(callee) || "assertEqual".equals(callee)) {
                    for (String arg : argExprs) {
                        infer(arg, line, env);
                    }
                    return TypeRef.unit();
                }
                if ("range".equals(callee)) {
                    for (String arg : argExprs) {
                        TypeRef a = infer(arg, line, env);
                        solver.unify(a, TypeRef.primitive("int"));
                    }
                    return TypeRef.builtin("range", List.of(TypeRef.primitive("int")));
                }
                if (callee.startsWith("Vector[") && callee.endsWith("]")) {
                    String inner = callee.substring(7, callee.length() - 1).trim();
                    TypeRef elem = TypeParser.parseCanonical(inner);
                    return TypeRef.builtin("vector", List.of(elem != null ? elem : TypeRef.primitive(inner.toLowerCase())));
                }
                if (callee.startsWith("Queue[") && callee.endsWith("]")) {
                    String inner = callee.substring(6, callee.length() - 1).trim();
                    TypeRef elem = TypeParser.parseCanonical(inner);
                    return TypeRef.builtin("queue", List.of(elem != null ? elem : TypeRef.primitive(inner.toLowerCase())));
                }
                if ("Vector".equals(callee) || "vector".equals(callee)) {
                    TypeRef elem = solver.fresh(0);
                    return TypeRef.builtin("vector", List.of(elem));
                }
                if ("Map".equals(callee) || "map".equals(callee)) {
                    TypeRef k = solver.fresh(0);
                    TypeRef v = solver.fresh(0);
                    return TypeRef.builtin("map", List.of(k, v));
                }
                if ("Queue".equals(callee) || "queue".equals(callee)) {
                    TypeRef elem = solver.fresh(0);
                    return TypeRef.builtin("queue", List.of(elem));
                }
                if ("Set".equals(callee) || "set".equals(callee)) {
                    TypeRef elem = solver.fresh(0);
                    return TypeRef.builtin("set", List.of(elem));
                }
                if ("Range".equals(callee) || "RangeBatch".equals(callee)) {
                    return TypeRef.primitive(callee);
                }

                // Check Object constructor
                if (objects.containsKey(callee)) {
                    ObjectType obj = objects.get(callee);
                    List<TypeRef.FieldSub> subs = checkConstructorArgs(callee, obj.fields, argExprs, line, env);
                    NominalDeclaration decl = typeContext != null ? typeContext.find(callee) : null;
                    return TypeRef.nominalBound(decl, callee, subs);
                }

                // Check Domain constructor
                if (domains.containsKey(callee)) {
                    return TypeRef.domain(callee);
                }

                // Check Enum case call: Enum.Case(args...)
                if (enums.containsKey(callee)) {
                    return TypeRef.enumeration(callee);
                }

                // Check Function call
                if (functions.containsKey(callee)) {
                    Function fn = functions.get(callee);
                    if (argExprs.size() != fn.params.size()) {
                        throw new CompileError(line, "function " + callee + " expects " +
                                fn.params.size() + " arguments, got " + argExprs.size(), "TYPE_MISMATCH");
                    }
                    List<TypeRef> actualArgs = new ArrayList<>();
                    boolean hasGenericParam = false;
                    for (int i = 0; i < argExprs.size(); i++) {
                        TypeRef actual = infer(argExprs.get(i), line, env);
                        actualArgs.add(actual);
                        Param param = fn.params.get(i);
                        TypeRef expected = param.typeRef != null ? param.typeRef : TypeParser.parseCanonical(param.type);
                        if (param.type == null || param.type.isEmpty() || traits.containsKey(param.type)
                                || "Vector".equals(param.type) || "Map".equals(param.type)
                                || (expected != null && expected.isBareTemplate())) {
                            hasGenericParam = true;
                        }
                        if (expected != null && !expected.isBareTemplate() && !solver.unify(actual, expected)) {
                            throw new CompileError(line, "argument " + (i + 1) + " to function '" +
                                    callee + "' has incompatible type '" + actual.toCanonicalString() +
                                    "', expected '" + expected.toCanonicalString() + "'", "TYPE_MISMATCH");
                        } else if (expected != null && expected.isBareTemplate() && !actual.sameHead(expected)) {
                            throw new CompileError(line, "argument " + (i + 1) + " to function '" +
                                    callee + "' has incompatible type '" + actual.toCanonicalString() +
                                    "', expected '" + expected.toCanonicalString() + "'", "TYPE_MISMATCH");
                        }
                    }

                    boolean returnsOpenResult = (fn.returnType == null || fn.returnType.isEmpty())
                            && (fn.returnTypeRef == null || !fn.returnTypeRef.isClosed());

                    // For generic or open-result functions, specialize the return type
                    if ((hasGenericParam || returnsOpenResult) && (!fn.body.isEmpty() || (fn.resultExpression != null && !fn.resultExpression.isEmpty()))) {
                        List<String> argKeyParts = new ArrayList<>();
                        for (TypeRef a : actualArgs) {
                            argKeyParts.add(solver.resolve(a).toCanonicalString());
                        }
                        String specKey = callee + "<" + String.join(",", argKeyParts) + ">";
                        for (String active : activeSpecializations) {
                            if (active.startsWith(callee + "<")) {
                                throw new CompileError(line,
                                        "recursive generic specialization cycle on '" + callee + "': " + active + " -> " + specKey,
                                        "TYPE_RECURSIVE_SPECIALIZATION");
                            }
                        }
                        activeSpecializations.push(specKey);
                        try {
                            TypeEnv callEnv = new TypeEnv();
                            for (int i = 0; i < fn.params.size(); i++) {
                                callEnv.put(fn.params.get(i).name, actualArgs.get(i));
                            }
                            for (Stmt s : fn.body) {
                                if (s.kind == Stmt.Kind.Return) {
                                    String retExpr = s.a != null && !s.a.isEmpty() ? s.a.trim() : s.text.trim();
                                    if (retExpr.startsWith("return ")) {
                                        retExpr = retExpr.substring(7).trim();
                                    }
                                    if (!retExpr.isEmpty()) {
                                        return infer(retExpr, s.line, callEnv);
                                    }
                                } else if (s.kind == Stmt.Kind.Assign || s.kind == Stmt.Kind.Let || s.kind == Stmt.Kind.Var) {
                                    String vname = s.a != null ? s.a.trim() : "";
                                    String ve = s.b != null ? s.b.trim() : "";
                                    if (!vname.isEmpty() && !ve.isEmpty()) {
                                        if (!vname.contains("[")) {
                                            TypeRef vt = infer(ve, s.line, callEnv);
                                            callEnv.put(vname, vt);
                                        } else {
                                            infer(ve, s.line, callEnv);
                                        }
                                    }
                                } else if (s.kind == Stmt.Kind.Call && s.text != null) {
                                    infer(s.text.trim(), s.line, callEnv);
                                }
                            }
                            if (fn.resultExpression != null && !fn.resultExpression.isEmpty()) {
                                return infer(fn.resultExpression, fn.resultLine != 0 ? fn.resultLine : line, callEnv);
                            }
                        } finally {
                            activeSpecializations.pop();
                        }
                    }

                    if (fn.returnTypeRef != null && fn.returnTypeRef.isClosed()) {
                        return fn.returnTypeRef;
                    }
                    if (fn.returnType != null && !fn.returnType.isEmpty()) {
                        TypeRef parsed = TypeParser.parseCanonical(fn.returnType);
                        return parsed != null ? parsed : TypeRef.primitive(fn.returnType);
                    }
                    return solver.fresh(0);
                }

                // Builtin helpers: len, size, println, print, echo
                if ("len".equals(callee) || "size".equals(callee)) {
                    if (argExprs.size() != 1) {
                        throw new CompileError(line, "'" + callee + "' expects 1 argument", "ARGUMENT_COUNT_MISMATCH");
                    }
                    infer(argExprs.get(0), line, env);
                    return TypeRef.primitive("int");
                }
                if ("println".equals(callee) || "print".equals(callee) || "echo".equals(callee)) {
                    for (String arg : argExprs) {
                        infer(arg, line, env);
                    }
                    return TypeRef.unit();
                }

                if (callee.contains(".")) {
                    String[] parts = callee.split("\\.", 2);
                    if (program != null && program.imports.anySatisfy(imp -> imp.name.equals(parts[0]))) {
                        for (String arg : argExprs) {
                            if (arg.contains(":")) {
                                arg = arg.substring(arg.indexOf(':') + 1).trim();
                            }
                            infer(arg, line, env);
                        }
                        return TypeRef.primitive("int");
                    }
                }

                throw new CompileError(line, "unknown function or constructor '" + callee + "'", "UNKNOWN_SYMBOL_OR_TYPE");
            }
        }

        // 16. Member access without call: target.member (e.g. enum case or field read)
        int dot = e.indexOf('.');
        if (dot > 0 && !e.startsWith("\"")) {
            String enumTarget = e.substring(0, dot).trim();
            if (enums.containsKey(enumTarget)) {
                return TypeRef.enumeration(enumTarget);
            }
            int lastDot = e.lastIndexOf('.');
            String base = e.substring(0, lastDot).trim();
            String member = e.substring(lastDot + 1).trim();
            TypeRef targetType = solver.resolve(infer(base, line, env));
            if (targetType != null) {
                if (objects.containsKey(targetType.name())) {
                    ObjectType obj = objects.get(targetType.name());
                    for (Field f : obj.fields) {
                        if (f.name.equals(member)) {
                            for (TypeRef.FieldSub sub : targetType.fieldSubstitutions()) {
                                if (sub.name().equals(member)) {
                                    return solver.resolve(sub.type());
                                }
                            }
                            TypeRef fieldType = (f.type != null && !f.type.isEmpty())
                                    ? TypeParser.parseCanonical(f.type)
                                    : solver.fresh(0);
                            return fieldType != null ? fieldType : solver.fresh(0);
                        }
                    }
                    throw new CompileError(line, "type '" + targetType.name() + "' has no field '" + member + "'", "UNKNOWN_SYMBOL_OR_TYPE");
                }
                if (targetType.kind() == TypeKind.Primitive || targetType.kind() == TypeKind.InferenceVar) {
                    throw new CompileError(line, "cannot access field '" + member + "' on unconstrained receiver '" + base + "'", "UNTYPED_FIELD_ACCESS");
                }
            }
        }

        // 17. Identifiers in scope
        if (env.containsKey(e)) {
            TypeRef bound = env.get(e);
            return bound != null ? solver.resolve(bound) : TypeRef.primitive("int");
        }

        // 18. Declarations used as identifiers
        if (objects.containsKey(e)) {
            NominalDeclaration decl = typeContext != null ? typeContext.find(e) : null;
            return TypeRef.nominalBound(decl, e, Collections.emptyList());
        }
        if (domains.containsKey(e)) {
            return TypeRef.domain(e);
        }
        if (enums.containsKey(e)) {
            return TypeRef.enumeration(e);
        }
        if (functions.containsKey(e)) {
            Function fn = functions.get(e);
            return TypeRef.builtin("callable", Collections.emptyList());
        }
        if ("FileIO".equals(e) || "ro".equals(e) || "rw".equals(e) || "_".equals(e)) {
            return TypeRef.primitive(e);
        }

        // Fail closed with UNKNOWN_SYMBOL_OR_TYPE instead of silent fallback
        if (e.matches("^[a-zA-Z_][a-zA-Z0-9_]*$")) {
            throw new CompileError(line, "unknown identifier '" + e + "'", "UNKNOWN_SYMBOL_OR_TYPE");
        }
        throw new CompileError(line, "unknown symbol or unresolved expression '" + e + "'", "UNKNOWN_SYMBOL_OR_TYPE");
    }

    private TypeRef inferPipeline(String leftExpr, String stageExpr, int line, TypeEnv env) {
        TypeRef leftType = solver.resolve(infer(leftExpr, line, env));
        stageExpr = stageExpr.trim();

        TypeRef elemType = (leftType.kind() == TypeKind.Builtin && !leftType.typeArgs().isEmpty())
                ? leftType.typeArgs().get(0)
                : solver.fresh(0);

        if (stageExpr.equals("sum")) {
            return elemType;
        }
        if (stageExpr.equals("count")) {
            return TypeRef.primitive("int");
        }
        if (stageExpr.equals("any") || stageExpr.equals("all") ||
            stageExpr.startsWith("any(") || stageExpr.startsWith("all(")) {
            return TypeRef.primitive("bool");
        }
        if (stageExpr.startsWith("map(") && stageExpr.endsWith(")")) {
            String inner = stageExpr.substring(4, stageExpr.length() - 1).trim();
            if (functions.containsKey(inner)) {
                Function fn = functions.get(inner);
                TypeRef ret = fn.returnTypeRef != null ? fn.returnTypeRef :
                        (fn.returnType != null ? TypeParser.parseCanonical(fn.returnType) : solver.fresh(0));
                return TypeRef.builtin("vector", List.of(ret));
            }
            return TypeRef.builtin("vector", List.of(solver.fresh(0)));
        }
        if (stageExpr.startsWith("filter(") && stageExpr.endsWith(")")) {
            return TypeRef.builtin("vector", List.of(elemType));
        }
        if (stageExpr.startsWith("reduce(") && stageExpr.endsWith(")")) {
            String argsStr = stageExpr.substring(7, stageExpr.length() - 1).trim();
            List<String> args = parseArguments(argsStr);
            if (!args.isEmpty()) {
                return infer(args.get(0), line, env);
            }
            return elemType;
        }

        // Pipe operator x |> f(args...) -> f(x, args...)
        int openParen = stageExpr.indexOf('(');
        if (openParen > 0 && stageExpr.endsWith(")")) {
            String callee = stageExpr.substring(0, openParen).trim();
            String argsStr = stageExpr.substring(openParen + 1, stageExpr.length() - 1).trim();
            String transformedCall = argsStr.isEmpty() ? (callee + "(" + leftExpr + ")") : (callee + "(" + leftExpr + ", " + argsStr + ")");
            return infer(transformedCall, line, env);
        }
        if (functions.containsKey(stageExpr)) {
            return infer(stageExpr + "(" + leftExpr + ")", line, env);
        }

        return TypeRef.builtin("vector", List.of(elemType));
    }

    public TypeRef inferMessageCall(String callExpr, int line, TypeEnv env) {
        int dot = callExpr.indexOf('.');
        int openParen = callExpr.indexOf('(');
        if (dot <= 0 || openParen <= dot || !callExpr.endsWith(")")) {
            throw new CompileError(line, "malformed message statement", "PARSE_ERROR");
        }
        String receiverName = callExpr.substring(0, dot).trim();
        String handlerName = callExpr.substring(dot + 1, openParen).trim();
        String argsString = callExpr.substring(openParen + 1, callExpr.length() - 1).trim();
        List<String> argExprs = parseArguments(argsString);

        if (!env.containsKey(receiverName)) {
            throw new CompileError(line, "unknown domain receiver '" + receiverName + "'", "UNKNOWN_SYMBOL_OR_TYPE");
        }
        TypeRef receiverType = solver.resolve(env.get(receiverName));
        if (receiverType == null || receiverType.kind() != TypeKind.Domain) {
            throw new CompileError(line, "'" + receiverName + "' is not a domain", "TYPE_MISMATCH");
        }
        Domain domain = domains.get(receiverType.name());
        if (domain == null) {
            // Imported domain
            return TypeRef.primitive("int");
        }
        Handler target = null;
        for (Handler h : domain.handlers) {
            if (h.name.equals(handlerName)) {
                target = h;
                break;
            }
        }
        if (target == null) {
            throw new CompileError(line, "domain '" + domain.name + "' has no handler '" + handlerName + "'", "UNKNOWN_SYMBOL_OR_TYPE");
        }
        if (argExprs.size() != target.params.size()) {
            throw new CompileError(line, "handler '" + handlerName + "' expects " +
                    target.params.size() + " arguments, got " + argExprs.size(), "ARGUMENT_COUNT_MISMATCH");
        }
        for (int i = 0; i < argExprs.size(); i++) {
            TypeRef actual = infer(argExprs.get(i), line, env);
            Param param = target.params.get(i);
            TypeRef expected = param.typeRef != null ? param.typeRef : TypeParser.parseCanonical(param.type);
            if (expected != null && !solver.unify(actual, expected)) {
                throw new CompileError(line, "argument " + (i + 1) + " to handler '" +
                        handlerName + "' has incompatible type '" + actual.toCanonicalString() + "'", "TYPE_MISMATCH");
            }
        }
        if (target.replyTypeRef != null) {
            return target.replyTypeRef;
        }
        if (target.replyType != null && !target.replyType.isEmpty()) {
            TypeRef parsed = TypeParser.parseCanonical(target.replyType);
            return parsed != null ? parsed : TypeRef.primitive(target.replyType);
        }
        return TypeRef.unit();
    }

    private TypeRef inferVectorLiteral(String e, int line, TypeEnv env) {
        String content = e.substring(1, e.length() - 1).trim();
        if (content.isEmpty()) {
            TypeRef freshVar = solver.fresh(0);
            return TypeRef.builtin("vector", List.of(freshVar));
        }
        List<String> items = parseArguments(content);
        TypeRef elemType = null;
        for (int i = 0; i < items.size(); i++) {
            TypeRef t = infer(items.get(i), line, env);
            if (elemType == null) {
                elemType = t;
            } else {
                if (!solver.unify(elemType, t)) {
                    throw new CompileError(line, "vector elements must have homogeneous type; element " +
                            (i + 1) + " has type '" + t.toCanonicalString() + "', expected '" +
                            solver.resolve(elemType).toCanonicalString() + "'", "TYPE_MISMATCH");
                }
            }
        }
        return TypeRef.builtin("vector", List.of(solver.resolve(elemType)));
    }

    private TypeRef inferMethodCall(String target, String member, List<String> argExprs, int line, TypeEnv env) {
        TypeRef targetType = infer(target, line, env);
        targetType = solver.resolve(targetType);

        if (targetType.kind() == TypeKind.Builtin && "vector".equals(targetType.name())) {
            if ("len".equals(member) || "size".equals(member) || "count".equals(member)) {
                return TypeRef.primitive("int");
            }
            if ("push".equals(member) || "append".equals(member)) {
                if (argExprs.size() == 1 && !targetType.typeArgs().isEmpty()) {
                    TypeRef itemType = infer(argExprs.get(0), line, env);
                    if (!solver.unify(targetType.typeArgs().get(0), itemType)) {
                        throw new CompileError(line, "cannot push element of type '" +
                                itemType.toCanonicalString() + "' to vector of '" +
                                targetType.typeArgs().get(0).toCanonicalString() + "'", "TYPE_MISMATCH");
                    }
                }
                return TypeRef.unit();
            }
            if ("pop".equals(member)) {
                return targetType.typeArgs().isEmpty() ? TypeRef.primitive("int") : targetType.typeArgs().get(0);
            }
        }

        if (targetType.kind() == TypeKind.Builtin && "map".equals(targetType.name())) {
            if ("len".equals(member) || "size".equals(member) || "count".equals(member)) {
                return TypeRef.primitive("int");
            }
            if ("contains".equals(member) || "hasKey".equals(member)) {
                return TypeRef.primitive("bool");
            }
            if ("keys".equals(member)) {
                TypeRef k = targetType.typeArgs().isEmpty() ? solver.fresh(0) : targetType.typeArgs().get(0);
                return TypeRef.builtin("vector", List.of(k));
            }
            if ("values".equals(member)) {
                TypeRef v = targetType.typeArgs().size() < 2 ? solver.fresh(0) : targetType.typeArgs().get(1);
                return TypeRef.builtin("vector", List.of(v));
            }
            if ("delete".equals(member)) {
                for (String a : argExprs) infer(a, line, env);
                return targetType.typeArgs().size() < 2 ? solver.fresh(0) : targetType.typeArgs().get(1);
            }
            if ("get".equals(member)) {
                if (argExprs.size() != 2) {
                    throw new CompileError(line, "method 'get' on map expects 2 arguments (key, fallback), got " + argExprs.size(), "TYPE_MISMATCH");
                }
                TypeRef kArg = solver.resolve(infer(argExprs.get(0), line, env));
                TypeRef fbArg = solver.resolve(infer(argExprs.get(1), line, env));
                if (targetType.typeArgs().size() >= 1 && !solver.unify(targetType.typeArgs().get(0), kArg)) {
                    throw new CompileError(line, "map 'get' key type mismatch: expected '" +
                            solver.resolve(targetType.typeArgs().get(0)).toCanonicalString() + "', got '" +
                            kArg.toCanonicalString() + "'", "TYPE_MISMATCH");
                }
                if (targetType.typeArgs().size() >= 2 && !solver.unify(targetType.typeArgs().get(1), fbArg)) {
                    throw new CompileError(line, "map 'get' fallback type mismatch: expected '" +
                            solver.resolve(targetType.typeArgs().get(1)).toCanonicalString() + "', got '" +
                            fbArg.toCanonicalString() + "'", "TYPE_MISMATCH");
                }
                return targetType.typeArgs().size() >= 2 ? solver.resolve(targetType.typeArgs().get(1)) : fbArg;
            }
        }

        if (targetType.kind() == TypeKind.Builtin && "queue".equals(targetType.name())) {
            if ("push".equals(member)) {
                if (argExprs.size() == 1 && !targetType.typeArgs().isEmpty()) {
                    TypeRef itemType = infer(argExprs.get(0), line, env);
                    solver.unify(targetType.typeArgs().get(0), itemType);
                }
                return TypeRef.unit();
            }
            if ("pop".equals(member)) {
                return targetType.typeArgs().isEmpty() ? TypeRef.primitive("int") : targetType.typeArgs().get(0);
            }
        }

        if (targetType.sameHead(TypeRef.primitive("string"))) {
            if ("len".equals(member) || "size".equals(member) || "length".equals(member)) {
                if (!argExprs.isEmpty()) {
                    throw new CompileError(line, "method '" + member + "' on string expects 0 arguments, got " + argExprs.size(), "TYPE_MISMATCH");
                }
                return TypeRef.primitive("int");
            }
            if ("char_at".equals(member)) {
                if (argExprs.size() != 1) {
                    throw new CompileError(line, "method 'char_at' on string expects 1 argument, got " + argExprs.size(), "TYPE_MISMATCH");
                }
                TypeRef idxType = solver.resolve(infer(argExprs.get(0), line, env));
                if (!solver.unify(idxType, TypeRef.primitive("int"))) {
                    throw new CompileError(line, "method 'char_at' requires int argument, got '" + idxType.toCanonicalString() + "'", "TYPE_MISMATCH");
                }
                return TypeRef.primitive("string");
            }
            if ("split".equals(member)) {
                if (argExprs.size() != 1) {
                    throw new CompileError(line, "method 'split' on string expects 1 argument, got " + argExprs.size(), "TYPE_MISMATCH");
                }
                TypeRef sepType = solver.resolve(infer(argExprs.get(0), line, env));
                if (!solver.unify(sepType, TypeRef.primitive("string"))) {
                    throw new CompileError(line, "method 'split' requires string argument, got '" + sepType.toCanonicalString() + "'", "TYPE_MISMATCH");
                }
                return TypeRef.builtin("vector", List.of(TypeRef.primitive("string")));
            }
            if ("parse_int".equals(member)) {
                for (String a : argExprs) infer(a, line, env);
                return TypeRef.primitive("int");
            }
            if ("parse_float".equals(member)) {
                for (String a : argExprs) infer(a, line, env);
                return TypeRef.primitive("float");
            }
            if ("parse_bool".equals(member)) {
                for (String a : argExprs) infer(a, line, env);
                return TypeRef.primitive("bool");
            }
            if ("trim".equals(member) || "to_upper".equals(member) || "to_lower".equals(member) || "slice".equals(member)) {
                for (String a : argExprs) infer(a, line, env);
                return TypeRef.primitive("string");
            }
            if ("starts_with".equals(member) || "ends_with".equals(member) || "contains".equals(member)) {
                for (String a : argExprs) infer(a, line, env);
                return TypeRef.primitive("bool");
            }
            throw new CompileError(line, "type 'string' has no method '" + member + "'", "TYPE_MISMATCH");
        }

        if (traits.containsKey(targetType.name())) {
            Trait tr = traits.get(targetType.name());
            for (TraitMethod m : tr.methods) {
                if (m.name.equals(member)) {
                    TypeRef ret = m.returnTypeRef != null ? m.returnTypeRef :
                            (m.returnType != null && !m.returnType.isEmpty() ? TypeParser.parseCanonical(m.returnType) : TypeRef.unit());
                    return ret != null ? ret : TypeRef.unit();
                }
            }
        }

        if (objects.containsKey(targetType.name())) {
            ObjectType obj = objects.get(targetType.name());
            for (Method m : obj.methods) {
                if (m.name.equals(member)) {
                    if (m.returnTypeRef != null && m.returnTypeRef.isClosed()) {
                        return m.returnTypeRef;
                    }
                    if (m.returnType != null && !m.returnType.isEmpty()) {
                        TypeRef parsed = TypeParser.parseCanonical(m.returnType);
                        if (parsed != null) return parsed;
                    }
                    // Infer return type from method body
                    TypeEnv methodEnv = new TypeEnv();
                    for (Field f : obj.fields) {
                        TypeRef ft = (f.type != null && !f.type.isEmpty()) ? TypeParser.parseCanonical(f.type) : solver.fresh(0);
                        methodEnv.put(f.name, ft);
                    }
                    for (int i = 0; i < m.params.size() && i < argExprs.size(); i++) {
                        methodEnv.put(m.params.get(i).name, infer(argExprs.get(i), line, env));
                    }
                    for (Stmt s : m.body) {
                        if (s.kind == Stmt.Kind.Return) {
                            String retExpr = s.a != null && !s.a.isEmpty() ? s.a.trim() : s.text.trim();
                            if (retExpr.startsWith("return ")) retExpr = retExpr.substring(7).trim();
                            if (!retExpr.isEmpty()) {
                                return infer(retExpr, s.line, methodEnv);
                            }
                        }
                    }
                    return solver.fresh(0);
                }
            }
        }

        if (targetType.kind() == TypeKind.Domain || targetType.kind() == TypeKind.NominalInstance || targetType.kind() == TypeKind.InferenceVar
                || "FileIO".equals(target) || "Range".equalsIgnoreCase(targetType.name()) || "Executor".equals(targetType.name()) || "file".equals(target) || "executor".equals(target)) {
            for (String arg : argExprs) {
                if (!arg.equals("ro") && !arg.equals("rw") && !arg.equals("_")) {
                    infer(arg, line, env);
                }
            }
            if ("length".equals(member)) return TypeRef.primitive("int");
            if ("threads".equals(member) || "start".equals(member)) return TypeRef.primitive("Executor");
            if ("open".equals(member)) return TypeRef.primitive("FileIO");
            return solver.fresh(0);
        }

        return TypeRef.unit();
    }

    private List<TypeRef.FieldSub> checkConstructorArgs(String objectName, List<Field> fields, List<String> argExprs, int line, TypeEnv env) {
        if (argExprs.isEmpty() && fields.isEmpty()) {
            return Collections.emptyList();
        }
        List<TypeRef.FieldSub> subs = new ArrayList<>();
        boolean hasNamed = false;
        Map<String, String> namedArgs = new HashMap<>();
        for (String a : argExprs) {
            int sep = a.indexOf('=');
            if (sep <= 0) sep = a.indexOf(':');
            if (sep > 0 && !a.startsWith("\"")) {
                hasNamed = true;
                namedArgs.put(a.substring(0, sep).trim(), a.substring(sep + 1).trim());
            }
        }

        if (hasNamed) {
            for (Field f : fields) {
                if (namedArgs.containsKey(f.name)) {
                    TypeRef actual = solver.resolve(infer(namedArgs.get(f.name), line, env));
                    TypeRef expected = (f.type != null && !f.type.isEmpty()) ? TypeParser.parseCanonical(f.type) : null;
                    if (expected != null) {
                        if (!solver.unify(actual, expected)) {
                            throw new CompileError(line, "field '" + f.name + "' of type '" + objectName +
                                    "' expects '" + expected.toCanonicalString() + "', got '" + actual.toCanonicalString() + "'", "TYPE_MISMATCH");
                        }
                    } else {
                        subs.add(new TypeRef.FieldSub(f.name, actual));
                    }
                }
            }
        } else {
            if (argExprs.size() != fields.size()) {
                throw new CompileError(line, "constructor for '" + objectName + "' expects " +
                        fields.size() + " arguments, got " + argExprs.size(), "ARGUMENT_COUNT_MISMATCH");
            }
            for (int i = 0; i < argExprs.size(); i++) {
                TypeRef actual = solver.resolve(infer(argExprs.get(i), line, env));
                Field f = fields.get(i);
                TypeRef expected = (f.type != null && !f.type.isEmpty()) ? TypeParser.parseCanonical(f.type) : null;
                if (expected != null) {
                    if (!solver.unify(actual, expected)) {
                        throw new CompileError(line, "field '" + f.name + "' expects '" +
                                expected.toCanonicalString() + "', got '" + actual.toCanonicalString() + "'", "TYPE_MISMATCH");
                    }
                } else {
                    subs.add(new TypeRef.FieldSub(f.name, actual));
                }
            }
        }
        return subs;
    }

    public static String stripRedundantParentheses(String s) {
        if (s == null) return "";
        s = s.trim();
        while (s.startsWith("(") && s.endsWith(")")) {
            int depth = 0;
            boolean matchedEnd = false;
            for (int i = 0; i < s.length(); i++) {
                char c = s.charAt(i);
                if (c == '(') depth++;
                else if (c == ')') {
                    depth--;
                    if (depth == 0) {
                        matchedEnd = (i == s.length() - 1);
                        break;
                    }
                }
            }
            if (matchedEnd) {
                s = s.substring(1, s.length() - 1).trim();
            } else {
                break;
            }
        }
        return s;
    }

    public static BinarySplit splitBinary(String expression, List<String> operators) {
        int par = 0, br = 0, sq = 0;
        boolean inStr = false;
        for (int i = expression.length() - 1; i >= 0; i--) {
            char c = expression.charAt(i);
            if (inStr) {
                if (c == '"' && (i == 0 || expression.charAt(i - 1) != '\\')) {
                    inStr = false;
                }
                continue;
            }
            if (c == '"') {
                inStr = true;
                continue;
            }
            if (c == ')') par++;
            else if (c == '(') par--;
            else if (c == '}') br++;
            else if (c == '{') br--;
            else if (c == ']') sq++;
            else if (c == '[') sq--;

            if (par != 0 || br != 0 || sq != 0) continue;

            for (String op : operators) {
                if (i + op.length() > expression.length()) continue;
                if (!expression.regionMatches(i, op, 0, op.length())) continue;

                // Protect |> against |
                if (op.equals("|")) {
                    if (i + 1 < expression.length() && (expression.charAt(i + 1) == '>' || expression.charAt(i + 1) == '|' || expression.charAt(i + 1) == '='))
                        continue;
                    if (i > 0 && expression.charAt(i - 1) == '|')
                        continue;
                }
                if (op.equals("&")) {
                    if (i + 1 < expression.length() && (expression.charAt(i + 1) == '&' || expression.charAt(i + 1) == '='))
                        continue;
                    if (i > 0 && expression.charAt(i - 1) == '&')
                        continue;
                }
                if (op.equals("^")) {
                    if (i + 1 < expression.length() && expression.charAt(i + 1) == '=')
                        continue;
                }
                if (op.equals("<")) {
                    if (i + 1 < expression.length() && (expression.charAt(i + 1) == '<' || expression.charAt(i + 1) == '='))
                        continue;
                    if (i > 0 && expression.charAt(i - 1) == '<')
                        continue;
                }
                if (op.equals(">")) {
                    if (i + 1 < expression.length() && (expression.charAt(i + 1) == '>' || expression.charAt(i + 1) == '='))
                        continue;
                    if (i > 0 && (expression.charAt(i - 1) == '>' || expression.charAt(i - 1) == '|'))
                        continue;
                }

                // Disambiguate multi-char ops and unary signs
                if (op.equals("+") || op.equals("-")) {
                    int before = i;
                    while (before > 0 && Character.isWhitespace(expression.charAt(before - 1))) {
                        before--;
                    }
                    if (before == 0) continue; // Unary prefix
                    char prior = expression.charAt(before - 1);
                    if (prior == '(' || prior == '[' || prior == '{' || prior == ',' ||
                            prior == ':' || prior == '+' || prior == '-' || prior == '*' ||
                            prior == '/' || prior == '%' || prior == '=' || prior == '<' ||
                            prior == '>' || prior == '!' || prior == '|' || prior == '&' ||
                            prior == '^' || prior == '~') {
                        continue; // Unary sign
                    }
                }

                String left = expression.substring(0, i).trim();
                String right = expression.substring(i + op.length()).trim();
                return new BinarySplit(left, op.trim(), right);
            }
        }
        return null;
    }

    public static List<String> parseArguments(String argsString) {
        List<String> result = new ArrayList<>();
        if (argsString == null || argsString.trim().isEmpty()) {
            return result;
        }
        int par = 0, br = 0, sq = 0;
        boolean inStr = false;
        int start = 0;
        for (int i = 0; i < argsString.length(); i++) {
            char c = argsString.charAt(i);
            if (inStr) {
                if (c == '"' && (i == 0 || argsString.charAt(i - 1) != '\\')) {
                    inStr = false;
                }
                continue;
            }
            if (c == '"') {
                inStr = true;
                continue;
            }
            if (c == '(') par++;
            else if (c == ')') par--;
            else if (c == '{') br++;
            else if (c == '}') br--;
            else if (c == '[') sq++;
            else if (c == ']') sq--;

            if (c == ',' && par == 0 && br == 0 && sq == 0) {
                result.add(argsString.substring(start, i).trim());
                start = i + 1;
            }
        }
        if (start < argsString.length()) {
            result.add(argsString.substring(start).trim());
        }
        return result;
    }

    public static int findMatchingOpenBracket(String s, int closeIndex) {
        int sq = 0;
        for (int i = closeIndex; i >= 0; i--) {
            char c = s.charAt(i);
            if (c == ']') sq++;
            else if (c == '[') {
                sq--;
                if (sq == 0) return i;
            }
        }
        return -1;
    }

    public static int findMatchingOpenParen(String s, int closeIndex) {
        int par = 0;
        for (int i = closeIndex; i >= 0; i--) {
            char c = s.charAt(i);
            if (c == ')') par++;
            else if (c == '(') {
                par--;
                if (par == 0) return i;
            }
        }
        return -1;
    }
}
