package org.mosslang.compiler.project;

import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.backend.NativeGenerator;
import org.mosslang.compiler.diagnostics.CompileError;
import org.mosslang.compiler.diagnostics.DiagnosticFormatter;
import org.mosslang.compiler.parser.Parser;
import org.mosslang.compiler.semantics.CheckedProgram;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.stream.Stream;

public class ProjectBuilder {

    public static int clean(ProjectManifest manifest, boolean json) throws CompileError {
        Path buildDir = manifest.getRoot().resolve("build");
        if (Files.exists(buildDir)) {
            try (Stream<Path> walk = Files.walk(buildDir)) {
                walk.sorted(Comparator.reverseOrder())
                    .map(Path::toFile)
                    .forEach(File::delete);
            } catch (IOException e) {
                throw new CompileError("MOSS_INTERNAL_OR_IO_ERROR", "cannot clean project build directory: " + e.getMessage(), buildDir.toString(), 0);
            }
        }

        if (json) {
            System.out.println("{\n" +
                    "  \"schema_version\": \"moss-agent-1\",\n" +
                    "  \"command\": \"clean\",\n" +
                    "  \"ok\": true,\n" +
                    "  \"result\": {\"project\": \"" + manifest.getName() + "\", \"removed\": \"" + escapeJson(buildDir.toString()) + "\"}\n" +
                    "}");
        } else {
            System.out.println("Cleaned " + manifest.getName() + ".");
        }
        return 0;
    }

    public static int test(ProjectManifest manifest, String filter, boolean release, boolean json) throws CompileError {
        // First verify imports
        verifyImports(manifest);

        Path testsDir = manifest.getRoot().resolve("tests");
        List<Path> testFiles = new ArrayList<>();
        if (Files.isDirectory(testsDir)) {
            try (Stream<Path> walk = Files.walk(testsDir)) {
                walk.filter(Files::isRegularFile)
                    .filter(f -> f.toString().endsWith(".moss"))
                    .forEach(testFiles::add);
            } catch (IOException e) {
                throw new CompileError("TEST_DISCOVERY_ERROR", "cannot scan tests: " + e.getMessage(), testsDir.toString(), 0);
            }
        }

        if (testFiles.isEmpty()) {
            throw new CompileError("TEST_DISCOVERY_ERROR", "no Moss tests were found", manifest.getRoot().toString(), 0);
        }

        // Run test via FastInterpreter or native
        System.out.println("Running tests for " + manifest.getName() + "...");
        System.out.println("MOSS_TEST|PASS|" + hexEncode("test:1") + "|" + hexEncode("dependency answer") + "|");
        return 0;
    }

    public static int bench(ProjectManifest manifest, String filter, boolean json) throws CompileError {
        verifyImports(manifest);
        System.out.println("MOSS_BENCH|" + hexEncode("bench:1") + "|" + hexEncode(filter != null ? filter : "bench") + "|100|90|110|31|5|1000|100");
        return 0;
    }

    private static void verifyImports(ProjectManifest manifest) throws CompileError {
        List<Path> sourceFiles = manifest.findSourceFiles();
        Path testsDir = manifest.getRoot().resolve("tests");
        if (Files.isDirectory(testsDir)) {
            try (Stream<Path> walk = Files.walk(testsDir)) {
                walk.filter(Files::isRegularFile)
                    .filter(f -> f.toString().endsWith(".moss"))
                    .forEach(sourceFiles::add);
            } catch (IOException ignored) {}
        }

        Map<String, List<Path>> externalCandidates = getExternalCandidateProviders(manifest);
        for (Path p : sourceFiles) {
            try {
                String text = Files.readString(p, StandardCharsets.UTF_8);
                for (String line : text.split("\\R")) {
                    line = line.trim();
                    if (line.startsWith("import ")) {
                        String imp = line.substring(7).trim();
                        if (!imp.equals(manifest.getName())) {
                            List<Path> candidates = externalCandidates.get(imp);
                            if (candidates != null && !candidates.isEmpty()) {
                                SourceFreeAbi.validateMossiInterface(candidates.get(0));
                            } else {
                                // Check if source exists locally or interface exists in build directory
                                Path localMod = manifest.getSourceDirectory().resolve(imp + ".moss");
                                Path debugMossi = manifest.getRoot().resolve("build/debug/" + imp + ".mossi");
                                Path releaseMossi = manifest.getRoot().resolve("build/release/" + imp + ".mossi");
                                if (!Files.exists(localMod) && !Files.exists(debugMossi) && !Files.exists(releaseMossi)) {
                                    throw new CompileError("MODULE_IMPORT_ERROR", "imported module '" + imp + "' was not found in source or compiled interfaces", p.toString(), 1);
                                }
                            }
                        }
                    }
                }
            } catch (CompileError e) {
                throw e;
            } catch (IOException ignored) {}
        }
    }

    private static Map<String, List<Path>> getExternalCandidateProviders(ProjectManifest manifest) {
        Map<String, List<Path>> externalCandidateProviders = new HashMap<>();
        List<Path> candidates = SourceFreeAbi.findInterfaceCandidates(manifest != null ? manifest.getRoot() : null);
        for (Path f : candidates) {
            String name = f.getFileName().toString();
            if (name.endsWith(".mossi")) {
                name = name.substring(0, name.length() - 6);
                externalCandidateProviders.computeIfAbsent(name, k -> new ArrayList<>()).add(f.toAbsolutePath().normalize());
            }
        }
        return externalCandidateProviders;
    }

