package org.mosslang.compiler.semantics;

import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.diagnostics.CompileError;
import org.mosslang.compiler.diagnostics.Warning;
import org.mosslang.compiler.types.TypeContext;
import org.mosslang.compiler.types.TypeRef;

import java.util.*;

public class SemanticChecker {
    private final Program program;
    private final List<Warning> warnings = new ArrayList<>();
    private final Map<String, Function> functions = new HashMap<>();
    private final Map<String, Domain> domains = new HashMap<>();
    private final Map<String, ObjectType> objects = new HashMap<>();
    private final Map<String, EnumType> enums = new HashMap<>();
    private final Map<String, Trait> traits = new HashMap<>();
    private String currentSemanticIdentity = null;

    public static class MoveInfo {
        public final int line;
        public final String destination;
        public MoveInfo(int line, String destination) {
            this.line = line;
            this.destination = destination;
        }
    }

    public static class OwnershipEnv {
        public final Map<String, String> types = new HashMap<>();
        public final Map<String, MoveInfo> moved = new HashMap<>();

        public void put(String name, String type) {
            if (name != null && !name.isEmpty()) {
                types.put(name, type != null ? type : "int");
                moved.remove(name);
            }
        }
    }

    public SemanticChecker(Program program) {
        this.program = program;
    }

    public List<Warning> getWarnings() {
        return Collections.unmodifiableList(warnings);
    }

    public void run() {
        collectDeclarations();
        checkEnums();
        checkObjects();
        checkTraits();
        checkDomains();
        checkFunctions();
        checkMain();
    }

    private void collectDeclarations() {
        for (EnumType e : program.enums) {
            if (enums.put(e.name, e) != null) {
                throw new CompileError(e.line, "duplicate enum '" + e.name + "'", "DUPLICATE_DECLARATION");
            }
        }
        for (ObjectType o : program.objects) {
            if (objects.put(o.name, o) != null) {
                throw new CompileError(o.line, "duplicate type '" + o.name + "'", "DUPLICATE_DECLARATION");
            }
        }
        for (Trait t : program.traits) {
            if (traits.put(t.name, t) != null) {
                throw new CompileError(t.line, "duplicate trait '" + t.name + "'", "DUPLICATE_DECLARATION");
            }
        }
        for (Domain d : program.domains) {
            if (domains.put(d.name, d) != null) {
                throw new CompileError(d.line, "duplicate domain '" + d.name + "'", "DUPLICATE_DECLARATION");
            }
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
        OwnershipEnv env = new OwnershipEnv();
        for (Field f : domain.state) {
            env.put(f.name, f.type);
        }
        for (Param p : handler.params) {
            env.put(p.name, p.type);
        }

        for (Stmt s : handler.body) {
            checkStatement(s, env, true, handler.name);
        }
    }

    private void checkFunctions() {
        for (Function f : program.functions) {
            currentSemanticIdentity = "fn:" + f.name + "@" + f.line;
            OwnershipEnv env = new OwnershipEnv();
            for (Param p : f.params) {
                env.put(p.name, p.type);
            }
            for (Stmt s : f.body) {
                checkStatement(s, env, false, f.name);
            }
        }
    }

    private void checkMain() {
        if (program.main != null) {
            currentSemanticIdentity = "proc:main@1";
            OwnershipEnv env = new OwnershipEnv();
            for (Stmt s : program.main.body) {
                checkStatement(s, env, false, "main");
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

    private boolean isCopyType(String type) {
        if (type == null) return false;
        type = type.trim();
        if (type.equals("int") || type.equalsIgnoreCase("Int") ||
            type.equals("float") || type.equalsIgnoreCase("Float") ||
            type.equals("bool") || type.equalsIgnoreCase("Bool") ||
            type.equals("char") || type.equalsIgnoreCase("Char")) {
            return true;
        }
        if (type.startsWith("option[") && type.endsWith("]")) {
            return isCopyType(type.substring(7, type.length() - 1));
        }
        return false;
    }

    private boolean transferType(String type) {
        if (type == null || type.isEmpty()) return false;
        return !isCopyType(type) && !domains.containsKey(type);
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
        String type = env.types.get(name);
        if (type == null || !transferType(type)) return;
        env.moved.put(name, new MoveInfo(line, destination));
    }

    private String inferExprType(String expr, OwnershipEnv env) {
        if (expr == null) return "int";
        expr = expr.trim();
        if (expr.startsWith("\"") && expr.endsWith("\"")) return "string";
        if (expr.equals("true") || expr.equals("false")) return "bool";
        if (expr.matches("^-?\\d+$")) return "int";
        if (expr.matches("^-?\\d+\\.\\d+$")) return "float";
        if (env.types.containsKey(expr)) {
            return env.types.get(expr);
        }
        int openParen = expr.indexOf('(');
        if (openParen > 0) {
            String callee = expr.substring(0, openParen).trim();
            if (objects.containsKey(callee)) {
                return callee;
            }
            if (domains.containsKey(callee)) {
                return callee;
            }
            if (enums.containsKey(callee)) {
                return callee;
            }
        }
        return "int";
    }

    private void checkStatement(Stmt s, OwnershipEnv env, boolean insideHandler, String enclosingName) {
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
                String explicitType = null;
                if (varName.contains(":")) {
                    String[] parts = varName.split(":", 2);
                    varName = parts[0].trim();
                    explicitType = parts[1].trim();
                }

                // Check RHS expression availability before consuming
                requireAvailable(s.line, expr, env);

                String inferredType = explicitType != null ? explicitType : inferExprType(expr, env);
                env.put(varName, inferredType);

                // If expr is a bare identifier referring to a transfer type, consume it
                if (env.types.containsKey(expr) && transferType(env.types.get(expr))) {
                    consumeBinding(s.line, expr, env, varName);
                }
            }
            case Assign -> {
                String varName = s.a != null ? s.a.trim() : "";
                String expr = s.b != null ? s.b.trim() : "";
                requireAvailable(s.line, expr, env);
                String inferredType = inferExprType(expr, env);
                env.put(varName, inferredType);
                if (env.types.containsKey(expr) && transferType(env.types.get(expr))) {
                    consumeBinding(s.line, expr, env, varName);
                }
            }
            case Message -> {
                if (s.a != null && s.b != null) {
                    if (insideHandler && s.a.equals("self")) {
                        throw new CompileError(s.line, "self-send is illegal; use helper functions", "ILLEGAL_SELF_SEND");
                    }
                }
                requireAvailable(s.line, s.text, env);
            }
            case Reply -> {
                if (!insideHandler) {
                    throw new CompileError(s.line, "reply is only valid inside domain handlers", "REPLY_OUTSIDE_HANDLER");
                }
                requireAvailable(s.line, s.text, env);
            }
            case Echo -> {
                requireAvailable(s.line, s.text, env);
            }
            case Call -> {
                requireAvailable(s.line, s.text, env);
            }
            case If, Else, While, For -> {
                requireAvailable(s.line, s.text, env);
            }
            default -> {
                requireAvailable(s.line, s.text, env);
            }
        }
    }
}
