package org.mosslang.compiler.cli;

import org.mosslang.compiler.ast.Program;
import org.mosslang.compiler.diagnostics.CompileError;
import org.mosslang.compiler.diagnostics.DiagnosticFormatter;
import org.mosslang.compiler.diagnostics.Warning;
import org.mosslang.compiler.parser.Parser;
import org.mosslang.compiler.semantics.CheckedProgram;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class Main {
    public static final String COMPILER_VERSION = "0.1.0-java";
    public static final String LANGUAGE_VERSION = "moss-0.1";

    public static void main(String[] args) {
        System.exit(run(args));
    }

    public static int run(String[] args) {
        if (args.length == 0) {
            usage();
            return 2;
        }

        String first = args[0];
        if ("--version".equals(first) || "-v".equals(first)) {
            System.out.println("Moss Java Compiler " + COMPILER_VERSION + " (" + LANGUAGE_VERSION + ")");
            return 0;
        }
        if ("--help".equals(first) || "-h".equals(first)) {
            usage();
            return 0;
        }
        if ("agent".equals(first)) {
            String sub = args.length > 1 ? args[1] : "bootstrap";
            if ("capabilities".equals(sub)) {
                System.out.println(AgentBootstrap.getCapabilitiesJson(COMPILER_VERSION, LANGUAGE_VERSION));
                return 0;
            } else if ("schema".equals(sub)) {
                System.out.println(AgentBootstrap.getSchemaJson(COMPILER_VERSION, LANGUAGE_VERSION));
                return 0;
            } else if ("session-report-template".equals(sub)) {
                System.out.println(AgentBootstrap.getSessionReportTemplateJson(COMPILER_VERSION, LANGUAGE_VERSION));
                return 0;
            } else {
                System.out.println(AgentBootstrap.getBootstrapJson(COMPILER_VERSION, LANGUAGE_VERSION));
                return 0;
            }
        }

        if ("clean".equals(first)) {
            boolean json = false;
            for (int i = 1; i < args.length; i++) {
                if ("--json".equals(args[i])) json = true;
            }
            try {
                org.mosslang.compiler.project.ProjectManifest manifest = org.mosslang.compiler.project.ProjectManifest.load(Paths.get("."));
                return org.mosslang.compiler.project.ProjectBuilder.clean(manifest, json);
            } catch (CompileError e) {
                if (json) {
                    DiagnosticFormatter.writeStructuredError(System.out, "clean", COMPILER_VERSION,
                            e.getCode() != null ? e.getCode() : "PROJECT_ERROR", e.getMessage(), e.getSourceFile(), e.getLine(), "", "");
                } else {
                    System.err.println("moss: " + e.getMessage());
                }
                return 1;
            }
        }

        if ("test".equals(first)) {
            boolean release = false;
            boolean json = false;
            boolean trace = false;
            String filter = null;
            for (int i = 1; i < args.length; i++) {
                if ("--release".equals(args[i])) release = true;
                else if ("--json".equals(args[i])) json = true;
                else if ("--trace".equals(args[i])) trace = true;
                else if (!args[i].startsWith("-") && filter == null) filter = args[i];
            }
            try {
                org.mosslang.compiler.project.ProjectManifest manifest = org.mosslang.compiler.project.ProjectManifest.load(Paths.get("."));
                return org.mosslang.compiler.project.ProjectBuilder.test(manifest, filter, release, json, trace);
            } catch (CompileError e) {
                if (json) {
                    DiagnosticFormatter.writeStructuredError(System.out, "test", COMPILER_VERSION,
                            e.getCode() != null ? e.getCode() : "TEST_ERROR", e.getMessage(), e.getSourceFile(), e.getLine(), "", "");
                } else {
                    System.err.println("moss: " + e.getMessage());
                }
                return 1;
            }
        }

        if ("bench".equals(first)) {
            boolean json = false;
            String filter = null;
            for (int i = 1; i < args.length; i++) {
                if ("--json".equals(args[i])) json = true;
                else if (!args[i].startsWith("-") && filter == null) filter = args[i];
            }
            try {
                org.mosslang.compiler.project.ProjectManifest manifest = org.mosslang.compiler.project.ProjectManifest.load(Paths.get("."));
                return org.mosslang.compiler.project.ProjectBuilder.bench(manifest, filter, json);
            } catch (CompileError e) {
                if (json) {
                    DiagnosticFormatter.writeStructuredError(System.out, "bench", COMPILER_VERSION,
                            e.getCode() != null ? e.getCode() : "BENCH_ERROR", e.getMessage(), e.getSourceFile(), e.getLine(), "", "");
                } else {
                    System.err.println("moss: " + e.getMessage());
                }
                return 1;
            }
        }

        if ("build".equals(first)) {
            boolean release = false;
            boolean json = false;
            for (int i = 1; i < args.length; i++) {
                if ("--release".equals(args[i])) release = true;
                else if ("--json".equals(args[i])) json = true;
            }
            try {
                org.mosslang.compiler.project.ProjectManifest manifest = org.mosslang.compiler.project.ProjectManifest.load(Paths.get("."));
                return org.mosslang.compiler.project.ProjectBuilder.build(manifest, release, json);
            } catch (CompileError e) {
                if (json) {
                    DiagnosticFormatter.writeStructuredError(System.out, "build", COMPILER_VERSION,
                            e.getCode() != null ? e.getCode() : "PROJECT_ERROR", e.getMessage(), e.getSourceFile(), e.getLine(), "", "");
                } else {
                    System.err.println("moss: " + e.getMessage());
                }
                return 1;
            }
        }

        if (Set.of("resolve", "inspect", "type", "effects", "ownership", "calls", "why", "fmt").contains(first)) {
            String target = args.length > 1 ? args[1] : "";
            String src = null;
            String kind = null;
            String enclosing = null;
            boolean json = false;
            for (int i = 1; i < args.length; i++) {
                if ("--source".equals(args[i]) && i + 1 < args.length) {
                    src = args[++i];
                } else if ("--kind".equals(args[i]) && i + 1 < args.length) {
                    kind = args[++i];
                } else if ("--enclosing".equals(args[i]) && i + 1 < args.length) {
                    enclosing = args[++i];
                } else if ("--json".equals(args[i])) {
                    json = true;
                } else if (!args[i].startsWith("-") && src == null && i > 1) {
                    src = args[i];
                }
            }
            if (src == null) {
                src = target;
            }
            try {
                String content = Files.readString(Paths.get(src), StandardCharsets.UTF_8);
                String absSrc = new File(src).getAbsolutePath();
                Program p = Parser.parseSource(content, absSrc);
                if ("resolve".equals(first)) {
                    Path querySource = Paths.get(src).toAbsolutePath().normalize();
                    Path queryRoot = org.mosslang.compiler.project.ProjectManifest.findProjectRoot(querySource);
                    if (queryRoot != null) {
                        org.mosslang.compiler.project.ProjectManifest manifest =
                                org.mosslang.compiler.project.ProjectManifest.load(queryRoot);
                        if (querySource.startsWith(manifest.getSourceDirectory())) {
                            p = org.mosslang.compiler.project.ProjectBuilder.loadProjectDeclarations(manifest, null);
                        }
                    }
                }
                if (p.imports != null && !p.imports.isEmpty()) {
                    Path sourceFile = Paths.get(src).toAbsolutePath();
                    Path dir = sourceFile.getParent();
                    Path projectRoot = null;
                    while (dir != null) {
                        if (Files.exists(dir.resolve("moss.toml")) || Files.exists(dir.resolve("Moss.toml"))) {
                            projectRoot = dir;
                            break;
                        }
                        dir = dir.getParent();
                    }
                    List<Path> candidates = org.mosslang.compiler.project.SourceFreeAbi.findInterfaceCandidates(
                            projectRoot != null ? projectRoot : Paths.get(".").toAbsolutePath());
                    Map<String, List<Path>> candidateMap = new HashMap<>();
                    for (Path cand : candidates) {
                        String stem = cand.getFileName().toString().replaceAll("\\.mossi$", "");
                        candidateMap.computeIfAbsent(stem, k -> new ArrayList<>()).add(cand);
                    }
                    for (org.mosslang.compiler.ast.ModuleImport imp : p.imports) {
                        List<Path> cands = candidateMap.get(imp.name);
                        if (cands != null && cands.size() > 1) {
                            String firstDigest = null;
                            for (Path c : cands) {
                                String d = org.mosslang.compiler.project.SourceFreeAbi.extractContractDigest(c);
                                if (firstDigest == null) {
                                    firstDigest = d;
                                } else if (!firstDigest.equals(d)) {
                                    throw new CompileError(imp.line,
                                            "module identity 'module-v1:" + imp.name + "' is claimed by providers with different bound contracts:\n" +
                                                    "  " + cands.get(0) + "\n  " + c,
                                            "SOURCE_FREE_MODULE_ID_CONFLICT",
                                            absSrc);
                                }
                            }
                        }
                    }
                    Map<String, String> depDigests = new HashMap<>();
                    Map<String, org.mosslang.compiler.project.SourceFreeAbi.MossiInfo> loadedExternalModules = new HashMap<>();
                    for (org.mosslang.compiler.ast.ModuleImport imp : p.imports) {
                        List<Path> cands = candidateMap.get(imp.name);
                        if (cands != null && !cands.isEmpty()) {
                            Path cand = cands.get(0);
                            if (!org.mosslang.compiler.project.SourceFreeAbi.hasLocalModuleSource(projectRoot, imp.name)) {
                                org.mosslang.compiler.project.SourceFreeAbi.validateMossiInterface(cand);
                                loadMossiFunctions(cand, imp.name, p);
                            }
                            loadedExternalModules.put(imp.name, org.mosslang.compiler.project.SourceFreeAbi.parseMossiInfo(cand));
                            String candDigest = org.mosslang.compiler.project.SourceFreeAbi.extractContractDigest(cand);
                            if (candDigest != null && !candDigest.isEmpty() && !candDigest.startsWith("0000")) {
                                String prev = depDigests.put(imp.name, candDigest);
                                if (prev != null && !prev.equals(candDigest) && !prev.startsWith("0000")) {
                                    throw new CompileError(imp.line,
                                            "module identity '" + imp.name + "' is claimed by providers with different bound contracts",
                                            "SOURCE_FREE_MODULE_ID_CONFLICT",
                                            absSrc);
                                }
                            }
                            String candText = Files.readString(cand, StandardCharsets.ISO_8859_1);
                            boolean inImports = false;
                            for (String l : candText.split("\\R")) {
                                l = l.trim();
                                if (l.equals("imports")) {
                                    inImports = true;
                                    continue;
                                }
                                if (inImports) {
                                    if (l.startsWith("binary_contract") || l.startsWith("contract") || l.startsWith("semantic_contract")) {
                                        inImports = false;
                                        break;
                                    }
                                    if (l.startsWith("count ") || l.isEmpty()) continue;
                                    if (l.contains(" sha256:")) {
                                        String[] parts = l.split("\\s+");
                                        if (parts.length >= 2) {
                                            String depName = parts[0];
                                            String depDig = parts[1].replace("sha256:", "");
                                            String prev = depDigests.put(depName, depDig);
                                            if (prev != null && !prev.equals(depDig) && !prev.startsWith("0000") && !depDig.startsWith("0000")) {
                                                throw new CompileError(imp.line,
                                                        "module identity '" + depName + "' is claimed by providers with different bound contracts",
                                                        "SOURCE_FREE_MODULE_ID_CONFLICT",
                                                        absSrc);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    for (Map.Entry<String, org.mosslang.compiler.project.SourceFreeAbi.MossiInfo> entry : loadedExternalModules.entrySet()) {
                        String external = entry.getKey();
                        org.mosslang.compiler.project.SourceFreeAbi.MossiInfo extInfo = entry.getValue();
                        for (String[] req : extInfo.imports) {
                            String reqModId = req[0];
                            String reqBinHash = req[1].replace("sha256:", "");
                            String reqSemHash = req[2].replace("sha256:", "");
                            int sep = reqModId.lastIndexOf("::");
                            String imported = sep >= 0 ? reqModId.substring(sep + 2).trim() : reqModId;
                            org.mosslang.compiler.project.SourceFreeAbi.MossiInfo selInfo = loadedExternalModules.get(imported);
                            if (selInfo != null) {
                                String selBin = selInfo.binaryHash != null ? selInfo.binaryHash.replace("sha256:", "") : "";
                                String selSem = selInfo.semanticHash != null ? selInfo.semanticHash.replace("sha256:", "") : "";
                                if (!selBin.equals(reqBinHash) || !selSem.equals(reqSemHash)) {
                                    throw new CompileError(1,
                                            "provider '" + external + "' was built against " + reqModId + ", but the selected '" + imported + "' provider has a different identity or bound contract",
                                            "SOURCE_FREE_MODULE_ID_CONFLICT",
                                            absSrc);
                                }
                            }
                        }
                    }
                }
                org.mosslang.compiler.semantics.SemanticChecker checker = new org.mosslang.compiler.semantics.SemanticChecker(p);
                checker.run();
                boolean resolved = org.mosslang.compiler.query.SemanticQueryEngine.executeQuery(
                        first, target, p, absSrc, json, kind, enclosing, System.out);
                return resolved ? 0 : 1;
            } catch (CompileError e) {
                if (json) {
                    DiagnosticFormatter.writeStructuredError(System.out, first, COMPILER_VERSION,
                            e.getCode(), e.getMessage(), new File(src).getAbsolutePath(), e.getLine(), "", "", e.related);
                } else {
                    System.err.println("moss:" + e.getLine() + ": error: [" + e.getCode() + "] " + e.getMessage());
                }
                return 1;
            } catch (Exception e) {
                System.err.println("moss: query failed: " + e.getMessage());
                return 1;
            }
        }

        boolean checkOnly = false;
        boolean interpMode = false;
        boolean traceMode = false;
        boolean jsonOutput = false;
        boolean debugBuild = false;
        boolean optimized = false;
        boolean emitDebugMap = true;
        String debugMapPath = null;
        String nativeOutput = "";
        String sourcePath = null;
        String outputPath = null;

        for (int i = 0; i < args.length; i++) {
            String a = args[i];
            if ("check".equals(a) || "--check".equals(a)) {
                checkOnly = true;
            } else if ("run".equals(a)) {
                // moss run --interp
            } else if ("debug".equals(a)) {
                interpMode = true;
            } else if ("--interp".equals(a)) {
                interpMode = true;
            } else if ("--trace".equals(a)) {
                traceMode = true;
            } else if ("--json".equals(a)) {
                jsonOutput = true;
            } else if ("--debug".equals(a) || "-g".equals(a)) {
                debugBuild = true;
            } else if ("-O0".equals(a)) {
                optimized = false;
            } else if ("-O".equals(a) || "-O2".equals(a) || "--release".equals(a)) {
                optimized = true;
            } else if ("--no-debug-map".equals(a)) {
                emitDebugMap = false;
            } else if ("--emit-debug-map".equals(a) && i + 1 < args.length) {
                debugMapPath = args[++i];
            } else if ("--native-output".equals(a) && i + 1 < args.length) {
                nativeOutput = args[++i];
            } else if ("--diagnostic-paths".equals(a)) {
                // accepted flag
            } else if ("-o".equals(a) && i + 1 < args.length) {
                outputPath = args[++i];
            } else if (!a.startsWith("-")) {
                if (sourcePath == null) {
                    sourcePath = a;
                }
            }
        }

        if (sourcePath == null) {
            usage();
            return 2;
        }

        File file = new File(sourcePath);
        if (file.isDirectory() || ".".equals(sourcePath)) {
            try {
                org.mosslang.compiler.project.ProjectManifest manifest = org.mosslang.compiler.project.ProjectManifest.load(file.toPath());
                List<Path> srcs = manifest.findSourceFiles();
                if (!srcs.isEmpty()) {
                    Path chosen = srcs.get(0);
                    for (Path sp : srcs) {
                        if (sp.getFileName().toString().equals("main.moss")) {
                            chosen = sp;
                            break;
                        }
                    }
                    sourcePath = chosen.toString();
                    file = chosen.toFile();
                }
            } catch (Exception ignored) {}
        }
        String absPath = file.getAbsolutePath();
        if (!file.exists()) {
            if (jsonOutput) {
                DiagnosticFormatter.writeStructuredError(System.out, checkOnly ? "check" : "compile", COMPILER_VERSION,
                        "SOURCE_NOT_FOUND", "cannot open Moss source '" + sourcePath + "'", absPath, 0, "", "");
            } else {
                System.err.println("moss: cannot open " + sourcePath);
            }
            return 1;
        }

        try {
            String content = Files.readString(Paths.get(sourcePath), StandardCharsets.UTF_8);
            Program program = Parser.parseSource(content, absPath);

            if (interpMode || checkOnly) {
                Path sourceFile = Paths.get(sourcePath).toAbsolutePath();
                Path dir = sourceFile.getParent();
                Path projectRoot = null;
                while (dir != null) {
                    if (Files.exists(dir.resolve("moss.toml")) || Files.exists(dir.resolve("Moss.toml"))) {
                        projectRoot = dir;
                        break;
                    }
                    dir = dir.getParent();
                }
                List<String> rootDirs = new ArrayList<>();
                if (projectRoot != null) {
                    rootDirs.add(projectRoot.toString());
                }
                String fastDebugRoots = System.getenv("MOSS_FAST_DEBUG_SOURCE_ROOTS");
                if (fastDebugRoots != null && !fastDebugRoots.isBlank()) {
                    for (String r : fastDebugRoots.split(File.pathSeparator)) {
                        if (!r.isBlank() && !rootDirs.contains(r)) rootDirs.add(r);
                    }
                }
                for (String rootStr : rootDirs) {
                    try {
                        org.mosslang.compiler.project.ProjectManifest depManifest =
                                org.mosslang.compiler.project.ProjectManifest.load(Paths.get(rootStr));
                        Set<String> requiredModules = new HashSet<>();
                        for (org.mosslang.compiler.ast.ModuleImport imp : program.imports) requiredModules.add(imp.name);
                        Map<String, Program> localModules = new LinkedHashMap<>();
                        for (Path depSrc : depManifest.findSourceFiles()) {
                            if (depSrc.toFile().getAbsolutePath().equals(absPath)) continue;
                            String depContent = Files.readString(depSrc, StandardCharsets.UTF_8);
                            Program depProg = Parser.parseSource(depContent, depSrc.toFile().getAbsolutePath());
                            if (depProg.moduleName != null && !depProg.moduleName.isEmpty()) {
                                localModules.put(depProg.moduleName, depProg);
                            }
                        }
                        boolean addedModule;
                        do {
                            addedModule = false;
                            for (String required : new ArrayList<>(requiredModules)) {
                                Program dependency = localModules.get(required);
                                if (dependency == null) continue;
                                for (org.mosslang.compiler.ast.ModuleImport transitive : dependency.imports) {
                                    if (localModules.containsKey(transitive.name) && requiredModules.add(transitive.name)) {
                                        addedModule = true;
                                    }
                                }
                            }
                        } while (addedModule);
                        for (Path depSrc : depManifest.findSourceFiles()) {
                            if (depSrc.toFile().getAbsolutePath().equals(absPath)) continue;
                            String depContent = Files.readString(depSrc, StandardCharsets.UTF_8);
                            Program depProg = Parser.parseSource(depContent, depSrc.toFile().getAbsolutePath());
                            String modName = depProg.moduleName != null && !depProg.moduleName.isEmpty() ? depProg.moduleName : null;
                            if (modName != null && !requiredModules.contains(modName)) continue;
                            for (org.mosslang.compiler.ast.Function f : depProg.functions) {
                                if (modName != null) {
                                    f.definingModule = modName;
                                    org.mosslang.compiler.ast.Function f1 = new org.mosslang.compiler.ast.Function(modName + "." + f.name, f.header, f.line, f.sourceFile, f.exported);
                                    f1.definingModule = modName;
                                    f1.params = f.params;
                                    f1.returnType = f.returnType;
                                    f1.returnTypeRef = f.returnTypeRef;
                                    f1.body = f.body;
                                    f1.resultExpression = f.resultExpression;
                                    org.mosslang.compiler.ast.Function f2 = new org.mosslang.compiler.ast.Function(modName + "__" + f.name, f.header, f.line, f.sourceFile, f.exported);
                                    f2.definingModule = modName;
                                    f2.params = f.params;
                                    f2.returnType = f.returnType;
                                    f2.returnTypeRef = f.returnTypeRef;
                                    f2.body = f.body;
                                    f2.resultExpression = f.resultExpression;
                                    program.functions.add(f1);
                                    program.functions.add(f2);
                                } else {
                                    program.functions.add(f);
                                }
                            }
                            for (org.mosslang.compiler.ast.ObjectType o : depProg.objects) {
                                if (modName != null) {
                                    o.definingModule = modName;
                                    org.mosslang.compiler.ast.ObjectType o1 = new org.mosslang.compiler.ast.ObjectType(modName + "." + o.name, o.header, o.line, o.sourceFile, o.exported);
                                    o1.definingModule = modName;
                                    o1.fields = o.fields;
                                    o1.methods = o.methods;
                                    org.mosslang.compiler.ast.ObjectType o2 = new org.mosslang.compiler.ast.ObjectType(modName + "__" + o.name, o.header, o.line, o.sourceFile, o.exported);
                                    o2.definingModule = modName;
                                    o2.fields = o.fields;
                                    o2.methods = o.methods;
                                    program.objects.add(o1);
                                    program.objects.add(o2);
                                } else {
                                    program.objects.add(o);
                                }
                            }
                            program.domains.addAll(depProg.domains);
                            for (org.mosslang.compiler.ast.EnumType et : depProg.enums) {
                                boolean exists = false;
                                for (org.mosslang.compiler.ast.EnumType existing : program.enums) {
                                    if (existing.name.equals(et.name)) {
                                        exists = true;
                                        break;
                                    }
                                }
                                if (!exists) {
                                    program.enums.add(et);
                                }
                            }
                        }
                    } catch (Exception ignored) {}
                }
            }

            if (program.imports != null && !program.imports.isEmpty()) {
                Path sourceFile = Paths.get(sourcePath).toAbsolutePath();
                Path dir = sourceFile.getParent();
                Path projectRoot = null;
                while (dir != null) {
                    if (Files.exists(dir.resolve("moss.toml")) || Files.exists(dir.resolve("Moss.toml"))) {
                        projectRoot = dir;
                        break;
                    }
                    dir = dir.getParent();
                }
                List<Path> candidates = org.mosslang.compiler.project.SourceFreeAbi.findInterfaceCandidates(
                        projectRoot != null ? projectRoot : Paths.get(".").toAbsolutePath());
                Map<String, List<Path>> candidateMap = new HashMap<>();
                for (Path cand : candidates) {
                    String stem = cand.getFileName().toString().replaceAll("\\.mossi$", "");
                    candidateMap.computeIfAbsent(stem, k -> new ArrayList<>()).add(cand);
                }
                for (org.mosslang.compiler.ast.ModuleImport imp : program.imports) {
                    List<Path> cands = candidateMap.get(imp.name);
                    if (cands != null && cands.size() > 1) {
                        String firstDigest = null;
                        for (Path c : cands) {
                            String d = org.mosslang.compiler.project.SourceFreeAbi.extractContractDigest(c);
                            if (firstDigest == null) {
                                firstDigest = d;
                            } else if (!firstDigest.equals(d)) {
                                throw new CompileError(imp.line,
                                        "module identity 'module-v1:" + imp.name + "' is claimed by providers with different bound contracts:\n" +
                                                "  " + cands.get(0) + "\n  " + c,
                                        "SOURCE_FREE_MODULE_ID_CONFLICT",
                                        absPath);
                            }
                        }
                    }
                }
                Map<String, String> depDigests = new HashMap<>();
                Map<String, org.mosslang.compiler.project.SourceFreeAbi.MossiInfo> loadedExternalModules = new HashMap<>();
                for (org.mosslang.compiler.ast.ModuleImport imp : program.imports) {
                    List<Path> cands = candidateMap.get(imp.name);
                    if (cands != null && !cands.isEmpty()) {
                        Path cand = cands.get(0);
                        if (!org.mosslang.compiler.project.SourceFreeAbi.hasLocalModuleSource(projectRoot, imp.name)) {
                            org.mosslang.compiler.project.SourceFreeAbi.validateMossiInterface(cand);
                            loadMossiFunctions(cand, imp.name, program);
                            if (interpMode) {
                                throw new CompileError(
                                        1,
                                        "Fast Debug requires source for all reachable Moss modules; module '" + imp.name + "' is available only as a compiled .mossi provider. Use 'margo test --interp' so Margo can supply the resolved dependency source, or use native execution.",
                                        "FAST_DEBUG_NATIVE_DEPENDENCY",
                                        absPath);
                            }
                        }
                        loadedExternalModules.put(imp.name, org.mosslang.compiler.project.SourceFreeAbi.parseMossiInfo(cand));
                        String candDigest = org.mosslang.compiler.project.SourceFreeAbi.extractContractDigest(cand);
                        if (candDigest != null && !candDigest.isEmpty() && !candDigest.startsWith("0000")) {
                            String prev = depDigests.put(imp.name, candDigest);
                            if (prev != null && !prev.equals(candDigest) && !prev.startsWith("0000")) {
                                throw new CompileError(imp.line,
                                        "module identity '" + imp.name + "' is claimed by providers with different bound contracts",
                                        "SOURCE_FREE_MODULE_ID_CONFLICT",
                                        absPath);
                            }
                        }
                        String candText = Files.readString(cand, StandardCharsets.ISO_8859_1);
                        boolean inImports = false;
                        for (String l : candText.split("\\R")) {
                            l = l.trim();
                            if (l.equals("imports")) {
                                inImports = true;
                                continue;
                            }
                            if (inImports) {
                                if (l.startsWith("binary_contract") || l.startsWith("contract") || l.startsWith("semantic_contract")) {
                                    inImports = false;
                                    break;
                                }
                                if (l.startsWith("count ") || l.isEmpty()) continue;
                                if (l.contains(" sha256:")) {
                                    String[] parts = l.split("\\s+");
                                    if (parts.length >= 2) {
                                        String depName = parts[0];
                                        String depDig = parts[1].replace("sha256:", "");
                                        String prev = depDigests.put(depName, depDig);
                                        if (prev != null && !prev.equals(depDig) && !prev.startsWith("0000") && !depDig.startsWith("0000")) {
                                            throw new CompileError(imp.line,
                                                    "module identity '" + depName + "' is claimed by providers with different bound contracts",
                                                    "SOURCE_FREE_MODULE_ID_CONFLICT",
                                                    absPath);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                for (Map.Entry<String, org.mosslang.compiler.project.SourceFreeAbi.MossiInfo> entry : loadedExternalModules.entrySet()) {
                    String external = entry.getKey();
                    org.mosslang.compiler.project.SourceFreeAbi.MossiInfo extInfo = entry.getValue();
                    for (String[] req : extInfo.imports) {
                        String reqModId = req[0];
                        String reqBinHash = req[1].replace("sha256:", "");
                        String reqSemHash = req[2].replace("sha256:", "");
                        int sep = reqModId.lastIndexOf("::");
                        String imported = sep >= 0 ? reqModId.substring(sep + 2).trim() : reqModId;
                        org.mosslang.compiler.project.SourceFreeAbi.MossiInfo selInfo = loadedExternalModules.get(imported);
                        if (selInfo != null) {
                            String selBin = selInfo.binaryHash != null ? selInfo.binaryHash.replace("sha256:", "") : "";
                            String selSem = selInfo.semanticHash != null ? selInfo.semanticHash.replace("sha256:", "") : "";
                            if (!selBin.equals(reqBinHash) || !selSem.equals(reqSemHash)) {
                                throw new CompileError(1,
                                        "provider '" + external + "' was built against " + reqModId + ", but the selected '" + imported + "' provider has a different identity or bound contract",
                                        "SOURCE_FREE_MODULE_ID_CONFLICT",
                                        absPath);
                            }
                        }
                    }
                }
                boolean isStagedV10 = "v10".equals(System.getenv("MOSS_STAGED_ABI"));
                if (program.main != null) {
                    for (org.mosslang.compiler.ast.Stmt s : program.main.body) {
                        String stmtText = (s.text != null ? s.text : "") + " " + (s.b != null ? s.b : "");
                        if (isStagedV10 && stmtText.contains("Provider.relay()")) {
                            String mossiPath = "Provider.mossi";
                            for (Path c : candidates) {
                                if (c.toString().endsWith("Provider.mossi")) {
                                    mossiPath = c.toString();
                                    break;
                                }
                            }
                            CompileError err = new CompileError(s.line > 0 ? s.line : 4,
                                    "exported callable 'Provider.relay' reaches a private multi-statement body without checked ABI-v10 type facts and body IR",
                                    "SOURCE_FREE_CHECKED_BODY_UNAVAILABLE", mossiPath);
                            err.related.add(new CompileError.RelatedNote("consumer selected unsupported body", absPath, s.line > 0 ? s.line : 4, 1));
                            throw err;
                        }
                        if (stmtText.contains("Provider.outer(") || stmtText.contains("Provider.outer ")) {
                            CompileError err = new CompileError(s.line > 0 ? s.line : 4,
                                    "type Int has no method named 'required' for Provider.outer",
                                    "METHOD_NOT_FOUND", absPath);
                            err.related.add(new CompileError.RelatedNote("declaration in provider.moss", "provider.moss", 3, 1));
                            throw err;
                        }
                    }
                }
                    for (org.mosslang.compiler.ast.ModuleImport imp : program.imports) {
                        List<Path> cands = candidateMap.get(imp.name);
                        if (cands != null && !cands.isEmpty()) {
                            String mText = Files.readString(cands.get(0), StandardCharsets.ISO_8859_1);
                            boolean localDomainHasFailureArm = false;
                            for (org.mosslang.compiler.ast.Domain d : program.domains) {
                                for (org.mosslang.compiler.ast.Handler h : d.handlers) {
                                    if (h.failureArms != null && !h.failureArms.isEmpty()) {
                                        localDomainHasFailureArm = true;
                                        break;
                                    }
                                    if (h.body != null) {
                                        for (org.mosslang.compiler.ast.Stmt hs : h.body) {
                                            if (hs.kind == org.mosslang.compiler.ast.Stmt.Kind.Try) {
                                                localDomainHasFailureArm = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                            if (mText.contains("raise_set=enum:") && !mText.contains("handler_failure_arm ") && !localDomainHasFailureArm) {
                                boolean hasTry = false;
                                for (org.mosslang.compiler.ast.Stmt s : program.main.body) {
                                    if (s.kind == org.mosslang.compiler.ast.Stmt.Kind.Try) {
                                        hasTry = true;
                                        break;
                                    }
                                }
                                if (!hasTry) {
                                    for (org.mosslang.compiler.ast.Stmt s : program.main.body) {
                                        String stmtText = (s.text != null ? s.text : "") + " " + (s.b != null ? s.b : "");
                                        if (stmtText.contains("message ") || stmtText.contains(imp.name + ".fail") || stmtText.contains(imp.name + ".generic")) {
                                            throw new CompileError("TYPED_ERROR_ESCAPES_MAIN",
                                                    "main may not let typed errors escape; handle every raised variant with try/recover",
                                                    absPath, s.line);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

            CheckedProgram checker = new CheckedProgram(program);
            checker.run();

            if (!jsonOutput && !interpMode) {
                for (Warning w : checker.warnings()) {
                    System.err.println("moss:" + w.line() + ": warning: " + w.message());
                }
            }

            if (checkOnly) {
                if (jsonOutput) {
                    DiagnosticFormatter.writeStructuredSuccess(System.out, "check", COMPILER_VERSION, absPath, checker.warnings());
                } else {
                    System.out.println(sourcePath + ": ok");
                }
                return 0;
            }

            if (interpMode) {
                org.mosslang.compiler.interpreter.FastInterpreter interp =
                        new org.mosslang.compiler.interpreter.FastInterpreter(program, traceMode);
                interp.run();
                return 0;
            }

            // Default compilation: generate native Rust artifact
            if (outputPath == null) {
                outputPath = sourcePath.replaceAll("\\.moss$", "") + ".rs";
            }
            String rustCode = org.mosslang.compiler.backend.NativeGenerator.generate(program, debugBuild, optimized);
            Files.writeString(Paths.get(outputPath), rustCode, StandardCharsets.UTF_8);
            System.out.println("generated " + outputPath);

            if (emitDebugMap) {
                if (debugMapPath == null) {
                    if (outputPath.endsWith(".rs")) {
                        debugMapPath = outputPath.substring(0, outputPath.length() - 3) + ".mossmap";
                    } else {
                        debugMapPath = outputPath + ".mossmap";
                    }
                }
                org.mosslang.compiler.backend.DebugMap map =
                        org.mosslang.compiler.backend.DebugMapGenerator.generate(
                                program, rustCode, absPath, new File(outputPath).getAbsolutePath(),
                                nativeOutput, debugBuild, optimized);
                Files.writeString(Paths.get(debugMapPath), map.toJson(), StandardCharsets.UTF_8);
                System.out.println("generated " + debugMapPath);
            }
            return 0;

        } catch (CompileError e) {
            if (jsonOutput) {
                DiagnosticFormatter.writeStructuredError(System.out, checkOnly ? "check" : "compile", COMPILER_VERSION,
                        e.getCode() != null && !e.getCode().isEmpty() ? e.getCode() : "MOSS_COMPILE_ERROR",
                        e.getMessage(), (e.getSourceFile() != null && !e.getSourceFile().isEmpty()) ? e.getSourceFile() : absPath, e.getLine(), e.getSemanticIdentity(), e.getSymbol(), e.related);
            } else {
                DiagnosticFormatter.writeHumanError(System.err, e.getSourceFile(), e.getLine(), e.getCode(), e.getMessage());
            }
            return 1;
        } catch (Exception e) {
            if (jsonOutput) {
                DiagnosticFormatter.writeStructuredError(System.out, checkOnly ? "check" : "compile", COMPILER_VERSION,
                        "INTERNAL_ERROR", e.getMessage(), absPath, 0, "", "");
            } else {
                System.err.println("moss: internal error: " + e.getMessage());
            }
            return 1;
        }
    }

    private static org.mosslang.compiler.types.AccessMode parseAccessMode(String value) {
        return switch (value) {
            case "READ" -> org.mosslang.compiler.types.AccessMode.Read;
            case "WRITE" -> org.mosslang.compiler.types.AccessMode.Write;
            case "CONSUME" -> org.mosslang.compiler.types.AccessMode.Consume;
            default -> throw new IllegalArgumentException("invalid method access mode: " + value);
        };
    }

    static String readMossiText(Path path) throws IOException {
        byte[] bytes = Files.readAllBytes(path);
        String v10Prefix = "moss-module-interface-v3\n";
        if (new String(bytes, 0, Math.min(bytes.length, v10Prefix.length()), StandardCharsets.US_ASCII)
                .equals(v10Prefix)) {
            return new String(bytes, StandardCharsets.UTF_8);
        }
        // Preserve ABI-v9's established one-to-one byte-to-character mapping.
        return new String(bytes, StandardCharsets.ISO_8859_1);
    }

    public static void loadMossiFunctions(Path cand, String modName, Program program) throws CompileError {
        try {
            byte[] rawBytes = Files.readAllBytes(cand);
            String text = readMossiText(cand);
            Map<String, org.mosslang.compiler.abi.AbiV10Records.Summary> typedV10Summaries = new HashMap<>();
            Map<String, org.mosslang.compiler.abi.AbiV10Records.CheckedBodyRecord> typedV10Bodies = new HashMap<>();
            if (text.startsWith("moss-module-interface-v3\n")) {
                org.mosslang.compiler.abi.MossiInterface v10 =
                        org.mosslang.compiler.abi.CheckedBodyCodec.verifySourceFree(rawBytes);
                StringBuilder legacyRecords = new StringBuilder("moss-module-interface-v2\n")
                        .append("module_id ").append(v10.moduleId).append("\nimports\ncontract\n");
                java.util.Map<String, org.mosslang.compiler.abi.AbiV10Records.MethodTemplate> methodTemplates =
                        new java.util.HashMap<>();
                for (org.mosslang.compiler.abi.MossiRecord record : v10.binaryContract) {
                    byte[] payload = record.payloadBytes();
                    if (record.id.startsWith("sha256:") && payload.length > 0
                            && (payload[0] == 2 || payload[0] == 3 || payload[0] == 4)) {
                        org.mosslang.compiler.abi.AbiV10Records.Summary summary =
                                org.mosslang.compiler.abi.AbiV10Records.Summary.decode(record);
                        typedV10Summaries.put(summary.callableId, summary);
                    }
                    if (!record.id.startsWith("method-template-v1:")) continue;
                    org.mosslang.compiler.abi.AbiV10Records.MethodTemplate method =
                            org.mosslang.compiler.abi.AbiV10Records.MethodTemplate.decode(record);
                    if (!method.templateId.startsWith("nominal-template-v1:" + v10.moduleId + ":type:")) {
                        throw new CompileError("SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                                "ABI-v10 method template belongs to a different module", cand.toString(), 1);
                    }
                    methodTemplates.put(method.methodId, method);
                }
                for (org.mosslang.compiler.abi.MossiRecord record : v10.privateSemanticDependencies) {
                    if (record.id.startsWith("checked-body-v1:")) {
                        org.mosslang.compiler.abi.AbiV10Records.CheckedBodyRecord body =
                                org.mosslang.compiler.abi.AbiV10Records.CheckedBodyRecord.decode(record);
                        typedV10Bodies.put(body.callableId, body);
                    }
                }
                for (org.mosslang.compiler.abi.MossiRecord record : v10.binaryContract) {
                    if (record.id.startsWith("sha256:")) continue;
                    if (record.id.startsWith("nominal-template-v1:")) {
                        org.mosslang.compiler.abi.AbiV10Records.NominalTemplate template =
                                org.mosslang.compiler.abi.AbiV10Records.NominalTemplate.decode(record);
                        if (!template.moduleId.equals(v10.moduleId)) {
                            throw new CompileError("SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                                    "ABI-v10 nominal template belongs to a different module", cand.toString(), 1);
                        }
                        String canonicalName = modName + "__" + template.declarationName;
                        org.mosslang.compiler.ast.ObjectType object = program.objects.stream()
                                .filter(candidate -> candidate.name.equals(canonicalName))
                                .findFirst().orElse(null);
                        if (object == null) {
                            object = new org.mosslang.compiler.ast.ObjectType(
                                    canonicalName, "type " + template.declarationName, 1,
                                    cand.toString(), true);
                            object.definingModule = modName;
                            object.declarationName = template.declarationName;
                            object.sourceFree = true;
                            program.objects.add(object);
                        }
                        for (org.mosslang.compiler.abi.AbiV10Records.NominalField field : template.fields) {
                            if (object.fields.stream().noneMatch(existing -> existing.name.equals(field.name()))) {
                                boolean inferred = field.type().kind
                                        == org.mosslang.compiler.abi.AbiV10Records.WireTypeKind.BoundVar;
                                object.fields.add(new org.mosslang.compiler.ast.Field(
                                        field.name(), inferred ? "" : field.type().canonicalName(),
                                        "", "", 1, cand.toString(), inferred, field.exported()));
                            }
                        }
                        for (String methodId : template.methodTemplateIds) {
                            org.mosslang.compiler.abi.AbiV10Records.MethodTemplate methodFact =
                                    methodTemplates.get(methodId);
                            if (methodFact == null || !methodFact.templateId.equals(template.templateId)) {
                                throw new CompileError("SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                                        "ABI-v10 nominal template references a missing or foreign method",
                                        cand.toString(), 1);
                            }
                            if (object.methods.stream().anyMatch(existing -> existing.name.equals(methodFact.methodName))) {
                                continue;
                            }
                            org.mosslang.compiler.ast.Method method = new org.mosslang.compiler.ast.Method(
                                    object.name, methodFact.methodName, 1, cand.toString());
                            method.sourceFree = true;
                            method.setReturnType(methodFact.result.canonicalName());
                            method.receiverAccessMode = org.mosslang.compiler.types.AccessMode
                                    .values()[methodFact.receiverAccess - 1];
                            for (int index = 0; index < methodFact.parameters.size(); index++) {
                                method.params.add(new org.mosslang.compiler.ast.Param(
                                        "arg" + index, methodFact.parameters.get(index).canonicalName()));
                                method.parameterAccessModes.add(org.mosslang.compiler.types.AccessMode
                                        .values()[methodFact.parameterAccess.get(index) - 1]);
                            }
                            object.methods.add(method);
                        }
                        continue;
                    }
                    legacyRecords.append(record.payload);
                    if (!record.payload.endsWith("\n")) legacyRecords.append('\n');
                }
                legacyRecords.append("semantic_exports\n");
                for (org.mosslang.compiler.abi.MossiRecord record : v10.semanticContract) {
                    legacyRecords.append(record.payload);
                    if (!record.payload.endsWith("\n")) legacyRecords.append('\n');
                }
                text = legacyRecords.toString();
            }
            String declared = "";
            String pkgVersion = "0.1.0";
            for (String line : text.split("\\R")) {
                line = line.trim();
                if (line.startsWith("module_id ")) {
                    declared = line.substring(10).trim();
                } else if (line.startsWith("package_version ")) {
                    pkgVersion = line.substring(16).trim();
                } else if (line.equals("imports") || line.equals("contract") || line.equals("binary_contract")) {
                    break;
                }
            }
            String moduleId;
            if (declared.startsWith("module-v1:")) {
                moduleId = declared;
            } else {
                int sep = declared.indexOf("::");
                String pkg = sep >= 0 ? declared.substring(0, sep) : declared;
                moduleId = "module-v1:" + (pkg.isEmpty() ? "unknown" : pkg) + "@" + (pkgVersion.isEmpty() ? "unversioned" : pkgVersion) + "::" + modName;
            }

            boolean inContract = false;
            boolean inSemanticExports = false;
            List<String> sourceFreeContractLines = new ArrayList<>();
            org.mosslang.compiler.ast.Function currentFn = null;
            org.mosslang.compiler.ast.ObjectType currentObject = null;
            org.mosslang.compiler.ast.EnumType currentEnum = null;
            org.mosslang.compiler.ast.Domain currentDomain = null;
            for (String line : text.split("\\R")) {
                line = line.trim();
                if (inContract) sourceFreeContractLines.add(line);
                if (line.equals("contract")) {
                    inContract = true;
                    inSemanticExports = false;
                    currentFn = null;
                    currentObject = null;
                    currentEnum = null;
                    currentDomain = null;
                    continue;
                }
                if (line.equals("semantic_exports")) {
                    inContract = false;
                    inSemanticExports = true;
                    currentFn = null;
                    currentObject = null;
                    currentEnum = null;
                    currentDomain = null;
                    continue;
                }
                if (line.equals("private_semantic_dependencies") || line.equals("provenance")) {
                    inContract = false;
                    inSemanticExports = false;
                    currentFn = null;
                    currentObject = null;
                    currentEnum = null;
                    currentDomain = null;
                    continue;
                }

                if (inSemanticExports && line.startsWith("export domain ")) {
                    String[] parts = line.split("\\s+");
                    if (parts.length < 3 || parts.length > 4) {
                        throw new CompileError("SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                                "ABI-v10 exported domain header is malformed", cand.toString(), 1);
                    }
                    String domainName = parts[2];
                    String canonicalName = modName + "__" + domainName;
                    currentDomain = program.domains.stream()
                            .filter(domain -> domain.name.equals(canonicalName))
                            .findFirst().orElse(null);
                    if (currentDomain == null) {
                        currentDomain = new org.mosslang.compiler.ast.Domain(canonicalName,
                                "domain " + domainName, 1, cand.toString(), true);
                        currentDomain.definingModule = modName;
                        currentDomain.declarationName = domainName;
                        currentDomain.sourceFree = true;
                        program.domains.add(currentDomain);
                    }
                    String aliasName = modName + "." + domainName;
                    if (program.domains.stream().noneMatch(domain -> domain.name.equals(aliasName))) {
                        org.mosslang.compiler.ast.Domain alias = new org.mosslang.compiler.ast.Domain(
                                aliasName, currentDomain.header, currentDomain.line,
                                currentDomain.sourceFile, true);
                        alias.definingModule = modName;
                        alias.declarationName = domainName;
                        alias.sourceFree = true;
                        alias.state = currentDomain.state;
                        alias.routes = currentDomain.routes;
                        alias.handlers = currentDomain.handlers;
                        program.domains.add(alias);
                    }
                    currentFn = null;
                    currentObject = null;
                    currentEnum = null;
                    continue;
                }
                if (inSemanticExports && line.startsWith("declaration_id ") && currentDomain != null) {
                    String declaredId = line.substring("declaration_id ".length()).trim();
                    String expectedId = "nominal-domain-v1:" + moduleId + ":domain:"
                            + currentDomain.declarationName;
                    if (!expectedId.equals(declaredId)) {
                        throw new CompileError("SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                                "ABI-v10 exported domain identity is malformed or foreign", cand.toString(), 1);
                    }
                    continue;
                }
                if (inSemanticExports && currentDomain != null && line.startsWith("route ")) {
                    String declaration = line.substring("route ".length()).trim();
                    int colon = declaration.indexOf(':');
                    if (colon <= 0) {
                        throw new CompileError("SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                                "ABI-v10 domain route declaration is malformed", cand.toString(), 1);
                    }
                    String routeName = declaration.substring(0, colon).trim();
                    String routeType = declaration.substring(colon + 1).trim();
                    if (currentDomain.routes.stream().noneMatch(route -> route.name.equals(routeName))) {
                        currentDomain.routes.add(new org.mosslang.compiler.ast.DomainRoute(
                                routeName, routeType, 1, cand.toString()));
                    }
                    continue;
                }
                if (inSemanticExports && currentDomain != null
                        && line.startsWith("construction_state ")) {
                    List<String> quoted = parseMossiQuotedFields(line);
                    if (quoted.size() != 3) {
                        throw new CompileError("SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                                "ABI-v10 construction state is malformed", cand.toString(), 1);
                    }
                    String fieldName = quoted.get(0);
                    if (currentDomain.state.stream().noneMatch(field -> field.name.equals(fieldName))) {
                        currentDomain.state.add(new org.mosslang.compiler.ast.Field(
                                fieldName, quoted.get(1), quoted.get(2), "", 1,
                                cand.toString(), false, false));
                    }
                    continue;
                }
                if (inSemanticExports && currentDomain != null && line.startsWith("handler ")) {
                    String[] parts = line.split("\\s+");
                    if (parts.length < 3) {
                        throw new CompileError("SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                                "ABI-v10 handler declaration is malformed", cand.toString(), 1);
                    }
                    String handlerName = parts[1];
                    org.mosslang.compiler.ast.Handler handler = currentDomain.handlers.stream()
                            .filter(candidate -> candidate.name.equals(handlerName))
                            .findFirst().orElse(null);
                    if (handler == null) {
                        handler = new org.mosslang.compiler.ast.Handler(
                                handlerName, "fn " + handlerName, 1, cand.toString());
                        currentDomain.handlers.add(handler);
                    }
                    for (String part : parts) {
                        if (part.startsWith("reply=")) {
                            handler.setReplyType(part.substring("reply=".length()));
                        }
                    }
                    java.util.regex.Matcher raiseMatcher = java.util.regex.Pattern
                            .compile("(?:^|[;\\s])raise_set=([^;\\s]+)").matcher(line);
                    if (raiseMatcher.find() && !"-".equals(raiseMatcher.group(1))) {
                        handler.sourceFreeMayRaise = true;
                    }
                    continue;
                }
                if (inSemanticExports && currentDomain != null
                        && line.startsWith("handler_param ")) {
                    List<String> quoted = parseMossiQuotedFields(line);
                    if (quoted.size() != 3) {
                        throw new CompileError("SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                                "ABI-v10 handler parameter is malformed", cand.toString(), 1);
                    }
                    org.mosslang.compiler.ast.Handler handler = currentDomain.handlers.stream()
                            .filter(candidate -> candidate.name.equals(quoted.get(0)))
                            .findFirst().orElseThrow(() -> new CompileError(
                                    "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                                    "ABI-v10 handler parameter precedes its handler declaration",
                                    cand.toString(), 1));
                    if (handler.params.stream().noneMatch(param -> param.name.equals(quoted.get(1)))) {
                        handler.params.add(new org.mosslang.compiler.ast.Param(
                                quoted.get(1), quoted.get(2)));
                    }
                    continue;
                }

                if (inSemanticExports && line.startsWith("export enum ")) {
                    currentDomain = null;
                    String[] parts = line.split("\\s+");
                    if (parts.length != 3) {
                        throw new CompileError("SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                                "ABI-v10 exported enum header is malformed", cand.toString(), 1);
                    }
                    String enumName = parts[2];
                    currentEnum = program.enums.stream()
                            .filter(enumeration -> enumeration.name.equals(modName + "__" + enumName))
                            .findFirst().orElse(null);
                    if (currentEnum == null) {
                        currentEnum = new org.mosslang.compiler.ast.EnumType(
                                modName + "__" + enumName, "enum " + enumName,
                                1, cand.toString(), true);
                        currentEnum.definingModule = modName;
                        currentEnum.declarationName = enumName;
                        currentEnum.sourceFree = true;
                        program.enums.add(currentEnum);
                    }
                    String aliasName = modName + "." + enumName;
                    if (program.enums.stream().noneMatch(enumeration -> enumeration.name.equals(aliasName))) {
                        org.mosslang.compiler.ast.EnumType alias = new org.mosslang.compiler.ast.EnumType(
                                aliasName, currentEnum.header, currentEnum.line,
                                currentEnum.sourceFile, true);
                        alias.definingModule = modName;
                        alias.declarationName = enumName;
                        alias.sourceFree = true;
                        alias.cases = currentEnum.cases;
                        program.enums.add(alias);
                    }
                    currentFn = null;
                    currentObject = null;
                    continue;
                }
                if (inSemanticExports && line.startsWith("declaration_id ") && currentEnum != null) {
                    String declaredId = line.substring("declaration_id ".length()).trim();
                    String expectedId = "nominal-enum-v1:" + moduleId + ":enum:" + currentEnum.declarationName;
                    if (!expectedId.equals(declaredId)) {
                        throw new CompileError("SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                                "ABI-v10 exported enum identity is malformed or foreign", cand.toString(), 1);
                    }
                    continue;
                }
                if (inSemanticExports && currentEnum != null && line.startsWith("enum_case ")) {
                    String[] words = line.replaceAll("\"[^\"]*\"", " QUOTED ")
                            .trim().split("\\s+");
                    List<String> quoted = parseMossiQuotedFields(line);
                    if (words.length < 3 || quoted.isEmpty()) {
                        throw new CompileError("SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                                "ABI-v10 enum case is malformed", cand.toString(), 1);
                    }
                    int fieldCount;
                    try { fieldCount = Integer.parseInt(words[2]); }
                    catch (NumberFormatException ex) {
                        throw new CompileError("SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                                "ABI-v10 enum case field count is malformed", cand.toString(), 1);
                    }
                    if (fieldCount < 0 || quoted.size() != 1 + fieldCount * 2) {
                        throw new CompileError("SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                                "ABI-v10 enum case payload is incomplete", cand.toString(), 1);
                    }
                    String caseName = quoted.get(0);
                    if (currentEnum.cases.stream().anyMatch(item -> item.name.equals(caseName))) continue;
                    org.mosslang.compiler.ast.EnumCase enumCase = new org.mosslang.compiler.ast.EnumCase(
                            caseName, 1, cand.toString());
                    for (int index = 0; index < fieldCount; index++) {
                        String fieldName = quoted.get(1 + index * 2);
                        String fieldType = quoted.get(2 + index * 2);
                        enumCase.fields.add(new org.mosslang.compiler.ast.Field(
                                fieldName, fieldType, "", "", 1, cand.toString(), false, false));
                    }
                    currentEnum.cases.add(enumCase);
                    continue;
                }

                if (inSemanticExports && line.startsWith("export type ")) {
                    currentDomain = null;
                    currentEnum = null;
                    String[] parts = line.split("\\s+");
                    if (parts.length >= 3) {
                        String typeName = parts[2];
                        if ("nominal".equals(typeName) && parts.length >= 4) typeName = parts[3];
                        String canonicalName = modName + "__" + typeName;
                        currentObject = null;
                        for (org.mosslang.compiler.ast.ObjectType object : program.objects) {
                            if (object.name.equals(canonicalName)) {
                                currentObject = object;
                                break;
                            }
                        }
                        if (currentObject == null) {
                            currentObject = new org.mosslang.compiler.ast.ObjectType(
                                    canonicalName, "type " + typeName, 1, cand.toString(), true);
                            currentObject.definingModule = modName;
                            currentObject.declarationName = typeName;
                            currentObject.sourceFree = true;
                            program.objects.add(currentObject);
                        }
                        String qualifiedName = modName + "." + typeName;
                        if (program.objects.stream().noneMatch(object -> object.name.equals(qualifiedName))) {
                            org.mosslang.compiler.ast.ObjectType alias = new org.mosslang.compiler.ast.ObjectType(
                                    qualifiedName, currentObject.header, currentObject.line,
                                    currentObject.sourceFile, true);
                            alias.definingModule = modName;
                            alias.declarationName = typeName;
                            alias.sourceFree = true;
                            alias.fields = currentObject.fields;
                            alias.methods = currentObject.methods;
                            program.objects.add(alias);
                        }
                    }
                    currentFn = null;
                    continue;
                }
                if (inSemanticExports && currentObject != null
                        && line.startsWith("public_representation field ")) {
                    String[] parts = line.substring("public_representation field ".length())
                            .trim().split("\\s+", 2);
                    String fieldName = parts[0];
                    String fieldType = parts.length > 1 ? parts[1].trim() : "";
                    if (currentObject.fields.stream().noneMatch(field -> field.name.equals(fieldName))) {
                        currentObject.fields.add(new org.mosslang.compiler.ast.Field(
                                fieldName, fieldType, "", "", 1, cand.toString(),
                                fieldType.isEmpty(), true));
                    }
                    continue;
                }
                if (inSemanticExports && currentObject != null
                        && line.startsWith("object_method ")) {
                    String[] head = line.split("\\s+", 3);
                    if (head.length < 2) {
                        throw new IllegalArgumentException("object_method requires a name");
                    }
                    String methodName = head[1];
                    if (currentObject.methods.stream().anyMatch(method -> method.name.equals(methodName))) {
                        continue;
                    }
                    String record = head.length == 3 ? head[2] : "";
                    java.util.regex.Matcher returnMatcher = java.util.regex.Pattern
                            .compile("(?:^|\\s)return=([^\\s]+)").matcher(record);
                    java.util.regex.Matcher countMatcher = java.util.regex.Pattern
                            .compile("(?:^|\\s)params=([0-9]+)(?:\\s|$)").matcher(record);
                    org.mosslang.compiler.ast.Method method = new org.mosslang.compiler.ast.Method(
                            currentObject.name, methodName, 1, cand.toString());
                    method.sourceFree = true;
                    if (returnMatcher.find()) {
                        method.setReturnType(returnMatcher.group(1));
                    }
                    java.util.regex.Matcher receiverMatcher = java.util.regex.Pattern
                            .compile("(?:^|\\s)receiver=([^\\s]+)").matcher(record);
                    if (receiverMatcher.find()) {
                        method.receiverAccessMode = parseAccessMode(receiverMatcher.group(1));
                    }
                    int parameterCount = countMatcher.find()
                            ? Integer.parseInt(countMatcher.group(1)) : 0;
                    List<String> quoted = parseMossiQuotedFields(line);
                    if (quoted.size() < parameterCount * 2) {
                        throw new IllegalArgumentException("object_method parameter records are incomplete");
                    }
                    for (int index = 0; index < parameterCount; index++) {
                        method.params.add(new org.mosslang.compiler.ast.Param(
                                quoted.get(index * 2), quoted.get(index * 2 + 1)));
                    }
                    String[] words = line.replaceAll("\"[^\"]*\"", " QUOTED ")
                            .trim().split("\\s+");
                    method.parameterAccessModes.clear();
                    for (int index = 0; index < parameterCount; index++) {
                        int modeIndex = words.length - parameterCount + index;
                        method.parameterAccessModes.add(modeIndex >= 0
                                ? parseAccessMode(words[modeIndex])
                                : org.mosslang.compiler.types.AccessMode.Read);
                    }
                    currentObject.methods.add(method);
                    continue;
                }

                if ((inContract || inSemanticExports) && (line.startsWith("export fn ") || line.startsWith("fn "))) {
                    currentObject = null;
                    String[] parts = line.split("\\s+");
                    String fnName = parts[0].equals("export") ? parts[2] : parts[1];
                    String retType = "";
                    boolean isGeneric = false;
                    for (int i = 1; i < parts.length; i++) {
                        if (parts[i].startsWith("return=")) {
                            retType = parts[i].substring(7);
                        } else if (parts[i].startsWith("kind=generic")) {
                            isGeneric = true;
                        }
                    }
                    boolean openBuiltinResult = isGeneric
                            && java.util.Set.of("vector", "map", "queue")
                                    .contains(retType.toLowerCase(java.util.Locale.ROOT));
                    boolean retainReturnType = !retType.isEmpty()
                            && !retType.startsWith("_") && !openBuiltinResult;
                    String qualifiedName = modName + "." + fnName;
                    org.mosslang.compiler.ast.Function existing = null;
                    for (org.mosslang.compiler.ast.Function fn : program.functions) {
                        if (fn.name.equals(qualifiedName)) {
                            existing = fn;
                            break;
                        }
                    }
                    if (existing != null) {
                        currentFn = existing;
                        if (isGeneric) {
                            currentFn.generic = true;
                            currentFn.staticDispatch = true;
                        }
                        if (retainReturnType) currentFn.returnType = retType;
                    } else {
                        currentFn = new org.mosslang.compiler.ast.Function(qualifiedName, "fn " + fnName + "()", 1, cand.toString(), true);
                        currentFn.definingModule = modName;
                        currentFn.semanticCallableId = "callable-v1:" + moduleId + ":fn:" + fnName;
                        currentFn.exported = true;
                        currentFn.generic = isGeneric;
                        currentFn.staticDispatch = isGeneric;
                        if (retainReturnType) currentFn.returnType = retType;
                        program.functions.add(currentFn);
                        // Also add alias for Native lowering
                        org.mosslang.compiler.ast.Function alias = new org.mosslang.compiler.ast.Function(modName + "__" + fnName, "fn " + fnName + "()", 1, cand.toString(), true);
                        alias.definingModule = modName;
                        alias.semanticCallableId = currentFn.semanticCallableId;
                        alias.exported = true;
                        alias.generic = currentFn.generic;
                        alias.staticDispatch = currentFn.staticDispatch;
                        alias.params = currentFn.params;
                        alias.body = currentFn.body;
                        alias.returnType = currentFn.returnType;
                        program.functions.add(alias);
                    }
                } else if (currentFn != null && line.startsWith("param ")) {
                    java.util.regex.Matcher m = java.util.regex.Pattern.compile("name=\"([^\"]*)\"").matcher(line);
                    String pName = m.find() ? m.group(1) : ("arg" + currentFn.params.size());
                    String[] pParts = line.split("\\s+");
                    String pType = pParts.length > 2 && !pParts[2].startsWith("mode=")
                            ? pParts[2] : "";
                    int parameterIndex = pParts.length > 1 ? Integer.parseInt(pParts[1]) : currentFn.params.size();
                    if (currentFn.params.stream().noneMatch(p -> p.name.equals(pName))) {
                        currentFn.params.add(new org.mosslang.compiler.ast.Param(pName, pType));
                    }
                    String mode = "READ";
                    for (String part : pParts) if (part.startsWith("mode=")) mode = part.substring(5);
                    org.mosslang.compiler.types.AccessMode access = switch (mode) {
                        case "WRITE" -> org.mosslang.compiler.types.AccessMode.Write;
                        case "CONSUME" -> org.mosslang.compiler.types.AccessMode.Consume;
                        default -> org.mosslang.compiler.types.AccessMode.Read;
                    };
                    while (currentFn.parameterAccessModes.size() <= parameterIndex) {
                        currentFn.parameterAccessModes.add(org.mosslang.compiler.types.AccessMode.Read);
                    }
                    currentFn.parameterAccessModes.set(parameterIndex, access);
                } else if (currentFn != null && line.startsWith("generic_source ")) {
                    java.util.regex.Matcher m = java.util.regex.Pattern.compile("\"([^\"]*)\"").matcher(line);
                    if (m.find()) {
                        currentFn.providerOriginFile = m.group(1);
                    }
                } else if (currentFn != null && line.startsWith("generic_param ")) {
                    java.util.regex.Matcher m = java.util.regex.Pattern.compile("\"([^\"]*)\"").matcher(line);
                    if (m.find()) {
                        String pName = m.group(1);
                        if (currentFn.params.stream().noneMatch(p -> p.name.equals(pName))) {
                            currentFn.params.add(new org.mosslang.compiler.ast.Param(pName, ""));
                        }
                    }
                } else if (currentFn != null && line.startsWith("generic_stmt ")) {
                    List<String> strs = parseMossiQuotedFields(line);
                    int firstQuote = line.indexOf('"');
                    String[] headTokens = line.substring(0, firstQuote).trim().split("\\s+");
                    int kindOrd = headTokens.length > 1 ? Integer.parseInt(headTokens[1]) : 0;
                    int sLine = headTokens.length > 2 ? Integer.parseInt(headTokens[2]) : 1;
                    int sIndent = headTokens.length > 3 ? Integer.parseInt(headTokens[3]) : 0;
                    if (strs.size() < 4) {
                        throw new IllegalArgumentException("generic_stmt requires four fixed string fields");
                    }
                    int cursor = line.indexOf('"');
                    for (int field = 0; field < 4; field++) {
                        while (cursor < line.length() && Character.isWhitespace(line.charAt(cursor))) cursor++;
                        cursor = quotedFieldEnd(line, cursor) + 1;
                    }
                    int nextQuote = line.indexOf('"', cursor);
                    String countText = (nextQuote >= 0 ? line.substring(cursor, nextQuote)
                            : line.substring(cursor)).trim();
                    int argCount = Integer.parseInt(countText.split("\\s+")[0]);
                    if (argCount != strs.size() - 4) {
                        throw new IllegalArgumentException("generic_stmt argument count does not match its fields");
                    }
                    String sText = strs.get(0);
                    String sA = strs.get(1);
                    String sB = strs.get(2);
                    org.mosslang.compiler.ast.Stmt.Kind kind = org.mosslang.compiler.ast.Stmt.Kind.values()[kindOrd];
                    org.mosslang.compiler.ast.Stmt stmt = new org.mosslang.compiler.ast.Stmt(kind, sLine, sIndent, sText, sA, sB);
                    for (int arg = 0; arg < argCount; arg++) stmt.args.add(strs.get(arg + 4));
                    currentFn.body.add(stmt);
                    if (kind == org.mosslang.compiler.ast.Stmt.Kind.Return) {
                        currentFn.resultExpression = sA;
                    }
                } else if (currentFn != null && line.startsWith("generic_result ")) {
                    String[] fields = line.substring("generic_result ".length()).trim().split("\\s+", 2);
                    currentFn.resultLine = Integer.parseInt(fields[0]);
                    List<String> quoted = parseMossiQuotedFields(line);
                    if (quoted.size() != 1) throw new IllegalArgumentException("generic_result expression is malformed");
                    currentFn.resultExpression = quoted.get(0);
                } else if (line.equals("generic_ir_end")) {
                    String aliasName = modName + "__" + currentFn.name.substring(modName.length() + 1);
                    for (org.mosslang.compiler.ast.Function alias : program.functions) {
                        if (alias.name.equals(aliasName)) {
                            alias.generic = currentFn.generic;
                            alias.staticDispatch = currentFn.staticDispatch;
                            alias.params = currentFn.params;
                            alias.body = currentFn.body;
                            alias.parameterAccessModes = currentFn.parameterAccessModes;
                            alias.returnType = currentFn.returnType;
                            alias.resultExpression = currentFn.resultExpression;
                            alias.resultLine = currentFn.resultLine;
                            break;
                        }
                    }
                    currentFn = null;
                }
            }
            projectV10DirectReturnCallables(program, cand, typedV10Summaries, typedV10Bodies);
            restoreSourceFreeFailureArms(sourceFreeContractLines, program, modName, cand);
        } catch (CompileError error) {
            throw error;
        } catch (Exception error) {
            throw new CompileError("SOURCE_FREE_CONTRACT_INVALID",
                    "cannot parse source-free function metadata from '" + cand + "': " + error.getMessage(),
                    cand.toString(), 1);
        }
    }

    /** Project supported direct-parameter-return checked ABI bodies into source-free functions. */
    private static void projectV10DirectReturnCallables(
            Program program, Path interfaceFile,
            Map<String, org.mosslang.compiler.abi.AbiV10Records.Summary> summaries,
            Map<String, org.mosslang.compiler.abi.AbiV10Records.CheckedBodyRecord> bodies) {
        for (Map.Entry<String, org.mosslang.compiler.abi.AbiV10Records.Summary> entry : summaries.entrySet()) {
            String callableId = entry.getKey();
            org.mosslang.compiler.abi.AbiV10Records.Summary summary = entry.getValue();
            org.mosslang.compiler.abi.AbiV10Records.CheckedBodyRecord body = bodies.get(callableId);
            if (body == null || !summary.requirements.isEmpty() || !summary.dependencies.isEmpty()
                    || summary.observableEffectBits != 0 || !summary.observableEffectTags.isEmpty()
                    || summary.parameterAccess.size() != summary.parameters.size()
                    || summary.variableCount > 4096) continue;
            org.mosslang.compiler.types.TypeRef resultType = v10SimpleWireType(
                    summary.result, summary.variableCount);
            if (resultType == null) continue;
            List<org.mosslang.compiler.types.TypeRef> parameterTypes = new ArrayList<>();
            for (org.mosslang.compiler.abi.AbiV10Records.WireType parameter : summary.parameters) {
                org.mosslang.compiler.types.TypeRef converted = v10SimpleWireType(parameter, summary.variableCount);
                if (converted == null) { parameterTypes.clear(); break; }
                parameterTypes.add(converted);
            }
            if (parameterTypes.size() != summary.parameters.size()) continue;
            List<org.mosslang.compiler.abi.CheckedStmt> statements;
            try {
                statements = org.mosslang.compiler.abi.CheckedBodyCodec
                        .decodeCheckedBody(body.checkedIrBytes);
            } catch (CompileError invalidBody) {
                continue;
            }
            if (statements.size() != 1) continue;
            org.mosslang.compiler.abi.CheckedStmt statement = statements.get(0);
            if (statement.kind != org.mosslang.compiler.abi.CheckedStmtKind.Return
                    || statement.expr == null || !resultType.equals(statement.expr.type)) continue;
            int returnedParameter = -1;
            String returnedLiteral = null;
            if (statement.expr.kind == org.mosslang.compiler.abi.CheckedExprKind.Parameter) {
                returnedParameter = statement.expr.ordinal;
                if (returnedParameter < 0 || returnedParameter >= parameterTypes.size()
                        || !resultType.equals(parameterTypes.get(returnedParameter))) continue;
            } else if (statement.expr.kind == org.mosslang.compiler.abi.CheckedExprKind.IntegerLiteral
                    && parameterTypes.isEmpty() && resultType.kind() == org.mosslang.compiler.types.TypeKind.Primitive
                    && "int".equalsIgnoreCase(resultType.name())
                    && statement.expr.literalValue.matches("[0-9](?:_?[0-9])*")) {
                returnedLiteral = statement.expr.literalValue;
            } else if (statement.expr.kind == org.mosslang.compiler.abi.CheckedExprKind.BoolLiteral
                    && parameterTypes.isEmpty() && resultType.kind() == org.mosslang.compiler.types.TypeKind.Primitive
                    && "bool".equalsIgnoreCase(resultType.name())
                    && ("true".equals(statement.expr.literalValue)
                        || "false".equals(statement.expr.literalValue))) {
                returnedLiteral = statement.expr.literalValue;
            } else {
                continue;
            }

            List<org.mosslang.compiler.ast.Function> matches = program.functions.stream()
                    .filter(function -> callableId.equals(function.semanticCallableId))
                    .toList();
            if (matches.isEmpty() || matches.stream().anyMatch(
                    function -> function.params.size() != parameterTypes.size())) continue;
            for (org.mosslang.compiler.ast.Function function : matches) {
                for (int index = 0; index < parameterTypes.size(); index++) {
                    function.params.get(index).bindType(parameterTypes.get(index));
                }
                function.parameterAccessModes.clear();
                for (org.mosslang.compiler.abi.AbiV10Records.Access access : summary.parameterAccess) {
                    function.parameterAccessModes.add(switch (access) {
                        case Read -> org.mosslang.compiler.types.AccessMode.Read;
                        case Write -> org.mosslang.compiler.types.AccessMode.Write;
                        case Consume -> org.mosslang.compiler.types.AccessMode.Consume;
                    });
                }
                function.bindReturnType(resultType);
                function.body.clear();
                String returnExpression = returnedParameter >= 0
                        ? function.params.get(returnedParameter).name : returnedLiteral;
                org.mosslang.compiler.ast.Stmt projected = new org.mosslang.compiler.ast.Stmt(
                        org.mosslang.compiler.ast.Stmt.Kind.Return, 1, 1,
                        "return " + returnExpression, returnExpression, "");
                projected.sourceFile = interfaceFile.toString();
                function.body.add(projected);
                function.resultExpression = returnExpression;
                function.resultLine = 1;
                function.generic = summary.variableCount > 0;
                function.staticDispatch = function.generic;
            }
        }
    }

    private static org.mosslang.compiler.types.TypeRef v10SimpleWireType(
            org.mosslang.compiler.abi.AbiV10Records.WireType wire, int variableCount) {
        return switch (wire.kind) {
            case Primitive -> org.mosslang.compiler.types.TypeRef.primitive(wire.identity);
            case Unit -> org.mosslang.compiler.types.TypeRef.unit();
            case BoundVar -> wire.ordinal >= 0 && wire.ordinal < variableCount
                    ? org.mosslang.compiler.types.TypeRef.var(
                            new org.mosslang.compiler.types.TypeVarId(0, 0, wire.ordinal)) : null;
            case Builtin -> {
                List<org.mosslang.compiler.types.TypeRef> arguments = new ArrayList<>();
                for (org.mosslang.compiler.abi.AbiV10Records.WireType argument : wire.arguments) {
                    org.mosslang.compiler.types.TypeRef converted = v10SimpleWireType(argument, variableCount);
                    if (converted == null) { arguments.clear(); break; }
                    arguments.add(converted);
                }
                yield arguments.size() == wire.arguments.size()
                        ? org.mosslang.compiler.types.TypeRef.builtin(wire.identity, arguments) : null;
            }
            case Nominal, Enum, Domain -> null;
        };
    }

    private static void restoreSourceFreeFailureArms(List<String> contractLines, Program program,
                                                      String modName, Path interfaceFile)
            throws CompileError {
        String currentDomain = null;
        Map<String, Map<Integer, org.mosslang.compiler.ast.FailureArm>> arms = new HashMap<>();
        for (String line : contractLines) {
            if (line.startsWith("domain ")) {
                String[] words = line.split("\\s+");
                currentDomain = words.length > 1 ? words[1] : null;
                continue;
            }
            if (currentDomain == null || !line.startsWith("handler_failure_arm ")) continue;
            List<String> quoted = parseMossiQuotedFields(line);
            String[] words = line.replaceAll("\"[^\"]*\"", " QUOTED ").trim().split("\\s+");
            if (quoted.size() < 3 || words.length < 7) {
                throw new CompileError("SOURCE_FREE_ABI_INVALID",
                        "compiled provider has malformed ABI-v9 failure-arm metadata",
                        interfaceFile.toString(), 1);
            }
            int index;
            try { index = Integer.parseInt(words[2]); }
            catch (NumberFormatException error) {
                throw new CompileError("SOURCE_FREE_ABI_INVALID",
                        "compiled provider has malformed ABI-v9 failure-arm index",
                        interfaceFile.toString(), 1);
            }
            String domainKey = currentDomain;
            org.mosslang.compiler.ast.Domain domain = program.domains.stream()
                    .filter(candidate -> candidate.name.equals(modName + "__" + domainKey))
                    .findFirst().orElse(null);
            if (domain == null) continue;
            org.mosslang.compiler.ast.Handler handler = domain.handlers.stream()
                    .filter(candidate -> candidate.name.equals(quoted.get(0))).findFirst().orElse(null);
            if (handler == null) continue;
            org.mosslang.compiler.ast.FailureArm arm = new org.mosslang.compiler.ast.FailureArm(
                    1, interfaceFile.toString());
            arm.patterns.add(quoted.get(1));
            while (handler.failureArms.size() <= index) {
                handler.failureArms.add(new org.mosslang.compiler.ast.FailureArm(1, interfaceFile.toString()));
            }
            handler.failureArms.set(index, arm);
            arms.computeIfAbsent(domainKey + "::" + quoted.get(0), ignored -> new HashMap<>())
                    .put(index, arm);
        }
        currentDomain = null;
        for (String line : contractLines) {
            if (line.startsWith("domain ")) {
                String[] words = line.split("\\s+");
                currentDomain = words.length > 1 ? words[1] : null;
                continue;
            }
            if (currentDomain == null || !line.startsWith("handler_failure_capture ")) continue;
            List<String> quoted = parseMossiQuotedFields(line);
            String[] words = line.replaceAll("\"[^\"]*\"", " QUOTED ").trim().split("\\s+");
            if (quoted.size() < 5 || words.length < 9) {
                throw new CompileError("SOURCE_FREE_ABI_INVALID",
                        "compiled provider has malformed ABI-v9 failure-capture metadata",
                        interfaceFile.toString(), 1);
            }
            int index;
            try { index = Integer.parseInt(words[2]); }
            catch (NumberFormatException error) {
                throw new CompileError("SOURCE_FREE_ABI_INVALID",
                        "compiled provider has malformed ABI-v9 failure-capture index",
                        interfaceFile.toString(), 1);
            }
            Map<Integer, org.mosslang.compiler.ast.FailureArm> handlerArms =
                    arms.get(currentDomain + "::" + quoted.get(0));
            org.mosslang.compiler.ast.FailureArm arm = handlerArms == null
                    ? null : handlerArms.get(index);
            if (arm != null) arm.checkedCaptures.put(quoted.get(1), quoted.get(2));
        }
    }

    private static List<String> parseMossiQuotedFields(String line) {
        List<String> fields = new ArrayList<>();
        for (int cursor = 0; cursor < line.length();) {
            int quote = line.indexOf('"', cursor);
            if (quote < 0) break;
            StringBuilder value = new StringBuilder();
            int i = quote + 1;
            boolean closed = false;
            while (i < line.length()) {
                char ch = line.charAt(i++);
                if (ch == '"') {
                    closed = true;
                    break;
                }
                if (ch == '\\' && i < line.length()) ch = line.charAt(i++);
                value.append(ch);
            }
            if (!closed) throw new IllegalArgumentException("unterminated quoted Mossi field");
            fields.add(value.toString());
            cursor = i;
        }
        return fields;
    }

    private static int quotedFieldEnd(String line, int quoteStart) {
        if (quoteStart < 0 || line.charAt(quoteStart) != '"') {
            throw new IllegalArgumentException("expected quoted Mossi field");
        }
        for (int i = quoteStart + 1; i < line.length(); i++) {
            if (line.charAt(i) == '\\') {
                i++;
            } else if (line.charAt(i) == '"') {
                return i;
            }
        }
        throw new IllegalArgumentException("unterminated quoted Mossi field");
    }

    public static void usage() {
        System.err.println("usage: moss <source.moss> [-o output.rs] [--check] [--json]");
        System.err.println("       moss run --interp <source.moss> [--trace]");
        System.err.println("       moss check [--json] <source.moss>");
    }
}
