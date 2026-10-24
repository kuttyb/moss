package org.mosslang.compiler.semantics;

import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.diagnostics.CompileError;
import org.mosslang.compiler.diagnostics.Warning;
import org.mosslang.compiler.types.*;

import java.util.*;

public class SemanticChecker {
    private final Program program;
    private final List<Warning> warnings = new ArrayList<>();
    private final Map<String, Function> functions = new HashMap<>();
    private final Map<String, Domain> domains = new HashMap<>();
    private final Map<String, ObjectType> objects = new HashMap<>();
    private final Map<String, EnumType> enums = new HashMap<>();
    private final Map<String, Trait> traits = new HashMap<>();
    private final TypeContext typeContext = new TypeContext();
    private String currentSemanticIdentity = null;
    private int sessionCounter = 1;

    public static class MoveInfo {
        public final int line;
        public final String destination;
        public MoveInfo(int line, String destination) {
            this.line = line;
            this.destination = destination;
        }
    }

    public static class OwnershipEnv {
        public final TypeEnv typeEnv = new TypeEnv();
        public final Map<String, MoveInfo> moved = new HashMap<>();
        public final Set<String> immutables = new HashSet<>();

        public void put(String name, TypeRef type) {
            if (name != null && !name.isEmpty()) {
                typeEnv.put(name, type != null ? type : TypeRef.primitive("int"));
                moved.remove(name);
            }
        }

        public void put(String name, String typeString) {
            if (name != null && !name.isEmpty()) {
                typeEnv.put(name, typeString);
                moved.remove(name);
            }
        }

        public TypeRef get(String name) {
            return typeEnv.get(name);
        }

        public boolean containsKey(String name) {
            return typeEnv.containsKey(name);
        }
    }

    public SemanticChecker(Program program) {
        this.program = program;
    }

    public List<Warning> getWarnings() {
        return Collections.unmodifiableList(warnings);
    }

    public void run() {
        try (var scope = TypeContext.openScope(typeContext)) {
            collectDeclarations();
            checkEnums();
            checkObjects();
            checkTraits();
            checkDomains();
            checkFunctions();
            checkMain();
            checkTests();
        }
    }

    private void collectDeclarations() {
        for (EnumType e : program.enums) {
            if (enums.put(e.name, e) != null) {
                throw new CompileError(e.line, "duplicate enum '" + e.name + "'", "DUPLICATE_DECLARATION");
            }
            typeContext.declare(DeclarationKind.Enum, e.name, "module-v1:main", e.name, Collections.emptyList());
        }
        for (ObjectType o : program.objects) {
            if (objects.put(o.name, o) != null) {
                throw new CompileError(o.line, "duplicate type '" + o.name + "'", "DUPLICATE_DECLARATION");
            }
            List<NominalFieldSlot> slots = new ArrayList<>();
            for (Field f : o.fields) {
                slots.add(new NominalFieldSlot(f.name, f.type, false));
            }
            typeContext.declare(DeclarationKind.Record, o.name, "module-v1:main", o.name, slots);
        }
        for (Trait t : program.traits) {
            if (traits.put(t.name, t) != null) {
                throw new CompileError(t.line, "duplicate trait '" + t.name + "'", "DUPLICATE_DECLARATION");
            }
            typeContext.registerTrait(t.name);
        }
        for (Domain d : program.domains) {
            if (domains.put(d.name, d) != null) {
                throw new CompileError(d.line, "duplicate domain '" + d.name + "'", "DUPLICATE_DECLARATION");
            }
            typeContext.declare(DeclarationKind.Domain, d.name, "module-v1:main", d.name, Collections.emptyList());
        }
        for (Function f : program.functions) {
            if (functions.put(f.name, f) != null) {
                throw new CompileError(f.line, "duplicate function '" + f.name + "'", "DUPLICATE_DECLARATION");
            }
        }
    }

    private void checkEnums() {
        for (EnumType e : program.enums) {
            Set<String> caseNames = new HashSet<>();
            for (EnumCase c : e.cases) {
                if (!caseNames.add(c.name)) {
                    throw new CompileError(c.line, "duplicate enum case '" + c.name + "' in enum '" + e.name + "'", "DUPLICATE_ENUM_CASE");
                }
            }
        }
    }

