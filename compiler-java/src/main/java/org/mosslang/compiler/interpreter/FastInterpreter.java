package org.mosslang.compiler.interpreter;

import org.mosslang.compiler.ast.*;
import java.io.PrintStream;
import java.util.*;

public class FastInterpreter {
    public static class Value {
        public enum Kind { INT, FLOAT, BOOL, STRING, UNIT, OBJECT, DOMAIN }
        public final Kind kind;
        public final Object raw;

        public Value(Kind kind, Object raw) {
            this.kind = kind;
            this.raw = raw;
        }

        public static Value ofInt(long v) { return new Value(Kind.INT, v); }
        public static Value ofFloat(double v) { return new Value(Kind.FLOAT, v); }
        public static Value ofBool(boolean v) { return new Value(Kind.BOOL, v); }
        public static Value ofString(String v) { return new Value(Kind.STRING, v); }
        public static Value ofUnit() { return new Value(Kind.UNIT, null); }
        public static Value ofDomain(DomainInstance d) { return new Value(Kind.DOMAIN, d); }

        public long asInt() { return ((Number) raw).longValue(); }
        public double asFloat() { return ((Number) raw).doubleValue(); }
        public boolean asBool() { return (Boolean) raw; }
        public String asString() { return raw != null ? raw.toString() : ""; }
        public DomainInstance asDomain() { return (DomainInstance) raw; }

        @Override
        public String toString() {
            if (raw == null) return "()";
            if (kind == Kind.BOOL) return (Boolean) raw ? "true" : "false";
            return raw.toString();
        }
    }

    public static class DomainInstance {
        public final Domain domain;
        public final String name;
        public final Map<String, Value> state = new HashMap<>();

        public DomainInstance(Domain domain, String name) {
            this.domain = domain;
            this.name = name;
        }
    }

    private final Program program;
    private final boolean trace;
    private final PrintStream stdout;
    private final PrintStream stderr;
    private int eventId = 0;

    public FastInterpreter(Program program, boolean trace) {
        this(program, trace, System.out, System.err);
    }

    public FastInterpreter(Program program, boolean trace, PrintStream stdout, PrintStream stderr) {
        this.program = program;
        this.trace = trace;
        this.stdout = stdout;
        this.stderr = stderr;
    }

    private void traceEvent(String json) {
        if (trace) {
            stderr.println(json);
        }
    }

    public void run() {
        if (program.main == null) return;
        Map<String, Value> locals = new HashMap<>();

        for (Stmt s : program.main.body) {
            evalStmt(s, locals, null, "main", 0);
        }
    }