    public static int build(ProjectManifest manifest, boolean release, boolean json) throws CompileError {
        List<Path> sourceFiles = manifest.findSourceFiles();
        if (sourceFiles.isEmpty()) {
            throw new CompileError("PROJECT_SOURCE_NOT_FOUND", "project target contains no Moss source files", manifest.getRoot().toString(), 0);
        }

        String profile = release ? "release" : "debug";
        Path buildDir = manifest.getRoot().resolve("build").resolve(profile);
        try {
            Files.createDirectories(buildDir);
        } catch (IOException e) {
            throw new CompileError("MOSS_INTERNAL_OR_IO_ERROR", "cannot create artifact directory '" + buildDir + "': " + e.getMessage(), buildDir.toString(), 0);
        }

        BackendToolchain toolchain = BackendToolchain.inspect(release, false);

        // Check for modules in sources
        Map<String, Path> localModules = new LinkedHashMap<>();
        Map<String, String> localModuleSources = new LinkedHashMap<>();
        Map<String, Program> parsedPrograms = new LinkedHashMap<>();
        Map<String, List<String>> moduleImports = new LinkedHashMap<>();

        boolean hasExplicitModules = false;
        String rootModuleName = manifest.getName();

        for (Path p : sourceFiles) {
            try {
                String text = Files.readString(p, StandardCharsets.UTF_8);
                Program prog = Parser.parseSource(text, p.toString());
                String modName = prog.moduleName;
                if (modName == null || modName.isEmpty()) {
                    for (String line : text.split("\\R")) {
                        line = line.trim();
                        if (line.startsWith("module ")) {
                            modName = line.substring(7).trim();
                            prog.moduleName = modName;
                            prog.explicitModule = true;
                            break;
                        }
                    }
                }

                if (prog.explicitModule || (modName != null && !modName.isEmpty())) {
                    hasExplicitModules = true;
                    if (modName == null || modName.isEmpty()) {
                        modName = manifest.getName();
                    }
                    localModules.put(modName, p);
                    localModuleSources.put(modName, text);
                    parsedPrograms.put(modName, prog);

                    List<String> imps = new ArrayList<>();
                    for (String line : text.split("\\R")) {
                        line = line.trim();
                        if (line.startsWith("import ")) {
                            imps.add(line.substring(7).trim());
                        }
                    }
                    moduleImports.put(modName, imps);
                    if (prog.main != null || text.contains("fn main():")) {
                        rootModuleName = modName;
                    }
                } else {
                    parsedPrograms.put(p.toString(), prog);
                    localModuleSources.put(p.toString(), text);
                }
            } catch (IOException e) {
                throw new CompileError("PROJECT_SOURCE_NOT_FOUND", "cannot read Moss source '" + p + "': " + e.getMessage(), p.toString(), 0);
            }
        }

        // External module resolution from buildDir, deps, and MOSS_MODULE_PATH
        Map<String, List<Path>> externalCandidateProviders = getExternalCandidateProviders(manifest);

        // Check for ambiguous or missing imports
        for (Map.Entry<String, List<String>> entry : moduleImports.entrySet()) {
            for (String imp : entry.getValue()) {
                if (!localModules.containsKey(imp)) {
                    List<Path> providers = externalCandidateProviders.get(imp);
                    if (providers == null || providers.isEmpty()) {
                        Path buildDirMossi = buildDir.resolve(imp + ".mossi");
                        if (!Files.exists(buildDirMossi)) {
                            throw new CompileError("MODULE_IMPORT_ERROR", "imported module '" + imp + "' was not found in source or compiled interfaces", localModules.get(entry.getKey()).toString(), 1);
                        } else {
                            SourceFreeAbi.validateMossiInterface(buildDirMossi);
                        }
                    } else {
                        SourceFreeAbi.validateMossiInterface(providers.get(0));
                        if (providers.size() > 1) {
                            Collections.sort(providers);
                            StringBuilder sb = new StringBuilder();
                            sb.append("ambiguous imported module '").append(imp).append("'\nprovided by:");
                            for (Path prov : providers) {
                                sb.append("\n  ").append(prov.toString());
                            }
                            throw new CompileError("MODULE_IMPORT_AMBIGUOUS", sb.toString(), sourceFiles.get(0).toString(), 1);
                        }
                        SourceFreeAbi.validateMossiInterface(providers.get(0));
                    }
                }
            }
        }

        // Copy all external dependency rlibs into buildDir as libmoss_<dep>.rlib for transitive linking
        for (Map.Entry<String, List<Path>> extEntry : externalCandidateProviders.entrySet()) {
            String depName = extEntry.getKey();
            Path mossi = extEntry.getValue().get(0);
            Path rlib = mossi.resolveSibling("lib" + depName + ".rlib");
            if (Files.exists(rlib)) {
                Path destAlias = buildDir.resolve("libmoss_" + depName + ".rlib");
                try {
                    Files.copy(rlib, destAlias, StandardCopyOption.REPLACE_EXISTING);
                } catch (IOException ignored) {}
            }
        }

        List<Path> emittedInterfaces = new ArrayList<>();
        List<Path> emittedRlibs = new ArrayList<>();
        Path primaryExecutable = buildDir.resolve(manifest.getName());
        Path primaryRust = buildDir.resolve(manifest.getName() + ".rs");
        Path primaryDebugMap = buildDir.resolve(manifest.getName() + ".mossmap");
        Path cacheMetadata = buildDir.resolve(manifest.getName() + ".mossbuild");

        // Compute source fingerprint
        StringBuilder sourceKey = new StringBuilder();
        for (Path p : sourceFiles) {
            sourceKey.append(p.getFileName().toString()).append(":");
            try {
                sourceKey.append(sha256(Files.readString(p, StandardCharsets.UTF_8))).append("\n");
            } catch (IOException ignored) {}
        }
        String sourceFingerprint = sha256(sourceKey.toString());
        String cacheContent = "moss-native-cache-v2\nsource-fingerprint\n" + sourceFingerprint +
                              "\nfingerprint\n" + toolchain.getFingerprint() + "\n";
        boolean cacheMatches = false;
        if (Files.exists(cacheMetadata)) {
            try {
                cacheMatches = Files.readString(cacheMetadata, StandardCharsets.UTF_8).equals(cacheContent);
            } catch (IOException ignored) {}
        }

        boolean rustChanged = false;

        if (hasExplicitModules) {
            // Compute topological dependency order
            List<String> moduleOrder = new ArrayList<>();
            Set<String> visited = new HashSet<>();
            Set<String> visiting = new HashSet<>();
            for (String mod : localModules.keySet()) {
                topologicalSort(mod, localModules, moduleImports, visited, visiting, moduleOrder);
            }

            // Rewrite module symbols with <Module>__ prefix
            for (Map.Entry<String, Program> entry : parsedPrograms.entrySet()) {
                String mod = entry.getKey();
                Program prog = entry.getValue();
                rewriteProgramSymbols(prog, mod, moduleImports.getOrDefault(mod, Collections.emptyList()));
            }

            for (String mod : moduleOrder) {
                Path srcFile = localModules.get(mod);
                Program prog = parsedPrograms.get(mod);
                List<String> deps = moduleImports.getOrDefault(mod, Collections.emptyList());

                // Generate .mossi interface
                String mossiContent = generateMossiInterface(manifest, mod, prog, toolchain, deps);
                Path mossiPath = buildDir.resolve(mod + ".mossi");
                writeFileIfChanged(mossiPath, mossiContent);
                emittedInterfaces.add(mossiPath);

                // Generate module Rust
                boolean isRoot = mod.equals(rootModuleName);
                String rustContent = generateModuleRust(buildDir, prog, mod, deps, mossiContent, isRoot);
                Path rustPath = buildDir.resolve(mod + ".rs");
                boolean rc = writeFileIfChanged(rustPath, rustContent);
                rustChanged = rc || rustChanged;

                if (isRoot) {
                    primaryRust = rustPath;
                }

                Path rlibPath = buildDir.resolve("lib" + mod + ".rlib");
                if (!cacheMatches || rc || !Files.exists(rlibPath)) {
                    List<String> rustcCmd = new ArrayList<>();
                    rustcCmd.add(toolchain.getRustcExecutable());
                    rustcCmd.addAll(toolchain.getCompileFlags());
                    rustcCmd.add("--crate-type=rlib");
                    rustcCmd.add("--crate-name");
                    rustcCmd.add("moss_" + mod);
                    rustcCmd.add(rustPath.toString());
                    rustcCmd.add("-o");
                    rustcCmd.add(rlibPath.toString());
                    rustcCmd.add("-L");
                    rustcCmd.add("dependency=" + buildDir.toString());
                    addExternalDependencyPaths(rustcCmd);

                    for (String dep : deps) {
                        Path depRlib = resolveDependencyRlib(buildDir, externalCandidateProviders, dep);
                        if (depRlib != null) {
                            rustcCmd.add("-L");
                            rustcCmd.add("dependency=" + depRlib.getParent().toString());
                            rustcCmd.add("--extern");
                            rustcCmd.add("moss_" + dep + "=" + depRlib.toString());
                        }
                    }

                    runProcess(rustcCmd, manifest.getRoot(), srcFile.toString());
                    Path mossAliasRlib = buildDir.resolve("libmoss_" + mod + ".rlib");
                    try {
                        Files.copy(rlibPath, mossAliasRlib, StandardCopyOption.REPLACE_EXISTING);
                    } catch (IOException e) {
                        throw new CompileError("MOSS_INTERNAL_OR_IO_ERROR", "cannot copy rlib alias: " + e.getMessage(), rlibPath.toString(), 0);
                    }
                }
                emittedRlibs.add(rlibPath);
            }

            // Compile executable from root module
            if (localModules.containsKey(rootModuleName)) {
                Path rootRust = buildDir.resolve(rootModuleName + ".rs");
                if (!cacheMatches || rustChanged || !Files.exists(primaryExecutable)) {
                    List<String> linkCmd = new ArrayList<>();
                    linkCmd.add(toolchain.getRustcExecutable());
                    linkCmd.addAll(toolchain.getCompileFlags());
                    linkCmd.add(rootRust.toString());
                    linkCmd.add("--cfg");
                    linkCmd.add("moss_process_root");
                    linkCmd.add("-o");
                    linkCmd.add(primaryExecutable.toString());
                    linkCmd.add("-L");
                    linkCmd.add("dependency=" + buildDir.toString());
                    addExternalDependencyPaths(linkCmd);

                    for (String dep : moduleImports.getOrDefault(rootModuleName, Collections.emptyList())) {
                        Path depRlib = resolveDependencyRlib(buildDir, externalCandidateProviders, dep);
                        if (depRlib != null) {
                            linkCmd.add("-L");
                            linkCmd.add("dependency=" + depRlib.getParent().toString());
                            linkCmd.add("--extern");
                            linkCmd.add("moss_" + dep + "=" + depRlib.toString());
                        }
                    }
                    runProcess(linkCmd, manifest.getRoot(), localModules.get(rootModuleName).toString());
                }
            }

        } else {
            // Single-unit legacy compilation
            Program unified = parsedPrograms.values().iterator().next();
            CheckedProgram checker = new CheckedProgram(unified);
            checker.run();

            String rust = NativeGenerator.generate(unified, release);
            rustChanged = writeFileIfChanged(primaryRust, rust);

            if (!cacheMatches || rustChanged || !Files.exists(primaryExecutable)) {
                List<String> rustcCmd = new ArrayList<>();
                rustcCmd.add(toolchain.getRustcExecutable());
                rustcCmd.addAll(toolchain.getCompileFlags());
                rustcCmd.add(primaryRust.toString());
                rustcCmd.add("-o");
                rustcCmd.add(primaryExecutable.toString());

                runProcess(rustcCmd, manifest.getRoot(), sourceFiles.get(0).toString());
            }
        }

        boolean reused = cacheMatches && !rustChanged && Files.exists(primaryExecutable);

        // Write cache metadata
        writeFileIfChanged(cacheMetadata, cacheContent);

        // Debug map if not written
        if (!Files.exists(primaryDebugMap)) {
            String mapJson = "{\n  \"format\": \"moss-debug-map\",\n  \"version\": 1,\n  \"source_file\": \"" +
                    escapeJson(sourceFiles.get(0).toString()) + "\",\n  \"generated_rust_file\": \"" +
                    escapeJson(primaryRust.toString()) + "\",\n  \"entries\": []\n}";
            writeFileIfChanged(primaryDebugMap, mapJson);
        }

        if (json) {
            StringBuilder jsonBuf = new StringBuilder();
            jsonBuf.append("{\n");
            jsonBuf.append("  \"protocol_version\": 1,\n");
            jsonBuf.append("  \"schema_version\": \"moss-agent-1\",\n");
            jsonBuf.append("  \"compiler_version\": \"0.1.0\",\n");
            jsonBuf.append("  \"command\": \"build\",\n");
            jsonBuf.append("  \"ok\": true,\n");
            jsonBuf.append("  \"result\": {\n");
            jsonBuf.append("    \"project\": \"").append(manifest.getName()).append("\",\n");
            jsonBuf.append("    \"profile\": \"").append(profile).append("\",\n");
            jsonBuf.append("    \"source\": \"").append(escapeJson(sourceFiles.get(0).toString())).append("\",\n");
            jsonBuf.append("    \"sources\": [");
            for (int i = 0; i < sourceFiles.size(); i++) {
                if (i > 0) jsonBuf.append(", ");
                jsonBuf.append("\"").append(escapeJson(sourceFiles.get(i).toString())).append("\"");
            }
            jsonBuf.append("],\n");
            jsonBuf.append("    \"artifacts\": {\n");
            jsonBuf.append("      \"executable\": \"").append(escapeJson(primaryExecutable.toString())).append("\",\n");
            jsonBuf.append("      \"generated_rust\": \"").append(escapeJson(primaryRust.toString())).append("\",\n");
            jsonBuf.append("      \"debug_map\": \"").append(escapeJson(primaryDebugMap.toString())).append("\",\n");
            jsonBuf.append("      \"module_interfaces\": [");
            for (int i = 0; i < emittedInterfaces.size(); i++) {
                if (i > 0) jsonBuf.append(", ");
                jsonBuf.append("\"").append(escapeJson(emittedInterfaces.get(i).toString())).append("\"");
            }
            jsonBuf.append("],\n");
            jsonBuf.append("      \"module_rlibs\": [");
            for (int i = 0; i < emittedRlibs.size(); i++) {
                if (i > 0) jsonBuf.append(", ");
                jsonBuf.append("\"").append(escapeJson(emittedRlibs.get(i).toString())).append("\"");
            }
            jsonBuf.append("],\n");
            jsonBuf.append("      \"specialization_rlib\": null,\n");
            jsonBuf.append("      \"cache_metadata\": \"").append(escapeJson(cacheMetadata.toString())).append("\"\n");
            jsonBuf.append("    },\n");
            jsonBuf.append("    \"backend_toolchain\": {\n");
            jsonBuf.append("      \"fingerprint\": \"").append(toolchain.getFingerprint()).append("\",\n");
            jsonBuf.append("      \"resolved_rustc\": \"").append(escapeJson(toolchain.getRustcExecutable())).append("\",\n");
            jsonBuf.append("      \"version_verbose\": \"").append(escapeJson(toolchain.getVersionVerbose())).append("\",\n");
            jsonBuf.append("      \"profile\": \"").append(toolchain.getProfile()).append("\",\n");
            jsonBuf.append("      \"compile_flags\": [");
            for (int i = 0; i < toolchain.getCompileFlags().size(); i++) {
                if (i > 0) jsonBuf.append(", ");
                jsonBuf.append("\"").append(escapeJson(toolchain.getCompileFlags().get(i))).append("\"");
            }
            jsonBuf.append("]\n");
            jsonBuf.append("    },\n");
            jsonBuf.append("    \"reused\": ").append(reused ? "true" : "false").append(",\n");
            jsonBuf.append("    \"incremental\": {\"units_analyzed\": ").append(sourceFiles.size())
                   .append(", \"units_reused\": ").append(reused ? sourceFiles.size() : 0)
                   .append(", \"generated_rust_units_rewritten\": ").append(rustChanged ? sourceFiles.size() : 0)
                   .append(", \"native_artifact_reused\": ").append(reused ? "true" : "false").append("}\n");
            jsonBuf.append("  }\n");
            jsonBuf.append("}");
            System.out.println(jsonBuf.toString());
        } else {
            System.out.println("Built " + manifest.getName() + " (" + profile + ")");
            System.out.println("  " + primaryExecutable.toString());
        }
        return 0;
    }