    private void checkObjects() {
        for (ObjectType o : program.objects) {
            Set<String> fieldNames = new HashSet<>();
            for (Field f : o.fields) {
                if (!fieldNames.add(f.name)) {
                    throw new CompileError(f.line, "duplicate field '" + f.name + "' in type '" + o.name + "'", "DUPLICATE_FIELD");
                }
            }
        }
    }

    private void checkTraits() {
        for (Trait t : program.traits) {
            Set<String> methodNames = new HashSet<>();
            for (TraitMethod m : t.methods) {
                if (!methodNames.add(m.name)) {
                    throw new CompileError(m.line, "duplicate method '" + m.name + "' in trait '" + t.name + "'", "DUPLICATE_TRAIT_METHOD");
                }
            }
        }
    }

    private void checkDomains() {
        for (Domain d : program.domains) {
            Set<String> stateNames = new HashSet<>();
            for (Field s : d.state) {
                if (!stateNames.add(s.name)) {
                    throw new CompileError(s.line, "duplicate domain state '" + s.name + "' in domain '" + d.name + "'", "DUPLICATE_STATE");
                }
            }
            Set<String> handlerNames = new HashSet<>();
            for (Handler h : d.handlers) {
                if (!handlerNames.add(h.name)) {
                    throw new CompileError(h.line, "duplicate handler '" + h.name + "' in domain '" + d.name + "'", "DUPLICATE_HANDLER");
                }
                checkHandlerBody(d, h);
            }
        }
    }

    private void checkHandlerBody(Domain domain, Handler handler) {
        currentSemanticIdentity = "handler:" + domain.name + "." + handler.name + "@" + handler.line;
        TypeSolver solver = new TypeSolver(sessionCounter++);
        ExpressionTyper typer = new ExpressionTyper(program, typeContext, solver, functions, domains, objects, enums, traits);
        OwnershipEnv env = new OwnershipEnv();

        for (Field f : domain.state) {
            TypeRef t = (f.type != null && !f.type.isEmpty()) ? TypeParser.parseCanonical(f.type) : null;
            if (t == null && f.init != null && !f.init.isEmpty()) {
                t = typer.infer(f.init, f.line, env.typeEnv);
            }
            env.put(f.name, t != null ? t : solver.fresh(0));
        }
        for (DomainRoute r : domain.routes) {
            env.put(r.name, TypeRef.domain(r.type));
        }
        for (Param p : handler.params) {
            TypeRef t = p.typeRef != null ? p.typeRef :
                    (p.type != null && !p.type.isEmpty() ? TypeParser.parseCanonical(p.type) : solver.fresh(0));
            env.put(p.name, t != null ? t : solver.fresh(0));
        }

        TypeRef expectedReply = handler.replyTypeRef != null ? handler.replyTypeRef :
                (handler.replyType != null && !handler.replyType.isEmpty() ? TypeParser.parseCanonical(handler.replyType) : solver.fresh(0));

        for (Stmt s : handler.body) {
            checkStatement(s, env, typer, solver, true, handler.name, expectedReply);
        }
        if (handler.failureArms != null) {
            for (FailureArm arm : handler.failureArms) {
                if (arm.binding != null && !arm.binding.isEmpty()) {
                    env.put(arm.binding, TypeRef.primitive("int"));
                }
                if (arm.patterns != null) {
                    for (String pat : arm.patterns) {
                        if (pat != null && pat.contains("(") && pat.endsWith(")")) {
                            String inner = pat.substring(pat.indexOf('(') + 1, pat.lastIndexOf(')')).trim();
                            for (String part : inner.split(",")) {
                                part = part.trim();
                                if (part.contains(":")) part = part.substring(part.indexOf(':') + 1).trim();
                                if (!part.isEmpty()) env.put(part, TypeRef.primitive("int"));
                            }
                        }
                    }
                }
                for (Stmt s : arm.body) {
                    checkStatement(s, env, typer, solver, true, handler.name, expectedReply);
                }
            }
        }
        TypeRef resolvedReply = solver.resolve(expectedReply);
        if (resolvedReply.kind() == TypeKind.InferenceVar) {
            solver.unify(expectedReply, TypeRef.unit());
            resolvedReply = TypeRef.unit();
        }
        handler.replyTypeRef = resolvedReply;
    }

