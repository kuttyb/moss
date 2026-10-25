package org.mosslang.compiler.interpreter;

import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.semantics.ExpressionTyper;
import org.mosslang.compiler.types.AccessMode;
import java.io.PrintStream;
import java.util.*;

public class FastInterpreter {
    public static class Value {
        public enum Kind { INT, FLOAT, BOOL, STRING, CHAR, UNIT, OBJECT, DOMAIN, VECTOR, MAP, QUEUE, ENUM }
        public final Kind kind;
        public final Object raw;
        public String typeName = null;
        // Trace-only provenance for object values borrowed from domain state.
        // The runtime value still shares the same payload; this path only lets
        // Fast Debug report later reads at the point where the borrowed value
        // is actually used.
        public String traceStatePath = null;

        public Value(Kind kind, Object raw) {
            this.kind = kind;
            this.raw = raw;
        }

        public Value(Kind kind, Object raw, String typeName) {
            this.kind = kind;
            this.raw = raw;
            this.typeName = typeName;
        }

        public static Value ofInt(long v) { return new Value(Kind.INT, v); }
        public static Value ofFloat(double v) { return new Value(Kind.FLOAT, v); }
        public static Value ofBool(boolean v) { return new Value(Kind.BOOL, v); }
        public static Value ofString(String v) { return new Value(Kind.STRING, v); }
        public static Value ofChar(int v) { return new Value(Kind.CHAR, v); }
        public static Value ofUnit() { return new Value(Kind.UNIT, null); }
        public static Value ofDomain(DomainInstance d) { return new Value(Kind.DOMAIN, d); }
        public static Value ofEnum(String enumName, String variant, Map<String, Value> fields) {
            return new Value(Kind.ENUM, new EnumValue(enumName, variant, fields), enumName);
        }

        public long asInt() { return ((Number) raw).longValue(); }
        public double asFloat() { return ((Number) raw).doubleValue(); }
        public boolean asBool() { return (Boolean) raw; }
        public String asString() { return raw != null ? raw.toString() : ""; }
        public DomainInstance asDomain() { return (DomainInstance) raw; }

