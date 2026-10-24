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
            case Call -> {
                String callee = s.a != null ? s.a.trim() : "";
                if (callee.equals("assert")) {
                    String condStr = !s.args.isEmpty() ? s.args.get(0) : "";
                    if (condStr.isEmpty() && s.text.contains("(") && s.text.endsWith(")")) {
                        condStr = s.text.substring(s.text.indexOf('(') + 1, s.text.lastIndexOf(')'));
                    }
                    Value cond = evalExpr(condStr, locals, currentDomain);
                    if (!cond.asBool()) {
                        throw new RuntimeException("Moss assertion failed at line " + s.line + ": " + s.text);
                    }
                    return Value.ofUnit();
                } else if (callee.equals("assertEqual")) {
                    Value v1 = evalExpr(s.args.get(0), locals, currentDomain);
                    Value v2 = evalExpr(s.args.get(1), locals, currentDomain);
                    if (!Objects.equals(v1.raw, v2.raw)) {
                        throw new RuntimeException("Moss assertion failed at line " + s.line + ": " + s.text + "; actual=" + v1 + ", expected=" + v2);
                    }
                    return Value.ofUnit();
                }
                // Dotted member call or ordinary function call
                if (s.b != null && !s.b.isEmpty()) {
                    // Receiver in s.a, method in s.b
                    Value recv = locals.get(s.a);
                    if (recv != null && recv.kind == Value.Kind.DOMAIN) {
                        // Forward to message handler
                        Stmt msg = new Stmt(Stmt.Kind.Message, s.line, s.indent, s.text, s.sourceFile);
                        msg.a = s.a;
                        msg.b = s.b;
                        msg.args = s.args;
                        return evalStmt(msg, locals, currentDomain, funcName, depth);
                    }
                }
                // Function call
                evalExpr(s.text, locals, currentDomain);
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
                if (text.startsWith("assert(") && text.endsWith(")")) {
                    String condStr = text.substring(7, text.length() - 1);
                    Value cond = evalExpr(condStr, locals, currentDomain);
                    if (!cond.asBool()) {
                        throw new RuntimeException("Moss assertion failed at line " + s.line + ": " + s.text);
                    }
                    return Value.ofUnit();
                } else if (text.startsWith("assert ") && !text.startsWith("assertEqual")) {
                    String condStr = text.substring(7).trim();
                    Value cond = evalExpr(condStr, locals, currentDomain);
                    if (!cond.asBool()) {
                        throw new RuntimeException("Moss assertion failed at line " + s.line + ": " + s.text);
                    }
                    return Value.ofUnit();
                } else if (text.startsWith("echo ")) {
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

    private static boolean hasOuterParens(String s) {
        if (s == null || s.length() < 2 || !s.startsWith("(") || !s.endsWith(")")) return false;
        int depth = 0;
        boolean inQuote = false;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '"' && (i == 0 || s.charAt(i - 1) != '\\')) {
                inQuote = !inQuote;
            } else if (!inQuote) {
                if (c == '(') depth++;
                else if (c == ')') {
                    depth--;
                    if (depth == 0 && i < s.length() - 1) return false;
                }
            }
        }
        return depth == 0;
    }

    private static String[] splitOperator(String input, String... operators) {
        int parens = 0, brackets = 0, braces = 0;
        boolean inQuote = false;
        for (int i = input.length() - 1; i >= 0; i--) {
            char c = input.charAt(i);
            if (c == '"' && (i == 0 || input.charAt(i - 1) != '\\')) {
                inQuote = !inQuote;
            }
            if (inQuote) continue;
            if (c == ')') parens++;
            else if (c == '(') parens--;
            else if (c == ']') brackets++;
            else if (c == '[') brackets--;
            else if (c == '}') braces++;
            else if (c == '{') braces--;
            if (parens > 0 || brackets > 0 || braces > 0) continue;

            for (String op : operators) {
                if (i + op.length() <= input.length() && input.regionMatches(i, op, 0, op.length())) {
                    if (op.equals("|")) {
                        if (i + 1 < input.length() && (input.charAt(i + 1) == '>' || input.charAt(i + 1) == '|' || input.charAt(i + 1) == '=')) continue;
                        if (i > 0 && input.charAt(i - 1) == '|') continue;
                    } else if (op.equals("&")) {
                        if (i + 1 < input.length() && (input.charAt(i + 1) == '&' || input.charAt(i + 1) == '=')) continue;
                        if (i > 0 && input.charAt(i - 1) == '&') continue;
                    } else if (op.equals("^")) {
                        if (i + 1 < input.length() && input.charAt(i + 1) == '=') continue;
                    } else if (op.equals("<")) {
                        if (i + 1 < input.length() && (input.charAt(i + 1) == '<' || input.charAt(i + 1) == '=')) continue;
                        if (i > 0 && input.charAt(i - 1) == '<') continue;
                    } else if (op.equals(">")) {
                        if (i + 1 < input.length() && (input.charAt(i + 1) == '>' || input.charAt(i + 1) == '=')) continue;
                        if (i > 0 && (input.charAt(i - 1) == '>' || input.charAt(i - 1) == '|')) continue;
                    } else if (op.equals("-") || op.equals("+")) {
                        int before = i - 1;
                        while (before >= 0 && Character.isWhitespace(input.charAt(before))) before--;
                        if (before < 0) continue;
                        char prev = input.charAt(before);
                        if (prev == '(' || prev == '[' || prev == '{' || prev == ',' || prev == ':' ||
                            prev == '+' || prev == '-' || prev == '*' || prev == '/' || prev == '%' ||
                            prev == '=' || prev == '<' || prev == '>' || prev == '!' || prev == '|' ||
                            prev == '&' || prev == '^' || prev == '~') {
                            continue;
                        }
                    }
                    return new String[]{input.substring(0, i).trim(), input.substring(i + op.length()).trim(), op};
                }
            }
        }
        return null;
    }

    private static List<String> splitArgs(String s) {
        List<String> list = new ArrayList<>();
        if (s == null || s.trim().isEmpty()) return list;
        int depth = 0;
        boolean inQuote = false;
        int start = 0;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '"' && (i == 0 || s.charAt(i - 1) != '\\')) inQuote = !inQuote;
            if (!inQuote) {
                if (c == '(' || c == '[' || c == '{') depth++;
                else if (c == ')' || c == ']' || c == '}') depth--;
                else if (c == ',' && depth == 0) {
                    list.add(s.substring(start, i).trim());
                    start = i + 1;
                }
            }
        }
        if (start < s.length()) {
            list.add(s.substring(start).trim());
        }
        return list;
    }

    private Value evalExpr(String expr, Map<String, Value> locals, DomainInstance currentDomain) {
        if (expr == null || expr.isEmpty()) return Value.ofUnit();
        expr = expr.trim();

        while (hasOuterParens(expr)) {
            expr = expr.substring(1, expr.length() - 1).trim();
        }
        if (expr.isEmpty()) return Value.ofUnit();

        // 1. " or "
        String[] op = splitOperator(expr, " or ");
        if (op != null) {
            Value left = evalExpr(op[0], locals, currentDomain);
            if (left.asBool()) return Value.ofBool(true);
            return Value.ofBool(evalExpr(op[1], locals, currentDomain).asBool());
        }
        // 2. " xor "
        op = splitOperator(expr, " xor ");
        if (op != null) {
            boolean left = evalExpr(op[0], locals, currentDomain).asBool();
            boolean right = evalExpr(op[1], locals, currentDomain).asBool();
            return Value.ofBool(left ^ right);
        }
        // 3. " and "
        op = splitOperator(expr, " and ");
        if (op != null) {
            Value left = evalExpr(op[0], locals, currentDomain);
            if (!left.asBool()) return Value.ofBool(false);
            return Value.ofBool(evalExpr(op[1], locals, currentDomain).asBool());
        }
        // 4. Comparisons: "==", "!=", "<=", ">=", "<", ">"
        op = splitOperator(expr, "==", "!=", "<=", ">=", "<", ">");
        if (op != null) {
            Value left = evalExpr(op[0], locals, currentDomain);
            Value right = evalExpr(op[1], locals, currentDomain);
            String oper = op[2];
            switch (oper) {
                case "==" -> {
                    return Value.ofBool(Objects.equals(left.raw, right.raw));
                }
                case "!=" -> {
                    return Value.ofBool(!Objects.equals(left.raw, right.raw));
                }
                case "<=" -> {
                    return Value.ofBool(left.asInt() <= right.asInt());
                }
                case ">=" -> {
                    return Value.ofBool(left.asInt() >= right.asInt());
                }
                case "<" -> {
                    return Value.ofBool(left.asInt() < right.asInt());
                }
                case ">" -> {
                    return Value.ofBool(left.asInt() > right.asInt());
                }
            }
        }
        // 5. "|"
        op = splitOperator(expr, "|");
        if (op != null) {
            Value left = evalExpr(op[0], locals, currentDomain);
            Value right = evalExpr(op[1], locals, currentDomain);
            return Value.ofInt(left.asInt() | right.asInt());
        }
        // 6. "^"
        op = splitOperator(expr, "^");
        if (op != null) {
            Value left = evalExpr(op[0], locals, currentDomain);
            Value right = evalExpr(op[1], locals, currentDomain);
            return Value.ofInt(left.asInt() ^ right.asInt());
        }
        // 7. "&"
        op = splitOperator(expr, "&");
        if (op != null) {
            Value left = evalExpr(op[0], locals, currentDomain);
            Value right = evalExpr(op[1], locals, currentDomain);
            return Value.ofInt(left.asInt() & right.asInt());
        }
        // 8. "<<", ">>"
        op = splitOperator(expr, "<<", ">>");
        if (op != null) {
            Value left = evalExpr(op[0], locals, currentDomain);
            Value right = evalExpr(op[1], locals, currentDomain);
            if (op[2].equals("<<")) {
                long shift = right.asInt();
                if (shift < 0 || shift >= 64) return Value.ofInt(0);
                return Value.ofInt(left.asInt() << (int) shift);
            } else {
                long shift = right.asInt();
                if (shift < 0 || shift >= 64) return Value.ofInt(left.asInt() < 0 ? -1 : 0);
                return Value.ofInt(left.asInt() >> (int) shift);
            }
        }
        // 9. "+", "-"
        op = splitOperator(expr, "+", "-");
        if (op != null) {
            Value left = evalExpr(op[0], locals, currentDomain);
            Value right = evalExpr(op[1], locals, currentDomain);
            if (op[2].equals("+")) {
                if (left.kind == Value.Kind.INT && right.kind == Value.Kind.INT) {
                    return Value.ofInt(left.asInt() + right.asInt());
                }
                return Value.ofString(left.toString() + right.toString());
            } else {
                return Value.ofInt(left.asInt() - right.asInt());
            }
        }
        // 10. "*", "/", "%"
        op = splitOperator(expr, "*", "/", "%");
        if (op != null) {
            Value left = evalExpr(op[0], locals, currentDomain);
            Value right = evalExpr(op[1], locals, currentDomain);
            if (op[2].equals("*")) {
                return Value.ofInt(left.asInt() * right.asInt());
            } else if (op[2].equals("/")) {
                return Value.ofInt(left.asInt() / right.asInt());
            } else {
                return Value.ofInt(left.asInt() % right.asInt());
            }
        }

        // Unaries:
        if (expr.startsWith("not ") || expr.startsWith("not(")) {
            String sub = expr.startsWith("not ") ? expr.substring(4) : expr.substring(3);
            return Value.ofBool(!evalExpr(sub.trim(), locals, currentDomain).asBool());
        }
        if (expr.startsWith("~")) {
            return Value.ofInt(~evalExpr(expr.substring(1).trim(), locals, currentDomain).asInt());
        }
        if (expr.startsWith("-") && !expr.matches("^-?\\d+(\\.\\d+)?$")) {
            return Value.ofInt(-evalExpr(expr.substring(1).trim(), locals, currentDomain).asInt());
        }

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

        // Function call: callee(args...)
        int p = expr.indexOf('(');
        if (p > 0 && expr.endsWith(")")) {
            String callee = expr.substring(0, p).trim();
            String argsStr = expr.substring(p + 1, expr.length() - 1).trim();
            List<String> argList = splitArgs(argsStr);
            List<Value> evaluatedArgs = new ArrayList<>();
            for (String a : argList) {
                if (!a.isEmpty()) {
                    evaluatedArgs.add(evalExpr(a, locals, currentDomain));
                }
            }

            // Domain construction check e.g. Counter()
            for (Domain d : program.domains) {
                if (d.name.equals(callee) || d.name.endsWith("__" + callee)) {
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

            // Check program functions
            for (Function f : program.functions) {
                if (f.name.equals(callee) || f.name.endsWith("__" + callee) || callee.endsWith("." + f.name) || (callee.contains(".") && f.name.equals(callee.replace('.', '_')))) {
                    Map<String, Value> fnLocals = new HashMap<>();
                    for (int i = 0; i < f.params.size() && i < evaluatedArgs.size(); i++) {
                        fnLocals.put(f.params.get(i).name, evaluatedArgs.get(i));
                    }
                    Value res = Value.ofUnit();
                    for (Stmt fs : f.body) {
                        res = evalStmt(fs, fnLocals, null, f.name, 0);
                        if (fs.kind == Stmt.Kind.Return || fs.kind == Stmt.Kind.Reply) break;
                    }
                    return res;
                }
            }
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
