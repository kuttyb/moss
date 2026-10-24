package org.mosslang.compiler.interpreter;

import org.mosslang.compiler.ast.*;
import java.io.PrintStream;
import java.util.*;

public class FastInterpreter {
    public static class Value {
        public enum Kind { INT, FLOAT, BOOL, STRING, UNIT, OBJECT, DOMAIN, VECTOR, MAP, QUEUE }
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
            if (kind == Kind.FLOAT) return formatFloat(((Number) raw).doubleValue());
            return raw.toString();
        }

        public static String formatFloat(double value) {
            if (Double.isNaN(value)) return "NaN";
            if (value == Double.POSITIVE_INFINITY) return "inf";
            if (value == Double.NEGATIVE_INFINITY) return "-inf";
            if (value == 0.0) {
                return (Double.doubleToRawLongBits(value) < 0) ? "-0.0" : "0.0";
            }
            java.math.BigDecimal bd = new java.math.BigDecimal(Double.toString(Math.abs(value))).stripTrailingZeros();
            String digits = bd.unscaledValue().toString();
            int exponent = digits.length() - 1 - bd.scale();
            StringBuilder output = new StringBuilder();
            if (Double.doubleToRawLongBits(value) < 0) output.append('-');
            if (exponent >= -4 && exponent < 16) {
                int decimalAt = exponent + 1;
                if (decimalAt <= 0) {
                    output.append("0.");
                    for (int i = 0; i < -decimalAt; i++) output.append('0');
                    output.append(digits);
                } else if (decimalAt >= digits.length()) {
                    output.append(digits);
                    for (int i = digits.length(); i < decimalAt; i++) output.append('0');
                    output.append(".0");
                } else {
                    output.append(digits, 0, decimalAt).append('.').append(digits.substring(decimalAt));
                }
            } else {
                output.append(digits.charAt(0));
                if (digits.length() > 1) {
                    output.append('.').append(digits.substring(1));
                }
                output.append('e').append(exponent);
            }
            return output.toString();
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

    private Value currentRaisedValue = null;
    private String currentRaisedVariant = "";

    public static class RaisedException extends RuntimeException {
        public final Value raisedValue;
        public final String variantName;
        public RaisedException(Value raisedValue, String variantName) {
            super(variantName != null && !variantName.isEmpty() ? variantName : (raisedValue != null ? raisedValue.toString() : "raised"));
            this.raisedValue = raisedValue;
            this.variantName = variantName != null ? variantName : "";
        }
        public RaisedException(Value raisedValue) {
            this(raisedValue, raisedValue != null ? raisedValue.toString() : "");
        }
    }

    public static class ReturnSignal extends RuntimeException {
        public final Value returnValue;
        public ReturnSignal(Value returnValue) {
            this.returnValue = returnValue;
        }
    }

    public void run() {
        if (program.main == null) return;
        Map<String, Value> locals = new HashMap<>();

        try {
            evalBlock(program.main.body, 0, program.main.body.size(), locals, null, "main", 0);
        } catch (ReturnSignal ignored) {}
    }

    private Value evalBlock(List<Stmt> stmts, int startIdx, int endIdx, Map<String, Value> locals,
                            DomainInstance currentDomain, String funcName, int depth) {
        Value lastVal = Value.ofUnit();
        int i = startIdx;
        while (i < endIdx) {
            Stmt s = stmts.get(i);
            if (s == null) {
                i++;
                continue;
            }
            if (s.kind == Stmt.Kind.If) {
                int baseInd = s.indent;
                int thenEnd = i + 1;
                while (thenEnd < endIdx && stmts.get(thenEnd).indent > baseInd) {
                    thenEnd++;
                }
                int elseStart = -1;
                int elseEnd = thenEnd;
                if (thenEnd < endIdx && stmts.get(thenEnd).kind == Stmt.Kind.Else && stmts.get(thenEnd).indent == baseInd) {
                    elseStart = thenEnd + 1;
                    elseEnd = elseStart;
                    while (elseEnd < endIdx && stmts.get(elseEnd).indent > baseInd) {
                        elseEnd++;
                    }
                }
                Value cond = evalExpr(s.a, locals, currentDomain);
                if (cond.asBool()) {
                    lastVal = evalBlock(stmts, i + 1, thenEnd, locals, currentDomain, funcName, depth);
                } else if (elseStart != -1) {
                    lastVal = evalBlock(stmts, elseStart, elseEnd, locals, currentDomain, funcName, depth);
                }
                i = elseEnd;
                continue;
            } else if (s.kind == Stmt.Kind.Else) {
                int baseInd = s.indent;
                i++;
                while (i < endIdx && stmts.get(i).indent > baseInd) i++;
                continue;
            } else if (s.kind == Stmt.Kind.Try) {
                int baseInd = s.indent;
                int tryEnd = i + 1;
                while (tryEnd < endIdx && stmts.get(tryEnd).indent > baseInd) {
                    tryEnd++;
                }
                try {
                    lastVal = evalBlock(stmts, i + 1, tryEnd, locals, currentDomain, funcName, depth);
                    i = tryEnd;
                    while (i < endIdx && stmts.get(i).kind == Stmt.Kind.Recover && stmts.get(i).indent == baseInd) {
                        i++;
                        while (i < endIdx && stmts.get(i).indent > baseInd) i++;
                    }
                } catch (RaisedException rex) {
                    i = tryEnd;
                    boolean recovered = false;
                    while (i < endIdx && stmts.get(i).kind == Stmt.Kind.Recover && stmts.get(i).indent == baseInd) {
                        Stmt recStmt = stmts.get(i);
                        int recStart = i + 1;
                        int recEnd = recStart;
                        while (recEnd < endIdx && stmts.get(recEnd).indent > baseInd) {
                            recEnd++;
                        }
                        String pat = recStmt.a != null ? recStmt.a.trim() : "";
                        boolean armMatches = pat.isEmpty();
                        for (String subPat : pat.split(",")) {
                            subPat = subPat.trim();
                            String cleanSub = subPat.contains("(") ? subPat.substring(0, subPat.indexOf('(')).trim() : subPat;
                            if (cleanSub.isEmpty() || rex.variantName.equals(cleanSub) || rex.variantName.endsWith("." + cleanSub) || cleanSub.endsWith("." + rex.variantName) || rex.variantName.contains(cleanSub)) {
                                armMatches = true;
                                break;
                            }
                        }
                        if (!recovered && armMatches) {
                            recovered = true;
                            Value prevRaised = currentRaisedValue;
                            currentRaisedValue = rex.raisedValue;
                            String prevVariant = currentRaisedVariant;
                            currentRaisedVariant = rex.variantName;
                            try {
                                if (pat.contains("(") && pat.endsWith(")")) {
                                    String binding = pat.substring(pat.indexOf('(') + 1, pat.lastIndexOf(')')).trim();
                                    if (!binding.isEmpty() && rex.raisedValue != null) {
                                        locals.put(binding, rex.raisedValue);
                                    }
                                }
                                lastVal = evalBlock(stmts, recStart, recEnd, locals, currentDomain, funcName, depth);
                            } finally {
                                currentRaisedValue = prevRaised;
                                currentRaisedVariant = prevVariant;
                            }
                        }
                        i = recEnd;
                    }
                    if (!recovered) throw rex;
                }
                continue;
            } else if (s.kind == Stmt.Kind.Match) {
                int baseInd = s.indent;
                int matchEnd = i + 1;
                while (matchEnd < endIdx && stmts.get(matchEnd).indent > baseInd) {
                    matchEnd++;
                }
                Value scrut = evalExpr(s.a, locals, currentDomain);
                String scrutStr = scrut.toString();
                int ci = i + 1;
                boolean matched = false;
                while (ci < matchEnd) {
                    Stmt cs = stmts.get(ci);
                    if (cs.kind == Stmt.Kind.Case) {
                        int caseInd = cs.indent;
                        int caseBodyStart = ci + 1;
                        int caseBodyEnd = caseBodyStart;
                        while (caseBodyEnd < matchEnd && stmts.get(caseBodyEnd).indent > caseInd) {
                            caseBodyEnd++;
                        }
                        String pat = cs.a != null ? cs.a.trim() : "";
                        if (!matched && (scrutStr.equals(pat) || scrutStr.endsWith("." + pat) || scrutStr.endsWith("::" + pat) || scrutStr.contains(pat))) {
                            matched = true;
                            lastVal = evalBlock(stmts, caseBodyStart, caseBodyEnd, locals, currentDomain, funcName, depth);
                        }
                        ci = caseBodyEnd;
                    } else {
                        ci++;
                    }
                }
                i = matchEnd;
                continue;
            } else if (s.kind == Stmt.Kind.Raise) {
                String rExpr = s.a != null ? s.a.trim() : "";
                if (rExpr.isEmpty() || rExpr.equals("raise")) {
                    throw new RaisedException(currentRaisedValue != null ? currentRaisedValue : Value.ofString(currentRaisedVariant), currentRaisedVariant);
                }
                String vName = rExpr.contains("(") ? rExpr.substring(0, rExpr.indexOf('(')).trim() : rExpr;
                Value rVal = evalExpr(rExpr, locals, currentDomain);
                throw new RaisedException(rVal, vName);
            } else if (s.kind == Stmt.Kind.Reply || s.kind == Stmt.Kind.Return) {
                Value rVal = (s.a != null && !s.a.isEmpty()) ? evalExpr(s.a, locals, currentDomain) : Value.ofUnit();
                throw new ReturnSignal(rVal);
            }
            lastVal = evalStmt(s, locals, currentDomain, funcName, depth);
            i++;
        }
        return lastVal;
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
                return evalExpr(s.text, locals, currentDomain);
            }
            case Let, Var, Assign -> {
                String varName = s.a != null ? s.a.trim() : "";
                if (varName.contains(":") && !varName.contains("[")) {
                    varName = varName.split(":")[0].trim();
                }
                String exprStr = s.b != null ? s.b.trim() : "";
                Value val = evalExpr(exprStr, locals, currentDomain);
                if (varName.endsWith("]") && varName.contains("[")) {
                    int openB = varName.indexOf('[');
                    String baseName = varName.substring(0, openB).trim();
                    String keyExpr = varName.substring(openB + 1, varName.length() - 1).trim();
                    Value baseVal = locals.containsKey(baseName) ? locals.get(baseName) : (currentDomain != null ? currentDomain.state.get(baseName) : null);
                    Value keyVal = evalExpr(keyExpr, locals, currentDomain);
                    if (baseVal != null && baseVal.raw instanceof Map<?, ?> mp) {
                        @SuppressWarnings("unchecked")
                        Map<Object, Value> m = (Map<Object, Value>) mp;
                        m.put(keyVal.raw, val);
                    } else if (baseVal != null && baseVal.raw instanceof List<?> lst) {
                        @SuppressWarnings("unchecked")
                        List<Value> l = (List<Value>) lst;
                        l.set((int) keyVal.asInt(), val);
                    }
                    return Value.ofUnit();
                }
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
                try {
                    retVal = evalBlock(handler.body, 0, handler.body.size(), handlerLocals, targetDomain, targetDomain.domain.name + "." + handlerName, depth + 2);
                } catch (ReturnSignal rs) {
                    retVal = rs.returnValue;
                } catch (RaisedException rex) {
                    if (handler.failureArms != null && !handler.failureArms.isEmpty()) {
                        FailureArm arm = handler.failureArms.get(0);
                        try {
                            retVal = evalBlock(arm.body, 0, arm.body.size(), handlerLocals, targetDomain, targetDomain.domain.name + "." + handlerName, depth + 2);
                        } catch (ReturnSignal rs) {
                            retVal = rs.returnValue;
                        }
                    } else {
                        throw rex;
                    }
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

        // 0. "|>" pipeline
        String[] pipe = splitOperator(expr, "|>");
        if (pipe != null) {
            Value left = evalExpr(pipe[0], locals, currentDomain);
            String stage = pipe[1].trim();
            if (stage.equals("count")) {
                if (left.raw instanceof Collection<?> col) return Value.ofInt(col.size());
                if (left.raw instanceof Map<?, ?> mp) return Value.ofInt(mp.size());
                return Value.ofInt(0);
            }
            if (stage.equals("sum")) {
                long sum = 0;
                if (left.raw instanceof Collection<?> col) {
                    for (Object o : col) {
                        if (o instanceof Value v) sum += v.asInt();
                    }
                }
                return Value.ofInt(sum);
            }
            if (stage.startsWith("map(") && stage.endsWith(")")) {
                String fnName = stage.substring(4, stage.length() - 1).trim();
                List<Value> outList = new ArrayList<>();
                if (left.raw instanceof Collection<?> col) {
                    for (Object o : col) {
                        if (o instanceof Value v) {
                            Map<String, Value> tmpLocals = new HashMap<>(locals);
                            tmpLocals.put("__pipe_item", v);
                            outList.add(evalExpr(fnName + "(__pipe_item)", tmpLocals, currentDomain));
                        }
                    }
                }
                return new Value(Value.Kind.VECTOR, outList);
            }
            if (stage.startsWith("reduce(") && stage.endsWith(")")) {
                List<String> rArgs = splitArgs(stage.substring(7, stage.length() - 1).trim());
                Value acc = !rArgs.isEmpty() ? evalExpr(rArgs.get(0), locals, currentDomain) : Value.ofInt(0);
                String fnName = rArgs.size() >= 2 ? rArgs.get(1).trim() : "";
                if (left.raw instanceof Collection<?> col && !fnName.isEmpty()) {
                    for (Object o : col) {
                        if (o instanceof Value v) {
                            Map<String, Value> tmpLocals = new HashMap<>(locals);
                            tmpLocals.put("__pipe_acc", acc);
                            tmpLocals.put("__pipe_item", v);
                            acc = evalExpr(fnName + "(__pipe_acc, __pipe_item)", tmpLocals, currentDomain);
                        }
                    }
                }
                return acc;
            }
        }

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
                if (left.kind == Value.Kind.FLOAT || right.kind == Value.Kind.FLOAT) {
                    return Value.ofFloat(left.asFloat() + right.asFloat());
                }
                if (left.kind == Value.Kind.INT && right.kind == Value.Kind.INT) {
                    return Value.ofInt(left.asInt() + right.asInt());
                }
                return Value.ofString(left.toString() + right.toString());
            } else {
                if (left.kind == Value.Kind.FLOAT || right.kind == Value.Kind.FLOAT) {
                    return Value.ofFloat(left.asFloat() - right.asFloat());
                }
                return Value.ofInt(left.asInt() - right.asInt());
            }
        }
        // 10. "*", "/", "%"
        op = splitOperator(expr, "*", "/", "%");
        if (op != null) {
            Value left = evalExpr(op[0], locals, currentDomain);
            Value right = evalExpr(op[1], locals, currentDomain);
            if (op[2].equals("*")) {
                if (left.kind == Value.Kind.FLOAT || right.kind == Value.Kind.FLOAT) {
                    return Value.ofFloat(left.asFloat() * right.asFloat());
                }
                return Value.ofInt(left.asInt() * right.asInt());
            } else if (op[2].equals("/")) {
                if (left.kind == Value.Kind.FLOAT || right.kind == Value.Kind.FLOAT) {
                    return Value.ofFloat(left.asFloat() / right.asFloat());
                }
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
        if (expr.startsWith("-") && !expr.matches("^-?\\d+(\\.\\d+)?([eE][+-]?\\d+)?$")) {
            Value inner = evalExpr(expr.substring(1).trim(), locals, currentDomain);
            if (inner.kind == Value.Kind.FLOAT) {
                return Value.ofFloat(-inner.asFloat());
            }
            return Value.ofInt(-inner.asInt());
        }

        if (expr.startsWith("\"") && expr.endsWith("\"") && expr.length() >= 2) {
            return Value.ofString(expr.substring(1, expr.length() - 1));
        }
        if (expr.equals("true")) return Value.ofBool(true);
        if (expr.equals("false")) return Value.ofBool(false);
        if (expr.matches("^-?\\d+$")) {
            return Value.ofInt(Long.parseLong(expr));
        }
        if (expr.matches("^-?(\\d+\\.\\d+|\\d+[eE][+-]?\\d+|\\d+\\.\\d+[eE][+-]?\\d+)$")) {
            return Value.ofFloat(Double.parseDouble(expr));
        }

        if (expr.startsWith("message ")) {
            String inner = expr.substring(8).trim();
            int openP = inner.indexOf('(');
            int dot = inner.indexOf('.');
            if (dot > 0 && openP > dot && inner.endsWith(")")) {
                Stmt msgStmt = new Stmt(Stmt.Kind.Message, 0, 0, expr, "");
                msgStmt.a = inner.substring(0, dot).trim();
                msgStmt.b = inner.substring(dot + 1, openP).trim();
                String argsStr = inner.substring(openP + 1, inner.length() - 1).trim();
                msgStmt.args.addAll(splitArgs(argsStr));
                return evalStmt(msgStmt, locals, currentDomain, "main", 0);
            }
        }

        if (expr.startsWith("[") && expr.endsWith("]")) {
            List<String> items = splitArgs(expr.substring(1, expr.length() - 1).trim());
            List<Value> vec = new ArrayList<>();
            for (String item : items) {
                if (!item.isEmpty()) vec.add(evalExpr(item, locals, currentDomain));
            }
            return new Value(Value.Kind.VECTOR, vec);
        }

        if (expr.endsWith("]") && !expr.startsWith("[") && !expr.endsWith(")")) {
            int openB = expr.indexOf('[');
            if (openB > 0) {
                Value baseVal = evalExpr(expr.substring(0, openB).trim(), locals, currentDomain);
                Value idxVal = evalExpr(expr.substring(openB + 1, expr.length() - 1).trim(), locals, currentDomain);
                if (baseVal.raw instanceof List<?> lst) {
                    @SuppressWarnings("unchecked")
                    List<Value> l = (List<Value>) lst;
                    return l.get((int) idxVal.asInt());
                }
                if (baseVal.raw instanceof Map<?, ?> mp) {
                    @SuppressWarnings("unchecked")
                    Map<Object, Value> m = (Map<Object, Value>) mp;
                    return m.getOrDefault(idxVal.raw, Value.ofUnit());
                }
            }
        }

        // Function call: callee(args...)
        int p = -1;
        if (expr.endsWith(")")) {
            int depth = 0;
            boolean inQ = false;
            for (int i = expr.length() - 1; i >= 0; i--) {
                char c = expr.charAt(i);
                if (c == '"' && (i == 0 || expr.charAt(i - 1) != '\\')) inQ = !inQ;
                if (!inQ) {
                    if (c == ')') depth++;
                    else if (c == '(') {
                        depth--;
                        if (depth == 0) {
                            p = i;
                            break;
                        }
                    }
                }
            }
        }
        if (p > 0 && expr.endsWith(")")) {
            String callee = expr.substring(0, p).trim();
            if (callee.equals("FileIO.open") || expr.contains("FileIO.open(")) {
                throw new org.mosslang.compiler.diagnostics.CompileError(
                        0,
                        "Fast Debug FileIO execution is intentionally unsupported in Phase 20; run this program with the native backend",
                        "FAST_DEBUG_FILEIO_UNSUPPORTED");
            }
            String argsStr = expr.substring(p + 1, expr.length() - 1).trim();
            List<String> argList = splitArgs(argsStr);
            List<Value> evaluatedArgs = new ArrayList<>();
            for (String a : argList) {
                if (!a.isEmpty()) {
                    int colIdx = -1;
                    int pDepth = 0;
                    boolean inStr = false;
                    for (int ci = 0; ci < a.length(); ci++) {
                        char ch = a.charAt(ci);
                        if (ch == '"' && (ci == 0 || a.charAt(ci - 1) != '\\')) inStr = !inStr;
                        if (!inStr) {
                            if (ch == '(' || ch == '[' || ch == '{') pDepth++;
                            else if (ch == ')' || ch == ']' || ch == '}') pDepth--;
                            else if (ch == ':' && pDepth == 0) {
                                colIdx = ci;
                                break;
                            }
                        }
                    }
                    if (colIdx > 0) {
                        a = a.substring(colIdx + 1).trim();
                    }
                    evaluatedArgs.add(evalExpr(a, locals, currentDomain));
                }
            }

            if (callee.equals("Vector") || callee.startsWith("Vector[")) {
                return new Value(Value.Kind.VECTOR, new ArrayList<Value>());
            }
            if (callee.equals("Map") || callee.startsWith("Map[")) {
                return new Value(Value.Kind.MAP, new LinkedHashMap<Object, Value>());
            }
            if (callee.equals("Queue") || callee.startsWith("Queue[")) {
                return new Value(Value.Kind.QUEUE, new ArrayDeque<Value>());
            }

            if (callee.equals("sqrt") && !evaluatedArgs.isEmpty()) {
                return Value.ofFloat(Math.sqrt(evaluatedArgs.get(0).asFloat()));
            }

            // Domain construction check e.g. Counter()
            for (Domain d : program.domains) {
                if (d.name.equals(callee) || d.name.endsWith("__" + callee) || callee.endsWith("." + d.name)) {
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

            // Object construction check e.g. Carrier() or provider.Carrier() or provider.Gauge(value: 19)
            for (ObjectType obj : program.objects) {
                if (obj.name.equals(callee) || obj.name.endsWith("__" + callee) || callee.endsWith("." + obj.name)) {
                    Map<String, Value> fieldMap = new HashMap<>();
                    for (int i = 0; i < obj.fields.size() && i < evaluatedArgs.size(); i++) {
                        fieldMap.put(obj.fields.get(i).name, evaluatedArgs.get(i));
                    }
                    return new Value(Value.Kind.OBJECT, fieldMap);
                }
            }

            // Object or Builtin method call check e.g. carrier.Reset(error), x.length(), v.push(1), m.delete(...)
            if (callee.contains(".")) {
                int dot = callee.lastIndexOf('.');
                String recvName = callee.substring(0, dot).trim();
                String methodName = callee.substring(dot + 1).trim();
                Value recvVal = locals.containsKey(recvName) ? locals.get(recvName)
                        : (currentDomain != null && currentDomain.state.containsKey(recvName) ? currentDomain.state.get(recvName) : null);
                if (recvVal == null) {
                    try {
                        recvVal = evalExpr(recvName, locals, currentDomain);
                    } catch (Exception ignored) {}
                }
                if (recvVal != null) {
                    if (recvVal.kind == Value.Kind.STRING) {
                        if (methodName.equals("length") || methodName.equals("len") || methodName.equals("size")) {
                            return Value.ofInt(recvVal.asString().length());
                        }
                        if (methodName.equals("char_at") && !evaluatedArgs.isEmpty()) {
                            int idx = (int) evaluatedArgs.get(0).asInt();
                            return Value.ofString(String.valueOf(recvVal.asString().charAt(idx)));
                        }
                        if (methodName.equals("split") && !evaluatedArgs.isEmpty()) {
                            String sep = evaluatedArgs.get(0).asString();
                            List<Value> parts = new ArrayList<>();
                            for (String pt : recvVal.asString().split(java.util.regex.Pattern.quote(sep), -1)) {
                                parts.add(Value.ofString(pt));
                            }
                            return new Value(Value.Kind.VECTOR, parts);
                        }
                    } else if (recvVal.kind == Value.Kind.VECTOR && recvVal.raw instanceof List<?> lst) {
                        @SuppressWarnings("unchecked")
                        List<Value> l = (List<Value>) lst;
                        if (methodName.equals("push") && !evaluatedArgs.isEmpty()) {
                            l.add(evaluatedArgs.get(0));
                            return Value.ofUnit();
                        }
                        if (methodName.equals("pop")) {
                            return l.remove(l.size() - 1);
                        }
                        if (methodName.equals("length") || methodName.equals("len") || methodName.equals("size") || methodName.equals("count")) {
                            return Value.ofInt(l.size());
                        }
                    } else if (recvVal.kind == Value.Kind.QUEUE && recvVal.raw instanceof Deque<?> dq) {
                        @SuppressWarnings("unchecked")
                        Deque<Value> q = (Deque<Value>) dq;
                        if (methodName.equals("push") && !evaluatedArgs.isEmpty()) {
                            q.addLast(evaluatedArgs.get(0));
                            return Value.ofUnit();
                        }
                        if (methodName.equals("pop")) {
                            return q.removeFirst();
                        }
                    } else if (recvVal.kind == Value.Kind.MAP && recvVal.raw instanceof Map<?, ?> mp) {
                        @SuppressWarnings("unchecked")
                        Map<Object, Value> m = (Map<Object, Value>) mp;
                        if (methodName.equals("keys")) {
                            List<Value> keys = new ArrayList<>();
                            for (Object k : m.keySet()) {
                                if (k instanceof Long l) keys.add(Value.ofInt(l));
                                else if (k instanceof Boolean b) keys.add(Value.ofBool(b));
                                else keys.add(Value.ofString(String.valueOf(k)));
                            }
                            return new Value(Value.Kind.VECTOR, keys);
                        }
                        if (methodName.equals("values")) {
                            return new Value(Value.Kind.VECTOR, new ArrayList<>(m.values()));
                        }
                        if (methodName.equals("delete") && evaluatedArgs.size() >= 2) {
                            Object k = evaluatedArgs.get(0).raw;
                            Value fallback = evaluatedArgs.get(1);
                            boolean present = m.containsKey(k);
                            Value removed = present ? m.remove(k) : fallback;
                            if (argList.size() >= 3) {
                                String foundVar = argList.get(2).trim();
                                if (locals.containsKey(foundVar)) {
                                    locals.put(foundVar, Value.ofBool(present));
                                }
                            }
                            return removed;
                        }
                    }
                }
                if (recvVal != null && recvVal.kind == Value.Kind.OBJECT) {
                    for (ObjectType obj : program.objects) {
                        for (Method m : obj.methods) {
                            if (m.name.equals(methodName)) {
                                Map<String, Value> mLocals = new HashMap<>();
                                if (recvVal.raw instanceof Map<?, ?> fm) {
                                    for (Map.Entry<?, ?> entry : fm.entrySet()) {
                                        if (entry.getKey() instanceof String k && entry.getValue() instanceof Value v) {
                                            mLocals.put(k, v);
                                        }
                                    }
                                }
                                for (int i = 0; i < m.params.size() && i < evaluatedArgs.size(); i++) {
                                    mLocals.put(m.params.get(i).name, evaluatedArgs.get(i));
                                }
                                Value res = Value.ofUnit();
                                try {
                                    res = evalBlock(m.body, 0, m.body.size(), mLocals, null, m.name, 0);
                                    if (m.resultExpression != null && !m.resultExpression.isEmpty()) {
                                        res = evalExpr(m.resultExpression, mLocals, null);
                                    }
                                } catch (ReturnSignal rs) {
                                    res = rs.returnValue;
                                } finally {
                                    for (int i = 0; i < m.params.size() && i < argList.size(); i++) {
                                        String rawArg = argList.get(i).trim();
                                        String pName = m.params.get(i).name;
                                        if (locals.containsKey(rawArg) && mLocals.containsKey(pName)) {
                                            locals.put(rawArg, mLocals.get(pName));
                                        }
                                    }
                                }
                                return res;
                            }
                        }
                    }
                }
            }

            // Check program functions
            for (Function f : program.functions) {
                if (f.name.equals(callee) || f.name.endsWith("__" + callee) || callee.endsWith("." + f.name)
                        || (callee.contains(".") && f.name.equals(callee.replace('.', '_')))
                        || (callee.contains(".") && f.name.endsWith("__" + callee.substring(callee.lastIndexOf('.') + 1)))) {
                    Map<String, Value> fnLocals = new HashMap<>();
                    for (int i = 0; i < f.params.size() && i < evaluatedArgs.size(); i++) {
                        fnLocals.put(f.params.get(i).name, evaluatedArgs.get(i));
                    }
                    Value res = Value.ofUnit();
                    try {
                        res = evalBlock(f.body, 0, f.body.size(), fnLocals, null, f.name, 0);
                        if (f.resultExpression != null && !f.resultExpression.isEmpty()) {
                            res = evalExpr(f.resultExpression, fnLocals, null);
                        }
                    } catch (ReturnSignal rs) {
                        res = rs.returnValue;
                    } finally {
                        for (int i = 0; i < f.params.size() && i < argList.size(); i++) {
                            String rawArg = argList.get(i).trim();
                            String pName = f.params.get(i).name;
                            if (locals.containsKey(rawArg) && fnLocals.containsKey(pName)) {
                                locals.put(rawArg, fnLocals.get(pName));
                            }
                        }
                    }
                    return res;
                }
            }
            // Enum variant constructor with payload e.g. Fault.Bad(code: 17)
            if (callee.contains(".")) {
                int lastDot = callee.lastIndexOf('.');
                String variant = callee.substring(lastDot + 1).trim();
                if (!variant.isEmpty() && Character.isUpperCase(variant.charAt(0)) && !evaluatedArgs.isEmpty()) {
                    return evaluatedArgs.get(0);
                }
            }
        }
        if (expr.contains(".") && !expr.contains("(")) {
            int lastDot = expr.lastIndexOf('.');
            String variant = expr.substring(lastDot + 1).trim();
            if (!variant.isEmpty() && Character.isUpperCase(variant.charAt(0))) {
                return Value.ofString(expr);
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
