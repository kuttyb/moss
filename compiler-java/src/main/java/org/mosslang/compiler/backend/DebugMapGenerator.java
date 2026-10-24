package org.mosslang.compiler.backend;

import org.mosslang.compiler.ast.Program;
import org.mosslang.compiler.ast.Trait;
import org.mosslang.compiler.backend.DebugMap.*;

import java.nio.charset.StandardCharsets;
import java.util.*;

public class DebugMapGenerator {

    public static String stableHash(String value) {
        if (value == null) return "0000000000000000";
        long hash = 0xcbf29ce484222325L;
        byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
        for (byte b : bytes) {
            hash ^= (b & 0xff);
            hash *= 0x100000001b3L;
        }
        return String.format("%016x", hash);
    }

    public static String toolingName(String value) {
        if (value == null) return "anonymous";
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            if (Character.isLetterOrDigit(c) || c == '_') {
                out.append(c);
            } else {
                out.append('_');
            }
        }
        while (out.length() > 0 && out.charAt(out.length() - 1) == '_') {
            out.setLength(out.length() - 1);
        }
        return out.length() == 0 ? "anonymous" : out.toString();
    }

    public static String toolingNativeSymbol(String kind, String name, String semanticIdentity) {
        return "moss__" + toolingName(kind) + "__" + toolingName(name) + "__" + stableHash(semanticIdentity);
    }

    private static class DebugMarkerRegion {
        String kind = "";
        String semanticIdentity = "";
        String generatedSymbol = "";
        String nativeSymbol = "";
        int beginLine = 0;
        int endLine = 0;
    }

    public static boolean isNonCodeLine(String line) {
        String trimmed = line.trim();
        return trimmed.isEmpty() || trimmed.startsWith("//") || trimmed.startsWith("#[");
    }

    public static int nextGeneratedCodeLine(List<String> lines, int line, int limit) {
        int candidate = Math.max(1, line);
        int end = Math.min(limit, lines.size());
        while (candidate <= end && isNonCodeLine(lines.get(candidate - 1))) {
            candidate++;
        }
        return candidate <= end ? candidate : Math.max(1, Math.min(line, end));
    }

    public static int previousGeneratedCodeLine(List<String> lines, int line, int limit) {
        int candidate = Math.min(line, lines.size());
        while (candidate >= limit && lines.get(candidate - 1).trim().isEmpty()) {
            candidate--;
        }
        return candidate >= limit ? candidate : limit;
    }

    public static Integer mossCommentLine(String line) {
        String value = line.trim();
        String prefix = "// Moss line ";
        if (!value.startsWith(prefix)) return null;
        int begin = prefix.length();
        int end = begin;
        while (end < value.length() && Character.isDigit(value.charAt(end))) {
            end++;
        }
        if (end == begin) return null;
        try {
            return Integer.parseInt(value.substring(begin, end));
        } catch (NumberFormatException e) {
            return null;
        }
    }

    public static DebugMap generate(Program program, String rustSource,
                                    String sourceFile, String generatedRustFile,
                                    String nativeExecutable,
                                    boolean debugBuild, boolean optimized) {
        DebugMap map = new DebugMap();
        map.formatVersion = 1;
        map.sourceFile = sourceFile != null ? sourceFile : "";
        map.generatedRustFile = generatedRustFile != null ? generatedRustFile : "";
        map.nativeExecutable = nativeExecutable != null ? nativeExecutable : "";
        map.debugBuild = debugBuild;
        map.optimized = optimized;

        List<String> lines = Arrays.asList(rustSource.split("\r?\n", -1));
        if (lines.isEmpty()) {
            lines = Collections.singletonList("");
        }

        // 1. Collect regions from // Moss tooling begin|... and end|...
        String beginPrefix = "// Moss tooling begin|";
        String endPrefix = "// Moss tooling end|";
        List<DebugMarkerRegion> regions = new ArrayList<>();
        Map<String, Integer> open = new HashMap<>();

        for (int i = 0; i < lines.size(); i++) {
            String trimmed = lines.get(i).trim();
            if (trimmed.startsWith(beginPrefix)) {
                String payload = trimmed.substring(beginPrefix.length());
                String[] fields = payload.split("\\|", -1);
                if (fields.length == 4) {
                    DebugMarkerRegion region = new DebugMarkerRegion();
                    region.kind = fields[0];
                    region.semanticIdentity = fields[1];
                    region.generatedSymbol = fields[2];
                    region.nativeSymbol = fields[3];
                    region.beginLine = i + 1;
                    open.put(region.semanticIdentity, regions.size());
                    regions.add(region);
                }
            } else if (trimmed.startsWith(endPrefix)) {
                String identity = trimmed.substring(endPrefix.length());
                Integer idx = open.remove(identity);
                if (idx != null) {
                    regions.get(idx).endLine = i + 1;
                }
            }
        }

        // 2. Build entries from regions
        for (DebugMarkerRegion region : regions) {
            if (region.endLine == 0) {
                region.endLine = lines.size();
            }
            DebugMapEntry entry = new DebugMapEntry();
            entry.semanticIdentity = region.semanticIdentity;
            entry.constructKind = region.kind;
            entry.generatedSymbol = region.generatedSymbol;
            entry.nativeSymbol = region.nativeSymbol;
            entry.provenance.add(region.semanticIdentity);
            entry.source.file = map.sourceFile;
            entry.generated.file = map.generatedRustFile;

            entry.generated.startLine = nextGeneratedCodeLine(lines, region.beginLine + 1, region.endLine - 1);
            entry.generated.endLine = previousGeneratedCodeLine(lines, region.endLine - 1, entry.generated.startLine);

            Set<Integer> mappedSourceLines = new HashSet<>();
            int minSourceLine = 0;
            int maxSourceLine = 0;

            for (int line = region.beginLine + 1; line < region.endLine; line++) {
                Integer mossLine = mossCommentLine(lines.get(line - 1));
                if (mossLine == null) continue;
                if (minSourceLine == 0 || mossLine < minSourceLine) {
                    minSourceLine = mossLine;
                }
                if (mossLine > maxSourceLine) {
                    maxSourceLine = mossLine;
                }
                if (mappedSourceLines.add(mossLine)) {
                    int genLine = nextGeneratedCodeLine(lines, line + 1, region.endLine - 1);
                    entry.lineMappings.add(new DebugLineMapping(mossLine, genLine));
                }
            }

            if (minSourceLine == 0) {
                minSourceLine = 1;
                maxSourceLine = 1;
            }
            entry.source.startLine = minSourceLine;
            entry.source.endLine = maxSourceLine;

            // Merge if duplicate identity exists
            DebugMapEntry existing = null;
            for (DebugMapEntry e : map.entries) {
                if (e.semanticIdentity.equals(entry.semanticIdentity)) {
                    existing = e;
                    break;
                }
            }
            if (existing != null) {
                existing.lineMappings.addAll(entry.lineMappings);
            } else {
                map.entries.add(entry);
            }
        }

        // 3. Add erased traits from program
        if (program != null) {
            for (Trait t : program.traits) {
                DebugMapEntry entry = new DebugMapEntry();
                entry.semanticIdentity = "trait:" + t.name + "@" + t.line;
                entry.constructKind = "trait";
                entry.source = new DebugSourceSpan(
                        (t.sourceFile != null && !t.sourceFile.isEmpty()) ? t.sourceFile : map.sourceFile,
                        t.line, 1, t.line, 1
                );
                entry.generated = new DebugGeneratedRange(map.generatedRustFile, 0, 1, 0, 1);
                entry.generatedSymbol = t.name;
                entry.nativeSymbol = "";
                entry.provenance.add(entry.semanticIdentity);
                map.entries.add(entry);
            }
        }

        // 4. Parse functional pipelines & traversal groups from generated Rust comments
        collectFunctionalEntries(map, lines);

        // 5. Deterministic sorting matching canonical C++ sort
        map.entries.sort((left, right) -> {
            if (left.source.startLine != right.source.startLine) {
                return Integer.compare(left.source.startLine, right.source.startLine);
            }
            if (!left.constructKind.equals(right.constructKind)) {
                return left.constructKind.compareTo(right.constructKind);
            }
            return left.semanticIdentity.compareTo(right.semanticIdentity);
        });

        return map;
    }

    private static void collectFunctionalEntries(DebugMap map, List<String> lines) {
        // Collect pipelines from comments like:
        // // Moss backend: EAGER FUNCTIONAL PIPELINE ... semantic <id>
        // // Moss backend: FUSED FUNCTIONAL PIPELINE ... semantic <id>; provenance <p1> <p2> ...
        // // Moss dataflow group ... semantic <id>; provenance <p1> <p2> ...
        // // Moss functional origin|<id>
        List<PipelineMarkerInfo> pipelines = new ArrayList<>();
        List<GroupMarkerInfo> groups = new ArrayList<>();

        for (int i = 0; i < lines.size(); i++) {
            String line = lines.get(i).trim();
            if (line.contains("FUNCTIONAL PIPELINE") && line.contains("semantic ")) {
                PipelineMarkerInfo info = new PipelineMarkerInfo();
                info.markerLine = i + 1;
                info.isFused = line.contains("FUSED");
                int semIdx = line.indexOf("semantic ");
                String afterSem = line.substring(semIdx + 9);
                int endSem = afterSem.indexOf(';');
                if (endSem < 0) endSem = afterSem.indexOf(')');
                if (endSem < 0) endSem = afterSem.length();
                info.semanticIdentity = afterSem.substring(0, endSem).trim();

                int provIdx = line.indexOf("provenance ");
                if (provIdx >= 0) {
                    String provPart = line.substring(provIdx + 11).trim();
                    if (provPart.endsWith(")")) provPart = provPart.substring(0, provPart.length() - 1);
                    for (String p : provPart.split("\\s+")) {
                        p = p.trim();
                        if (!p.isEmpty()) info.provenance.add(p);
                    }
                }
                pipelines.add(info);
            } else if (line.contains("dataflow group") && line.contains("semantic ")) {
                GroupMarkerInfo info = new GroupMarkerInfo();
                info.markerLine = i + 1;
                int semIdx = line.indexOf("semantic ");
                String afterSem = line.substring(semIdx + 9);
                int endSem = afterSem.indexOf(';');
                if (endSem < 0) endSem = afterSem.length();
                info.semanticIdentity = afterSem.substring(0, endSem).trim();

                int provIdx = line.indexOf("provenance ");
                if (provIdx >= 0) {
                    String provPart = line.substring(provIdx + 11).trim();
                    for (String p : provPart.split("\\s+")) {
                        p = p.trim();
                        if (!p.isEmpty()) info.provenance.add(p);
                    }
                }
                groups.add(info);
            }
        }

        // For each pipeline marker, find its functional origin markers if not in provenance
        for (PipelineMarkerInfo p : pipelines) {
            DebugMapEntry parent = findParentForIdentity(map.entries, p.semanticIdentity);
            int generatedLine = nextGeneratedCodeLine(lines, p.markerLine + 1,
                    parent != null ? parent.generated.endLine : lines.size());
            int generatedEnd = functionalGeneratedEndLine(lines, p.markerLine,
                    parent != null ? parent.generated.endLine : lines.size());

            int mossLine = parseLineFromIdentity(p.semanticIdentity);

            // If provenance wasn't explicit in the comment, gather subsequent origin markers
            if (p.provenance.isEmpty()) {
                for (int i = p.markerLine; i < lines.size() && (parent == null || i < parent.generated.endLine); i++) {
                    String l = lines.get(i).trim();
                    if (l.startsWith("// Moss functional origin|")) {
                        p.provenance.add(l.substring("// Moss functional origin|".length()).trim());
                    } else if (l.contains("FUNCTIONAL PIPELINE")) {
                        break;
                    }
                }
            }

            // Functional pipeline entry
            DebugMapEntry pipeEntry = new DebugMapEntry();
            pipeEntry.semanticIdentity = p.semanticIdentity;
            pipeEntry.constructKind = "functional_pipeline";
            pipeEntry.source = new DebugSourceSpan(map.sourceFile, mossLine, 1, mossLine, 1);
            pipeEntry.generated = new DebugGeneratedRange(map.generatedRustFile, generatedLine, 1, generatedEnd, 1);
            if (parent != null) {
                pipeEntry.generatedSymbol = parent.generatedSymbol;
                pipeEntry.nativeSymbol = parent.nativeSymbol;
            }
            pipeEntry.provenance = new ArrayList<>(p.provenance);
            pipeEntry.lineMappings.add(new DebugLineMapping(mossLine, generatedLine));
            addOrMerge(map, pipeEntry);

            // Node entries for each item in provenance
            for (String origin : p.provenance) {
                int nodeMossLine = mossLine;
                if (origin.endsWith(":stage:1")) nodeMossLine = mossLine + 1;
                else if (origin.endsWith(":stage:2")) nodeMossLine = mossLine + 2;
                else if (origin.endsWith(":stage:3")) nodeMossLine = mossLine + 3;

                int nodeGeneratedLine = generatedLine;
                int nodeGeneratedEnd = generatedEnd;

                if (!p.isFused) {
                    // In eager mode, look for // Moss functional origin|<origin>
                    int originMarkerLine = 0;
                    for (int i = p.markerLine - 1; i < lines.size() && (parent == null || i < parent.generated.endLine); i++) {
                        if (lines.get(i).contains(origin)) {
                            originMarkerLine = i + 1;
                            break;
                        }
                    }
                    if (originMarkerLine > 0) {
                        nodeGeneratedLine = nextGeneratedCodeLine(lines, originMarkerLine + 1,
                                parent != null ? parent.generated.endLine : lines.size());
                        nodeGeneratedEnd = functionalGeneratedEndLine(lines, originMarkerLine,
                                parent != null ? parent.generated.endLine : lines.size());
                    }
                }

                DebugMapEntry nodeEntry = new DebugMapEntry();
                nodeEntry.semanticIdentity = origin;
                nodeEntry.constructKind = "functional_node";
                nodeEntry.source = new DebugSourceSpan(map.sourceFile, nodeMossLine, 1, nodeMossLine, 1);
                nodeEntry.generated = new DebugGeneratedRange(map.generatedRustFile, nodeGeneratedLine, 1, nodeGeneratedEnd, 1);
                if (parent != null) {
                    nodeEntry.generatedSymbol = parent.generatedSymbol;
                    nodeEntry.nativeSymbol = parent.nativeSymbol;
                }
                nodeEntry.provenance = new ArrayList<>(p.provenance);
                nodeEntry.lineMappings.add(new DebugLineMapping(nodeMossLine, nodeGeneratedLine));
                addOrMerge(map, nodeEntry);
            }
        }

        // For each dataflow group
        for (GroupMarkerInfo g : groups) {
            DebugMapEntry parent = findParentForIdentity(map.entries, g.semanticIdentity);
            int generatedLine = nextGeneratedCodeLine(lines, g.markerLine + 1,
                    parent != null ? parent.generated.endLine : lines.size());
            int generatedEnd = functionalGeneratedEndLine(lines, g.markerLine,
                    parent != null ? parent.generated.endLine : lines.size());

            int mossLine = parseLineFromIdentity(g.semanticIdentity);

            DebugMapEntry groupEntry = new DebugMapEntry();
            groupEntry.semanticIdentity = g.semanticIdentity;
            groupEntry.constructKind = "functional_dataflow_group";
            groupEntry.source = new DebugSourceSpan(map.sourceFile, mossLine, 1, mossLine, 1);
            groupEntry.generated = new DebugGeneratedRange(map.generatedRustFile, generatedLine, 1, generatedEnd, 1);
            if (parent != null) {
                groupEntry.generatedSymbol = parent.generatedSymbol;
                groupEntry.nativeSymbol = parent.nativeSymbol;
            }
            groupEntry.provenance = new ArrayList<>(g.provenance);
            groupEntry.lineMappings.add(new DebugLineMapping(mossLine, generatedLine));
            addOrMerge(map, groupEntry);

            // Group contributing origins by pipeline
            Map<String, List<String>> pipelineOrigins = new LinkedHashMap<>();
            for (String origin : g.provenance) {
                // e.g. origin = main@3:expression:0:source -> pipe = main@3:expression:0
                int lastColon = origin.lastIndexOf(':');
                String pipe = (lastColon > 0) ? origin.substring(0, lastColon) : origin;
                if (pipe.endsWith(":stage")) {
                    int prevColon = pipe.lastIndexOf(':');
                    if (prevColon > 0) pipe = pipe.substring(0, prevColon);
                }
                pipelineOrigins.computeIfAbsent(pipe, k -> new ArrayList<>()).add(origin);
            }

            for (Map.Entry<String, List<String>> entry : pipelineOrigins.entrySet()) {
                String pipeId = entry.getKey();
                List<String> origins = entry.getValue();
                int pLine = parseLineFromIdentity(pipeId);

                // Add node entries
                for (String origin : origins) {
                    int nLine = parseLineFromIdentity(origin);
                    DebugMapEntry nodeEntry = new DebugMapEntry();
                    nodeEntry.semanticIdentity = origin;
                    nodeEntry.constructKind = "functional_node";
                    nodeEntry.source = new DebugSourceSpan(map.sourceFile, nLine, 1, nLine, 1);
                    nodeEntry.generated = new DebugGeneratedRange(map.generatedRustFile, generatedLine, 1, generatedEnd, 1);
                    if (parent != null) {
                        nodeEntry.generatedSymbol = parent.generatedSymbol;
                        nodeEntry.nativeSymbol = parent.nativeSymbol;
                    }
                    nodeEntry.provenance = new ArrayList<>(origins);
                    nodeEntry.lineMappings.add(new DebugLineMapping(nLine, generatedLine));
                    addOrMerge(map, nodeEntry);
                }

                // Add pipeline entry
                DebugMapEntry pipeEntry = new DebugMapEntry();
                pipeEntry.semanticIdentity = pipeId;
                pipeEntry.constructKind = "functional_pipeline";
                pipeEntry.source = new DebugSourceSpan(map.sourceFile, pLine, 1, pLine, 1);
                pipeEntry.generated = new DebugGeneratedRange(map.generatedRustFile, generatedLine, 1, generatedEnd, 1);
                if (parent != null) {
                    pipeEntry.generatedSymbol = parent.generatedSymbol;
                    pipeEntry.nativeSymbol = parent.nativeSymbol;
                }
                pipeEntry.provenance = new ArrayList<>(origins);
                pipeEntry.lineMappings.add(new DebugLineMapping(pLine, generatedLine));
                addOrMerge(map, pipeEntry);
            }
        }
    }

    private static void addOrMerge(DebugMap map, DebugMapEntry entry) {
        for (DebugMapEntry e : map.entries) {
            if (e.semanticIdentity.equals(entry.semanticIdentity)) {
                e.lineMappings.addAll(entry.lineMappings);
                return;
            }
        }
        map.entries.add(entry);
    }

    private static int parseLineFromIdentity(String identity) {
        int atIdx = identity.indexOf('@');
        if (atIdx >= 0) {
            int end = atIdx + 1;
            while (end < identity.length() && Character.isDigit(identity.charAt(end))) {
                end++;
            }
            if (end > atIdx + 1) {
                try {
                    return Integer.parseInt(identity.substring(atIdx + 1, end));
                } catch (NumberFormatException ignored) {}
            }
        }
        return 1;
    }

    private static DebugMapEntry findParentForIdentity(List<DebugMapEntry> entries, String identity) {
        String context = identity;
        int at = identity.indexOf('@');
        if (at > 0) {
            context = identity.substring(0, at);
        }
        for (DebugMapEntry e : entries) {
            if (context.startsWith("main") && e.constructKind.equals("main")) {
                return e;
            }
            if (e.semanticIdentity.startsWith(context + "@") &&
                    (e.constructKind.equals("function") || e.constructKind.equals("method") ||
                     e.constructKind.equals("handler") || e.constructKind.equals("main"))) {
                return e;
            }
        }
        // Fallback to any enclosing construct
        for (DebugMapEntry e : entries) {
            if (e.constructKind.equals("function") || e.constructKind.equals("main")) {
                return e;
            }
        }
        return null;
    }

    private static int functionalGeneratedEndLine(List<String> lines, int markerLine, int limit) {
        if (markerLine <= 0 || markerLine > lines.size()) {
            return markerLine;
        }
        String marker = lines.get(markerLine - 1);
        boolean originMarker = marker.trim().startsWith("// Moss functional origin|");
        int markerIndent = 0;
        while (markerIndent < marker.length() && (marker.charAt(markerIndent) == ' ' || marker.charAt(markerIndent) == '\t')) {
            markerIndent++;
        }
        for (int line = markerLine + 1; line <= limit && line <= lines.size(); line++) {
            String candidate = lines.get(line - 1);
            if (candidate.trim().isEmpty()) continue;
            if (originMarker && candidate.trim().startsWith("// Moss functional origin|")) {
                return previousGeneratedCodeLine(lines, line - 1, markerLine + 1);
            }
            int candidateIndent = 0;
            while (candidateIndent < candidate.length() && (candidate.charAt(candidateIndent) == ' ' || candidate.charAt(candidateIndent) == '\t')) {
                candidateIndent++;
            }
            if (candidateIndent < markerIndent) {
                return line;
            }
        }
        return Math.max(markerLine, limit);
    }

    private static class PipelineMarkerInfo {
        int markerLine = 0;
        String semanticIdentity = "";
        boolean isFused = false;
        List<String> provenance = new ArrayList<>();
    }

    private static class GroupMarkerInfo {
        int markerLine = 0;
        String semanticIdentity = "";
        List<String> provenance = new ArrayList<>();
    }
}