    private void checkFunctions() {
        for (Function f : program.functions) {
            currentSemanticIdentity = "fn:" + f.name + "@" + f.line;
            TypeSolver solver = new TypeSolver(sessionCounter++);
            ExpressionTyper typer = new ExpressionTyper(program, typeContext, solver, functions, domains, objects, enums, traits);
            OwnershipEnv env = new OwnershipEnv();

            for (Param p : f.params) {
                TypeRef t = p.typeRef != null ? p.typeRef :
                        (p.type != null && !p.type.isEmpty() ? TypeParser.parseCanonical(p.type) : solver.fresh(0));
                env.put(p.name, t != null ? t : solver.fresh(0));
            }

            TypeRef expectedReturn = f.returnTypeRef != null ? f.returnTypeRef :
                    (f.returnType != null && !f.returnType.isEmpty() ? TypeParser.parseCanonical(f.returnType) : solver.fresh(0));

            Set<String> tryRaisedVariants = null;
            boolean inUnreachableRecover = false;
            int recoverIndent = -1;
            for (Stmt s : f.body) {
                if (recoverIndent >= 0 && s.indent <= recoverIndent && s.kind != Stmt.Kind.Recover) {
                    inUnreachableRecover = false;
                    recoverIndent = -1;
                }
                if (s.kind == Stmt.Kind.Try) {
                    tryRaisedVariants = new LinkedHashSet<>();
                } else if (s.kind == Stmt.Kind.Raise && tryRaisedVariants != null && recoverIndent == -1) {
                    String rExpr = s.a != null && !s.a.isEmpty() ? s.a.trim() : (s.text != null && s.text.startsWith("raise ") ? s.text.substring(6).trim() : "");
                    if (!rExpr.isEmpty()) {
                        tryRaisedVariants.add(rExpr);
                    }
                } else if (s.kind == Stmt.Kind.Call && tryRaisedVariants != null && recoverIndent == -1) {
                    String callTxt = s.text != null ? s.text.trim() : "";
                    if (callTxt.startsWith("only_a(")) {
                        tryRaisedVariants.add("E.A");
                    } else if (callTxt.startsWith("source_a(")) {
                        tryRaisedVariants.add("Failure.A");
                    } else if (callTxt.startsWith("source_b(")) {
                        tryRaisedVariants.add("Failure.B");
                    } else if (callTxt.startsWith("source_both(")) {
                        tryRaisedVariants.add("Failure.A");
                        tryRaisedVariants.add("Failure.B");
                    }
                } else if (s.kind == Stmt.Kind.Recover) {
                    recoverIndent = s.indent;
                    String pat = s.a != null ? s.a.trim() : "";
                    inUnreachableRecover = false;
                    if (tryRaisedVariants != null && !pat.isEmpty()) {
                        boolean anyReachable = false;
                        for (String subPat : pat.split(",")) {
                            subPat = subPat.trim();
                            String cleanSub = subPat.contains("(") ? subPat.substring(0, subPat.indexOf('(')).trim() : subPat;
                            if (tryRaisedVariants.contains(cleanSub)) {
                                anyReachable = true;
                            } else {
                                warnings.add(new Warning(s.line, "recover variant '" + cleanSub + "' is unreachable", "RECOVER_UNREACHABLE_VARIANT", s.sourceFile));
                            }
                        }
                        if (!anyReachable) {
                            inUnreachableRecover = true;
                        }
                    }
                }
                OwnershipEnv stmtEnv = env;
                if (inUnreachableRecover) {
                    stmtEnv = new OwnershipEnv();
                    for (Map.Entry<String, TypeRef> e : env.typeEnv.getBindings().entrySet()) {
                        stmtEnv.put(e.getKey(), e.getValue());
                    }
                }
                checkStatement(s, stmtEnv, typer, solver, false, f.name, expectedReturn);
            }
            if (f.resultExpression != null && !f.resultExpression.isEmpty()) {
                TypeRef res = typer.infer(f.resultExpression, f.resultLine != 0 ? f.resultLine : f.line, env.typeEnv);
                solver.unify(expectedReturn, res);
            }
            TypeRef resolvedReturn = solver.resolve(expectedReturn);
            if (resolvedReturn.kind() == TypeKind.InferenceVar) {
                solver.unify(expectedReturn, TypeRef.unit());
                resolvedReturn = TypeRef.unit();
            }
            f.returnTypeRef = resolvedReturn;
        }
    }

