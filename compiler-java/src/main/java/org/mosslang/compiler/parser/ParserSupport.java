package org.mosslang.compiler.parser;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.mosslang.compiler.ast.Line;

import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public final class ParserSupport {
    private ParserSupport() {}

    public static String trim(String value) {
        return value == null ? "" : value.trim();
    }

    public static String rtrim(String value) {
        if (value == null) return "";
        int i = value.length() - 1;
        while (i >= 0 && (value.charAt(i) == ' ' || value.charAt(i) == '\t' || value.charAt(i) == '\n' || value.charAt(i) == '\r')) {
            --i;
        }
        return i < 0 ? "" : value.substring(0, i + 1);
    }

    public static boolean startsWith(String value, String prefix) {
        return value != null && value.startsWith(prefix);
    }

    public static boolean endsWith(String value, String suffix) {
        return value != null && value.endsWith(suffix);
    }

    public static MutableList<String> splitTopLevel(String s, char delim) {
        MutableList<String> out = new FastList<>();
        if (s == null) return out;
        int par = 0, br = 0, sq = 0;
        boolean inStr = false, esc = false;
        int start = 0;
        for (int i = 0; i < s.length(); ++i) {
            char c = s.charAt(i);
            if (inStr) {
                if (esc) esc = false;
                else if (c == '\\') esc = true;
                else if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') {
                inStr = true;
                continue;
            }
            if (c == '(') ++par;
            else if (c == ')') --par;
            else if (c == '{') ++br;
            else if (c == '}') --br;
            else if (c == '[') ++sq;
            else if (c == ']') --sq;
            else if (c == delim && par == 0 && br == 0 && sq == 0) {
                out.add(trim(s.substring(start, i)));
                start = i + 1;
            }
        }
        out.add(trim(s.substring(start)));
        return out;
    }

    public static String canonicalTypeName(String type) {
        type = trim(type);
        if ("Int".equals(type)) return "int";
        if ("Float".equals(type)) return "float";
        if ("Bool".equals(type)) return "bool";
        if ("Char".equals(type)) return "char";
        if ("String".equals(type)) return "string";
        if ("Vector".equals(type)) return "vector";
        if ("Map".equals(type)) return "map";
        if ("Queue".equals(type)) return "queue";
        if ("Set".equals(type)) return "set";

        int bracket = type.indexOf('[');
        if (bracket != -1) {
            if (!endsWith(type, "]")) {
                throw new org.mosslang.compiler.diagnostics.CompileError(
                        0,
                        "malformed type expression '" + type + "': unclosed '['",
                        "MALFORMED_TYPE_EXPRESSION");
            }
            int depth = 0;
            for (int i = 0; i < type.length(); ++i) {
                char c = type.charAt(i);
                if (c == '[') ++depth;
                else if (c == ']') {
                    --depth;
                    if (depth < 0 || (depth == 0 && i < type.length() - 1)) {
                        throw new org.mosslang.compiler.diagnostics.CompileError(
                                0,
                                "malformed type expression '" + type + "'",
                                "MALFORMED_TYPE_EXPRESSION");
                    }
                }
            }
            if (depth != 0) {
                throw new org.mosslang.compiler.diagnostics.CompileError(
                        0,
                        "malformed type expression '" + type + "': unbalanced brackets",
                        "MALFORMED_TYPE_EXPRESSION");
            }
            String head = canonicalTypeName(type.substring(0, bracket));
            MutableList<String> arguments = splitTopLevel(type.substring(bracket + 1, type.length() - 1), ',');
            StringBuilder normalized = new StringBuilder();
            normalized.append(head).append("[");
            for (int index = 0; index < arguments.size(); ++index) {
                if (index > 0) normalized.append(",");
                String argument = arguments.get(index);
                int eq = argument.indexOf('=');
                if (eq != -1) {
                    String k = trim(argument.substring(0, eq));
                    String v = canonicalTypeName(argument.substring(eq + 1));
                    normalized.append(k).append("=").append(v);
                } else {
                    normalized.append(canonicalTypeName(argument));
                }
            }
            normalized.append("]");
            return normalized.toString();
        }
        return type;
    }

    public static boolean plainIdentifier(String value) {
        String s = trim(value);
        if (s.isEmpty() || (!Character.isLetter(s.charAt(0)) && s.charAt(0) != '_')) {
            return false;
        }
        for (int i = 1; i < s.length(); ++i) {
            char c = s.charAt(i);
            if (!Character.isLetterOrDigit(c) && c != '_') return false;
        }
        return true;
    }

    public static class SimpleCallResult {
        public String callee = "";
        public MutableList<String> arguments = new FastList<>();
    }

    public static boolean parseSimpleCall(String text, SimpleCallResult out) {
        String value = trim(text);
        int lp = value.indexOf('(');
        int rp = value.lastIndexOf(')');
        if (lp == -1 || rp != value.length() - 1 || lp == 0) return false;

        int depth = 0;
        for (int i = lp; i < value.length(); ++i) {
            char c = value.charAt(i);
            if (c == '"' || c == '\'') {
                for (++i; i < value.length() && value.charAt(i) != c; ++i) {
                    if (value.charAt(i) == '\\') ++i;
                }
                continue;
            }
            if (c == '(' || c == '[' || c == '{') ++depth;
            else if ((c == ')' || c == ']' || c == '}') && --depth == 0 && i != rp) return false;
        }
        out.callee = trim(value.substring(0, lp));
        if (out.callee.isEmpty()) return false;
        String inside = value.substring(lp + 1, rp);
        out.arguments.clear();
        if (!trim(inside).isEmpty()) {
            out.arguments.addAll(splitTopLevel(inside, ','));
        }
        return true;
    }

    public static class MemberCallResult {
        public String receiver = "";
        public String handler = "";
        public MutableList<String> arguments = new FastList<>();
    }

    public static boolean parseMemberCall(String text, MemberCallResult out) {
        String value = trim(text);
        int dot = -1;
        int nesting = 0;
        boolean quoted = false, escaped = false;
        for (int index = 0; index < value.length(); ++index) {
            char c = value.charAt(index);
            if (quoted) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') quoted = false;
                continue;
            }
            if (c == '"') {
                quoted = true;
                continue;
            }
            if (c == '(' || c == '[') ++nesting;
            else if (c == ')' || c == ']') --nesting;
            else if (c == '.' && nesting == 0) dot = index;
        }
        int lp = value.indexOf('(', dot == -1 ? 0 : dot);
        int rp = value.lastIndexOf(')');
        if (dot == -1 || lp == -1 || rp != value.length() - 1 || dot >= lp) {
            return false;
        }
        out.receiver = trim(value.substring(0, dot));
        out.handler = trim(value.substring(dot + 1, lp));
        if (out.receiver.isEmpty() || out.handler.isEmpty()) return false;
        String inside = value.substring(lp + 1, rp);
        out.arguments.clear();
        if (!trim(inside).isEmpty()) {
            out.arguments.addAll(splitTopLevel(inside, ','));
        }
        return true;
    }

    public static int matchingParen(String text, int open) {
        if (open < 0 || open >= text.length() || text.charAt(open) != '(') return -1;
        int depth = 0;
        boolean inStr = false, esc = false;
        for (int i = open; i < text.length(); ++i) {
            char c = text.charAt(i);
            if (inStr) {
                if (esc) esc = false;
                else if (c == '\\') esc = true;
                else if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') {
                inStr = true;
                continue;
            }
            if (c == '(') ++depth;
            else if (c == ')' && --depth == 0) return i;
        }
        return -1;
    }

    public static int topLevelAssignment(String text) {
        int par = 0, br = 0, sq = 0;
        boolean inStr = false, esc = false;
        for (int i = 0; i < text.length(); ++i) {
            char c = text.charAt(i);
            if (inStr) {
                if (esc) esc = false;
                else if (c == '\\') esc = true;
                else if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') {
                inStr = true;
                continue;
            }
            if (c == '(') ++par;
            else if (c == ')') --par;
            else if (c == '{') ++br;
            else if (c == '}') --br;
            else if (c == '[') ++sq;
            else if (c == ']') --sq;
            else if (c == '=' && par == 0 && br == 0 && sq == 0) {
                char before = i > 0 ? text.charAt(i - 1) : '\0';
                char after = i + 1 < text.length() ? text.charAt(i + 1) : '\0';
                if (before != '=' && before != '!' && before != '<' && before != '>' && after != '=') {
                    return i;
                }
            }
        }
        return -1;
    }

    public static int topLevelColon(String text) {
        int par = 0, br = 0, sq = 0;
        boolean inStr = false, esc = false;
        for (int i = 0; i < text.length(); ++i) {
            char c = text.charAt(i);
            if (inStr) {
                if (esc) esc = false;
                else if (c == '\\') esc = true;
                else if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') {
                inStr = true;
                continue;
            }
            if (c == '(') ++par;
            else if (c == ')') --par;
            else if (c == '{') ++br;
            else if (c == '}') --br;
            else if (c == '[') ++sq;
            else if (c == ']') --sq;
            else if (c == ':' && par == 0 && br == 0 && sq == 0) {
                return i;
            }
        }
        return -1;
    }

    public static int parseRustEscape(String line, int at, int[] outCodepoint) {
        if (at + 1 >= line.length()) return -1;
        char e = line.charAt(at + 1);
        String simple = "nrt0'\\\"";
        int[] values = {'\n', '\r', '\t', 0, '\'', '\\', '"'};
        int which = simple.indexOf(e);
        if (which != -1) {
            outCodepoint[0] = values[which];
            return at + 2;
        }
        if (e == 'x') {
            if (at + 3 >= line.length() || !NumericLiteral.isHexDigit(line.charAt(at + 2)) ||
                    !NumericLiteral.isHexDigit(line.charAt(at + 3))) {
                return -1;
            }
            int cp = Integer.parseInt(line.substring(at + 2, at + 4), 16);
            if (cp <= 0x7F) {
                outCodepoint[0] = cp;
                return at + 4;
            }
            return -1;
        }
        if (e == 'u' && at + 2 < line.length() && line.charAt(at + 2) == '{') {
            int close = line.indexOf('}', at + 3);
            if (close == -1 || close == at + 3 || close - (at + 3) > 6) return -1;
            for (int k = at + 3; k < close; ++k) {
                if (!NumericLiteral.isHexDigit(line.charAt(k))) return -1;
            }
            int cp = Integer.parseInt(line.substring(at + 3, close), 16);
            if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return -1;
            outCodepoint[0] = cp;
            return close + 1;
        }
        return -1;
    }

    public static String sourceCodeOnly(String line) {
        char[] chars = line.toCharArray();
        int len = chars.length;
        boolean inString = false, escaped = false;
        for (int index = 0; index < len; ++index) {
            char ch = chars[index];
            if (inString) {
                chars[index] = ' ';
                if (escaped) escaped = false;
                else if (ch == '\\') escaped = true;
                else if (ch == '"') inString = false;
            } else if (ch == '"') {
                chars[index] = ' ';
                inString = true;
            } else if (ch == '#') {
                return new String(chars, 0, index);
            }
        }
        return new String(chars);
    }

    private static boolean standardErrorShortName(String name) {
        return "FileError".equals(name) || "ParseError".equals(name) || "ConversionError".equals(name);
    }

    private static String standardErrorIdentity(String name) {
        return "std__" + name;
    }

    public static void normalizeStandardErrorLines(List<Line> lines, Set<String> forcedAliases) {
        boolean explicitModule = false;
        Set<String> localNames = new HashSet<>();
        for (Line line : lines) {
            String code = trim(sourceCodeOnly(line.text));
            if (startsWith(code, "module ")) explicitModule = true;
            String declaration = startsWith(code, "export ") ? trim(code.substring(7)) : code;
            if (startsWith(declaration, "enum ")) {
                int colon = declaration.indexOf(':');
                String name = trim(colon == -1 ? declaration.substring(5) : declaration.substring(5, colon));
                if (standardErrorShortName(name)) localNames.add(name);
            }
        }
        if (forcedAliases != null) {
            for (String alias : forcedAliases) {
                localNames.remove(alias);
            }
        }
        for (Line line : lines) {
            StringBuilder rewritten = new StringBuilder();
            boolean inString = false;
            String value = line.text;
            for (int at = 0; at < value.length();) {
                char ch = value.charAt(at);
                if (ch == '"' && (at == 0 || value.charAt(at - 1) != '\\')) {
                    inString = !inString;
                }
                if (!inString && ch == '#') {
                    rewritten.append(value.substring(at));
                    break;
                }
                if (inString || (!Character.isLetter(ch) && ch != '_')) {
                    rewritten.append(ch);
                    ++at;
                    continue;
                }
                int end = at + 1;
                while (end < value.length() && (Character.isLetterOrDigit(value.charAt(end)) || value.charAt(end) == '_')) {
                    ++end;
                }
                String token = value.substring(at, end);
                if ("std".equals(token) && end < value.length() && value.charAt(end) == '.') {
                    int member = end + 1;
                    while (member < value.length() && (Character.isLetterOrDigit(value.charAt(member)) || value.charAt(member) == '_')) {
                        ++member;
                    }
                    String name = value.substring(end + 1, member);
                    if (standardErrorShortName(name)) {
                        rewritten.append(standardErrorIdentity(name));
                        at = member;
                        continue;
                    }
                }
                if (standardErrorShortName(token) &&
                        (!explicitModule || !localNames.contains(token)) &&
                        (at == 0 || value.charAt(at - 1) != '.')) {
                    rewritten.append(standardErrorIdentity(token));
                } else {
                    rewritten.append(token);
                }
                at = end;
            }
            line.text = rewritten.toString();
        }
    }
}