        @Override
        public String toString() {
            if (raw == null) return "()";
            if (raw instanceof EnumValue enumValue) return enumValue.toString();
            if (kind == Kind.BOOL) return (Boolean) raw ? "true" : "false";
            if (kind == Kind.CHAR) return new String(Character.toChars(((Number) raw).intValue()));
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

    private static final class EnumValue {
        final String enumName;
        final String variant;
        final Map<String, Value> fields;

        EnumValue(String enumName, String variant, Map<String, Value> fields) {
            this.enumName = enumName;
            this.variant = variant;
            this.fields = new LinkedHashMap<>(fields);
        }

        @Override public String toString() {
            if (fields.isEmpty()) return enumName + "." + variant;
            return enumName + "." + variant + fields;
        }
    }

    private static boolean valuesEqual(Value left, Value right) {
        boolean leftNumber = left.kind == Value.Kind.INT || left.kind == Value.Kind.FLOAT;
        boolean rightNumber = right.kind == Value.Kind.INT || right.kind == Value.Kind.FLOAT;
        if (leftNumber && rightNumber) {
            return left.kind == Value.Kind.FLOAT || right.kind == Value.Kind.FLOAT
                    ? left.asFloat() == right.asFloat()
                    : left.asInt() == right.asInt();
        }
        if (left.kind != right.kind) return false;
        if (left.kind == Value.Kind.VECTOR) {
            if (!(left.raw instanceof List<?> leftItems)
                    || !(right.raw instanceof List<?> rightItems)
                    || leftItems.size() != rightItems.size()) return false;
            for (int index = 0; index < leftItems.size(); index++) {
                if (!(leftItems.get(index) instanceof Value leftItem)
                        || !(rightItems.get(index) instanceof Value rightItem)
                        || !valuesEqual(leftItem, rightItem)) return false;
            }
            return true;
        }
        if (left.kind == Value.Kind.QUEUE) {
            if (!(left.raw instanceof Deque<?> leftQueue)
                    || !(right.raw instanceof Deque<?> rightQueue)
                    || leftQueue.size() != rightQueue.size()) return false;
            Iterator<?> leftItems = leftQueue.iterator();
            Iterator<?> rightItems = rightQueue.iterator();
            while (leftItems.hasNext()) {
                if (!(leftItems.next() instanceof Value leftItem)
                        || !(rightItems.next() instanceof Value rightItem)
                        || !valuesEqual(leftItem, rightItem)) return false;
            }
            return true;
        }
        if (left.kind == Value.Kind.MAP || left.kind == Value.Kind.OBJECT) {
            if (!(left.raw instanceof Map<?, ?> leftFields)
                    || !(right.raw instanceof Map<?, ?> rightFields)
                    || (left.kind == Value.Kind.OBJECT && !Objects.equals(left.typeName, right.typeName))
                    || leftFields.size() != rightFields.size()) return false;
            for (Map.Entry<?, ?> entry : leftFields.entrySet()) {
                if (!rightFields.containsKey(entry.getKey())) return false;
                Object leftValue = entry.getValue();
                Object rightValue = rightFields.get(entry.getKey());
                if (leftValue instanceof Value leftItem && rightValue instanceof Value rightItem) {
                    if (!valuesEqual(leftItem, rightItem)) return false;
                } else if (!Objects.equals(leftValue, rightValue)) {
                    return false;
                }
            }
            return true;
        }
        if (left.kind == Value.Kind.ENUM
                && left.raw instanceof EnumValue leftEnum
                && right.raw instanceof EnumValue rightEnum) {
            return Objects.equals(leftEnum.enumName, rightEnum.enumName)
                    && Objects.equals(leftEnum.variant, rightEnum.variant)
                    && valuesEqual(leftEnum.fields, rightEnum.fields);
        }
        return Objects.equals(left.raw, right.raw);
    }

    private static boolean valuesEqual(Map<String, Value> left, Map<String, Value> right) {
        if (!left.keySet().equals(right.keySet())) return false;
        for (String key : left.keySet()) {
            if (!valuesEqual(left.get(key), right.get(key))) return false;
        }
        return true;
    }

    private Value evalSortKey(String functionName, Value value, Map<String, Value> locals,
                              DomainInstance currentDomain) {
        Function function = program.functions.stream()
                .filter(candidate -> candidate.name.equals(functionName)).findFirst().orElse(null);
        if (function == null || function.params.isEmpty()) return Value.ofUnit();
        String temporary = "__moss_sort_key_argument";
        boolean hadPrevious = locals.containsKey(temporary);
        Value previous = locals.get(temporary);
        locals.put(temporary, value);
        try {
            return evalExpr(function.name + "(" + temporary + ")", locals, currentDomain);
        } finally {
            if (hadPrevious) locals.put(temporary, previous);
            else locals.remove(temporary);
        }
    }

    private static int compareSortValues(Value left, Value right) {
        if (left.kind == Value.Kind.FLOAT || right.kind == Value.Kind.FLOAT) {
            double a = left.asFloat();
            double b = right.asFloat();
            if (mossFloatLess(a, b)) return -1;
            if (mossFloatLess(b, a)) return 1;
            return 0;
        }
        return switch (left.kind) {
            case INT -> Long.compare(left.asInt(), right.asInt());
            case BOOL -> Boolean.compare(left.asBool(), right.asBool());
            case CHAR -> Integer.compare((int) left.raw, (int) right.raw);
            case STRING -> compareCodePointStrings(left.asString(), right.asString());
            default -> throw new IllegalArgumentException("sort key is not a total-order scalar");
        };
    }

    private static boolean mossFloatLess(double left, double right) {
        if (Double.isNaN(left)) return false;
        if (Double.isNaN(right)) return true;
        if (left == 0.0 && right == 0.0)
            return Double.doubleToRawLongBits(left) < 0
                    && Double.doubleToRawLongBits(right) >= 0;
        return left < right;
    }

    private static int compareCodePointStrings(String left, String right) {
        int[] leftPoints = left.codePoints().toArray();
        int[] rightPoints = right.codePoints().toArray();
        int limit = Math.min(leftPoints.length, rightPoints.length);
        for (int index = 0; index < limit; index++) {
            int comparison = Integer.compare(leftPoints[index], rightPoints[index]);
            if (comparison != 0) return comparison;
        }
        return Integer.compare(leftPoints.length, rightPoints.length);
    }

    public static class DomainInstance {
        public final Domain domain;
        public String name;
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
    private String traceFunction = "main";
    private String traceSourceFile = "";
    private int traceLine = 1;
    private int traceDepth = 0;
    private final Deque<Integer> traceParentStack = new ArrayDeque<>();
    private final Deque<Integer> traceControlStack = new ArrayDeque<>();
    private final Deque<Integer> traceCallStack = new ArrayDeque<>();
    private final Deque<Integer> traceMessageStack = new ArrayDeque<>();
    private final Deque<Integer> traceHandlerStack = new ArrayDeque<>();
    private String traceInstance = "";
    private String traceSpecialization = "";
    private String traceHandler = "";
    private String traceCallableSpecialization = "";
    private String traceSemanticIdentity = "main@1";
    private boolean deferDomainStateTrace = false;
    private int deferredSourceArgumentReads = 0;

    private String traceIdentity() {
        return traceSemanticIdentity;
    }

    private void setTraceContext(Stmt statement, String function, int depth) {
        traceFunction = function;
        traceSourceFile = statement.sourceFile == null ? "" : statement.sourceFile;
        traceLine = statement.line;
        traceDepth = depth;
    }

    private String traceContextFields() {
        StringBuilder fields = new StringBuilder();
        if (!traceParentStack.isEmpty()) fields.append(",\"parent_event_id\":").append(traceParentStack.peek());
        if (!traceCallStack.isEmpty()) fields.append(",\"call_event_id\":").append(traceCallStack.peek());
        if (!traceMessageStack.isEmpty()) fields.append(",\"message_event_id\":").append(traceMessageStack.peek());
        if (!traceHandlerStack.isEmpty()) fields.append(",\"handler_event_id\":").append(traceHandlerStack.peek());
        if (!traceControlStack.isEmpty()) fields.append(",\"control_event_id\":").append(traceControlStack.peek());
        return fields.toString();
    }

    private String traceDomainFields() {
        if (traceInstance.isEmpty()) {
            return traceCallableSpecialization.isEmpty() ? ""
                    : ",\"specialization\":\"" + jsonEscape(traceCallableSpecialization) + "\"";
        }
        return ",\"instance\":\"" + jsonEscape(traceInstance) + "\",\"specialization\":\""
                + jsonEscape(traceSpecialization) + "\",\"handler\":\"" + jsonEscape(traceHandler) + "\"";
    }

    private void traceDomainConstruction(DomainInstance instance, String bindingName) {
        String normalizedName = bindingName.toLowerCase(Locale.ROOT);
        instance.name = "main::" + normalizedName;
        eventId++;
        traceEvent("{\"event_id\":" + eventId + ",\"event\":\"domain_instance\",\"function\":\""
                + jsonEscape(traceFunction) + "\",\"source_file\":\"" + jsonEscape(traceSourceFile)
                + "\",\"semantic_identity\":\"" + jsonEscape(traceIdentity()) + "\",\"line\":"
                + traceLine + traceContextFields() + ",\"depth\":" + traceDepth
                + ",\"instance\":\"" + jsonEscape(instance.name)
                + "\",\"specialization\":\"domain-specialization:" + jsonEscape(instance.domain.name)
                + ":" + jsonEscape(normalizedName) + "\"}");
    }

    private boolean isDirectDomainConstruction(String expression) {
        if (expression == null) return false;
        int open = expression.indexOf('(');
        if (open <= 0 || !expression.endsWith(")")) return false;
        String callee = expression.substring(0, open).trim();
        return program.domains.stream().anyMatch(domain -> domain.name.equals(callee)
                || domain.name.endsWith("__" + callee) || callee.endsWith("." + domain.name));
    }

    private static String specializationForCall(Function function, List<Value> args) {
        List<String> argumentTypes = new ArrayList<>();
        for (Value value : args) {
            argumentTypes.add(traceTypeOf(value));
        }
        boolean inferredContainerArgument = false;
        for (int index = 0; index < function.params.size() && index < argumentTypes.size(); index++) {
            String declared = function.params.get(index).type;
            String actual = argumentTypes.get(index);
            if (declared == null || actual == null) continue;
            String normalized = declared.toLowerCase(java.util.Locale.ROOT);
            if ((normalized.equals("vector") && actual.startsWith("vector["))
                    || (normalized.equals("queue") && actual.startsWith("queue["))) {
                inferredContainerArgument = true;
            }
        }
        if (!function.generic && !inferredContainerArgument) return "";
        for (FunctionSpecialization specialization : function.specializations) {
            if (specialization.parameterTypes.equals(argumentTypes)) {
                return "fn:" + function.name + "<" + String.join(",", argumentTypes) + ">@" + function.line;
            }
        }
        if (inferredContainerArgument) {
            return "fn:" + function.name + "<" + String.join(",", argumentTypes) + ">@" + function.line;
        }
        return "";
    }

    private static String traceTypeOf(Value value) {
        return switch (value.kind) {
            case INT -> "int";
            case FLOAT -> "float";
            case BOOL -> "bool";
            case STRING -> "string";
            case CHAR -> "char";
            case VECTOR -> "vector[" + collectionElementTraceType(value.raw) + "]";
            case MAP -> "map";
            case QUEUE -> "queue[" + collectionElementTraceType(value.raw) + "]";
            case OBJECT, ENUM -> value.typeName == null ? "" : value.typeName;
            case DOMAIN -> "domain";
            case UNIT -> "unit";
        };
    }

    private static String collectionElementTraceType(Object raw) {
        if (raw instanceof Collection<?> collection) {
            for (Object element : collection) {
                if (element instanceof Value value) return traceTypeOf(value);
            }
        }
        return "_";
    }

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

    private static String jsonEscape(String value) {
        StringBuilder escaped = new StringBuilder();
        for (int index = 0; index < value.length(); index++) {
            char c = value.charAt(index);
            switch (c) {
                case '"' -> escaped.append("\\\"");
                case '\\' -> escaped.append("\\\\");
                case '\b' -> escaped.append("\\b");
                case '\f' -> escaped.append("\\f");
                case '\n' -> escaped.append("\\n");
                case '\r' -> escaped.append("\\r");
                case '\t' -> escaped.append("\\t");
                default -> {
                    if (c < 0x20) escaped.append(String.format("\\u%04x", (int) c));
                    else escaped.append(c);
                }
            }
        }
        return escaped.toString();
    }

    private static String traceValueSummary(Value value) {
        return switch (value.kind) {
            case VECTOR -> "Vector(size=" + ((List<?>) value.raw).size() + ")";
            case QUEUE -> "Queue(size=" + ((Deque<?>) value.raw).size() + ")";
            case MAP -> "Map(size=" + ((Map<?, ?>) value.raw).size() + ")";
            case OBJECT -> value.typeName == null ? "<struct>" : value.typeName;
            case ENUM -> value.typeName == null ? "<enum>" : value.typeName;
            default -> value.toString();
        };
    }

    private static String traceWriteValueSummary(Value value) {
        if (value.kind == Value.Kind.ENUM && value.typeName != null) return value.typeName;
        return traceValueSummary(value);
    }

    private static String traceReturnValueSummary(Value value) {
        if (value.kind == Value.Kind.ENUM && value.raw instanceof EnumValue enumValue)
            return enumValue.enumName + "." + enumValue.variant;
        return traceValueSummary(value);
    }

    private static String raisedTraceIdentity(Value value, String fallback) {
        if (value != null && value.raw instanceof EnumValue enumValue)
            return "enum:" + enumValue.enumName + ":" + enumValue.variant;
        return fallback == null ? "" : fallback;
    }

    private Value evalPipelineCallback(String callback, Value item,
                                       Map<String, Value> locals, DomainInstance currentDomain) {
        Map<String, Value> callbackLocals = new HashMap<>(locals);
        String pipelineParameter = "__moss_pipeline_value";
        while (callbackLocals.containsKey(pipelineParameter)) pipelineParameter += "_";
        callbackLocals.put(pipelineParameter, item);
        callbackLocals.put("_", item);

        String target = callback.trim();
        if (target.matches("[A-Za-z_][A-Za-z0-9_]*")) {
            Value callable = callbackLocals.get(target);
            if (callable != null && callable.kind == Value.Kind.STRING
                    && callable.asString().startsWith("function:")) {
                target = callable.asString().substring("function:".length());
                return evalExpr(target + "(" + pipelineParameter + ")", callbackLocals, currentDomain);
            }
            String functionName = target;
            boolean namedFunction = program.functions.stream().anyMatch(function ->
                    (function.name.equals(functionName) || function.name.endsWith("__" + functionName)
                            || functionName.endsWith("." + function.name)) && function.params.size() == 1);
            if (namedFunction)
                return evalExpr(target + "(" + pipelineParameter + ")", callbackLocals, currentDomain);
        }
        if (!target.startsWith("_.")
                && target.matches("[A-Za-z_][A-Za-z0-9_]*\\.[A-Za-z_][A-Za-z0-9_]*"))
            return evalExpr(target + "(" + pipelineParameter + ")", callbackLocals, currentDomain);
        return evalExpr(target, callbackLocals, currentDomain);
    }

    private void traceLocalWrite(Stmt statement, String name, Value value, Value before,
                                 String function, int depth) {
        if (!trace) return;
        String after = traceWriteValueSummary(value);
        eventId++;
        traceEvent("{\"event_id\":" + eventId + ",\"event\":\"LocalWrite\",\"function\":\""
                + jsonEscape(function) + "\",\"source_file\":\"" + jsonEscape(statement.sourceFile)
                + "\",\"semantic_identity\":\"" + jsonEscape(traceIdentity()) + "\",\"line\":"
                + statement.line + traceContextFields() + ",\"depth\":" + depth + ",\"detail\":\""
                + jsonEscape(name) + "\"" + traceDomainFields()
                + (before == null ? "" : ",\"before\":\"" + jsonEscape(traceWriteValueSummary(before)) + "\"")
                + (after.isEmpty() ? "" : ",\"after\":\"" + jsonEscape(after) + "\"") + "}");
    }

    private void traceLocalWriteCurrent(String name, Value value, Value before) {
        if (!trace) return;
        String after = traceWriteValueSummary(value);
        eventId++;
        traceEvent("{\"event_id\":" + eventId + ",\"event\":\"LocalWrite\",\"function\":\""
                + jsonEscape(traceFunction) + "\",\"source_file\":\"" + jsonEscape(traceSourceFile)
                + "\",\"semantic_identity\":\"" + jsonEscape(traceIdentity()) + "\",\"line\":"
                + traceLine + traceContextFields() + ",\"depth\":" + traceDepth + ",\"detail\":\""
                + jsonEscape(name) + "\"" + traceDomainFields()
                + (before == null ? "" : ",\"before\":\"" + jsonEscape(traceWriteValueSummary(before)) + "\"")
                + (after.isEmpty() ? "" : ",\"after\":\"" + jsonEscape(after) + "\"") + "}");
    }

    private void traceLocalMutation(String name) {
        if (!trace) return;
        eventId++;
        traceEvent("{\"event_id\":" + eventId + ",\"event\":\"LocalWrite\",\"function\":\""
                + jsonEscape(traceFunction) + "\",\"source_file\":\"" + jsonEscape(traceSourceFile)
                + "\",\"semantic_identity\":\"" + jsonEscape(traceIdentity()) + "\",\"line\":"
                + traceLine + traceContextFields() + ",\"depth\":" + traceDepth + ",\"detail\":\""
                + jsonEscape(name) + "\"" + traceDomainFields() + "}");
    }

    private void traceStateMutation(String path, Value before, Value after) {
        if (!trace) return;
        eventId++;
        traceEvent("{\"event_id\":" + eventId + ",\"event\":\"state_write\",\"function\":\""
                + jsonEscape(traceFunction) + "\",\"source_file\":\"" + jsonEscape(traceSourceFile)
                + "\",\"semantic_identity\":\"" + jsonEscape(traceIdentity()) + "\",\"line\":"
                + traceLine + traceContextFields() + ",\"depth\":" + traceDepth + traceDomainFields()
                + ",\"path\":\"" + jsonEscape(path) + "\",\"before\":\""
                + jsonEscape(traceValueSummary(before)) + "\",\"after\":\""
                + jsonEscape(traceValueSummary(after)) + "\"}");
    }

    private void traceLocalRead(String name) {
        if (!trace || deferredSourceArgumentReads > 0) return;
        eventId++;
        traceEvent("{\"event_id\":" + eventId + ",\"event\":\"LocalRead\",\"function\":\""
                + jsonEscape(traceFunction) + "\",\"source_file\":\"" + jsonEscape(traceSourceFile)
                + "\",\"semantic_identity\":\"" + jsonEscape(traceIdentity()) + "\",\"line\":"
                + traceLine + traceContextFields() + ",\"depth\":" + traceDepth + ",\"detail\":\""
                + jsonEscape(name) + "\"" + traceDomainFields() + "}");
    }

    private void traceStateRead(String path) {
        if (!trace) return;
        eventId++;
        traceEvent("{\"event_id\":" + eventId + ",\"event\":\"state_read\",\"function\":\""
                + jsonEscape(traceFunction) + "\",\"source_file\":\"" + jsonEscape(traceSourceFile)
                + "\",\"semantic_identity\":\"" + jsonEscape(traceIdentity()) + "\",\"line\":"
                + traceLine + traceContextFields() + ",\"depth\":" + traceDepth + traceDomainFields()
                + ",\"path\":\"" + jsonEscape(path) + "\"}");
    }

    private void traceValueRead(String name, Value value, String member) {
        if (value != null && value.traceStatePath != null) {
            if (!deferDomainStateTrace)
                traceStateRead(value.traceStatePath + (member == null ? "" : "." + member));
        } else {
            traceLocalRead(member == null ? name : name + "." + member);
        }
    }

    private boolean isDomainStateRootedPath(String expression, Map<String, Value> locals,
                                            DomainInstance currentDomain) {
        if (expression == null || currentDomain == null) return false;
        String path = expression.trim();
        int separator = path.length();
        int dot = path.indexOf('.');
        int bracket = path.indexOf('[');
        if (dot >= 0) separator = Math.min(separator, dot);
        if (bracket >= 0) separator = Math.min(separator, bracket);
        String root = path.substring(0, separator).trim();
        return !root.isEmpty() && !locals.containsKey(root) && currentDomain.state.containsKey(root);
    }

    private static Value withTraceStatePath(Value value, String path) {
        if (value == null) return null;
        Value borrowed = new Value(value.kind, value.raw, value.typeName);
        borrowed.traceStatePath = path;
        return borrowed;
    }

    private static Value asLocalValue(Value value) {
        if (value == null || value.traceStatePath == null
                || value.kind == Value.Kind.OBJECT || value.kind == Value.Kind.VECTOR
                || value.kind == Value.Kind.MAP || value.kind == Value.Kind.QUEUE) return value;
        return new Value(value.kind, value.raw, value.typeName);
    }

    private static Value copyReturnedValue(Value value) {
        if (value == null) return Value.ofUnit();
        return switch (value.kind) {
            case OBJECT -> {
                Map<String, Value> fields = new LinkedHashMap<>();
                if (value.raw instanceof Map<?, ?> rawFields) {
                    for (Map.Entry<?, ?> entry : rawFields.entrySet()) {
                        if (entry.getKey() instanceof String name && entry.getValue() instanceof Value field)
                            fields.put(name, copyReturnedValue(field));
                    }
                }
                yield new Value(Value.Kind.OBJECT, fields, value.typeName);
            }
            case VECTOR -> {
                List<Value> items = new ArrayList<>();
                if (value.raw instanceof List<?> rawItems) {
                    for (Object item : rawItems)
                        items.add(item instanceof Value element ? copyReturnedValue(element) : Value.ofUnit());
                }
                yield new Value(Value.Kind.VECTOR, items, value.typeName);
            }
            case QUEUE -> {
                Deque<Value> items = new ArrayDeque<>();
                if (value.raw instanceof Deque<?> rawItems) {
                    for (Object item : rawItems)
                        items.addLast(item instanceof Value element ? copyReturnedValue(element) : Value.ofUnit());
                }
                yield new Value(Value.Kind.QUEUE, items, value.typeName);
            }
            case MAP -> {
                Map<Object, Value> items = new LinkedHashMap<>();
                if (value.raw instanceof Map<?, ?> rawItems) {
                    for (Map.Entry<?, ?> entry : rawItems.entrySet())
                        items.put(entry.getKey(), entry.getValue() instanceof Value element
                                ? copyReturnedValue(element) : Value.ofUnit());
                }
                yield new Value(Value.Kind.MAP, items, value.typeName);
            }
            case ENUM -> {
                if (value.raw instanceof EnumValue enumValue) {
                    Map<String, Value> fields = new LinkedHashMap<>();
                    for (Map.Entry<String, Value> entry : enumValue.fields.entrySet())
                        fields.put(entry.getKey(), copyReturnedValue(entry.getValue()));
                    yield Value.ofEnum(enumValue.enumName, enumValue.variant, fields);
                }
                yield new Value(value.kind, value.raw, value.typeName);
            }
            default -> new Value(value.kind, value.raw, value.typeName);
        };
    }

    private void traceLoopIteration(Stmt statement, String function, int iteration, int depth) {
        traceControlEvent(statement, function, "LoopIteration", Integer.toString(iteration), depth);
    }

    private void traceControlEvent(Stmt statement, String function, String event,
                                   String detail, int depth) {
        if (!trace) return;
        setTraceContext(statement, function, depth);
        eventId++;
        int controlEventId = eventId;
        traceEvent("{\"event_id\":" + controlEventId + ",\"event\":\"" + event
                + "\",\"function\":\""
                + jsonEscape(function) + "\",\"source_file\":\"" + jsonEscape(traceSourceFile)
                + "\",\"semantic_identity\":\"" + jsonEscape(traceIdentity()) + "\",\"line\":"
                + statement.line + traceContextFields() + ",\"depth\":" + depth
                + ",\"detail\":\"" + jsonEscape(detail) + "\"" + traceDomainFields() + "}");
        traceParentStack.push(controlEventId);
        traceControlStack.push(controlEventId);
    }

    private void traceBranchSelection(Stmt statement, String function,
                                      String detail, int depth) {
        if (!trace) return;
        setTraceContext(statement, function, depth);
        eventId++;
        traceEvent("{\"event_id\":" + eventId + ",\"event\":\"BranchTaken\",\"function\":\""
                + jsonEscape(function) + "\",\"source_file\":\"" + jsonEscape(traceSourceFile)
                + "\",\"semantic_identity\":\"" + jsonEscape(traceIdentity()) + "\",\"line\":"
                + statement.line + traceContextFields() + ",\"depth\":" + depth
                + ",\"detail\":\"" + jsonEscape(detail) + "\"" + traceDomainFields() + "}");
    }

    private void traceSimpleEvent(Stmt statement, String function, String event,
                                  String detail, int depth) {
        if (!trace) return;
        setTraceContext(statement, function, depth);
        eventId++;
        traceEvent("{\"event_id\":" + eventId + ",\"event\":\"" + event
                + "\",\"function\":\"" + jsonEscape(function)
                + "\",\"source_file\":\"" + jsonEscape(traceSourceFile)
                + "\",\"semantic_identity\":\"" + jsonEscape(traceIdentity())
                + "\",\"line\":" + statement.line + traceContextFields()
                + ",\"depth\":" + depth + ",\"detail\":\""
                + jsonEscape(detail) + "\"" + traceDomainFields() + "}");
    }

    private ExecutionError assertionFailure(Stmt statement, String function, int depth,
                                            String detail, String message) {
        traceSimpleEvent(statement, function, "AssertionFailure", detail, depth);
        return new ExecutionError(statement.line, message);
    }

    private int traceFunctionEvent(Function function, String event, int line) {
        return traceFunctionEvent(function, event, line, null);
    }

    private int traceFunctionEvent(Function function, String event, int line, String detail) {
        if (!trace) return 0;
        eventId++;
        String source = function.sourceFile == null ? "" : function.sourceFile;
        String identity = "fn:" + function.name + "@" + function.line;
        traceEvent("{\"event_id\":" + eventId + ",\"event\":\"" + event
                + "\",\"function\":\"" + jsonEscape(function.name)
                + "\",\"source_file\":\"" + jsonEscape(source)
                + "\",\"semantic_identity\":\"" + jsonEscape(identity)
                + "\",\"line\":" + line + traceContextFields()
                + ",\"depth\":" + traceParentStack.size()
                + (detail == null ? "" : ",\"detail\":\"" + jsonEscape(detail) + "\"")
                + traceDomainFields() + "}");
        return eventId;
    }

    private int traceMethodEvent(Method method, String event, int line, String detail) {
        if (!trace) return 0;
        eventId++;
        String function = method.owner + "." + method.name;
        String source = method.sourceFile == null ? "" : method.sourceFile;
        String identity = "method:" + function + "@" + method.line;
        traceEvent("{\"event_id\":" + eventId + ",\"event\":\"" + event
                + "\",\"function\":\"" + jsonEscape(function)
                + "\",\"source_file\":\"" + jsonEscape(source)
                + "\",\"semantic_identity\":\"" + jsonEscape(identity)
                + "\",\"line\":" + line + traceContextFields()
                + ",\"depth\":" + traceParentStack.size()
                + (detail == null ? "" : ",\"detail\":\"" + jsonEscape(detail) + "\"")
                + traceDomainFields() + "}");
        return eventId;
    }

    private void endTraceLoopIteration() {
        if (!trace) return;
        traceParentStack.pop();
        traceControlStack.pop();
    }

    private Value currentRaisedValue = null;
    private String currentRaisedVariant = "";

    public static class RaisedException extends RuntimeException {
        public final Value raisedValue;
        public final String variantName;
        public final Map<String, Value> edgeLocals;
        public RaisedException(Value raisedValue, String variantName) {
            this(raisedValue, variantName, Map.of());
        }
        public RaisedException(Value raisedValue, String variantName, Map<String, Value> edgeLocals) {
            super(variantName != null && !variantName.isEmpty() ? variantName : (raisedValue != null ? raisedValue.toString() : "raised"));
            this.raisedValue = raisedValue;
            this.variantName = variantName != null ? variantName : "";
            this.edgeLocals = new HashMap<>(edgeLocals);
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

    public static final class ExecutionError extends RuntimeException {
        public final int line;

        public ExecutionError(int line, String message) {
            super(message);
            this.line = line;
        }
    }

    public void run() {
        if (program.main == null) return;
        traceSemanticIdentity = "main@" + program.main.line;
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
            setTraceContext(s, funcName, depth);
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
                Set<String> joinedNames = new HashSet<>(blockDeclarations(stmts, i + 1, thenEnd));
                if (elseStart != -1) joinedNames.retainAll(blockDeclarations(stmts, elseStart, elseEnd));
                else joinedNames.clear();
                joinedNames.removeAll(locals.keySet());
                Value cond = evalExpr(s.a, locals, currentDomain);
                if (cond.asBool()) {
                    traceControlEvent(s, funcName, "BranchTaken", "then", depth);
                    try {
                        lastVal = evalJoinedScopedBlock(stmts, i + 1, thenEnd, locals,
                                currentDomain, funcName, depth + 1, joinedNames);
                    } finally {
                        endTraceLoopIteration();
                    }
                } else if (elseStart != -1) {
                    traceControlEvent(s, funcName, "BranchTaken", "else", depth);
                    try {
                        lastVal = evalJoinedScopedBlock(stmts, elseStart, elseEnd, locals,
                                currentDomain, funcName, depth + 1, joinedNames);
                    } finally {
                        endTraceLoopIteration();
                    }
                } else {
                    traceControlEvent(s, funcName, "BranchTaken", "else", depth);
                    endTraceLoopIteration();
                }
                i = elseEnd;
                continue;
            } else if (s.kind == Stmt.Kind.While) {
                int baseInd = s.indent;
                int bodyEnd = i + 1;
                while (bodyEnd < endIdx && stmts.get(bodyEnd).indent > baseInd) {
                    bodyEnd++;
                }
                int iterations = 0;
                while (true) {
                    setTraceContext(s, funcName, depth);
                    if (!evalExpr(s.a, locals, currentDomain).asBool()) break;
                    traceLoopIteration(s, funcName, ++iterations, depth);
                    try {
                        lastVal = evalScopedBlock(stmts, i + 1, bodyEnd, locals,
                                currentDomain, funcName, depth + 1, Set.of());
                    } finally {
                        endTraceLoopIteration();
                    }
                }
                i = bodyEnd;
                continue;
            } else if (s.kind == Stmt.Kind.For) {
                int baseInd = s.indent;
                int forEnd = i + 1;
                while (forEnd < endIdx && stmts.get(forEnd).indent > baseInd) {
                    forEnd++;
                }
                String loopVar = s.a != null ? s.a.trim() : "";
                String iterExpr = s.b != null ? s.b.trim() : "";
                List<Value> items = new ArrayList<>();
                if (iterExpr.startsWith("range(") && iterExpr.endsWith(")")) {
                    String inside = iterExpr.substring(6, iterExpr.length() - 1).trim();
                    List<String> rParts = splitArgs(inside);
                    long start = 0;
                    long end = 0;
                    long step = 1;
                    if (rParts.size() == 1) {
                        end = evalExpr(rParts.get(0).trim(), locals, currentDomain).asInt();
                    } else if (rParts.size() >= 2) {
                        start = evalExpr(rParts.get(0).trim(), locals, currentDomain).asInt();
                        end = evalExpr(rParts.get(1).trim(), locals, currentDomain).asInt();
                        if (rParts.size() >= 3) {
                            step = evalExpr(rParts.get(2).trim(), locals, currentDomain).asInt();
                        }
                    }
                    if (step > 0) {
                        for (long v = start; v < end; v += step) {
                            items.add(Value.ofInt(v));
                        }
                    } else if (step < 0) {
                        for (long v = start; v > end; v += step) {
                            items.add(Value.ofInt(v));
                        }
                    }
                } else {
                    Value iterVal = evalExpr(iterExpr, locals, currentDomain);
                    if (iterVal.raw instanceof List<?> lst) {
                        @SuppressWarnings("unchecked")
                        List<Value> l = (List<Value>) lst;
                        items.addAll(l);
                    } else if (iterVal.raw instanceof Map<?, ?> mp) {
                        @SuppressWarnings("unchecked")
                        Map<Object, Value> m = (Map<Object, Value>) mp;
                        for (Object k : m.keySet()) {
                            items.add(k instanceof Value ? (Value) k : Value.ofString(k.toString()));
                        }
                    }
                }
                int iterations = 0;
                for (Value itm : items) {
                    traceLoopIteration(s, funcName, ++iterations, depth);
                    try {
                        lastVal = evalScopedBlock(stmts, i + 1, forEnd, locals,
                                currentDomain, funcName, depth + 1, Set.of(loopVar), loopVar, itm);
                    } finally {
                        endTraceLoopIteration();
                    }
                }
                i = forEnd;
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
                Set<String> joinedNames = new HashSet<>(blockDeclarations(stmts, i + 1, tryEnd));
                // A branch-local declaration that shadows an existing binding
                // remains local, as it does in the native interpreter.
                joinedNames.removeAll(locals.keySet());
                int recoverScan = tryEnd;
                boolean sawRecover = false;
                while (recoverScan < endIdx && stmts.get(recoverScan).kind == Stmt.Kind.Recover && stmts.get(recoverScan).indent == baseInd) {
                    int armStart = recoverScan + 1;
                    int armEnd = armStart;
                    while (armEnd < endIdx && stmts.get(armEnd).indent > baseInd) armEnd++;
                    joinedNames.retainAll(blockDeclarations(stmts, armStart, armEnd));
                    sawRecover = true;
                    recoverScan = armEnd;
                }
                if (!sawRecover) joinedNames.clear();
                try {
                    lastVal = evalJoinedScopedBlock(stmts, i + 1, tryEnd, locals,
                            currentDomain, funcName, depth, joinedNames);
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
                        boolean errorBinding = pat.matches("[A-Za-z_][A-Za-z0-9_]*");
                        boolean armMatches = pat.isEmpty() || errorBinding;
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
                            traceSimpleEvent(recStmt, funcName, "recover",
                                    raisedTraceIdentity(rex.raisedValue, rex.variantName), depth);
                            Value prevRaised = currentRaisedValue;
                            currentRaisedValue = rex.raisedValue;
                            String prevVariant = currentRaisedVariant;
                            currentRaisedVariant = rex.variantName;
                            String recoveryBinding = null;
                            boolean hadRecoveryBinding = false;
                            Value previousRecoveryBinding = null;
                            try {
                                if (errorBinding && rex.raisedValue != null) {
                                    recoveryBinding = pat;
                                    hadRecoveryBinding = locals.containsKey(recoveryBinding);
                                    previousRecoveryBinding = locals.get(recoveryBinding);
                                    locals.put(recoveryBinding, rex.raisedValue);
                                } else if (pat.contains("(") && pat.endsWith(")")) {
                                    recoveryBinding = pat.substring(pat.indexOf('(') + 1,
                                            pat.lastIndexOf(')')).trim();
                                    if (!recoveryBinding.isEmpty() && rex.raisedValue != null) {
                                        hadRecoveryBinding = locals.containsKey(recoveryBinding);
                                        previousRecoveryBinding = locals.get(recoveryBinding);
                                        Value bindingValue = rex.raisedValue;
                                        if (bindingValue.kind == Value.Kind.ENUM
                                                && bindingValue.raw instanceof EnumValue enumValue) {
                                            Value payload = enumValue.fields.get(recoveryBinding);
                                            if (payload == null && enumValue.fields.size() == 1) {
                                                payload = enumValue.fields.values().iterator().next();
                                            }
                                            if (payload != null) bindingValue = payload;
                                        }
                                        locals.put(recoveryBinding, bindingValue);
                                    } else {
                                        recoveryBinding = null;
                                    }
                                }
                                lastVal = evalJoinedScopedBlock(stmts, recStart, recEnd, locals,
                                        currentDomain, funcName, depth, joinedNames);
                            } finally {
                                if (recoveryBinding != null) {
                                    if (hadRecoveryBinding) locals.put(recoveryBinding, previousRecoveryBinding);
                                    else locals.remove(recoveryBinding);
                                }
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
                EnumValue enumValue = scrut.raw instanceof EnumValue ev ? ev : null;
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
                        if (!matched && enumValue != null && enumValue.variant.equals(pat)) {
                            matched = true;
                            traceBranchSelection(cs, funcName, pat, depth);
                            Map<String, Value> branchLocals = new HashMap<>(locals);
                            for (String binder : cs.args) {
                                Value field = enumValue.fields.get(binder);
                                if (field == null) {
                                    throw new IllegalStateException("enum payload " + enumValue.enumName + "." +
                                            enumValue.variant + " is missing field '" + binder + "'");
                                }
                                branchLocals.put(binder, field);
                            }
                            lastVal = evalScopedBlock(stmts, caseBodyStart, caseBodyEnd, branchLocals,
                                    currentDomain, funcName, depth, Set.copyOf(cs.args));
                            for (Map.Entry<String, Value> entry : branchLocals.entrySet()) {
                                if (locals.containsKey(entry.getKey()) && !cs.args.contains(entry.getKey())) {
                                    locals.put(entry.getKey(), entry.getValue());
                                }
                            }
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
                    Value raised = currentRaisedValue != null ? currentRaisedValue : Value.ofString(currentRaisedVariant);
                    traceSimpleEvent(s, funcName, "raise", raisedTraceIdentity(raised, currentRaisedVariant), depth);
                    throw new RaisedException(raised, currentRaisedVariant, locals);
                }
                String vName = rExpr.contains("(") ? rExpr.substring(0, rExpr.indexOf('(')).trim() : rExpr;
                Value rVal = evalExpr(rExpr, locals, currentDomain);
                traceSimpleEvent(s, funcName, "raise", raisedTraceIdentity(rVal, vName), depth);
                throw new RaisedException(rVal, vName, locals);
            } else if (s.kind == Stmt.Kind.Reply || s.kind == Stmt.Kind.Return) {
                Value rVal = (s.a != null && !s.a.isEmpty()) ? evalExpr(s.a, locals, currentDomain) : Value.ofUnit();
                traceSimpleEvent(s, funcName, s.kind == Stmt.Kind.Reply ? "reply" : "Return",
                        s.kind == Stmt.Kind.Reply ? traceValueSummary(rVal) : traceReturnValueSummary(rVal), depth);
                throw new ReturnSignal(rVal);
            }
            lastVal = evalStmt(s, locals, currentDomain, funcName, depth);
            i++;
        }
        return lastVal;
    }

    private Value evalScopedBlock(List<Stmt> statements, int start, int end,
                                  Map<String, Value> parentLocals,
                                  DomainInstance currentDomain, String function,
                                  int depth, Set<String> additionalShadows) {
        return evalScopedBlock(statements, start, end, parentLocals,
                currentDomain, function, depth, additionalShadows, null, null);
    }

    private Value evalJoinedScopedBlock(List<Stmt> statements, int start, int end,
                                        Map<String, Value> parentLocals,
                                        DomainInstance currentDomain, String function,
                                        int depth, Set<String> joinedNames) {
        Map<String, Value> scopedLocals = new HashMap<>(parentLocals);
        Set<String> declarations = blockDeclarations(statements, start, end);
        try {
            Value result = evalBlock(statements, start, end, scopedLocals, currentDomain, function, depth);
            for (String name : joinedNames) {
                if (declarations.contains(name) && scopedLocals.containsKey(name)) parentLocals.put(name, scopedLocals.get(name));
            }
            return result;
        } finally {
            for (String name : new ArrayList<>(parentLocals.keySet())) {
                if (!declarations.contains(name)) {
                    if (scopedLocals.containsKey(name)) parentLocals.put(name, scopedLocals.get(name));
                    else parentLocals.remove(name);
                }
            }
        }
    }

    private Value evalScopedBlock(List<Stmt> statements, int start, int end,
                                  Map<String, Value> parentLocals,
                                  DomainInstance currentDomain, String function,
                                  int depth, Set<String> additionalShadows,
                                  String scopedName, Value scopedValue) {
        Map<String, Value> scopedLocals = new HashMap<>(parentLocals);
        Set<String> shadowed = new HashSet<>(additionalShadows);
        shadowed.addAll(blockDeclarations(statements, start, end));
        if (scopedName != null) {
            scopedLocals.put(scopedName, scopedValue);
            shadowed.add(scopedName);
        }
        try {
            return evalBlock(statements, start, end, scopedLocals,
                    currentDomain, function, depth);
        } catch (RaisedException raised) {
            for (String name : blockDeclarations(statements, start, end)) {
                Value value = scopedLocals.get(name);
                if (value != null) raised.edgeLocals.putIfAbsent(name, value);
            }
            throw raised;
        } finally {
            for (String name : new ArrayList<>(parentLocals.keySet())) {
                if (shadowed.contains(name)) continue;
                if (scopedLocals.containsKey(name)) parentLocals.put(name, scopedLocals.get(name));
                else parentLocals.remove(name);
            }
        }
    }

    private static Set<String> blockDeclarations(List<Stmt> statements, int start, int end) {
        if (start >= end) return Set.of();
        int baseIndent = statements.get(start).indent;
        Set<String> declarations = new HashSet<>();
        for (int index = start; index < end; index++) {
            Stmt statement = statements.get(index);
            if (statement.indent != baseIndent) continue;
            if (statement.kind == Stmt.Kind.Let || statement.kind == Stmt.Kind.Var
                    || statement.kind == Stmt.Kind.For) {
                String name = statement.a == null ? "" : statement.a.trim();
                int typeSeparator = name.indexOf(':');
                if (typeSeparator >= 0) name = name.substring(0, typeSeparator).trim();
                if (!name.isEmpty()) declarations.add(name);
            }
        }
        return declarations;
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
                        throw assertionFailure(s, funcName, depth, condStr, "assertion failed: " + condStr);
                    }
                    return Value.ofUnit();
                } else if (callee.equals("assertEqual")) {
                    Value v1 = evalExpr(s.args.get(0), locals, currentDomain);
                    Value v2 = evalExpr(s.args.get(1), locals, currentDomain);
                    if (!valuesEqual(v1, v2)) {
                        throw assertionFailure(s, funcName, depth, s.text,
                                "assertEqual failed: actual=" + v1 + ", expected=" + v2);
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
                if (val.kind == Value.Kind.DOMAIN && "main".equals(funcName)
                        && isDirectDomainConstruction(exprStr)) {
                    traceDomainConstruction(val.asDomain(), varName);
                }
                if (varName.endsWith("]") && varName.contains("[")) {
                    int openB = varName.indexOf('[');
                    String baseName = varName.substring(0, openB).trim();
                    String keyExpr = varName.substring(openB + 1, varName.length() - 1).trim();
                    Value baseVal = evalExpr(baseName, locals, currentDomain);
                    Value keyVal = evalExpr(keyExpr, locals, currentDomain);
                    if (baseVal != null && baseVal.raw instanceof Map<?, ?> mp) {
                        @SuppressWarnings("unchecked")
                        Map<Object, Value> m = (Map<Object, Value>) mp;
                        m.put(keyVal.raw, val);
                    } else if (baseVal != null && baseVal.raw instanceof List<?> lst) {
                        @SuppressWarnings("unchecked")
                        List<Value> l = (List<Value>) lst;
                        int index = (int) keyVal.asInt();
                        Value before = l.set(index, val);
                        if (baseVal.traceStatePath != null) {
                            eventId++;
                            traceEvent("{\"event_id\":" + eventId + ",\"event\":\"state_write\",\"function\":\""
                                    + jsonEscape(funcName) + "\",\"source_file\":\"" + jsonEscape(s.sourceFile)
                                    + "\",\"semantic_identity\":\"" + jsonEscape(traceIdentity()) + "\",\"line\":"
                                    + s.line + traceContextFields() + ",\"depth\":" + depth + traceDomainFields()
                                    + ",\"path\":\"" + jsonEscape(baseVal.traceStatePath + "[" + index + "]")
                                    + "\",\"before\":\"" + jsonEscape(traceValueSummary(before))
                                    + "\",\"after\":\"" + jsonEscape(traceValueSummary(val)) + "\"}");
                        }
                    }
                    return Value.ofUnit();
                }
                if (varName.contains(".") && !varName.contains("[")) {
                    String[] pathParts = varName.split("\\.");
                    String rootName = pathParts[0].trim();
                    Value rootValue = locals.containsKey(rootName) ? locals.get(rootName)
                            : (currentDomain != null ? currentDomain.state.get(rootName) : null);
                    Object target = rootValue == null ? null : rootValue.raw;
                    for (int part = 1; target instanceof Map<?, ?> fields && part < pathParts.length - 1; part++) {
                        Object child = fields.get(pathParts[part].trim());
                        target = child instanceof Value fieldValue ? fieldValue.raw : null;
                    }
                    if (target instanceof Map<?, ?> fields && pathParts.length > 1) {
                        @SuppressWarnings("unchecked")
                        Map<String, Value> writableFields = (Map<String, Value>) fields;
                        String fieldName = pathParts[pathParts.length - 1].trim();
                        Value before = writableFields.put(fieldName, val);
                        String statePath = rootValue.traceStatePath != null
                                ? rootValue.traceStatePath + "." + String.join(".", Arrays.copyOfRange(pathParts, 1, pathParts.length))
                                : currentDomain != null && currentDomain.state.containsKey(rootName) ? varName : null;
                        if (statePath != null) {
                            eventId++;
                            traceEvent("{\"event_id\":" + eventId + ",\"event\":\"state_write\",\"function\":\""
                                    + jsonEscape(funcName) + "\",\"source_file\":\"" + jsonEscape(s.sourceFile)
                                    + "\",\"semantic_identity\":\"" + jsonEscape(traceIdentity()) + "\",\"line\":"
                                    + s.line + traceContextFields() + ",\"depth\":" + depth + traceDomainFields()
                                    + ",\"path\":\"" + jsonEscape(statePath)
                                    + "\",\"before\":\"" + jsonEscape(traceValueSummary(before))
                                    + "\",\"after\":\"" + jsonEscape(traceValueSummary(val)) + "\"}");
                        } else traceLocalWriteCurrent(varName, val, before);
                    }
                    return Value.ofUnit();
                }
                if (currentDomain != null && currentDomain.state.containsKey(varName)) {
                    Value before = currentDomain.state.get(varName);
                    currentDomain.state.put(varName, val);
                    eventId++;
                    traceEvent("{\"event_id\":" + eventId + ",\"event\":\"state_write\",\"function\":\""
                            + jsonEscape(funcName) + "\",\"source_file\":\"" + jsonEscape(s.sourceFile)
                            + "\",\"semantic_identity\":\"" + jsonEscape(traceIdentity()) + "\",\"line\":" + s.line
                            + traceContextFields() + ",\"depth\":" + depth + traceDomainFields()
                            + ",\"path\":\"" + jsonEscape(varName) + "\",\"before\":\""
                            + jsonEscape(traceValueSummary(before)) + "\",\"after\":\""
                            + jsonEscape(traceValueSummary(val)) + "\"}");
                } else if (currentDomain == null
                        && locals.get("self") instanceof Value selfValue
                        && selfValue.kind == Value.Kind.OBJECT
                        && selfValue.raw instanceof Map<?, ?> fields
                        && fields.containsKey(varName)) {
                    @SuppressWarnings("unchecked")
                    Map<String, Value> objectFields = (Map<String, Value>) fields;
                    Value before = objectFields.get(varName);
                    objectFields.put(varName, val);
                    locals.put(varName, val);
                    traceLocalWrite(s, varName, val, before, funcName, depth);
                } else {
                    Value before = s.kind == Stmt.Kind.Assign ? locals.get(varName) : null;
                    locals.put(varName, asLocalValue(val));
                    if (val.kind != Value.Kind.DOMAIN)
                        traceLocalWrite(s, varName, val, before, funcName, depth);
                }
                return Value.ofUnit();
            }
            case Message -> {
                String targetName = s.a;
                String handlerName = s.b;
                Value targetVal = locals.get(targetName);
                if (targetVal == null && currentDomain != null) targetVal = currentDomain.state.get(targetName);
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

                if (locals.containsKey(targetName)) traceLocalRead(targetName);

                List<Value> args = new ArrayList<>();
                StringBuilder argDetails = new StringBuilder();
                for (int i = 0; i < s.args.size(); i++) {
                    if (i > 0) argDetails.append(", ");
                    Value v = evalExpr(s.args.get(i), locals, currentDomain);
                    args.add(copyReturnedValue(v));
                    argDetails.append(traceValueSummary(v));
                }

                eventId++;
                int msgCallId = eventId;
                traceEvent("{\"event_id\":" + msgCallId + ",\"event\":\"message_call\",\"function\":\""
                        + jsonEscape(funcName) + "\",\"source_file\":\"" + jsonEscape(s.sourceFile)
                        + "\",\"semantic_identity\":\"" + jsonEscape(traceIdentity()) + "\",\"line\":" + s.line
                        + traceContextFields() + ",\"depth\":" + depth
                        + (s.args.isEmpty() ? "" : ",\"detail\":\"" + jsonEscape(argDetails.toString()) + "\"")
                        + ",\"instance\":\"" + jsonEscape(targetDomain.name) + "\",\"specialization\":\"domain-specialization:"
                        + jsonEscape(targetDomain.domain.name) + ":" + jsonEscape(targetDomain.name.substring(targetDomain.name.lastIndexOf("::") + 2))
                        + "\",\"handler\":\"" + jsonEscape(handlerName) + "\"}");
                traceParentStack.push(msgCallId);
                traceMessageStack.push(msgCallId);

                eventId++;
                int handlerEnterId = eventId;
                String oldFunction = traceFunction, oldSource = traceSourceFile, oldIdentity = traceSemanticIdentity;
                String oldInstance = traceInstance, oldSpecialization = traceSpecialization, oldHandler = traceHandler;
                int oldLine = traceLine, oldDepth = traceDepth;
                String handlerFunction = targetDomain.domain.name + "." + handlerName;
                String handlerIdentity = "handler:" + handlerFunction + "@" + handler.line;
                traceEvent("{\"event_id\":" + handlerEnterId + ",\"event\":\"handler_enter\",\"function\":\""
                        + jsonEscape(handlerFunction) + "\",\"source_file\":\"" + jsonEscape(handler.sourceFile)
                        + "\",\"semantic_identity\":\"" + jsonEscape(handlerIdentity) + "\",\"line\":" + handler.line
                        + traceContextFields()
                        + ",\"depth\":" + (depth + 1) + ",\"instance\":\"" + jsonEscape(targetDomain.name)
                        + "\",\"specialization\":\"domain-specialization:" + jsonEscape(targetDomain.domain.name)
                        + ":" + jsonEscape(targetDomain.name.substring(targetDomain.name.lastIndexOf("::") + 2))
                        + "\",\"handler\":\"" + jsonEscape(handlerName) + "\"}");
                traceParentStack.push(handlerEnterId);
                traceCallStack.push(handlerEnterId);
                traceHandlerStack.push(handlerEnterId);
                traceFunction = handlerFunction;
                traceSourceFile = handler.sourceFile == null || handler.sourceFile.isEmpty() ? s.sourceFile : handler.sourceFile;
                traceSemanticIdentity = handlerIdentity;
                traceInstance = targetDomain.name;
                traceSpecialization = "domain-specialization:" + targetDomain.domain.name + ":"
                        + targetDomain.name.substring(targetDomain.name.lastIndexOf("::") + 2);
                traceHandler = handlerName;

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
                    if (!oldHandler.isEmpty()) {
                        // The nested handler propagates into its caller's
                        // try/recover. Unwind its trace context without
                        // inventing normal handler_exit/message_return events.
                        traceCallStack.pop();
                        traceParentStack.pop();
                        traceFunction = oldFunction;
                        traceSourceFile = oldSource;
                        traceSemanticIdentity = oldIdentity;
                        traceInstance = oldInstance;
                        traceSpecialization = oldSpecialization;
                        traceHandler = oldHandler;
                        traceLine = oldLine;
                        traceDepth = oldDepth;
                        traceParentStack.pop();
                        traceMessageStack.pop();
                        traceHandlerStack.pop();
                        throw rex;
                    }
                    if (oldHandler.isEmpty() && handler.failureArms != null && !handler.failureArms.isEmpty()) {
                        FailureArm arm = null;
                        for (FailureArm candidate : handler.failureArms) {
                            if (candidate.patterns.isEmpty()) { arm = candidate; break; }
                            for (String pattern : candidate.patterns) {
                                String normalized = pattern.replace("__", ".");
                                int payloadStart = normalized.indexOf('(');
                                if (payloadStart >= 0) normalized = normalized.substring(0, payloadStart).trim();
                                String variant = rex.raisedValue != null
                                        && rex.raisedValue.raw instanceof EnumValue enumValue
                                        ? enumValue.enumName + "." + enumValue.variant : rex.variantName;
                                String normalizedVariant = variant.replace("__", ".");
                                if (variant.equals(normalized) || normalizedVariant.equals(normalized)) {
                                    arm = candidate;
                                    break;
                                }
                            }
                            if (arm != null) break;
                        }
                        if (arm == null) throw rex;
                        for (String capture : arm.checkedCaptures.keySet()) {
                            Value captured = rex.edgeLocals.get(capture);
                            if (captured != null) handlerLocals.put(capture, captured);
                        }
                        String payloadBinding = arm.binding == null ? "" : arm.binding;
                        if (payloadBinding.isEmpty() && !arm.patterns.isEmpty()) {
                            String selectedPattern = arm.patterns.get(0);
                            int open = selectedPattern.indexOf('(');
                            int close = selectedPattern.lastIndexOf(')');
                            if (open >= 0 && close > open) payloadBinding = selectedPattern.substring(open + 1, close).trim();
                        }
                        if (!payloadBinding.isEmpty() && rex.raisedValue != null) {
                            Value bindingValue = rex.raisedValue;
                            if (bindingValue.raw instanceof EnumValue enumValue) {
                                Value payload = enumValue.fields.get(payloadBinding);
                                if (payload == null && enumValue.fields.size() == 1)
                                    payload = enumValue.fields.values().iterator().next();
                                if (payload != null) bindingValue = payload;
                            }
                            handlerLocals.put(payloadBinding, bindingValue);
                        }
                        String failFunction = targetDomain.domain.name + "." + handlerName + "$fail";
                        if (trace) {
                            eventId++;
                            traceEvent("{\"event_id\":" + eventId + ",\"event\":\"on_fail\",\"function\":\""
                                    + jsonEscape(failFunction) + "\",\"source_file\":\"" + jsonEscape(arm.sourceFile)
                                    + "\",\"semantic_identity\":\"" + jsonEscape(handlerIdentity)
                                    + "\",\"line\":" + arm.line + traceContextFields() + ",\"depth\":" + (depth + 2)
                                    + ",\"detail\":\"" + jsonEscape(raisedTraceIdentity(rex.raisedValue, rex.variantName))
                                    + "\"" + traceDomainFields() + "}");
                        }
                        String priorFunction = traceFunction;
                        traceFunction = failFunction;
                        try {
                            retVal = evalBlock(arm.body, 0, arm.body.size(), handlerLocals, targetDomain, failFunction, depth + 2);
                        } catch (ReturnSignal rs) {
                            retVal = rs.returnValue;
                        } finally {
                            traceFunction = priorFunction;
                        }
                    } else {
                        throw rex;
                    }
                }
                retVal = copyReturnedValue(retVal);

                eventId++;
                traceEvent("{\"event_id\":" + eventId + ",\"event\":\"handler_exit\",\"function\":\""
                        + jsonEscape(handlerFunction) + "\",\"source_file\":\"" + jsonEscape(traceSourceFile)
                        + "\",\"semantic_identity\":\"" + jsonEscape(handlerIdentity) + "\",\"line\":" + handler.line
                        + traceContextFields() + ",\"depth\":" + (depth + 2) + ",\"instance\":\""
                        + jsonEscape(targetDomain.name) + "\",\"specialization\":\"domain-specialization:"
                        + jsonEscape(targetDomain.domain.name) + ":" + jsonEscape(targetDomain.name.substring(targetDomain.name.lastIndexOf("::") + 2))
                        + "\",\"handler\":\"" + jsonEscape(handlerName) + "\"}");
                traceCallStack.pop();
                traceParentStack.pop();
                traceFunction = oldFunction;
                traceSourceFile = oldSource;
                traceSemanticIdentity = oldIdentity;
                traceInstance = oldInstance;
                traceSpecialization = oldSpecialization;
                traceHandler = oldHandler;
                traceLine = oldLine;
                traceDepth = oldDepth;

                eventId++;
                traceEvent("{\"event_id\":" + eventId + ",\"event\":\"message_return\",\"function\":\""
                        + jsonEscape(funcName) + "\",\"source_file\":\"" + jsonEscape(s.sourceFile)
                        + "\",\"semantic_identity\":\"" + jsonEscape(oldIdentity) + "\",\"line\":" + s.line
                        + traceContextFields() + ",\"depth\":" + (depth + 1)
                        + ",\"detail\":\"" + jsonEscape(traceValueSummary(retVal)) + "\",\"instance\":\""
                        + jsonEscape(targetDomain.name) + "\",\"specialization\":\"domain-specialization:"
                        + jsonEscape(targetDomain.domain.name) + ":" + jsonEscape(targetDomain.name.substring(targetDomain.name.lastIndexOf("::") + 2))
                        + "\",\"handler\":\"" + jsonEscape(handlerName) + "\"}");
                traceParentStack.pop();
                traceMessageStack.pop();
                traceHandlerStack.pop();

                if (s.messageResult != null && !s.messageResult.isEmpty()) {
                    Value before = locals.get(s.messageResult);
                    locals.put(s.messageResult, retVal);
                    traceLocalWrite(s, s.messageResult, retVal, before, funcName, depth);
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
                        throw assertionFailure(s, funcName, depth, condStr, "assertion failed: " + condStr);
                    }
                    return Value.ofUnit();
                } else if (text.startsWith("assert ") && !text.startsWith("assertEqual")) {
                    String condStr = text.substring(7).trim();
                    Value cond = evalExpr(condStr, locals, currentDomain);
                    if (!cond.asBool()) {
                        throw assertionFailure(s, funcName, depth, condStr, "assertion failed: " + condStr);
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

    private static String unescapeStringLiteral(String literal) {
        String body = literal.substring(1, literal.length() - 1);
        StringBuilder decoded = new StringBuilder(body.length());
        for (int index = 0; index < body.length();) {
            if (body.charAt(index) == '\\') {
                int[] codePoint = new int[1];
                int next = org.mosslang.compiler.parser.ParserSupport
                        .parseRustEscape(body, index, codePoint);
                if (next < 0) throw new IllegalArgumentException("invalid checked string escape");
                decoded.appendCodePoint(codePoint[0]);
                index = next;
            } else {
                int codePoint = body.codePointAt(index);
                decoded.appendCodePoint(codePoint);
                index += Character.charCount(codePoint);
            }
        }
        return decoded.toString();
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

    private static String asciiCase(String text, boolean upper) {
        StringBuilder result = new StringBuilder(text.length());
        for (int i = 0; i < text.length(); i++) {
            char ch = text.charAt(i);
            if (upper && ch >= 'a' && ch <= 'z') ch = (char) (ch - ('a' - 'A'));
            else if (!upper && ch >= 'A' && ch <= 'Z') ch = (char) (ch + ('a' - 'A'));
            result.append(ch);
        }
        return result.toString();
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

    private Value makeEnumValue(String qualifiedCase, List<String> rawArgs, List<Value> values) {
        int dot = qualifiedCase.lastIndexOf('.');
        if (dot < 0) return null;
        String enumQualifier = qualifiedCase.substring(0, dot);
        String variantName = qualifiedCase.substring(dot + 1);
        for (EnumType enumType : program.enums) {
            String declaredName = enumType.name;
            String shortName = declaredName.substring(declaredName.lastIndexOf('.') + 1);
            if (!(enumQualifier.equals(declaredName) || enumQualifier.endsWith("." + declaredName)
                    || enumQualifier.equals(shortName) || enumQualifier.endsWith("." + shortName)
                    || enumQualifier.endsWith("__" + shortName))) continue;
            for (EnumCase enumCase : enumType.cases) {
                if (!enumCase.name.equals(variantName)) continue;
                if (enumCase.fields.size() != values.size()) return null;
                Map<String, Value> fields = new LinkedHashMap<>();
                for (int i = 0; i < enumCase.fields.size(); i++) {
                    String fieldName = enumCase.fields.get(i).name;
                    if (i < rawArgs.size()) {
                        String raw = rawArgs.get(i).trim();
                        int colon = raw.indexOf(':');
                        if (colon > 0) {
                            String suppliedName = raw.substring(0, colon).trim();
                            if (!suppliedName.equals(fieldName)) return null;
                        }
                    }
                    fields.put(fieldName, values.get(i));
                }
                return Value.ofEnum(declaredName, variantName, fields);
            }
        }
        return null;
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
            if (stage.equals("any") || stage.equals("all")
                    || ((stage.startsWith("any(") || stage.startsWith("all(")) && stage.endsWith(")"))) {
                boolean any = stage.startsWith("any");
                boolean hasCallback = !stage.equals("any") && !stage.equals("all");
                String callback = hasCallback ? stage.substring(4, stage.length() - 1).trim() : "";
                boolean result = !any;
                if (left.raw instanceof Collection<?> col) {
                    for (Object o : col) {
                        if (!(o instanceof Value value)) continue;
                        boolean accepted = hasCallback
                                ? evalPipelineCallback(callback, value, locals, currentDomain).asBool()
                                : value.asBool();
                        result = any ? result || accepted : result && accepted;
                    }
                }
                return Value.ofBool(result);
            }
            if (stage.startsWith("map(") && stage.endsWith(")")) {
                String callback = stage.substring(4, stage.length() - 1).trim();
                List<Value> outList = new ArrayList<>();
                if (left.raw instanceof Collection<?> col) {
                    for (Object o : col) {
                        if (o instanceof Value v) {
                            outList.add(evalPipelineCallback(callback, v, locals, currentDomain));
                        }
                    }
                }
                return new Value(Value.Kind.VECTOR, outList);
            }
            if (stage.startsWith("filter(") && stage.endsWith(")")) {
                String callback = stage.substring(7, stage.length() - 1).trim();
                List<Value> outList = new ArrayList<>();
                if (left.raw instanceof Collection<?> col) {
                    for (Object o : col) {
                        if (!(o instanceof Value value)) continue;
                        Value accepted = evalPipelineCallback(callback, value, locals, currentDomain);
                        if (accepted.asBool()) outList.add(value);
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
            String stageName = stage;
            List<String> stageArguments = List.of();
            if (stage.endsWith(")")) {
                int stageOpen = stage.indexOf('(');
                if (stageOpen > 0) {
                    stageName = stage.substring(0, stageOpen).trim();
                    String rawArguments = stage.substring(stageOpen + 1, stage.length() - 1).trim();
                    stageArguments = rawArguments.isEmpty() ? List.of() : splitArgs(rawArguments);
                }
            }
            for (Function function : program.functions) {
                if (!(function.name.equals(stageName) || function.name.endsWith("__" + stageName)
                        || stageName.endsWith("." + function.name))
                        || function.params.size() != stageArguments.size() + 1) continue;
                Map<String, Value> fnLocals = new HashMap<>();
                String pipelineParameter = "__moss_pipeline_value";
                while (locals.containsKey(pipelineParameter)) pipelineParameter += "_";
                fnLocals.putAll(locals);
                fnLocals.put(pipelineParameter, left);
                StringBuilder call = new StringBuilder(function.name)
                        .append('(').append(pipelineParameter);
                for (String argument : stageArguments) call.append(", ").append(argument);
                call.append(')');
                return evalExpr(call.toString(), fnLocals, currentDomain);
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
            boolean floatingComparison = left.kind == Value.Kind.FLOAT
                    || right.kind == Value.Kind.FLOAT;
            double floatLeft = floatingComparison ? left.asFloat() : 0.0;
            double floatRight = floatingComparison ? right.asFloat() : 0.0;
            boolean equal = valuesEqual(left, right);
            switch (oper) {
                case "==" -> {
                    return Value.ofBool(equal);
                }
                case "!=" -> {
                    return Value.ofBool(!equal);
                }
                case "<=" -> {
                    return Value.ofBool(floatingComparison
                            ? floatLeft <= floatRight : left.asInt() <= right.asInt());
                }
                case ">=" -> {
                    return Value.ofBool(floatingComparison
                            ? floatLeft >= floatRight : left.asInt() >= right.asInt());
                }
                case "<" -> {
                    return Value.ofBool(floatingComparison
                            ? floatLeft < floatRight : left.asInt() < right.asInt());
                }
                case ">" -> {
                    return Value.ofBool(floatingComparison
                            ? floatLeft > floatRight : left.asInt() > right.asInt());
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
            return Value.ofString(unescapeStringLiteral(expr));
        }
        if (expr.startsWith("'") && expr.endsWith("'") && expr.length() >= 3) {
            String content = expr.substring(1, expr.length() - 1);
            if (content.startsWith("\\u{") && content.endsWith("}")) {
                return Value.ofChar(Integer.parseInt(content.substring(3, content.length() - 1), 16));
            }
            if (content.startsWith("\\")) {
                content = switch (content) {
                    case "\\n" -> "\n"; case "\\r" -> "\r"; case "\\t" -> "\t";
                    case "\\0" -> "\0"; case "\\\\" -> "\\"; case "\\'" -> "'";
                    default -> content.substring(1);
                };
            }
            int[] cps = content.codePoints().toArray();
            if (cps.length == 1) return Value.ofChar(cps[0]);
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
                Stmt msgStmt = new Stmt(Stmt.Kind.Message, traceLine, traceDepth, expr, traceSourceFile);
                msgStmt.a = inner.substring(0, dot).trim();
                msgStmt.b = inner.substring(dot + 1, openP).trim();
                String argsStr = inner.substring(openP + 1, inner.length() - 1).trim();
                msgStmt.args.addAll(splitArgs(argsStr));
                return evalStmt(msgStmt, locals, currentDomain, traceFunction, traceDepth);
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
            int closeB = expr.length() - 1;
            int depth = 0;
            int openB = -1;
            for (int i = closeB; i >= 0; i--) {
                char ch = expr.charAt(i);
                if (ch == ']') depth++;
                else if (ch == '[') {
                    depth--;
                    if (depth == 0) {
                        openB = i;
                        break;
                    }
                }
            }
            if (openB > 0) {
                String baseExpr = expr.substring(0, openB).trim();
                boolean localCollectionRead = baseExpr.matches("[A-Za-z_][A-Za-z0-9_]*")
                        && locals.containsKey(baseExpr);
                boolean wasDeferringStateTrace = deferDomainStateTrace;
                deferDomainStateTrace = true;
                Value baseVal;
                try {
                    baseVal = localCollectionRead ? locals.get(baseExpr)
                            : evalExpr(baseExpr, locals, currentDomain);
                } finally {
                    deferDomainStateTrace = wasDeferringStateTrace;
                }
                Value idxVal = evalExpr(expr.substring(openB + 1, closeB).trim(), locals, currentDomain);
                String indexedPath = baseVal.traceStatePath == null ? null
                        : baseVal.traceStatePath + "[" + idxVal + "]";
                if (baseVal.raw instanceof List<?> lst) {
                    @SuppressWarnings("unchecked")
                    List<Value> l = (List<Value>) lst;
                    Value item = l.get((int) idxVal.asInt());
                    if (localCollectionRead)
                        traceValueRead(baseExpr + "[" + idxVal + "]", item, null);
                    if (indexedPath == null) return item;
                    if (wasDeferringStateTrace) return withTraceStatePath(item, indexedPath);
                    traceStateRead(indexedPath);
                    return item;
                }
                if (baseVal.raw instanceof Map<?, ?> mp) {
                    @SuppressWarnings("unchecked")
                    Map<Object, Value> m = (Map<Object, Value>) mp;
                    Value item = m.getOrDefault(idxVal.raw, Value.ofUnit());
                    if (localCollectionRead)
                        traceValueRead(baseExpr + "[" + idxVal + "]", item, null);
                    if (indexedPath == null) return item;
                    if (wasDeferringStateTrace) return withTraceStatePath(item, indexedPath);
                    traceStateRead(indexedPath);
                    return item;
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
            String rawCallee = expr.substring(0, p).trim();
            String resolvedCallee = rawCallee;
            Value selfValue = locals.get("self");
            if (!rawCallee.contains(".") && selfValue != null && selfValue.kind == Value.Kind.OBJECT) {
                boolean topLevelFunction = false;
                for (Function function : program.functions) {
                    if (function.name.equals(rawCallee) || function.name.endsWith("__" + rawCallee)) {
                        topLevelFunction = true;
                        break;
                    }
                }
                boolean currentObjectMethod = false;
                for (ObjectType object : program.objects) {
                    if (selfValue.typeName != null && !selfValue.typeName.equals(object.name)) continue;
                    for (Method method : object.methods) {
                        if (method.name.equals(rawCallee)) currentObjectMethod = true;
                    }
                }
                if (currentObjectMethod && !topLevelFunction) resolvedCallee = "self." + rawCallee;
            }
            final String callee = resolvedCallee;
            boolean domainConstructor = program.domains.stream().anyMatch(d -> d.name.equals(callee)
                    || d.name.endsWith("__" + callee) || callee.endsWith("." + d.name));
            boolean sourceCallable = program.functions.stream().anyMatch(f -> f.name.equals(callee)
                    || f.name.endsWith("__" + callee) || callee.endsWith("." + f.name))
                    || program.objects.stream().flatMap(obj -> obj.methods.stream())
                    .anyMatch(method -> method.name.equals(callee.substring(callee.lastIndexOf('.') + 1)));
            if (callee.equals("FileIO.open") || expr.contains("FileIO.open(")) {
                throw new org.mosslang.compiler.diagnostics.CompileError(
                        0,
                        "Fast Debug FileIO execution is intentionally unsupported in Phase 20; run this program with the native backend",
                        "FAST_DEBUG_FILEIO_UNSUPPORTED");
            }
            String argsStr = expr.substring(p + 1, expr.length() - 1).trim();
            List<String> argList = splitArgs(argsStr);
            boolean builtinSort = "sort".equals(callee)
                    && program.functions.stream().noneMatch(function -> function.name.equals("sort"));
            boolean mapDeleteOutParameter = false;
            if (!expr.startsWith("message ") && callee.contains(".")) {
                int receiverDot = callee.lastIndexOf('.');
                String receiverName = callee.substring(0, receiverDot).trim();
                String methodName = callee.substring(receiverDot + 1).trim();
                Value receiverValue = locals.get(receiverName);
                if (receiverValue == null && currentDomain != null)
                    receiverValue = currentDomain.state.get(receiverName);
                if ("delete".equals(methodName) && receiverValue != null
                        && receiverValue.kind == Value.Kind.MAP && argList.size() >= 3) {
                    mapDeleteOutParameter = true;
                }
                boolean mutatingStringMethod = receiverValue != null
                        && receiverValue.kind == Value.Kind.STRING
                        && ("append".equals(methodName) || "reserve".equals(methodName));
                if (!mutatingStringMethod) {
                    if (locals.containsKey(receiverName))
                        traceValueRead(receiverName, locals.get(receiverName), null);
                    else if (currentDomain != null && currentDomain.state.containsKey(receiverName))
                        traceStateRead(receiverName);
                }
            }
            List<Value> evaluatedArgs = new ArrayList<>();
            boolean wasDeferringStateTrace = deferDomainStateTrace;
            int previousDeferredSourceArgumentReads = deferredSourceArgumentReads;
            if (sourceCallable) deferredSourceArgumentReads++;
            if (sourceCallable && currentDomain != null) deferDomainStateTrace = true;
            try {
            for (int argumentIndex = 0; argumentIndex < argList.size(); argumentIndex++) {
                String a = argList.get(argumentIndex);
                if (builtinSort && argumentIndex == 0) {
                    String target = a.trim();
                    Value targetValue = locals.get(target);
                    if (targetValue == null && currentDomain != null
                            && currentDomain.state.containsKey(target)) {
                        targetValue = withTraceStatePath(currentDomain.state.get(target), target);
                    }
                    if (targetValue != null) {
                        evaluatedArgs.add(targetValue);
                        continue;
                    }
                }
                if (builtinSort && argumentIndex == 1) {
                    evaluatedArgs.add(Value.ofUnit());
                    continue;
                }
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
                            else if (pDepth == 0 && (ch == ':' || (ch == '=' &&
                                    (ci + 1 >= a.length() || a.charAt(ci + 1) != '=') &&
                                    (ci == 0 || "=<>!".indexOf(a.charAt(ci - 1)) < 0)))) {
                                colIdx = ci;
                                break;
                            }
                        }
                    }
                    if (colIdx > 0) {
                        a = a.substring(colIdx + 1).trim();
                    }
                    if (mapDeleteOutParameter && argumentIndex == 2) {
                        evaluatedArgs.add(Value.ofUnit());
                    } else if ((sourceCallable || domainConstructor)
                            && a.matches("[A-Za-z_][A-Za-z0-9_]*") && locals.containsKey(a)) {
                        evaluatedArgs.add(locals.get(a));
                    } else {
                        evaluatedArgs.add(evalExpr(a, locals, currentDomain));
                    }
                }
            }
            } finally {
                deferDomainStateTrace = wasDeferringStateTrace;
                deferredSourceArgumentReads = previousDeferredSourceArgumentReads;
            }

            Value enumConstructor = makeEnumValue(callee, argList, evaluatedArgs);
            if (enumConstructor != null) return enumConstructor;

            if (builtinSort) {
                if (evaluatedArgs.isEmpty() || !(evaluatedArgs.get(0).raw instanceof List<?> rawValues))
                    return Value.ofUnit();
                @SuppressWarnings("unchecked")
                List<Value> values = (List<Value>) rawValues;
                String keyName = null;
                boolean reverse = false;
                if (argList.size() > 1) {
                    String named = argList.get(1).trim();
                    int colon = named.indexOf(':');
                    String key = colon < 0 ? named : named.substring(colon + 1).trim();
                    if (key.startsWith("Reverse(") && key.endsWith(")")) {
                        reverse = true;
                        key = key.substring(8, key.length() - 1).trim();
                    }
                    keyName = key;
                    for (Function function : program.functions) {
                        if (function.name.equals(keyName) || function.name.endsWith("__" + keyName)
                                || function.name.equals(keyName.replace('.', '_'))
                                || function.name.equals(keyName.replace(".", "__"))) {
                            keyName = function.name;
                            break;
                        }
                    }
                }
                final String resolvedKey = keyName;
                final boolean descending = reverse;
                values.sort((left, right) -> {
                    Value leftKey = resolvedKey == null ? left
                            : evalSortKey(resolvedKey, left, locals, currentDomain);
                    Value rightKey = resolvedKey == null ? right
                            : evalSortKey(resolvedKey, right, locals, currentDomain);
                    int comparison = compareSortValues(leftKey, rightKey);
                    return descending ? -comparison : comparison;
                });
                return Value.ofUnit();
            }

            String compactBuiltinCallee = callee.replaceAll("\\s+", "");
            if (compactBuiltinCallee.equals("Vector") || compactBuiltinCallee.startsWith("Vector[")) {
                return new Value(Value.Kind.VECTOR, new ArrayList<Value>());
            }
            if (compactBuiltinCallee.equals("Map") || compactBuiltinCallee.startsWith("Map[")) {
                return new Value(Value.Kind.MAP, new LinkedHashMap<Object, Value>());
            }
            if (compactBuiltinCallee.equals("Queue") || compactBuiltinCallee.startsWith("Queue[")) {
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
                    for (int i = 0; i < argList.size() && i < evaluatedArgs.size(); i++) {
                        String rawArg = argList.get(i);
                        int separator = rawArg.indexOf(':');
                        if (separator < 0) separator = rawArg.indexOf('=');
                        if (separator <= 0) continue;
                        String routeName = rawArg.substring(0, separator).trim();
                        if (d.routes.stream().anyMatch(route -> route.name.equals(routeName))
                                || d.state.stream().anyMatch(field -> field.name.equals(routeName))) {
                            inst.state.put(routeName, evaluatedArgs.get(i));
                        }
                    }
                    return Value.ofDomain(inst);
                }
            }

            // Object construction check e.g. Carrier() or provider.Carrier() or provider.Gauge(value: 19)
            for (ObjectType obj : program.objects) {
                if (obj.name.equals(callee) || obj.name.endsWith("__" + callee) || callee.endsWith("." + obj.name)) {
                    Map<String, Value> fieldMap = new HashMap<>();
                    for (Field f : obj.fields) {
                        if (f.init != null && !f.init.isEmpty()) {
                            fieldMap.put(f.name, evalExpr(f.init, locals, currentDomain));
                        } else {
                            fieldMap.put(f.name, Value.ofInt(0));
                        }
                    }
                    for (int i = 0; i < evaluatedArgs.size(); i++) {
                        String rawA = i < argList.size() ? argList.get(i) : "";
                        String namedField = null;
                        int pDepth = 0;
                        boolean inStr = false;
                        for (int ci = 0; ci < rawA.length(); ci++) {
                            char ch = rawA.charAt(ci);
                            if (ch == '"' && (ci == 0 || rawA.charAt(ci - 1) != '\\')) inStr = !inStr;
                            if (!inStr) {
                                if (ch == '(' || ch == '[' || ch == '{') pDepth++;
                                else if (ch == ')' || ch == ']' || ch == '}') pDepth--;
                                else if (pDepth == 0 && (ch == ':' || (ch == '=' &&
                                        (ci + 1 >= rawA.length() || rawA.charAt(ci + 1) != '=') &&
                                        (ci == 0 || "=<>!".indexOf(rawA.charAt(ci - 1)) < 0)))) {
                                    namedField = rawA.substring(0, ci).trim();
                                    break;
                                }
                            }
                        }
                        if (namedField != null && !namedField.isEmpty()) {
                            fieldMap.put(namedField, evaluatedArgs.get(i));
                        } else if (i < obj.fields.size()) {
                            fieldMap.put(obj.fields.get(i).name, evaluatedArgs.get(i));
                        }
                    }
                    return new Value(Value.Kind.OBJECT, fieldMap, obj.name);
                }
            }

            // Object or Builtin method call check e.g. carrier.Reset(error), x.length(), v.push(1), m.delete(...)
            if (callee.contains(".")) {
                int dot = callee.lastIndexOf('.');
                String recvName = callee.substring(0, dot).trim();
                String methodName = callee.substring(dot + 1).trim();
                Value recvVal = locals.containsKey(recvName) ? locals.get(recvName)
                        : (currentDomain != null && currentDomain.state.containsKey(recvName)
                        ? withTraceStatePath(currentDomain.state.get(recvName), recvName) : null);
                if (recvVal == null) {
                    try {
                        recvVal = evalExpr(recvName, locals, currentDomain);
                    } catch (Exception ignored) {}
                }
                if (recvVal != null) {
                    if (recvVal.kind == Value.Kind.STRING) {
                        if (methodName.equals("length") || methodName.equals("len") || methodName.equals("size")) {
                            return Value.ofInt(recvVal.asString().codePointCount(0, recvVal.asString().length()));
                        }
                        if (methodName.equals("char_at") && !evaluatedArgs.isEmpty()) {
                            int idx = (int) evaluatedArgs.get(0).asInt();
                            int[] cps = recvVal.asString().codePoints().toArray();
                            if (idx < 0 || idx >= cps.length) throw new IndexOutOfBoundsException("String character index out of bounds");
                            return Value.ofChar(cps[idx]);
                        }
                        if (methodName.equals("chars")) {
                            List<Value> chars = new ArrayList<>();
                            recvVal.asString().codePoints().forEach(cp -> chars.add(Value.ofChar(cp)));
                            return new Value(Value.Kind.VECTOR, chars);
                        }
                        if (methodName.equals("find") && !evaluatedArgs.isEmpty()) {
                            String value = recvVal.asString();
                            String pattern = evaluatedArgs.get(0).asString();
                            int byteIndex = value.indexOf(pattern);
                            return Value.ofInt(byteIndex < 0 ? -1 : value.codePointCount(0, byteIndex));
                        }
                        if (methodName.equals("slice") && evaluatedArgs.size() == 2) {
                            int[] cps = recvVal.asString().codePoints().toArray();
                            long start = evaluatedArgs.get(0).asInt(), length = evaluatedArgs.get(1).asInt();
                            if (start < 0 || length < 0 || start > cps.length || length > cps.length - start) return Value.ofString("");
                            return Value.ofString(new String(cps, (int) start, (int) length));
                        }
                        if (methodName.equals("ascii_lower")) return Value.ofString(asciiCase(recvVal.asString(), false));
                        if (methodName.equals("ascii_upper")) return Value.ofString(asciiCase(recvVal.asString(), true));
                        if (methodName.equals("append") && !evaluatedArgs.isEmpty()) {
                            Value appended = Value.ofString(recvVal.asString() + evaluatedArgs.get(0).asString());
                            if (locals.containsKey(recvName)) {
                                locals.put(recvName, appended);
                                if (recvVal.traceStatePath != null)
                                    traceStateMutation(recvVal.traceStatePath, recvVal, appended);
                                else traceLocalMutation(recvName);
                            } else if (currentDomain != null && currentDomain.state.containsKey(recvName)) {
                                currentDomain.state.put(recvName, appended);
                                traceStateMutation(recvName, recvVal, appended);
                            }
                            return Value.ofUnit();
                        }
                        if (methodName.equals("reserve")) {
                            if (locals.containsKey(recvName)) {
                                if (recvVal.traceStatePath != null)
                                    traceStateMutation(recvVal.traceStatePath, recvVal, recvVal);
                                else traceLocalMutation(recvName);
                            } else if (currentDomain != null && currentDomain.state.containsKey(recvName)) {
                                traceStateMutation(recvName, recvVal, recvVal);
                            }
                            return Value.ofUnit();
                        }
                        if (methodName.equals("split") && !evaluatedArgs.isEmpty()) {
                            String sep = evaluatedArgs.get(0).asString();
                            if (sep.isEmpty()) throw new IllegalArgumentException("String split requires a nonempty separator");
                            List<Value> parts = new ArrayList<>();
                            for (String pt : recvVal.asString().split(java.util.regex.Pattern.quote(sep), -1)) {
                                parts.add(Value.ofString(pt));
                            }
                            return new Value(Value.Kind.VECTOR, parts);
                        }
                        if (methodName.equals("join") && !evaluatedArgs.isEmpty() && evaluatedArgs.get(0).raw instanceof List<?> list) {
                            @SuppressWarnings("unchecked") List<Value> parts = (List<Value>) list;
                            List<String> strings = parts.stream().map(Value::asString).toList();
                            return Value.ofString(String.join(recvVal.asString(), strings));
                        }
                    } else if (recvVal.kind == Value.Kind.CHAR && methodName.equals("to_string")) {
                        return Value.ofString(recvVal.toString());
                    } else if (recvVal.kind == Value.Kind.CHAR && methodName.equals("codepoint")) {
                        return Value.ofInt(((Number) recvVal.raw).longValue());
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
                        if ((methodName.equals("len") || methodName.equals("size") || methodName.equals("count"))
                                && evaluatedArgs.isEmpty()) {
                            return Value.ofInt(m.size());
                        }
                        if ((methodName.equals("contains") || methodName.equals("hasKey"))
                                && evaluatedArgs.size() == 1) {
                            return Value.ofBool(m.containsKey(evaluatedArgs.get(0).raw));
                        }
                        if (methodName.equals("get") && evaluatedArgs.size() == 2) {
                            return m.getOrDefault(evaluatedArgs.get(0).raw, evaluatedArgs.get(1));
                        }
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
                                    Value before = locals.get(foundVar);
                                    Value foundValue = Value.ofBool(present);
                                    locals.put(foundVar, foundValue);
                                    traceLocalWriteCurrent(foundVar, foundValue, before);
                                }
                            }
                            return removed;
                        }
                    }
                }
                if (recvVal != null && recvVal.kind == Value.Kind.OBJECT) {
                    for (ObjectType obj : program.objects) {
                        if (recvVal.typeName != null && !recvVal.typeName.equals(obj.name)) {
                            continue;
                        }
                        for (Method m : obj.methods) {
                            if (m.name.equals(methodName)) {
                                Map<String, Value> mLocals = new HashMap<>();
                                if (recvVal.raw instanceof Map<?, ?> fm) {
                                    for (Map.Entry<?, ?> entry : fm.entrySet()) {
                                        if (entry.getKey() instanceof String k && entry.getValue() instanceof Value v) {
                                            mLocals.put(k, recvVal.traceStatePath == null ? v
                                                    : withTraceStatePath(v, recvVal.traceStatePath + "." + k));
                                        }
                                    }
                                }
                                mLocals.put("self", recvVal);
                                for (int i = 0; i < m.params.size() && i < evaluatedArgs.size(); i++) {
                                    mLocals.put(m.params.get(i).name, evaluatedArgs.get(i));
                                }
                                Value res = Value.ofUnit();
                                String oldFunction = traceFunction;
                                String oldSource = traceSourceFile;
                                String oldIdentity = traceSemanticIdentity;
                                String oldCallableSpecialization = traceCallableSpecialization;
                                int oldLine = traceLine;
                                int oldDepth = traceDepth;
                                int oldDeferredSourceArgumentReads = deferredSourceArgumentReads;
                                String methodFunction = m.owner + "." + m.name;
                                int enterId = traceMethodEvent(m, "MethodEnter", m.line, null);
                                if (trace && enterId != 0) {
                                    traceParentStack.push(enterId);
                                    traceCallStack.push(enterId);
                                }
                                traceFunction = methodFunction;
                                traceSourceFile = m.sourceFile == null ? "" : m.sourceFile;
                                traceSemanticIdentity = "method:" + methodFunction + "@" + m.line;
                                traceCallableSpecialization = "";
                                deferredSourceArgumentReads = 0;
                                traceLine = m.body.isEmpty() ? m.line : m.body.get(0).line;
                                boolean explicitReturn = false;
                                boolean completed = false;
                                try {
                                    res = evalBlock(m.body, 0, m.body.size(), mLocals, null, methodFunction,
                                            trace && enterId != 0 ? traceParentStack.size() : 0);
                                    if (m.resultExpression != null && !m.resultExpression.isEmpty()) {
                                        traceLine = m.resultLine > 0 ? m.resultLine : m.line;
                                        traceDepth = trace && enterId != 0 ? traceParentStack.size() : 0;
                                        res = evalExpr(m.resultExpression, mLocals, null);
                                    }
                                    completed = true;
                                } catch (ReturnSignal rs) {
                                    res = rs.returnValue;
                                    explicitReturn = true;
                                    completed = true;
                                } finally {
                                    if (completed) {
                                        if (!explicitReturn)
                                            traceMethodEvent(m, "Return", m.resultLine > 0 ? m.resultLine : m.line,
                                                    traceValueSummary(res));
                                        traceMethodEvent(m, "MethodExit", m.resultLine > 0 ? m.resultLine : m.line, null);
                                    }
                                    if (trace && enterId != 0) {
                                        traceCallStack.pop();
                                        traceParentStack.pop();
                                    }
                                    traceFunction = oldFunction;
                                    traceSourceFile = oldSource;
                                    traceSemanticIdentity = oldIdentity;
                                    traceCallableSpecialization = oldCallableSpecialization;
                                    traceLine = oldLine;
                                    traceDepth = oldDepth;
                                    deferredSourceArgumentReads = oldDeferredSourceArgumentReads;
                                    for (int i = 0; i < m.params.size() && i < argList.size(); i++) {
                                        String rawArg = argList.get(i).trim();
                                        String pName = m.params.get(i).name;
                                        AccessMode mode = i < m.parameterAccessModes.size()
                                                ? m.parameterAccessModes.get(i) : AccessMode.Read;
                                        if (locals.containsKey(rawArg) && mode == AccessMode.Consume) {
                                            locals.put(rawArg, Value.ofUnit());
                                        } else if (locals.containsKey(rawArg) && mode == AccessMode.Write
                                                && mLocals.containsKey(pName)) {
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

            String targetFn = callee;
            if ("replace".equals(callee) && argList.size() == 2) {
                Value replacement = evalExpr(argList.get(1), locals, currentDomain);
                String placeStr = argList.get(0).trim();
                if (placeStr.endsWith("]") && placeStr.contains("[")) {
                    int openB = ExpressionTyper.findMatchingOpenBracket(placeStr, placeStr.length() - 1);
                    String baseName = placeStr.substring(0, openB).trim();
                    String keyExpr = placeStr.substring(openB + 1, placeStr.length() - 1).trim();
                    Value baseVal = evalExpr(baseName, locals, currentDomain);
                    Value keyVal = evalExpr(keyExpr, locals, currentDomain);
                    if (baseVal != null && baseVal.raw instanceof List<?> lst) {
                        @SuppressWarnings("unchecked")
                        List<Value> l = (List<Value>) lst;
                        int idx = (int) keyVal.asInt();
                        Value old = l.get(idx);
                        l.set(idx, replacement);
                        return old;
                    } else if (baseVal != null && baseVal.raw instanceof Map<?, ?> mp) {
                        @SuppressWarnings("unchecked")
                        Map<Object, Value> m = (Map<Object, Value>) mp;
                        Value old = m.get(keyVal.raw);
                        m.put(keyVal.raw, replacement);
                        return old != null ? old : Value.ofUnit();
                    }
                } else if (placeStr.contains(".")) {
                    int dot = placeStr.indexOf('.');
                    String rootName = placeStr.substring(0, dot).trim();
                    String fieldName = placeStr.substring(dot + 1).trim();
                    Value objVal = locals.containsKey(rootName) ? locals.get(rootName) : (currentDomain != null ? currentDomain.state.get(rootName) : null);
                    if (objVal != null && objVal.raw instanceof Map<?, ?> mp) {
                        @SuppressWarnings("unchecked")
                        Map<String, Value> m = (Map<String, Value>) mp;
                        Value old = m.get(fieldName);
                        m.put(fieldName, replacement);
                        return old != null ? old : Value.ofUnit();
                    }
                } else {
                    Value old = locals.get(placeStr);
                    locals.put(placeStr, replacement);
                    return old != null ? old : Value.ofUnit();
                }
            }
            if (locals.containsKey(callee)) {
                Value cv = locals.get(callee);
                if (cv != null && cv.asString().startsWith("function:")) {
                    targetFn = cv.asString().substring(9);
                }
            }

            // Check program functions
            for (Function f : program.functions) {
                if (f.name.equals(targetFn) || f.name.endsWith("__" + targetFn) || targetFn.endsWith("." + f.name)
                        || (targetFn.contains(".") && f.name.equals(targetFn.replace('.', '_')))
                        || (targetFn.contains(".") && f.name.endsWith("__" + targetFn.substring(targetFn.lastIndexOf('.') + 1)))) {
                    Map<String, Value> fnLocals = new HashMap<>();
                    for (int i = 0; i < f.params.size() && i < evaluatedArgs.size(); i++) {
                        fnLocals.put(f.params.get(i).name, evaluatedArgs.get(i));
                    }
                    Value res = Value.ofUnit();
                    String oldFunction = traceFunction;
                    String oldSource = traceSourceFile;
                    String oldIdentity = traceSemanticIdentity;
                    String oldCallableSpecialization = traceCallableSpecialization;
                    int oldLine = traceLine;
                    int oldDepth = traceDepth;
                    int oldDeferredSourceArgumentReads = deferredSourceArgumentReads;
                    traceCallableSpecialization = specializationForCall(f, evaluatedArgs);
                    int enterId = traceFunctionEvent(f, "FunctionEnter", f.line);
                    if (trace && enterId != 0) {
                        traceParentStack.push(enterId);
                        traceCallStack.push(enterId);
                    }
                    traceFunction = f.name;
                    traceSourceFile = f.sourceFile == null ? "" : f.sourceFile;
                    traceSemanticIdentity = "fn:" + f.name + "@" + f.line;
                    deferredSourceArgumentReads = 0;
                    traceLine = f.body.isEmpty() ? f.line : f.body.get(0).line;
                    boolean explicitReturn = false;
                    boolean completed = false;
                    try {
                        res = evalBlock(f.body, 0, f.body.size(), fnLocals, null, f.name,
                                trace && enterId != 0 ? traceParentStack.size() : 0);
                        if (f.resultExpression != null && !f.resultExpression.isEmpty()) {
                            traceLine = f.resultLine > 0 ? f.resultLine : f.line;
                            traceDepth = trace && enterId != 0 ? traceParentStack.size() : 0;
                            res = evalExpr(f.resultExpression, fnLocals, null);
                        }
                        completed = true;
                    } catch (ReturnSignal rs) {
                        res = rs.returnValue;
                        explicitReturn = true;
                        completed = true;
                    } finally {
                        if (completed) {
                            if (!explicitReturn)
                                traceFunctionEvent(f, "Return", f.resultLine > 0 ? f.resultLine : f.line,
                                        traceValueSummary(res));
                            traceFunctionEvent(f, "FunctionExit", f.resultLine > 0 ? f.resultLine : f.line);
                        }
                        if (trace && enterId != 0) {
                            traceCallStack.pop();
                            traceParentStack.pop();
                        }
                        traceFunction = oldFunction;
                        traceSourceFile = oldSource;
                        traceSemanticIdentity = oldIdentity;
                        traceCallableSpecialization = oldCallableSpecialization;
                        traceLine = oldLine;
                        traceDepth = oldDepth;
                        deferredSourceArgumentReads = oldDeferredSourceArgumentReads;
                        for (int i = 0; i < f.params.size() && i < argList.size(); i++) {
                            String rawArg = argList.get(i).trim();
                            String pName = f.params.get(i).name;
                            AccessMode mode = i < f.parameterAccessModes.size()
                                    ? f.parameterAccessModes.get(i) : AccessMode.Read;
                            if (locals.containsKey(rawArg) && mode == AccessMode.Consume) {
                                locals.put(rawArg, Value.ofUnit());
                            } else if (locals.containsKey(rawArg) && mode == AccessMode.Write
                                    && fnLocals.containsKey(pName)) {
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
        if (expr.contains(".") && !expr.endsWith(")")) {
            int lastDot = -1;
            int pDepth = 0;
            boolean inStr = false;
            for (int ci = 0; ci < expr.length(); ci++) {
                char ch = expr.charAt(ci);
                if (ch == '"' && (ci == 0 || expr.charAt(ci - 1) != '\\')) inStr = !inStr;
                if (!inStr) {
                    if (ch == '(' || ch == '[' || ch == '{') pDepth++;
                    else if (ch == ')' || ch == ']' || ch == '}') pDepth--;
                    else if (ch == '.' && pDepth == 0) {
                        lastDot = ci;
                    }
                }
            }
            if (lastDot > 0) {
                String recvExpr = expr.substring(0, lastDot).trim();
                String fieldOrVariant = expr.substring(lastDot + 1).trim();
                if (!fieldOrVariant.isEmpty() && Character.isUpperCase(fieldOrVariant.charAt(0)) && !expr.contains("(")) {
                    Value enumConstructor = makeEnumValue(recvExpr + "." + fieldOrVariant,
                            Collections.emptyList(), Collections.emptyList());
                    return enumConstructor != null ? enumConstructor : Value.ofString(expr);
                }
                Value recvVal;
                if (locals.containsKey(recvExpr)) {
                    recvVal = locals.get(recvExpr);
                    traceValueRead(recvExpr, recvVal, fieldOrVariant);
                } else {
                    boolean deferReceiverStatePath = isDomainStateRootedPath(
                            recvExpr, locals, currentDomain);
                    boolean wasDeferringStateTrace = deferDomainStateTrace;
                    if (deferReceiverStatePath) deferDomainStateTrace = true;
                    try {
                        recvVal = evalExpr(recvExpr, locals, currentDomain);
                    } finally {
                        deferDomainStateTrace = wasDeferringStateTrace;
                    }
                    if (!deferDomainStateTrace && recvVal != null && recvVal.traceStatePath != null)
                        traceStateRead(recvVal.traceStatePath + "." + fieldOrVariant);
                }
                if (recvVal != null && recvVal.kind == Value.Kind.OBJECT && recvVal.raw instanceof Map<?, ?> fm) {
                    Object fVal = fm.get(fieldOrVariant);
                    if (fVal instanceof Value v) {
                        return recvVal.traceStatePath == null ? v
                                : withTraceStatePath(v, recvVal.traceStatePath + "." + fieldOrVariant);
                    }
                }
            }
        }

        // Domain state read e.g. value
        if (currentDomain != null && currentDomain.state.containsKey(expr)) {
            Value stateValue = currentDomain.state.get(expr);
            if (deferDomainStateTrace) return withTraceStatePath(stateValue, expr);
            traceStateRead(expr);
            return withTraceStatePath(stateValue, expr);
        }

        Value selfValue = locals.get("self");
        if (selfValue != null && selfValue.kind == Value.Kind.OBJECT
                && selfValue.raw instanceof Map<?, ?> fields
                && fields.get(expr) instanceof Value fieldValue) {
            if (selfValue.traceStatePath == null) traceLocalRead(expr);
            else traceValueRead("self", selfValue, expr);
            return selfValue.traceStatePath == null ? fieldValue
                    : withTraceStatePath(fieldValue, selfValue.traceStatePath + "." + expr);
        }

        // Local variable read
        if (locals.containsKey(expr)) {
            Value localValue = locals.get(expr);
            Value receiver = locals.get("self");
            if (receiver != null && receiver.traceStatePath != null
                    && receiver.raw instanceof Map<?, ?> fields && fields.containsKey(expr)) {
                traceValueRead("self", receiver, expr);
                return withTraceStatePath(localValue, receiver.traceStatePath + "." + expr);
            }
            traceValueRead(expr, localValue, null);
            return localValue;
        }

        // Function reference
        for (Function f : program.functions) {
            if (f.name.equals(expr)) {
                return Value.ofString("function:" + f.name);
            }
        }

        return Value.ofUnit();
    }

}