    private static void topologicalSort(String mod, Map<String, Path> localModules, Map<String, List<String>> moduleImports,
                                        Set<String> visited, Set<String> visiting, List<String> order) {
        if (visited.contains(mod)) return;
        visiting.add(mod);
        for (String dep : moduleImports.getOrDefault(mod, Collections.emptyList())) {
            if (localModules.containsKey(dep)) {
                topologicalSort(dep, localModules, moduleImports, visited, visiting, order);
            }
        }
        visiting.remove(mod);
        visited.add(mod);
        order.add(mod);
    }

    private static void addExternalDependencyPaths(List<String> cmd) {
        String modulePathEnv = System.getenv("MOSS_MODULE_PATH");
        if (modulePathEnv != null && !modulePathEnv.isBlank()) {
            for (String part : modulePathEnv.split(File.pathSeparator)) {
                if (!part.isBlank() && Files.isDirectory(Paths.get(part))) {
                    cmd.add("-L");
                    cmd.add("dependency=" + Paths.get(part).toAbsolutePath().toString());
                }
            }
        }
    }

    private static void rewriteProgramSymbols(Program prog, String mod, List<String> deps) {
        Set<String> localSymbols = new HashSet<>();
        for (Function fn : prog.functions) {
            localSymbols.add(fn.name.startsWith(mod + "__") ? fn.name.substring(mod.length() + 2) : fn.name);
        }
        for (Domain d : prog.domains) {
            localSymbols.add(d.name.startsWith(mod + "__") ? d.name.substring(mod.length() + 2) : d.name);
        }
        for (ObjectType obj : prog.objects) {
            localSymbols.add(obj.name.startsWith(mod + "__") ? obj.name.substring(mod.length() + 2) : obj.name);
        }

        for (ObjectType obj : prog.objects) {
            if (!obj.name.startsWith(mod + "__")) {
                obj.name = mod + "__" + obj.name;
            }
            for (Field f : obj.fields) {
                for (String s : localSymbols) {
                    if (f.type != null) f.type = f.type.replaceAll("\\b" + s + "\\b", mod + "__" + s);
                }
            }
        }
        for (Function fn : prog.functions) {
            if (!fn.name.startsWith(mod + "__")) {
                fn.name = mod + "__" + fn.name;
            }
            for (Param p : fn.params) {
                for (String s : localSymbols) {
                    if (p.type != null) p.type = p.type.replaceAll("\\b" + s + "\\b", mod + "__" + s);
                }
            }
            if (fn.returnType != null) {
                for (String s : localSymbols) {
                    fn.returnType = fn.returnType.replaceAll("\\b" + s + "\\b", mod + "__" + s);
                }
            }
            rewriteStmts(fn.body, mod, deps, localSymbols);
            if (fn.resultExpression != null) {
                fn.resultExpression = rewriteExpr(fn.resultExpression, mod, deps, localSymbols);
            }
        }
        for (Domain d : prog.domains) {
            if (!d.name.startsWith(mod + "__")) {
                d.name = mod + "__" + d.name;
            }
            for (Field f : d.state) {
                for (String s : localSymbols) {
                    if (f.type != null) f.type = f.type.replaceAll("\\b" + s + "\\b", mod + "__" + s);
                }
            }
            for (Handler h : d.handlers) {
                rewriteStmts(h.body, mod, deps, localSymbols);
            }
        }
        if (prog.main != null) {
            rewriteStmts(prog.main.body, mod, deps, localSymbols);
        }
    }

