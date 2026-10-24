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
            String filter = null;
            for (int i = 1; i < args.length; i++) {
                if ("--release".equals(args[i])) release = true;
                else if ("--json".equals(args[i])) json = true;
                else if (!args[i].startsWith("-") && filter == null) filter = args[i];
            }
            try {
                org.mosslang.compiler.project.ProjectManifest manifest = org.mosslang.compiler.project.ProjectManifest.load(Paths.get("."));
                return org.mosslang.compiler.project.ProjectBuilder.test(manifest, filter, release, json);
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

        if (Set.of("inspect", "type", "effects", "ownership", "calls", "why", "fmt").contains(first)) {
            String target = args.length > 1 ? args[1] : "";
            String src = null;
            boolean json = false;
            for (int i = 1; i < args.length; i++) {
                if ("--source".equals(args[i]) && i + 1 < args.length) {
                    src = args[++i];
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
                    for (org.mosslang.compiler.ast.ModuleImport imp : p.imports) {
                        for (Path cand : candidates) {
                            String stem = cand.getFileName().toString().replaceAll("\\.mossi$", "");
                            if (stem.equals(imp.name)) {
                                Path localModSrc = projectRoot != null ? projectRoot.resolve("src").resolve(imp.name + ".moss") : null;
                                if (localModSrc == null || !Files.exists(localModSrc)) {
                                    org.mosslang.compiler.project.SourceFreeAbi.validateMossiInterface(cand);
                                }
                            }
                        }
                    }
                }
                org.mosslang.compiler.query.SemanticQueryEngine.executeQuery(first, target, p, absSrc, json, System.out);
                return 0;
            } catch (CompileError e) {
                if (json) {
                    DiagnosticFormatter.writeStructuredError(System.out, first, COMPILER_VERSION,
                            e.getCode(), e.getMessage(), new File(src).getAbsolutePath(), e.getLine(), "", "");
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

            if (interpMode) {
                Path sourceFile = Paths.get(sourcePath).toAbsolutePath();
                Path dir = sourceFile.getParent();
                Path projectRoot = null;
                while (dir != null) {
                    if (Files.exists(dir.resolve("moss.toml"))) {
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
                        for (Path depSrc : depManifest.findSourceFiles()) {
                            if (depSrc.toFile().getAbsolutePath().equals(absPath)) continue;
                            String depContent = Files.readString(depSrc, StandardCharsets.UTF_8);
                            Program depProg = Parser.parseSource(depContent, depSrc.toFile().getAbsolutePath());
                            program.functions.addAll(depProg.functions);
                            program.domains.addAll(depProg.domains);
                            program.objects.addAll(depProg.objects);
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
                    if (Files.exists(dir.resolve("moss.toml"))) {
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
                    if (cands != null && !cands.isEmpty()) {
                        Path localModSrc = projectRoot != null ? projectRoot.resolve("src").resolve(imp.name + ".moss") : null;
                        if (localModSrc == null || !Files.exists(localModSrc)) {
                            org.mosslang.compiler.project.SourceFreeAbi.validateMossiInterface(cands.get(0));
                        }
                    }
                }
                if (program.main != null) {
                    for (org.mosslang.compiler.ast.ModuleImport imp : program.imports) {
                        List<Path> cands = candidateMap.get(imp.name);
                        if (cands != null && !cands.isEmpty()) {
                            String mText = Files.readString(cands.get(0), StandardCharsets.UTF_8);
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
                        e.getMessage(), (e.getSourceFile() != null && !e.getSourceFile().isEmpty()) ? e.getSourceFile() : absPath, e.getLine(), e.getSemanticIdentity(), e.getSymbol());
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

    public static void usage() {
        System.err.println("usage: moss <source.moss> [-o output.rs] [--check] [--json]");
        System.err.println("       moss run --interp <source.moss> [--trace]");
        System.err.println("       moss check [--json] <source.moss>");
    }
}
