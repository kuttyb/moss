package org.mosslang.compiler.diagnostics;

import java.io.PrintStream;

public class DiagnosticFormatter {
    public static void writeHumanError(PrintStream out, String source, int line, String code, String message) {
        String src = source != null && !source.isEmpty() ? source : "moss";
        String loc = line > 0 ? ":" + line : "";
        String codePrefix = code != null && !code.isEmpty() ? "[" + code + "] " : "";
        out.println(src + loc + ": error: " + codePrefix + message);
    }

    public static void writeStructuredSuccess(PrintStream out, String command, String compilerVersion, String sourceFile) {
        writeStructuredSuccess(out, command, compilerVersion, sourceFile, java.util.Collections.emptyList());
    }

    public static void writeStructuredSuccess(PrintStream out, String command, String compilerVersion, String sourceFile, java.util.List<Warning> warnings) {
        StringBuilder sb = new StringBuilder();
        sb.append("{\n");
        sb.append("  \"protocol_version\": 1,\n");
        sb.append("  \"schema_version\": \"moss-agent-1\",\n");
        sb.append("  \"compiler_version\": \"").append(escapeJson(compilerVersion)).append("\",\n");
        sb.append("  \"command\": \"").append(escapeJson(command)).append("\",\n");
        sb.append("  \"ok\": true,\n");
        sb.append("  \"result\": {\"source_file\": \"").append(escapeJson(sourceFile)).append("\", \"diagnostics\": [");
        if (warnings != null) {
            for (int i = 0; i < warnings.size(); i++) {
                if (i > 0) sb.append(", ");
                Warning w = warnings.get(i);
                sb.append("{\"code\": \"").append(escapeJson(w.code() != null ? w.code() : "MOSS_WARNING"))
                  .append("\", \"severity\": \"warning\", \"message\": \"").append(escapeJson(w.message()))
                  .append("\", \"line\": ").append(w.line()).append("}");
            }
        }
        sb.append("]}\n");
        sb.append("}");
        out.println(sb.toString());
    }

    public static void writeStructuredError(PrintStream out, String command, String compilerVersion,
                                           String code, String message, String sourceFile, int line,
                                           String semanticIdentity, String symbol) {
        StringBuilder sb = new StringBuilder();
        sb.append("{\n");
        sb.append("  \"protocol_version\": 1,\n");
        sb.append("  \"schema_version\": \"moss-agent-1\",\n");
        sb.append("  \"compiler_version\": \"").append(escapeJson(compilerVersion)).append("\",\n");
        sb.append("  \"command\": \"").append(escapeJson(command)).append("\",\n");
        sb.append("  \"ok\": false,\n");
        sb.append("  \"error\": {");
        sb.append("\"code\": \"").append(escapeJson(code)).append("\", ");
        sb.append("\"severity\": \"error\", ");
        sb.append("\"message\": \"").append(escapeJson(message)).append("\", ");
        sb.append("\"source_file\": \"").append(escapeJson(sourceFile != null ? sourceFile : "")).append("\", ");
        sb.append("\"line\": ").append(line).append(", ");
        sb.append("\"column\": 1, ");
        if (line > 0) {
            sb.append("\"span\": {\"start_line\": ").append(line).append(", \"start_column\": 1, \"end_line\": ").append(line).append(", \"end_column\": 1}, ");
            sb.append("\"source\": {\"file\": \"").append(escapeJson(sourceFile != null ? sourceFile : "")).append("\", \"line\": ").append(line).append(", \"column\": 1, \"span\": {\"start_line\": ").append(line).append(", \"start_column\": 1, \"end_line\": ").append(line).append(", \"end_column\": 1}}, ");
        } else {
            sb.append("\"span\": null, ");
            sb.append("\"source\": {\"file\": \"").append(escapeJson(sourceFile != null ? sourceFile : "")).append("\", \"line\": 0, \"column\": 1, \"span\": null}, ");
        }
        if (semanticIdentity != null && !semanticIdentity.isEmpty()) {
            sb.append("\"semantic_identity\": \"").append(escapeJson(semanticIdentity)).append("\", ");
        } else {
            sb.append("\"semantic_identity\": null, ");
        }
        if (symbol != null && !symbol.isEmpty()) {
            sb.append("\"symbol\": \"").append(escapeJson(symbol)).append("\", ");
        } else {
            sb.append("\"symbol\": null, ");
        }
        sb.append("\"rule\": null, \"cause\": null, \"related\": [], \"guidance\": null, ");
        if ("OWNERSHIP_USE_AFTER_CONSUME".equals(code)) {
            Integer consumeLine = null;
            int marker = message != null ? message.indexOf(" at line ") : -1;
            if (marker >= 0) {
                int start = marker + " at line ".length();
                int end = start;
                while (end < message.length() && Character.isDigit(message.charAt(end))) {
                    end++;
                }
                try {
                    consumeLine = Integer.parseInt(message.substring(start, end));
                } catch (NumberFormatException ignored) {}
            }
            sb.append("\"details\": {\"ownership_expected\": \"AVAILABLE\", \"ownership_actual\": \"CONSUMED\", \"consume_line\": ")
              .append(consumeLine != null ? consumeLine : "null")
              .append(", \"recursion_witness\": null, \"resolution\": null}, ");
            sb.append("\"fixes\": [], \"legal_alternatives\": [\"make the callee READ the value if that matches program intent\", \"keep the ownership transfer and stop using the old binding\", \"construct an explicit new owned value or explicit deep copy\"]");
        } else {
            sb.append("\"details\": {\"ownership_expected\": null, \"ownership_actual\": null, \"consume_line\": null, \"recursion_witness\": null, \"resolution\": null}, ");
            sb.append("\"fixes\": [], \"legal_alternatives\": []");
        }
        sb.append("}\n");
        sb.append("}");
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