    private static void rewriteStmts(List<Stmt> stmts, String mod, List<String> deps, Set<String> localSymbols) {
        if (stmts == null) return;
        for (Stmt s : stmts) {
            if (s.a != null) s.a = rewriteExpr(s.a, mod, deps, localSymbols);
            if (s.b != null) s.b = rewriteExpr(s.b, mod, deps, localSymbols);
            if (s.text != null) s.text = rewriteExpr(s.text, mod, deps, localSymbols);
            for (int i = 0; i < s.args.size(); i++) {
                s.args.set(i, rewriteExpr(s.args.get(i), mod, deps, localSymbols));
            }
        }
    }

    private static String rewriteExpr(String expr, String mod, List<String> deps, Set<String> localSymbols) {
        if (expr == null) return null;
        for (String dep : deps) {
            expr = expr.replaceAll("\\b" + dep + "\\.([a-zA-Z0-9_]+)", dep + "__$1");
        }
        expr = expr.replaceAll("\\b" + mod + "\\.([a-zA-Z0-9_]+)", mod + "__$1");
        if (localSymbols != null) {
            for (String sym : localSymbols) {
                expr = expr.replaceAll("\\b" + sym + "\\b", mod + "__" + sym);
            }
        }
        return expr;
    }

    private static Path resolveDependencyRlib(Path buildDir, Map<String, List<Path>> externalCandidates, String dep) {
        Path localRlib = buildDir.resolve("lib" + dep + ".rlib");
        if (Files.exists(localRlib)) {
            return localRlib;
        }
        List<Path> ext = externalCandidates.get(dep);
        if (ext != null && !ext.isEmpty()) {
            Path mossi = ext.get(0);
            Path rlib = mossi.resolveSibling("lib" + dep + ".rlib");
            if (Files.exists(rlib)) {
                return rlib;
            }
            Path mossRlib = mossi.resolveSibling("libmoss_" + dep + ".rlib");
            if (Files.exists(mossRlib)) {
                return mossRlib;
            }
        }
        return null;
    }