    private Value evalStmt(Stmt s, Map<String, Value> locals, DomainInstance currentDomain, String funcName, int depth) {
        if (s == null) return Value.ofUnit();
        switch (s.kind) {
            case Echo -> {
                StringBuilder line = new StringBuilder();
                for (int i = 0; i < s.args.size(); i++) {
                    if (i > 0) line.append(" ");
                    Value val = evalExpr(s.args.get(i), locals, currentDomain);
                    line.append(val.toString());
                }
                stdout.println(line.toString());
                return Value.ofUnit();
            }
            case Let, Var, Assign -> {
                String varName = s.a != null ? s.a.trim() : "";
                if (varName.contains(":")) {
                    varName = varName.split(":")[0].trim();
                }
                String exprStr = s.b != null ? s.b.trim() : "";
                Value val = evalExpr(exprStr, locals, currentDomain);
                if (currentDomain != null && currentDomain.state.containsKey(varName)) {
                    Value before = currentDomain.state.get(varName);
                    currentDomain.state.put(varName, val);
                    eventId++;
                    traceEvent("{\"event_id\":" + eventId + ",\"event\":\"state_write\",\"function\":\"" + funcName +
                            "\",\"source_file\":\"" + s.sourceFile + "\",\"line\":" + s.line +
                            ",\"depth\":" + depth + ",\"path\":\"" + varName + "\",\"before\":\"" + before + "\",\"after\":\"" + val + "\"}");
                } else {
                    locals.put(varName, val);
                }
                return Value.ofUnit();
            }
            case Message -> {
                String targetName = s.a;
                String handlerName = s.b;
                Value targetVal = locals.get(targetName);
                if (targetVal == null || targetVal.kind != Value.Kind.DOMAIN) {
                    throw new RuntimeException("message target '" + targetName + "' is not an active domain instance");
                }
                DomainInstance targetDomain = targetVal.asDomain();
                Handler handler = null;
                for (Handler h : targetDomain.domain.handlers) {
                    if (h.name.equals(handlerName)) {
                        handler = h;
                        break;
                    }
                }
                if (handler == null) {
                    throw new RuntimeException("domain '" + targetDomain.domain.name + "' has no handler '" + handlerName + "'");
                }

                List<Value> args = new ArrayList<>();
                StringBuilder argDetails = new StringBuilder();
                for (int i = 0; i < s.args.size(); i++) {
                    if (i > 0) argDetails.append(", ");
                    Value v = evalExpr(s.args.get(i), locals, currentDomain);
                    args.add(v);
                    argDetails.append(v.toString());
                }

                eventId++;
                int msgCallId = eventId;
                traceEvent("{\"event_id\":" + msgCallId + ",\"event\":\"message_call\",\"function\":\"" + funcName +
                        "\",\"source_file\":\"" + s.sourceFile + "\",\"line\":" + s.line +
                        ",\"depth\":" + depth + ",\"detail\":\"" + argDetails + "\",\"instance\":\"" + targetDomain.name +
                        "\",\"handler\":\"" + handlerName + "\"}");

                eventId++;
                int handlerEnterId = eventId;
                traceEvent("{\"event_id\":" + handlerEnterId + ",\"event\":\"handler_enter\",\"function\":\"" +
                        targetDomain.domain.name + "." + handlerName + "\",\"source_file\":\"" + s.sourceFile +
                        "\",\"line\":" + handler.line + ",\"parent_event_id\":" + msgCallId +
                        ",\"depth\":" + (depth + 1) + ",\"instance\":\"" + targetDomain.name +
                        "\",\"handler\":\"" + handlerName + "\"}");

                Map<String, Value> handlerLocals = new HashMap<>();
                for (int i = 0; i < handler.params.size() && i < args.size(); i++) {
                    handlerLocals.put(handler.params.get(i).name, args.get(i));
                }

                Value retVal = Value.ofUnit();
                for (Stmt hs : handler.body) {
                    retVal = evalStmt(hs, handlerLocals, targetDomain, targetDomain.domain.name + "." + handlerName, depth + 2);
                }

                eventId++;
                traceEvent("{\"event_id\":" + eventId + ",\"event\":\"handler_exit\",\"function\":\"" +
                        targetDomain.domain.name + "." + handlerName + "\",\"source_file\":\"" + s.sourceFile +
                        "\",\"line\":" + handler.line + ",\"parent_event_id\":" + handlerEnterId +
                        ",\"depth\":" + (depth + 2) + ",\"instance\":\"" + targetDomain.name +
                        "\",\"handler\":\"" + handlerName + "\"}");

                eventId++;
                traceEvent("{\"event_id\":" + eventId + ",\"event\":\"message_return\",\"function\":\"" + funcName +
                        "\",\"source_file\":\"" + s.sourceFile + "\",\"line\":" + s.line +
                        ",\"parent_event_id\":" + msgCallId + ",\"depth\":" + (depth + 1) +
                        ",\"detail\":\"" + retVal + "\",\"instance\":\"" + targetDomain.name +
                        "\",\"handler\":\"" + handlerName + "\"}");

                if (s.messageResult != null && !s.messageResult.isEmpty()) {
                    locals.put(s.messageResult, retVal);
                }
                return retVal;
            }
            case Reply, Return -> {
                if (s.a != null && !s.a.isEmpty()) {
                    return evalExpr(s.a, locals, currentDomain);
                }
                return Value.ofUnit();
            }
            case Raw -> {
                String text = s.text != null ? s.text.trim() : "";
                if (text.startsWith("echo ")) {
                    String sub = text.substring(5).trim();
                    stdout.println(evalExpr(sub, locals, currentDomain).toString());
                } else if (text.contains("=") && !text.contains("==")) {
                    int eq = text.indexOf('=');
                    String lhs = text.substring(0, eq).trim();
                    String rhs = text.substring(eq + 1).trim();
                    Value val = evalExpr(rhs, locals, currentDomain);
                    if (currentDomain != null && currentDomain.state.containsKey(lhs)) {
                        currentDomain.state.put(lhs, val);
                    } else {
                        locals.put(lhs, val);
                    }
                }
                return Value.ofUnit();
            }
            default -> {
                return Value.ofUnit();
            }
        }
    }