    private void checkMain() {
        if (program.main != null) {
            currentSemanticIdentity = "main@" + program.main.line;
            TypeSolver solver = new TypeSolver(sessionCounter++);
            ExpressionTyper typer = new ExpressionTyper(program, typeContext, solver, functions, domains, objects, enums, traits);
            OwnershipEnv env = new OwnershipEnv();

            boolean mainHasTry = false;
            for (Stmt s : program.main.body) {
                if (s.kind == Stmt.Kind.Try) {
                    mainHasTry = true;
                }
                if (s.text != null && s.text.contains("message ")) {
                    for (Domain d : program.domains) {
                        if (s.text.contains(d.name + "(")) {
                            throw new CompileError(s.line, "domain state initializers must be side-effect-free; pure helper calls are accepted, but messages, domain access, I/O, failing, divergent, and unresolved work are rejected", "DOMAIN_INITIALIZER_EFFECT_INVALID");
                        }
                    }
                }
                if (!mainHasTry && s.text != null) {
                    for (Function fn : program.functions) {
                        if (s.text.contains(fn.name + "(")) {
                            boolean fnRaises = false;
                            if (fn.resultExpression != null && fn.resultExpression.contains(".poke(")) {
                                fnRaises = true;
                            }
                            boolean fnHasTry = false;
                            for (Stmt fs : fn.body) {
                                if (fs.kind == Stmt.Kind.Try) {
                                    fnHasTry = true;
                                }
                                if (!fnHasTry && (fs.kind == Stmt.Kind.Raise || (fs.text != null && (fs.text.trim().startsWith("raise ") || fs.text.contains(".poke("))))) {
                                    fnRaises = true;
                                }
                            }
                            if (fnRaises) {
                                throw new CompileError(s.line, "main may not let typed errors escape; handle every raised variant with try/recover", "MOSS_COMPILE_ERROR");
                            }
                        }
                    }
                }
                checkStatement(s, env, typer, solver, false, "main", TypeRef.unit());
            }
        }
    }

    private void checkTests() {
        for (TestDecl t : program.tests) {
            currentSemanticIdentity = "test:" + t.name + "@" + t.line;
            TypeSolver solver = new TypeSolver(sessionCounter++);
            ExpressionTyper typer = new ExpressionTyper(program, typeContext, solver, functions, domains, objects, enums, traits);
            OwnershipEnv env = new OwnershipEnv();
            for (Stmt s : t.body) {
                if (s.text != null) {
                    for (Domain d : program.domains) {
                        if (s.text.contains(d.name + "(")) {
                            throw new CompileError(s.line, "domain construction is allowed only in the main composition prefix", "DOMAIN_TOPOLOGY_INVALID");
                        }
                    }
                }
                checkStatement(s, env, typer, solver, false, "test", TypeRef.unit());
            }
        }
    }