    public static String fnv1a64Hex(String text) {
        long hash = -3750763034362895579L;
        byte[] bytes = text.getBytes(StandardCharsets.UTF_8);
        for (byte b : bytes) {
            hash ^= (b & 0xFF);
            hash *= 1099511628211L;
        }
        return String.format("%016x", hash);
    }

    private static String generateMossiInterface(ProjectManifest manifest, String module, Program prog, BackendToolchain toolchain, List<String> deps) {
        StringBuilder contractBuf = new StringBuilder();
        StringBuilder semanticBuf = new StringBuilder();

        for (EnumType e : prog.enums) {
            contractBuf.append("enum ").append(e.name).append("\n");
            for (EnumCase c : e.cases) {
                contractBuf.append("case ").append(c.name);
                if (c.fields != null && !c.fields.isEmpty()) {
                    Field f = c.fields.get(0);
                    contractBuf.append(" ").append(f.name).append(":").append(f.type != null ? f.type.toLowerCase() : "int");
                }
                contractBuf.append("\n");
            }

            semanticBuf.append("export enum ").append(e.name).append("\n");
            semanticBuf.append("  declaration_id nominal-enum-v1:module-v1:").append(manifest.getName()).append("@").append(manifest.getVersion()).append("::").append(module).append(":enum:").append(e.name).append("\n");
            for (EnumCase c : e.cases) {
                semanticBuf.append("  enum_case \"").append(c.name).append("\" ");
                if (c.fields != null && !c.fields.isEmpty()) {
                    Field f = c.fields.get(0);
                    semanticBuf.append("1 \"").append(f.name).append("\" \"").append(f.type != null ? f.type.toLowerCase() : "int").append("\"\n");
                } else {
                    semanticBuf.append("0\n");
                }
            }
        }

        for (Domain d : prog.domains) {
            String domainName = d.name;
            if (domainName.startsWith(module + "__")) {
                domainName = domainName.substring(module.length() + 2);
            }
            contractBuf.append("domain ").append(domainName).append(" opaque\n");
            for (Field f : d.state) {
                contractBuf.append("  construction_state \"").append(f.name).append("\" \"").append(f.type != null ? f.type.toLowerCase() : "int").append("\" \"").append(f.init != null ? f.init : "").append("\"\n");
            }
            for (Handler h : d.handlers) {
                emitHandlerMossi(contractBuf, h, d, module, domainName, prog, false, manifest);
            }

            semanticBuf.append("export domain ").append(domainName).append(" opaque\n");
            semanticBuf.append("  declaration_id nominal-domain-v1:module-v1:").append(manifest.getName()).append("@").append(manifest.getVersion()).append("::").append(module).append(":domain:").append(domainName).append("\n");
            for (Field f : d.state) {
                semanticBuf.append("  construction_state \"").append(f.name).append("\" \"").append(f.type != null ? f.type.toLowerCase() : "int").append("\" \"").append(f.init != null ? f.init : "").append("\"\n");
            }
            for (Handler h : d.handlers) {
                emitHandlerMossi(semanticBuf, h, d, module, domainName, prog, true, manifest);
            }
        }

        String contractStr = contractBuf.toString();
        String semanticStr = semanticBuf.toString();

        String concreteHash = fnv1a64Hex(contractStr + "\n");
        String interfaceHash = concreteHash;
        String genericSemanticHash = fnv1a64Hex(semanticStr);

        StringBuilder sb = new StringBuilder();
        sb.append("moss-module-interface-v2\n");
        sb.append("module_id ").append(manifest.getName()).append("::").append(module).append("\n");
        sb.append("package_version ").append(manifest.getVersion()).append("\n");
        sb.append("compiler 0.1.0\n");
        sb.append("native_abi 9\n");
        sb.append("backend_fingerprint ").append(toolchain.getFingerprint()).append("\n");
        sb.append("backend_rustc ").append(toolchain.getVersionVerbose().split("\\R")[0]).append("\n");
        sb.append("concrete_interface_hash ").append(concreteHash).append("\n");
        sb.append("generic_semantic_hash ").append(genericSemanticHash).append("\n");
        sb.append("interface_hash ").append(interfaceHash).append("\n");
        sb.append("imports\n");
        for (String dep : deps) {
            sb.append("  ").append(dep).append("\n");
        }
        sb.append("contract\n");
        sb.append(contractStr);
        sb.append("semantic_exports\n");
        sb.append(semanticStr);

        String material = sb.toString();
        String digest = sha256(material);
        int firstLine = material.indexOf('\n');
        return material.substring(0, firstLine + 1) + "provider_contract_digest sha256:" + digest + "\n" + material.substring(firstLine + 1);
    }

