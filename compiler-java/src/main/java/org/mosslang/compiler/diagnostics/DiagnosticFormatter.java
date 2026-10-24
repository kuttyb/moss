package org.mosslang.compiler.diagnostics;

import java.io.PrintStream;

public class DiagnosticFormatter {
    public static void writeHumanError(PrintStream out, String source, int line, String code, String message) {
        String src = source != null && !source.isEmpty() ? source : "moss";
        String loc = line > 0 ? ":" + line : "";
        String codePrefix = code != null && !code.isEmpty() ? "[" + code + "] " : "";
        out.println(src + loc + ": error: " + codePrefix + message);
    }

    public static void writeStructuredError(PrintStream out, String command, String code, String message,
                                           String sourceFile, int line, String semanticIdentity, String symbol) {
        StringBuilder sb = new StringBuilder();
        sb.append("{\"status\":\"error\",\"error\":{");
        sb.append("\"command\":\"").append(escapeJson(command)).append("\",");
        sb.append("\"code\":\"").append(escapeJson(code)).append("\",");
        sb.append("\"message\":\"").append(escapeJson(message)).append("\"");
        if (sourceFile != null && !sourceFile.isEmpty()) {
            sb.append(",\"source_file\":\"").append(escapeJson(sourceFile)).append("\"");
        }
        if (line > 0) {
            sb.append(",\"line\":").append(line);
        }
        if (semanticIdentity != null && !semanticIdentity.isEmpty()) {
            sb.append(",\"semantic_identity\":\"").append(escapeJson(semanticIdentity)).append("\"");
        }
        if (symbol != null && !symbol.isEmpty()) {
            sb.append(",\"symbol\":\"").append(escapeJson(symbol)).append("\"");
        }
        sb.append("}}");
        out.println(sb.toString());
    }

    private static String escapeJson(String s) {
        if (s == null) return "";
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '"' -> sb.append("\\\"");
                case '\\' -> sb.append("\\\\");
                case '\b' -> sb.append("\\b");
                case '\f' -> sb.append("\\f");
                case '\n' -> sb.append("\\n");
                case '\r' -> sb.append("\\r");
                case '\t' -> sb.append("\\t");
                default -> {
                    if (c < 32) {
                        sb.append(String.format("\\u%04x", (int) c));
                    } else {
                        sb.append(c);
                    }
                }
            }
        }
        return sb.toString();
    }
}
