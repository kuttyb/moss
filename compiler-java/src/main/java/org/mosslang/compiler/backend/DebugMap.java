package org.mosslang.compiler.backend;

import java.util.ArrayList;
import java.util.List;

public class DebugMap {
    public static class DebugSourceSpan {
        public String file = "";
        public int startLine = 0;
        public int startColumn = 1;
        public int endLine = 0;
        public int endColumn = 1;

        public DebugSourceSpan() {}

        public DebugSourceSpan(String file, int startLine, int startColumn, int endLine, int endColumn) {
            this.file = file != null ? file : "";
            this.startLine = startLine;
            this.startColumn = startColumn;
            this.endLine = endLine;
            this.endColumn = endColumn;
        }
    }

    public static class DebugGeneratedRange {
        public String file = "";
        public int startLine = 0;
        public int startColumn = 1;
        public int endLine = 0;
        public int endColumn = 1;

        public DebugGeneratedRange() {}

        public DebugGeneratedRange(String file, int startLine, int startColumn, int endLine, int endColumn) {
            this.file = file != null ? file : "";
            this.startLine = startLine;
            this.startColumn = startColumn;
            this.endLine = endLine;
            this.endColumn = endColumn;
        }
    }

    public static class DebugLineMapping {
        public int mossLine = 0;
        public int generatedRustLine = 0;

        public DebugLineMapping() {}

        public DebugLineMapping(int mossLine, int generatedRustLine) {
            this.mossLine = mossLine;
            this.generatedRustLine = generatedRustLine;
        }
    }

    public static class DebugMapEntry {
        public String semanticIdentity = "";
        public String constructKind = "";
        public DebugSourceSpan source = new DebugSourceSpan();
        public DebugGeneratedRange generated = new DebugGeneratedRange();
        public String generatedSymbol = "";
        public String nativeSymbol = "";
        public List<String> provenance = new ArrayList<>();
        public List<DebugLineMapping> lineMappings = new ArrayList<>();
    }

    public int formatVersion = 1;
    public String sourceFile = "";
    public String generatedRustFile = "";
    public String nativeExecutable = "";
    public boolean debugBuild = false;
    public boolean optimized = false;
    public List<DebugMapEntry> entries = new ArrayList<>();

    public String toJson() {
        StringBuilder sb = new StringBuilder();
        sb.append("{\n");
        sb.append("  \"format\": \"moss-debug-map\",\n");
        sb.append("  \"version\": ").append(formatVersion).append(",\n");
        sb.append("  \"source_file\": \"").append(escapeJson(sourceFile)).append("\",\n");
        sb.append("  \"generated_rust_file\": \"").append(escapeJson(generatedRustFile)).append("\",\n");
        sb.append("  \"native_executable\": \"").append(escapeJson(nativeExecutable)).append("\",\n");
        sb.append("  \"debug_build\": ").append(debugBuild ? "true" : "false").append(",\n");
        sb.append("  \"optimized\": ").append(optimized ? "true" : "false").append(",\n");
        sb.append("  \"entries\": [\n");

        for (int i = 0; i < entries.size(); i++) {
            DebugMapEntry e = entries.get(i);
            sb.append("    {\n");
            sb.append("      \"semantic_identity\": \"").append(escapeJson(e.semanticIdentity)).append("\",\n");
            sb.append("      \"construct_kind\": \"").append(escapeJson(e.constructKind)).append("\",\n");
            sb.append("      \"source\": {\"file\": \"").append(escapeJson(e.source.file))
              .append("\", \"start_line\": ").append(e.source.startLine)
              .append(", \"start_column\": ").append(e.source.startColumn)
              .append(", \"end_line\": ").append(e.source.endLine)
              .append(", \"end_column\": ").append(e.source.endColumn)
              .append("},\n");
            sb.append("      \"generated\": {\"file\": \"").append(escapeJson(e.generated.file))
              .append("\", \"start_line\": ").append(e.generated.startLine)
              .append(", \"start_column\": ").append(e.generated.startColumn)
              .append(", \"end_line\": ").append(e.generated.endLine)
              .append(", \"end_column\": ").append(e.generated.endColumn)
              .append("},\n");
            sb.append("      \"generated_symbol\": \"").append(escapeJson(e.generatedSymbol)).append("\",\n");
            sb.append("      \"native_symbol\": \"").append(escapeJson(e.nativeSymbol)).append("\",\n");
            sb.append("      \"provenance\": [");
            for (int p = 0; p < e.provenance.size(); p++) {
                if (p > 0) sb.append(", ");
                sb.append("\"").append(escapeJson(e.provenance.get(p))).append("\"");
            }
            sb.append("],\n");
            sb.append("      \"line_mappings\": [");
            for (int m = 0; m < e.lineMappings.size(); m++) {
                if (m > 0) sb.append(", ");
                sb.append("{\"moss_line\": ").append(e.lineMappings.get(m).mossLine)
                  .append(", \"generated_rust_line\": ").append(e.lineMappings.get(m).generatedRustLine).append("}");
            }
            sb.append("]\n");
            sb.append("    }");
            if (i + 1 < entries.size()) {
                sb.append(",");
            }
            sb.append("\n");
        }

        sb.append("  ]\n");
        sb.append("}\n");
        return sb.toString();
    }

    public static String escapeJson(String s) {
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
                    if (c < 0x20) {
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