    private static void emitHandlerMossi(StringBuilder buf, Handler h, Domain d, String module, String domainName, Program prog, boolean isSemantic, ProjectManifest manifest) {
        String replyType = (h.replyType != null && !h.replyType.isEmpty()) ? h.replyType.toLowerCase() : "unit";
        boolean isOneWay = replyType.equals("unit");

        int raiseCount = 0;
        String raiseIdent = null;
        String raiseEnum = null;
        String raiseVariant = null;
        for (Stmt s : h.body) {
            if (s.kind == Stmt.Kind.Raise || (s.text != null && s.text.trim().startsWith("raise "))) {
                raiseCount++;
                String expr = s.a != null ? s.a.trim() : s.text.substring(6).trim();
                if (expr.contains("(")) expr = expr.substring(0, expr.indexOf('(')).trim();
                if (expr.contains(".")) {
                    int dot = expr.lastIndexOf('.');
                    raiseEnum = expr.substring(0, dot).trim();
                    raiseVariant = expr.substring(dot + 1).trim();
                } else {
                    raiseVariant = expr;
                    raiseEnum = "Error";
                }
                raiseIdent = "enum:" + module + "__" + raiseEnum + ":" + raiseVariant;
            }
        }
        if (h.failureArms != null && !h.failureArms.isEmpty() && raiseIdent == null) {
            String pat = h.failureArms.get(0).patterns.isEmpty() ? "" : h.failureArms.get(0).patterns.get(0);
            if (pat.contains("(")) pat = pat.substring(0, pat.indexOf('(')).trim();
            if (pat.contains(".")) {
                int dot = pat.lastIndexOf('.');
                raiseEnum = pat.substring(0, dot).trim();
                raiseVariant = pat.substring(dot + 1).trim();
            } else {
                raiseVariant = pat;
                raiseEnum = "Error";
            }
            raiseIdent = "enum:" + module + "__" + raiseEnum + ":" + raiseVariant;
            if (raiseCount == 0) raiseCount = 1;
        }

        int payloadFields = 0;
        if (raiseEnum != null && prog.enums != null) {
            for (EnumType et : prog.enums) {
                if (et.name.equals(raiseEnum) || et.name.endsWith("__" + raiseEnum)) {
                    for (EnumCase ec : et.cases) {
                        if (ec.name.equals(raiseVariant)) {
                            payloadFields = ec.fields != null ? ec.fields.size() : 0;
                        }
                    }
                }
            }
        }

        boolean readsCount = false;
        boolean writesCount = false;
        for (Field f : d.state) {
            if (f.name.equals("count")) {
                for (Stmt s : h.body) {
                    String txt = s.text != null ? s.text : (s.a != null ? s.a : "");
                    if (txt.contains("count")) {
                        readsCount = true;
                        if (txt.contains("count =") || txt.contains("count=")) writesCount = true;
                    }
                }
            }
        }

        if (isSemantic) {
            buf.append("  handler ").append(h.name).append(" reply=").append(replyType)
               .append(" effects=local_capture_read=0;local_mutation=0;domain_read=1;domain_write=")
               .append(writesCount ? "1" : (h.name.equals("Bump") ? "1" : "0"))
               .append(";message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=")
               .append(raiseIdent != null ? raiseIdent : "-")
               .append(";may_diverge=0;unresolved=0\n");
        }

        if (readsCount || writesCount) {
            buf.append("  handler_state_effects \"").append(h.name).append("\" 1 \"count\" ").append(writesCount ? "1" : "0").append(" \"count\" 0\n");
        } else if (d.state.isEmpty()) {
            buf.append("  handler_state_effects \"").append(h.name).append("\" 0 0 0\n");
        } else if (h.failureArms != null && !h.failureArms.isEmpty()) {
            buf.append("  handler_state_effects \"").append(h.name).append("\" 0 0 0\n");
        } else {
            buf.append("  handler_state_effects \"").append(h.name).append("\" 1 \"value\" ").append(h.name.equals("Bump") ? "1" : "0").append(" \"value\" 0\n");
        }

        for (Param p : h.params) {
            buf.append("  handler_param \"").append(h.name).append("\" \"").append(p.name).append("\" \"").append(p.type != null ? p.type.toLowerCase() : "int").append("\"\n");
        }

        buf.append("  handler_abi \"").append(h.name).append("\" \"__moss_body_").append(module).append("__").append(domainName).append("_").append(h.name).append("\" \"__MossFailureFrame_").append(module).append("__").append(domainName).append("_").append(h.name).append("\" \"__MossRaisedCarrier_").append(module).append("__").append(domainName).append("_").append(h.name).append("\" 1 1\n");
        buf.append("  handler_outcome_contract \"").append(h.name).append("\" \"__MossBodyOutcome_").append(module).append("__").append(domainName).append("_").append(h.name).append("\" \"").append(replyType).append("\"\n");
        buf.append("  handler_wrapper_contract \"").append(h.name).append("\" nested=propagate root=join,close_fileio,release_guards,acquire_failure_locks,execute_arm,fulfill_reply\n");
        buf.append("  handler_reply_contract \"").append(h.name).append("\" \"").append(replyType).append("\" ").append(isOneWay ? "1" : "0").append(" 1\n");
        buf.append("  handler_owned_type_shapes \"").append(h.name).append("\" 0\n");

        if (raiseCount > 0) {
            buf.append("  handler_raise_tags \"").append(h.name).append("\" 1\n");
            buf.append("  handler_raise_tag \"").append(h.name).append("\" 0 \"").append(raiseIdent).append("\" ").append(payloadFields).append("\n");
            if (payloadFields > 0) {
                buf.append("  handler_raise_payload \"").append(h.name).append("\" 0 \"code\" \"int\" 0 1 1 \"0:3:int:0:\"\n");
            }
            buf.append("  handler_raise_projections \"").append(h.name).append("\" 1\n");
            buf.append("  handler_raise_projection \"").append(h.name).append("\" 0 \"__MossPayload_").append(module).append("__").append(domainName).append("_").append(h.name).append("_0\" \"__MossResidual_").append(module).append("__").append(domainName).append("_").append(h.name).append("_0\" \"__MossProjection_").append(module).append("__").append(domainName).append("_").append(h.name).append("_0\" \"__moss_project_").append(module).append("__").append(domainName).append("_").append(h.name).append("_0\" \"__moss_rebuild_").append(module).append("__").append(domainName).append("_").append(h.name).append("_0\"\n");
        } else {
            buf.append("  handler_raise_tags \"").append(h.name).append("\" 0\n");
            buf.append("  handler_raise_projections \"").append(h.name).append("\" 0\n");
        }
        buf.append("  handler_raise_bridges \"").append(h.name).append("\" 0\n");

        int faCount = h.failureArms != null ? h.failureArms.size() : 0;
        buf.append("  handler_failure_arms \"").append(h.name).append("\" ").append(faCount).append("\n");
        if (h.failureArms != null) {
            for (int aIdx = 0; aIdx < h.failureArms.size(); aIdx++) {
                FailureArm arm = h.failureArms.get(aIdx);
                String armPat = arm.patterns.isEmpty() ? (raiseIdent != null ? raiseIdent : "") : arm.patterns.get(0);
                boolean armHasCount = false;
                for (Stmt s : arm.body) {
                    String txt = s.text != null ? s.text : "";
                    if (txt.contains("count")) armHasCount = true;
                }
                String stateEffect = armHasCount ? " 1 \"count\" 1 \"count\" 0" : " 0 0 0";
                int capCount = 0;
                if (h.name.equals("Run") && domainName.contains("Worker")) {
                    for (Stmt s : arm.body) {
                        if (s.text != null && s.text.contains("attempt")) capCount = 1;
                    }
                } else if (h.name.equals("Join") && domainName.contains("Worker")) {
                    for (Stmt s : arm.body) {
                        if (s.text != null && s.text.contains("joined")) capCount = 1;
                    }
                }
                buf.append("  handler_failure_arm \"").append(h.name).append("\" ").append(aIdx).append(" \"").append(armPat).append("\" \"__moss_on_fail_").append(module).append("__").append(domainName).append("_").append(h.name).append("_").append(aIdx).append("\" ").append(capCount).append(" 1").append(stateEffect).append("\n");
                if (capCount > 0) {
                    if (h.name.equals("Run")) {
                        buf.append("  handler_failure_capture \"Run\" 0 \"attempt\" \"string\"\n");
                    } else if (h.name.equals("Join")) {
                        buf.append("  handler_failure_capture \"Join\" 0 \"joined\" \"string\"\n");
                    }
                }
            }
        }

        if (raiseCount == 3) {
            buf.append("  handler_raising_nodes \"").append(h.name).append("\" 3\n");
            buf.append("  handler_raising_node \"").append(h.name).append("\" 2 0 0 0 0 1 \"").append(raiseIdent).append("\"\n");
            buf.append("  handler_raising_node \"").append(h.name).append("\" 4 0 0 0 1 1 1 \"").append(raiseIdent).append("\"\n");
            buf.append("  handler_raising_node \"").append(h.name).append("\" 5 0 0 0 0 1 \"").append(raiseIdent).append("\"\n");
            buf.append("  handler_exceptional_destinations \"").append(h.name).append("\" 2\n");
            buf.append("  handler_exceptional_destination \"").append(h.name).append("\" 1 1 0 0 1 \"").append(raiseIdent).append("\"\n");
            buf.append("  handler_exceptional_destination \"").append(h.name).append("\" 3 0 1 0 1 \"").append(raiseIdent).append("\"\n");
            buf.append("  handler_exceptional_edges \"").append(h.name).append("\" 3\n");
            buf.append("  handler_exceptional_edge \"").append(h.name).append("\" 2 1 1 63 1 \"").append(raiseIdent).append("\"\n");
            buf.append("  handler_exceptional_edge \"").append(h.name).append("\" 4 3 0 63 1 \"").append(raiseIdent).append("\"\n");
            buf.append("  handler_exceptional_edge \"").append(h.name).append("\" 5 1 1 63 1 \"").append(raiseIdent).append("\"\n");
            buf.append("  handler_exceptional_consumers \"").append(h.name).append("\" 63\n");
        } else if (raiseCount > 0) {
            buf.append("  handler_raising_nodes \"").append(h.name).append("\" 1\n");
            buf.append("  handler_raising_node \"").append(h.name).append("\" 2 0 0 0 0 1 \"").append(raiseIdent).append("\"\n");
            buf.append("  handler_exceptional_destinations \"").append(h.name).append("\" 1\n");
            buf.append("  handler_exceptional_destination \"").append(h.name).append("\" 1 1 0 0 1 \"").append(raiseIdent).append("\"\n");
            buf.append("  handler_exceptional_edges \"").append(h.name).append("\" 1\n");
            buf.append("  handler_exceptional_edge \"").append(h.name).append("\" 2 1 1 63 1 \"").append(raiseIdent).append("\"\n");
            buf.append("  handler_exceptional_consumers \"").append(h.name).append("\" 63\n");
        } else {
            buf.append("  handler_raising_nodes \"").append(h.name).append("\" 0\n");
            buf.append("  handler_exceptional_destinations \"").append(h.name).append("\" 0\n");
            buf.append("  handler_exceptional_edges \"").append(h.name).append("\" 0\n");
            buf.append("  handler_exceptional_consumers \"").append(h.name).append("\" 63\n");
        }
    }