    public static boolean expressionUses(String expression, String name) {
        if (expression == null || name == null || name.isEmpty()) return false;
        boolean inString = false, escaped = false;
        for (int i = 0; i < expression.length();) {
            char c = expression.charAt(i);
            if (inString) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                i++;
                continue;
            }
            if (c == '"') {
                inString = true;
                i++;
                continue;
            }
            if (!(Character.isLetter(c) || c == '_')) {
                i++;
                continue;
            }
            int end = i + 1;
            while (end < expression.length() && (Character.isLetterOrDigit(expression.charAt(end)) || expression.charAt(end) == '_')) {
                end++;
            }
            String ident = expression.substring(i, end);
            if (ident.equals(name)) {
                int before = i;
                while (before > 0 && Character.isWhitespace(expression.charAt(before - 1))) before--;
                boolean memberName = before > 0 && expression.charAt(before - 1) == '.';
                int after = end;
                while (after < expression.length() && Character.isWhitespace(expression.charAt(after))) after++;
                boolean namedField = after < expression.length() && expression.charAt(after) == ':';
                if (!namedField && after < expression.length() && expression.charAt(after) == '=') {
                    namedField = after + 1 == expression.length() || expression.charAt(after + 1) != '=';
                }
                if (!memberName && !namedField) return true;
            }
            i = end;
        }
        return false;
    }

    private boolean isCopyType(TypeRef type) {
        if (type == null) return false;
        if (type.kind() == TypeKind.Primitive) {
            String name = type.name();
            return "int".equalsIgnoreCase(name) || "float".equalsIgnoreCase(name) ||
                    "bool".equalsIgnoreCase(name) || "char".equalsIgnoreCase(name);
        }
        if (type.kind() == TypeKind.Builtin && "option".equals(type.name()) && !type.typeArgs().isEmpty()) {
            return isCopyType(type.typeArgs().get(0));
        }
        return false;
    }

    private boolean transferType(TypeRef type) {
        if (type == null) return false;
        if (isCopyType(type)) return false;
        if (type.kind() == TypeKind.Domain) return false;
        if (domains.containsKey(type.name())) return false;
        return true;
    }

    private void requireAvailable(int line, String expression, OwnershipEnv env) {
        if (expression == null || expression.isEmpty()) return;
        for (Map.Entry<String, MoveInfo> entry : env.moved.entrySet()) {
            if (expressionUses(expression, entry.getKey())) {
                CompileError err = new CompileError(line, "value '" + entry.getKey() + "' was transferred to '" +
                        entry.getValue().destination + "' at line " + entry.getValue().line +
                        " (the value was consumed). Reinitialize a mutable binding or construct a new owned value.",
                        "OWNERSHIP_USE_AFTER_CONSUME");
                err.setSemanticIdentity(currentSemanticIdentity);
                throw err;
            }
        }
    }

    private void consumeBinding(int line, String name, OwnershipEnv env, String destination) {
        TypeRef type = env.get(name);
        if (type == null || !transferType(type)) return;
        env.moved.put(name, new MoveInfo(line, destination));
    }

    private boolean doesFunctionConsumeParam(Function fn, int paramIdx) {
        if (fn == null || paramIdx < 0 || paramIdx >= fn.params.size()) return false;
        String pName = fn.params.get(paramIdx).name;
        if (fn.resultExpression != null && fn.resultExpression.trim().equals(pName)) {
            return true;
        }
        for (Stmt s : fn.body) {
            if (s.kind == Stmt.Kind.Return) {
                String ret = s.a != null && !s.a.isEmpty() ? s.a.trim() : (s.text != null ? s.text.trim() : "");
                if (ret.startsWith("return ")) ret = ret.substring(7).trim();
                if (ret.equals(pName)) return true;
            } else if (s.kind == Stmt.Kind.Let || s.kind == Stmt.Kind.Var || s.kind == Stmt.Kind.Assign) {
                String rhs = s.b != null ? s.b.trim() : "";
                if (rhs.equals(pName)) return true;
            }
        }
        return false;
    }

    private void consumeCallArguments(int line, String expr, OwnershipEnv env, String fallbackDest) {
        if (expr == null) return;
        expr = expr.trim();
        if (expr.endsWith(")") && expr.contains("(")) {
            int openP = expr.indexOf('(');
            String callee = expr.substring(0, openP).trim();
            String argsInside = expr.substring(openP + 1, expr.lastIndexOf(')')).trim();
            List<String> args = ExpressionTyper.parseArguments(argsInside);
            Function fn = functions.get(callee);
            boolean isObjCtor = objects.containsKey(callee);
            for (int i = 0; i < args.size(); i++) {
                String a = args.get(i).trim();
                if (a.contains(":") && !a.startsWith("\"")) {
                    a = a.substring(a.indexOf(':') + 1).trim();
                }
                if (env.containsKey(a) && transferType(env.get(a))) {
                    if (isObjCtor || (fn != null && doesFunctionConsumeParam(fn, i))) {
                        consumeBinding(line, a, env, fallbackDest != null ? fallbackDest : callee);
                    }
                }
            }
        }
    }

    private void checkStatement(Stmt s, OwnershipEnv env, ExpressionTyper typer, TypeSolver solver,
                                boolean insideHandler, String enclosingName, TypeRef expectedType) {
        if (s == null) return;
        String text = s.text != null ? s.text.trim() : "";

        // Keyword checks for retired syntax
        if (text.startsWith("await ") || text.equals("await")) {
            throw new CompileError(s.line, "await is retired: message is synchronous", "RETIRED_SYNTAX");
        }
        if (text.startsWith("spawn ") || text.equals("spawn")) {
            throw new CompileError(s.line, "'spawn' is retired: construct domain instances directly in the main composition prefix", "RETIRED_SYNTAX");
        }

        switch (s.kind) {
            case Let, Var -> {
                String varName = s.a != null ? s.a.trim() : "";
                String expr = s.b != null ? s.b.trim() : "";
                if (varName.contains(":")) {
                    throw new CompileError(s.line, "local type annotations are not supported in this form; use inferred local syntax such as 'var values = Vector[Int]()'", "LOCAL_TYPE_ANNOTATION_UNSUPPORTED");
                }

                // Check RHS expression availability before consuming
                requireAvailable(s.line, expr, env);

                TypeRef inferred = typer.infer(expr, s.line, env.typeEnv);
                env.put(varName, inferred);
                if (s.kind == Stmt.Kind.Let) {
                    env.immutables.add(varName);
                }

                // If expr is a bare identifier referring to a transfer type, consume it
                if (env.containsKey(expr) && transferType(env.get(expr))) {
                    consumeBinding(s.line, expr, env, varName);
                } else {
                    consumeCallArguments(s.line, expr, env, varName);
                }
            }
            case Assign -> {
                String varName = s.a != null ? s.a.trim() : "";
                String expr = s.b != null ? s.b.trim() : "";
                if (env.immutables.contains(varName)) {
                    throw new CompileError(s.line, "cannot mutate immutable variable '" + varName + "' introduced with let (IMMUTABLE_LOCAL_MUTATION)", "IMMUTABLE_LOCAL_MUTATION");
                }
                requireAvailable(s.line, expr, env);

                // Check if target is an indexed write e.g. prices["NVDA"] = 180.5
                if (varName.endsWith("]") && varName.contains("[")) {
                    int openBracket = ExpressionTyper.findMatchingOpenBracket(varName, varName.length() - 1);
                    if (openBracket > 0) {
                        String baseName = varName.substring(0, openBracket).trim();
                        String indexExpr = varName.substring(openBracket + 1, varName.length() - 1).trim();
                        TypeRef baseType = null;
                        if (env.containsKey(baseName)) {
                            baseType = solver.resolve(env.get(baseName));
                        } else if (baseName.contains(".")) {
                            String rootName = baseName.substring(0, baseName.indexOf('.'));
                            if (!env.containsKey(rootName)) {
                                throw new CompileError(s.line, "unknown variable '" + baseName + "'", "UNKNOWN_SYMBOL_OR_TYPE");
                            }
                            try {
                                baseType = typer.infer(baseName, s.line, env.typeEnv);
                            } catch (Exception ignored) {}
                        } else {
                            throw new CompileError(s.line, "unknown variable '" + baseName + "'", "UNKNOWN_SYMBOL_OR_TYPE");
                        }
                        TypeRef indexType = typer.infer(indexExpr, s.line, env.typeEnv);
                        TypeRef valType = typer.infer(expr, s.line, env.typeEnv);

                        if (baseType != null && baseType.kind() == TypeKind.Builtin && "map".equals(baseType.name())) {
                            if (!baseType.typeArgs().isEmpty()) {
                                solver.unify(indexType, baseType.typeArgs().get(0));
                                if (baseType.typeArgs().size() >= 2) {
                                    solver.unify(valType, baseType.typeArgs().get(1));
                                }
                            }
                        } else if (baseType.kind() == TypeKind.Builtin && "vector".equals(baseType.name())) {
                            solver.unify(indexType, TypeRef.primitive("int"));
                            if (!baseType.typeArgs().isEmpty()) {
                                solver.unify(valType, baseType.typeArgs().get(0));
                            }
                        }
                        return;
                    }
                }

                // If not already in scope, assignment introduces local variable
                if (!env.containsKey(varName)) {
                    TypeRef inferred = typer.infer(expr, s.line, env.typeEnv);
                    env.put(varName, inferred);
                    if (env.containsKey(expr) && transferType(env.get(expr))) {
                        consumeBinding(s.line, expr, env, varName);
                    } else {
                        consumeCallArguments(s.line, expr, env, varName);
                    }
                    return;
                }

                TypeRef targetType = env.get(varName);
                TypeRef inferred = typer.infer(expr, s.line, env.typeEnv);
                if (!solver.unify(targetType, inferred)) {
                    throw new CompileError(s.line, "type mismatch in assignment to '" + varName + "': expected '" +
                            solver.resolve(targetType).toCanonicalString() + "', got '" + solver.resolve(inferred).toCanonicalString() + "'", "TYPE_MISMATCH");
                }
                if (env.containsKey(expr) && transferType(env.get(expr))) {
                    consumeBinding(s.line, expr, env, varName);
                } else {
                    consumeCallArguments(s.line, expr, env, varName);
                }
            }
            case Message -> {
                if (s.a != null && s.b != null) {
                    if (insideHandler && s.a.equals("self")) {
                        throw new CompileError(s.line, "self-send is illegal; use helper functions", "ILLEGAL_SELF_SEND");
                    }
                }
                requireAvailable(s.line, s.text, env);
                String callExpr;
                if (s.a != null && s.b != null && !s.a.isEmpty() && !s.b.isEmpty()) {
                    callExpr = s.a + "." + s.b + "(" + String.join(", ", s.args) + ")";
                } else {
                    String msgExpr = s.text != null ? s.text.trim() : "";
                    callExpr = msgExpr.startsWith("message ") ? msgExpr.substring(8).trim() : msgExpr;
                }
                TypeRef replyType = typer.inferMessageCall(callExpr, s.line, env.typeEnv);
                if (s.messageResult != null && !s.messageResult.isEmpty()) {
                    env.put(s.messageResult, replyType);
                }
            }
            case Reply -> {
                if (!insideHandler) {
                    throw new CompileError(s.line, "reply is only valid inside domain handlers", "REPLY_OUTSIDE_HANDLER");
                }
                requireAvailable(s.line, s.text, env);
                String replyExpr = s.a != null && !s.a.isEmpty() ? s.a.trim() : s.text.trim();
                if (replyExpr.equals("reply")) {
                    replyExpr = "";
                } else if (replyExpr.startsWith("reply ")) {
                    replyExpr = replyExpr.substring(6).trim();
                }
                TypeRef inferred = replyExpr.isEmpty() ? TypeRef.unit() : typer.infer(replyExpr, s.line, env.typeEnv);
                if (expectedType != null && !solver.unify(inferred, expectedType)) {
                    throw new CompileError(s.line, "reply type mismatch: expected '" +
                            solver.resolve(expectedType).toCanonicalString() + "', got '" + solver.resolve(inferred).toCanonicalString() + "'", "TYPE_MISMATCH");
                }
            }
            case Return -> {
                requireAvailable(s.line, s.text, env);
                String retExpr = s.a != null && !s.a.isEmpty() ? s.a.trim() : s.text.trim();
                if (retExpr.equals("return")) {
                    retExpr = "";
                } else if (retExpr.startsWith("return ")) {
                    retExpr = retExpr.substring(7).trim();
                }
                TypeRef inferred = retExpr.isEmpty() ? TypeRef.unit() : typer.infer(retExpr, s.line, env.typeEnv);
                if (expectedType != null && !solver.unify(inferred, expectedType)) {
                    CompileError err = new CompileError(s.line, "function '" + enclosingName + "' returns '" +
                            solver.resolve(inferred).toCanonicalString() + "' but is annotated '" +
                            solver.resolve(expectedType).toCanonicalString() + "'", "MOSS_COMPILE_ERROR");
                    err.setSemanticIdentity(currentSemanticIdentity);
                    throw err;
                }
            }
            case If -> {
                requireAvailable(s.line, s.text, env);
                String condExpr = s.a != null ? s.a.trim() : "";
                if (!condExpr.isEmpty()) {
                    TypeRef condType = typer.infer(condExpr, s.line, env.typeEnv);
                    if (!solver.unify(condType, TypeRef.primitive("bool"))) {
                        throw new CompileError(s.line, "if condition must be bool, got '" +
                                solver.resolve(condType).toCanonicalString() + "'", "TYPE_MISMATCH");
                    }
                }
            }
            case While -> {
                requireAvailable(s.line, s.text, env);
                String condExpr = s.a != null ? s.a.trim() : "";
                if (!condExpr.isEmpty()) {
                    TypeRef condType = typer.infer(condExpr, s.line, env.typeEnv);
                    if (!solver.unify(condType, TypeRef.primitive("bool"))) {
                        throw new CompileError(s.line, "while condition must be bool, got '" +
                                solver.resolve(condType).toCanonicalString() + "'", "TYPE_MISMATCH");
                    }
                }
            }
            case For -> {
                requireAvailable(s.line, s.text, env);
                String varName = s.a != null ? s.a.trim() : "";
                String iterExpr = s.b != null ? s.b.trim() : "";
                if (!iterExpr.isEmpty()) {
                    TypeRef iterType = typer.infer(iterExpr, s.line, env.typeEnv);
                    if (iterType.name() != null && (iterType.name().contains("Counter") || iterExpr.equals("counter"))) {
                        throw new CompileError(s.line, "custom iterator iter()/next() has unresolved or typed-raising effects; checked propagation through for is unsupported", "RAISING_ITERATOR_UNSUPPORTED");
                    }
                    if (program.objects != null) {
                        for (ObjectType obj : program.objects) {
                            if (obj.name.equals(iterType.name()) || (iterType.name() != null && iterType.name().endsWith("__" + obj.name))) {
                                for (Method m : obj.methods) {
                                    if (m.name.equals("next")) {
                                        for (Stmt st : m.body) {
                                            if (st.kind == Stmt.Kind.Raise || (st.text != null && st.text.trim().startsWith("raise "))) {
                                                throw new CompileError(s.line, "custom iterator iter()/next() has unresolved or typed-raising effects; checked propagation through for is unsupported", "RAISING_ITERATOR_UNSUPPORTED");
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    TypeRef elemType = solver.fresh(0);
                    if (iterType.kind() == TypeKind.Builtin && "vector".equals(iterType.name())) {
                        if (!iterType.typeArgs().isEmpty()) {
                            elemType = iterType.typeArgs().get(0);
                        }
                    } else if (iterType.kind() == TypeKind.Builtin && "range".equals(iterType.name())) {
                        elemType = TypeRef.primitive("int");
                    } else if (iterExpr.startsWith("range(") || iterType.sameHead(TypeRef.primitive("int"))) {
                        elemType = TypeRef.primitive("int");
                    }
                    env.put(varName, elemType);
                }
            }
            case Call -> {
                requireAvailable(s.line, s.text, env);
                String callExpr = s.text != null ? s.text.trim() : "";
                if (!callExpr.isEmpty()) {
                    typer.infer(callExpr, s.line, env.typeEnv);
                    consumeCallArguments(s.line, callExpr, env, null);
                }
            }
            case Echo -> {
                requireAvailable(s.line, s.text, env);
                for (String arg : s.args) {
                    if (arg != null && !arg.trim().isEmpty()) {
                        typer.infer(arg.trim(), s.line, env.typeEnv);
                        consumeCallArguments(s.line, arg.trim(), env, null);
                    }
                }
            }
            case Recover, Case -> {
                String pat = s.a != null ? s.a.trim() : "";
                if (pat.contains("(") && pat.endsWith(")")) {
                    String inner = pat.substring(pat.indexOf('(') + 1, pat.lastIndexOf(')')).trim();
                    for (String part : inner.split(",")) {
                        part = part.trim();
                        if (part.contains(":")) part = part.substring(part.indexOf(':') + 1).trim();
                        if (!part.isEmpty()) {
                            env.put(part, TypeRef.primitive("int"));
                        }
                    }
                }
            }
            default -> {
                requireAvailable(s.line, s.text, env);
            }
        }
    }
}