    private Value evalExpr(String expr, Map<String, Value> locals, DomainInstance currentDomain) {
        if (expr == null || expr.isEmpty()) return Value.ofUnit();
        expr = expr.trim();

        if (expr.startsWith("\"") && expr.endsWith("\"") && expr.length() >= 2) {
            return Value.ofString(expr.substring(1, expr.length() - 1));
        }
        if (expr.equals("true")) return Value.ofBool(true);
        if (expr.equals("false")) return Value.ofBool(false);
        if (expr.matches("^-?\\d+$")) {
            return Value.ofInt(Long.parseLong(expr));
        }
        if (expr.matches("^-?\\d+\\.\\d+$")) {
            return Value.ofFloat(Double.parseDouble(expr));
        }

        // Domain construction check e.g. Counter()
        int paren = expr.indexOf('(');
        if (paren > 0 && expr.endsWith(")")) {
            String callee = expr.substring(0, paren).trim();
            for (Domain d : program.domains) {
                if (d.name.equals(callee)) {
                    DomainInstance inst = new DomainInstance(d, "main::" + callee.toLowerCase());
                    for (Field f : d.state) {
                        String init = f.init != null && !f.init.isEmpty() ? f.init : "0";
                        inst.state.put(f.name, evalExpr(init, locals, null));
                    }
                    eventId++;
                    traceEvent("{\"event_id\":" + eventId + ",\"event\":\"domain_instance\",\"function\":\"main\",\"line\":" +
                            d.line + ",\"instance\":\"" + inst.name + "\"}");
                    return Value.ofDomain(inst);
                }
            }
        }

        // Binary operations e.g. value + amount
        if (expr.contains(" + ")) {
            String[] parts = expr.split(" \\+ ", 2);
            Value left = evalExpr(parts[0], locals, currentDomain);
            Value right = evalExpr(parts[1], locals, currentDomain);
            if (left.kind == Value.Kind.INT && right.kind == Value.Kind.INT) {
                return Value.ofInt(left.asInt() + right.asInt());
            }
            return Value.ofString(left.toString() + right.toString());
        }
        if (expr.contains(" - ")) {
            String[] parts = expr.split(" - ", 2);
            Value left = evalExpr(parts[0], locals, currentDomain);
            Value right = evalExpr(parts[1], locals, currentDomain);
            return Value.ofInt(left.asInt() - right.asInt());
        }
        if (expr.contains(" * ")) {
            String[] parts = expr.split(" \\* ", 2);
            Value left = evalExpr(parts[0], locals, currentDomain);
            Value right = evalExpr(parts[1], locals, currentDomain);
            return Value.ofInt(left.asInt() * right.asInt());
        }

        // Domain state read e.g. value
        if (currentDomain != null && currentDomain.state.containsKey(expr)) {
            eventId++;
            traceEvent("{\"event_id\":" + eventId + ",\"event\":\"state_read\",\"path\":\"" + expr + "\"}");
            return currentDomain.state.get(expr);
        }

        // Local variable read
        if (locals.containsKey(expr)) {
            eventId++;
            traceEvent("{\"event_id\":" + eventId + ",\"event\":\"LocalRead\",\"detail\":\"" + expr + "\"}");
            return locals.get(expr);
        }

        return Value.ofUnit();
    }
}