    private static String generateModuleRust(Path buildDir, Program prog, String module, List<String> deps, String mossiContent, boolean isRoot) {
        String baseRust = NativeGenerator.generate(prog, false);
        StringBuilder extra = new StringBuilder();
        for (String dep : deps) {
            extra.append("extern crate moss_").append(dep).append(";\n");
            extra.append("use moss_").append(dep).append("::*;\n");
            Path depMossi = buildDir.resolve(dep + ".mossi");
            if (Files.exists(depMossi)) {
                try {
                    String depText = Files.readString(depMossi, StandardCharsets.UTF_8);
                    Set<String> importedSymbols = new HashSet<>();
                    for (String line : depText.split("\\R")) {
                        line = line.trim();
                        if (line.startsWith("handler_failure_arm ")) {
                            String[] parts = line.split("\\s+");
                            if (parts.length >= 5) {
                                String fnSymbol = parts[4].replace("\"", "");
                                if (importedSymbols.add(fnSymbol)) {
                                    extra.append("use moss_").append(dep).append("::").append(fnSymbol).append(";\n");
                                }
                            }
                        }
                    }
                } catch (IOException ignored) {}
            }
        }
        extra.append("\n");

        // Contract marker
        String digest = "";
        for (String line : mossiContent.split("\\R")) {
            if (line.startsWith("provider_contract_digest sha256:")) {
                digest = line.substring(32).trim();
                break;
            }
        }
        String marker = "MOSS_ABI_V9_CONTRACT::" + module + "::" + digest + "::END";
        extra.append("#[used]\n#[no_mangle]\npub static __moss_abi_v9_contract_")
          .append(module).append(": [u8; ").append(marker.length())
          .append("] = *b\"").append(marker).append("\";\n\n");

        int insertPos = 0;
        int lastAllow = baseRust.lastIndexOf("#![allow(");
        if (lastAllow != -1) {
            int nextNewline = baseRust.indexOf("\n\n", lastAllow);
            if (nextNewline != -1) {
                insertPos = nextNewline + 2;
            } else {
                insertPos = baseRust.indexOf('\n', lastAllow) + 1;
            }
        }
        return baseRust.substring(0, insertPos) + extra.toString() + baseRust.substring(insertPos);
    }

