package org.mosslang.compiler.parser;

public class NumericLiteral {
    public enum Kind { Int, Float }

    public static final long K_NUMERIC_INT_MAX_MAGNITUDE = Long.MAX_VALUE; // 9223372036854775807L
    public static final long K_NUMERIC_INT_MIN_MAGNITUDE = 0x8000000000000000L; // 9223372036854775808L unsigned

    public boolean ok = false;
    public Kind kind = Kind.Int;
    public long magnitude = 0; // treated as unsigned 64-bit integer
    public boolean magnitudeOverflow = false;
    public double floating = 0.0;
    public int end = 0;
    public String spelling = "";
    public String canonical = "";
    public String code = "";
    public String message = "";

    public static boolean isIdentChar(char c) {
        return Character.isLetterOrDigit(c) || c == '_';
    }

    public static boolean isHexDigit(char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    public static boolean isDecDigit(char c) {
        return c >= '0' && c <= '9';
    }

    private static boolean validGroup(String group, java.util.function.Predicate<Character> digit) {
        if (group.isEmpty() || group.charAt(0) == '_' || group.charAt(group.length() - 1) == '_') {
            return false;
        }
        for (int i = 0; i < group.length(); ++i) {
            char c = group.charAt(i);
            if (c == '_') {
                if (group.charAt(i + 1) == '_') return false;
                continue;
            }
            if (!digit.test(c)) return false;
        }
        return true;
    }

    private static String strip(String group) {
        StringBuilder out = new StringBuilder(group.length());
        for (int i = 0; i < group.length(); ++i) {
            char c = group.charAt(i);
            if (c != '_') out.append(c);
        }
        return out.toString();
    }

    private static boolean typeSuffix(String s) {
        if (!s.isEmpty() && s.charAt(0) == '_') {
            s = s.substring(1);
        }
        String[] suffixes = {"i8", "i16", "i32", "i64", "i128", "isize",
                             "u8", "u16", "u32", "u64", "u128", "usize",
                             "f32", "f64"};
        for (String suffix : suffixes) {
            if (s.equals(suffix)) return true;
        }
        return false;
    }

    public static NumericLiteral scanNumericLiteral(String text, int start) {
        NumericLiteral lit = new NumericLiteral();
        int i = start;

        java.util.function.BiFunction<Integer, String[], NumericLiteral> fail = (end, codeAndMsg) -> {
            lit.ok = false;
            lit.end = end;
            lit.spelling = text.substring(start, end);
            lit.code = codeAndMsg[0];
            lit.message = "numeric literal '" + lit.spelling + "' " + codeAndMsg[1];
            return lit;
        };

        java.util.function.Function<Integer, Integer> identEnd = from -> {
            while (from < text.length() && isIdentChar(text.charAt(from))) {
                ++from;
            }
            return from;
        };

        // Radix prefixes.
        if (text.charAt(i) == '0' && i + 1 < text.length() && "xXbBoO".indexOf(text.charAt(i + 1)) != -1) {
            char prefix = text.charAt(i + 1);
            int end = identEnd.apply(i + 2);
            if (end < text.length() && text.charAt(end) == '.' && end + 1 < text.length() &&
                    isDecDigit(text.charAt(end + 1))) {
                end = identEnd.apply(end + 1);
            }
            if (prefix == 'b' || prefix == 'B') {
                return fail.apply(end, new String[]{"UNSUPPORTED_NUMERIC_LITERAL",
                        "uses a binary prefix, which Moss v0.1 does not support; write a decimal or lowercase 0x hexadecimal Int literal"});
            }
            if (prefix == 'o' || prefix == 'O') {
                return fail.apply(end, new String[]{"UNSUPPORTED_NUMERIC_LITERAL",
                        "uses an octal prefix, which Moss v0.1 does not support; write a decimal or lowercase 0x hexadecimal Int literal"});
            }
            if (prefix == 'X') {
                return fail.apply(end, new String[]{"INVALID_NUMERIC_LITERAL",
                        "uses an uppercase hexadecimal prefix; write the prefix as lowercase '0x' (hexadecimal digits may be either case)"});
            }
            String body = text.substring(i + 2, end);
            if (body.indexOf('.') != -1) {
                return fail.apply(end, new String[]{"UNSUPPORTED_NUMERIC_LITERAL",
                        "is a hexadecimal fraction; hexadecimal literals are Int-only"});
            }
            int digitsEnd = 0;
            while (digitsEnd < body.length() && (isHexDigit(body.charAt(digitsEnd)) || body.charAt(digitsEnd) == '_')) {
                ++digitsEnd;
            }
            String rest = body.substring(digitsEnd);
            if (!rest.isEmpty()) {
                if (typeSuffix(rest)) {
                    return fail.apply(end, new String[]{"UNSUPPORTED_NUMERIC_LITERAL",
                            "has a type suffix; Moss literals have no suffixes (Int is always 64-bit)"});
                }
                return fail.apply(end, new String[]{"INVALID_NUMERIC_LITERAL",
                        "contains '" + rest.charAt(0) + "', which is not a hexadecimal digit"});
            }
            if (body.isEmpty() || strip(body).isEmpty()) {
                return fail.apply(end, new String[]{"INVALID_NUMERIC_LITERAL", "has no hexadecimal digits after '0x'"});
            }
            if (!validGroup(body, NumericLiteral::isHexDigit)) {
                return fail.apply(end, new String[]{"INVALID_DIGIT_SEPARATOR",
                        "misplaces '_'; a digit separator must sit between two digits"});
            }
            long value = 0;
            for (char c : strip(body).toCharArray()) {
                int d = isDecDigit(c) ? (c - '0') : (Character.toLowerCase(c) - 'a' + 10);
                if (Long.compareUnsigned(value, 0x0FFFFFFFFFFFFFFFL) > 0) {
                    lit.magnitudeOverflow = true;
                    value = -1L;
                    break;
                }
                value = (value << 4) | (d & 0xFL);
            }
            lit.ok = true;
            lit.kind = Kind.Int;
            lit.magnitude = value;
            lit.end = end;
            lit.spelling = text.substring(start, end);
            lit.canonical = lit.magnitudeOverflow ? lit.spelling : Long.toUnsignedString(value);
            return lit;
        }

        // Decimal: integer part [. fraction] [exponent].
        java.util.function.Function<Integer, Integer> groupEnd = from -> {
            while (from < text.length() && (isDecDigit(text.charAt(from)) || text.charAt(from) == '_')) {
                ++from;
            }
            return from;
        };

        int intBegin = i;
        i = groupEnd.apply(i);
        String intPart = text.substring(intBegin, i);
        boolean point = false;
        String fracPart = "";
        if (i < text.length() && text.charAt(i) == '.') {
            char next = i + 1 < text.length() ? text.charAt(i + 1) : '\0';
            int underscores = i + 1;
            while (underscores < text.length() && text.charAt(underscores) == '_') ++underscores;
            if (isDecDigit(next)) {
                point = true;
                int fracBegin = i + 1;
                i = groupEnd.apply(fracBegin);
                fracPart = text.substring(fracBegin, i);
            } else if (next == '_' && underscores < text.length() && isDecDigit(text.charAt(underscores))) {
                return fail.apply(groupEnd.apply(i + 1), new String[]{"INVALID_DIGIT_SEPARATOR",
                        "misplaces '_'; a digit separator must sit between two digits"});
            } else if ((next == 'e' || next == 'E') && i + 2 < text.length() &&
                    (isDecDigit(text.charAt(i + 2)) ||
                            ((text.charAt(i + 2) == '+' || text.charAt(i + 2) == '-') && i + 3 < text.length() &&
                                    isDecDigit(text.charAt(i + 3))))) {
                point = true;
                ++i;
            } else if (!isIdentChar(next) && next != '.') {
                point = true;
                ++i;
            }
        }

        boolean exponent = false;
        String expSign = "";
        String expPart = "";
        if (i < text.length() && (text.charAt(i) == 'e' || text.charAt(i) == 'E')) {
            int at = i + 1;
            if (at < text.length() && (text.charAt(at) == '+' || text.charAt(at) == '-')) {
                expSign = String.valueOf(text.charAt(at));
                ++at;
            }
            if (at < text.length() && isDecDigit(text.charAt(at))) {
                exponent = true;
                i = groupEnd.apply(at);
                expPart = text.substring(at, i);
            } else {
                int end = identEnd.apply(i);
                if (!expSign.isEmpty() && end == i + 1) end = at;
                return fail.apply(end, new String[]{"INVALID_NUMERIC_LITERAL",
                        "has an exponent marker without exponent digits"});
            }
        }

        int end = identEnd.apply(i);
        if (end != i) {
            String suffix = text.substring(i, end);
            if (typeSuffix(suffix)) {
                return fail.apply(end, new String[]{"UNSUPPORTED_NUMERIC_LITERAL",
                        "has a type suffix; Moss literals have no suffixes (Int and Float are always 64-bit)"});
            }
            return fail.apply(end, new String[]{"INVALID_NUMERIC_LITERAL",
                    "is followed by '" + suffix + "'; a numeric literal cannot be joined to a name"});
        }

        if ((!intPart.isEmpty() && !validGroup(intPart, NumericLiteral::isDecDigit)) ||
                (point && !fracPart.isEmpty() && !validGroup(fracPart, NumericLiteral::isDecDigit)) ||
                (exponent && !validGroup(expPart, NumericLiteral::isDecDigit))) {
            return fail.apply(end, new String[]{"INVALID_DIGIT_SEPARATOR",
                    "misplaces '_'; a digit separator must sit between two digits"});
        }

        lit.end = end;
        lit.spelling = text.substring(start, end);
        if (!point && !exponent) {
            long value = 0;
            for (char c : strip(intPart).toCharArray()) {
                int d = c - '0';
                long maxValBefore = Long.divideUnsigned(-1L - d, 10L);
                if (Long.compareUnsigned(value, maxValBefore) > 0) {
                    lit.magnitudeOverflow = true;
                    value = -1L;
                    break;
                }
                value = value * 10L + d;
            }
            lit.ok = true;
            lit.kind = Kind.Int;
            lit.magnitude = value;
            lit.canonical = lit.magnitudeOverflow ? strip(intPart) : Long.toUnsignedString(value);
            return lit;
        }

        String canonical = intPart.isEmpty() ? "0" : strip(intPart);
        if (point) {
            canonical += "." + (fracPart.isEmpty() ? "0" : strip(fracPart));
        }
        if (exponent) {
            canonical += "e" + expSign + strip(expPart);
        }
        double value;
        try {
            value = Double.parseDouble(canonical);
        } catch (NumberFormatException nfe) {
            value = Double.POSITIVE_INFINITY;
        }
        if (Double.isInfinite(value)) {
            return fail.apply(end, new String[]{"FLOAT_LITERAL_OUT_OF_RANGE",
                    "is too large for Float (IEEE binary64; the largest finite value is about 1.7976931348623157e308)"});
        }
        lit.ok = true;
        lit.kind = Kind.Float;
        lit.floating = value;
        lit.canonical = canonical;
        return lit;
    }

    public static boolean numericLiteralStartsAt(String line, int at) {
        if (at > 0 && (isIdentChar(line.charAt(at - 1)) || line.charAt(at - 1) == '.')) {
            return false;
        }
        if (isDecDigit(line.charAt(at))) {
            return true;
        }
        return line.charAt(at) == '.' && at + 1 < line.length() && isDecDigit(line.charAt(at + 1)) &&
                (at == 0 || ")]}'\"".indexOf(line.charAt(at - 1)) == -1);
    }

    public static boolean intLiteralInRange(NumericLiteral lit, boolean negated) {
        if (lit.kind != Kind.Int) return true;
        if (lit.magnitudeOverflow) return false;
        return Long.compareUnsigned(lit.magnitude, K_NUMERIC_INT_MAX_MAGNITUDE) <= 0 ||
                (negated && lit.magnitude == K_NUMERIC_INT_MIN_MAGNITUDE);
    }
}