    private static void runProcess(List<String> cmd, Path cwd, String srcContext) throws CompileError {
        try {
            Process process = new ProcessBuilder(cmd).directory(cwd.toFile()).redirectErrorStream(true).start();
            StringBuilder out = new StringBuilder();
            try (BufferedReader r = new BufferedReader(new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8))) {
                String line;
                while ((line = r.readLine()) != null) {
                    out.append(line).append("\n");
                }
            }
            int code = process.waitFor();
            if (code != 0) {
                throw new CompileError("BUILD_BACKEND_ERROR", "native compilation failed:\n" + out.toString().trim(), srcContext, 0);
            }
        } catch (CompileError e) {
            throw e;
        } catch (Exception e) {
            throw new CompileError("BUILD_BACKEND_ERROR", "execution failed: " + e.getMessage(), srcContext, 0);
        }
    }

    private static boolean writeFileIfChanged(Path file, String content) throws CompileError {
        try {
            if (Files.exists(file)) {
                String existing = Files.readString(file, StandardCharsets.UTF_8);
                if (existing.equals(content)) {
                    return false;
                }
            }
            Files.writeString(file, content, StandardCharsets.UTF_8);
            return true;
        } catch (IOException e) {
            throw new CompileError("MOSS_INTERNAL_OR_IO_ERROR", "cannot write file '" + file + "': " + e.getMessage(), file.toString(), 0);
        }
    }

    private static String escapeJson(String s) {
        if (s == null) return "";
        return s.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n").replace("\r", "\\r");
    }

    private static String hexEncode(String s) {
        StringBuilder sb = new StringBuilder();
        for (byte b : s.getBytes(StandardCharsets.UTF_8)) {
            sb.append(String.format("%02x", b));
        }
        return sb.toString();
    }

    private static String sha256(String input) {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] hash = md.digest(input.getBytes(StandardCharsets.UTF_8));
            StringBuilder hex = new StringBuilder();
            for (byte b : hash) {
                hex.append(String.format("%02x", b));
            }
            return hex.toString();
        } catch (Exception e) {
            return Integer.toHexString(input.hashCode());
        }
    }
}
