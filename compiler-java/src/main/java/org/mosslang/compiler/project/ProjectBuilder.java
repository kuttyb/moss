package org.mosslang.compiler.project;

import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.backend.NativeGenerator;
import org.mosslang.compiler.diagnostics.CompileError;
import org.mosslang.compiler.diagnostics.DiagnosticFormatter;
import org.mosslang.compiler.parser.Parser;
import org.mosslang.compiler.semantics.CheckedProgram;
import org.mosslang.compiler.semantics.SemanticChecker;
import org.mosslang.compiler.interpreter.FastInterpreter;
import org.mosslang.compiler.abi.AbiV10Records;
import org.mosslang.compiler.abi.CheckedExpr;
import org.mosslang.compiler.abi.CheckedStmt;
import org.mosslang.compiler.abi.CheckedBodyCodec;
import org.mosslang.compiler.abi.MossiImport;
import org.mosslang.compiler.abi.MossiInterface;
import org.mosslang.compiler.abi.MossiRecord;
import org.mosslang.compiler.types.TypeRef;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.regex.*;
import java.util.stream.Stream;

public class ProjectBuilder {

    private static final class ProjectTestResult {
        String id;
        String name;
        String sourceFile;
        int line;
        boolean passed;
        String detail;
    }

    private static final class ProjectBenchmarkResult {
        String id;
        String name;
        String sourceFile;
        int line;
        long medianNs;
        long p25Ns;
        long p75Ns;
        int samples;
        int warmup;
        int iterations;
        List<Long> sampleValues;
    }

    private static void reportProjectTests(ProjectManifest manifest, String filter,
                                           List<ProjectTestResult> results, boolean json) {
        int passed = (int) results.stream().filter(result -> result.passed).count();
        int failed = results.size() - passed;
        if (json) {
            StringBuilder out = new StringBuilder("{\n  \"schema_version\": \"moss-agent-1\",\n  \"command\": \"test\",\n  \"ok\": ")
                    .append(failed == 0).append(",\n  \"result\": {\"project\": \"")
                    .append(escapeJson(manifest.getName())).append("\", \"filter\": ")
                    .append(filter == null || filter.isBlank() ? "null" : "\"" + escapeJson(filter) + "\"")
                    .append(", \"tests\": [");
            for (int i = 0; i < results.size(); i++) {
                ProjectTestResult result = results.get(i);
                if (i > 0) out.append(", ");
                out.append("{\"id\": \"").append(escapeJson(result.id)).append("\", \"name\": \"")
                        .append(escapeJson(result.name)).append("\", \"status\": \"")
                        .append(result.passed ? "pass" : "fail").append("\", \"source_file\": \"")
                        .append(escapeJson(result.sourceFile)).append("\", \"line\": ").append(result.line)
                        .append(", \"diagnostic\": ")
                        .append(result.passed ? "null" : "{\"code\": \"TEST_ASSERTION_FAILED\", \"message\": \"" + escapeJson(result.detail) + "\"}")
                        .append("}");
            }
            out.append("], \"passed\": ").append(passed).append(", \"failed\": ").append(failed).append("}\n}");
            System.out.println(out);
            return;
        }
        System.out.println("Running tests for " + manifest.getName() + "...");
        for (ProjectTestResult result : results) {
            System.out.println((result.passed ? "PASS " : "FAIL ") + result.name);
            if (!result.passed) System.out.println("  " + result.sourceFile + ":" + result.line + ": " + result.detail);
        }
        System.out.println(passed + " passed; " + failed + " failed");
    }

    private static void reportProjectBenchmarks(ProjectManifest manifest, String filter,
                                                List<ProjectBenchmarkResult> results, boolean json) {
        if (json) {
            StringBuilder out = new StringBuilder("{\n  \"schema_version\": \"moss-agent-1\",\n  \"command\": \"bench\",\n  \"ok\": true,\n  \"result\": {\"project\": \"")
                    .append(escapeJson(manifest.getName())).append("\", \"filter\": ")
                    .append(filter == null || filter.isBlank() ? "null" : "\"" + escapeJson(filter) + "\"")
                    .append(", \"benchmarks\": [");
            for (int i = 0; i < results.size(); i++) {
                ProjectBenchmarkResult result = results.get(i);
                if (i > 0) out.append(", ");
                out.append("{\"id\": \"").append(escapeJson(result.id)).append("\", \"name\": \"")
                        .append(escapeJson(result.name)).append("\", \"source_file\": \"")
                        .append(escapeJson(result.sourceFile)).append("\", \"line\": ").append(result.line)
                        .append(", \"median_ns\": ").append(result.medianNs).append(", \"p25_ns\": ").append(result.p25Ns)
                        .append(", \"p75_ns\": ").append(result.p75Ns).append(", \"samples\": ").append(result.samples)
                        .append(", \"warmup\": ").append(result.warmup).append(", \"iterations\": ").append(result.iterations)
                        .append(", \"sample_values_ns\": ").append(result.sampleValues).append("}");
            }
            out.append("]}\n}");
            System.out.println(out);
            return;
        }
        System.out.println("Benchmarks for " + manifest.getName() + " (Fast Debug interpreter; nanoseconds per execution):");
        for (ProjectBenchmarkResult result : results) {
            System.out.println(result.name + ": median=" + result.medianNs + " ns, p25=" + result.p25Ns
                    + " ns, p75=" + result.p75Ns + " ns (" + result.samples + " samples, "
                    + result.warmup + " warmups, " + result.iterations + " executions/sample)");
        }
    }

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
        return test(manifest, filter, release, json, false);
    }

    public static int test(ProjectManifest manifest, String filter, boolean release, boolean json, boolean trace) throws CompileError {
        verifyImports(manifest);
        Program program = loadProjectDeclarations(manifest, "tests");
        List<TestDecl> selected = new ArrayList<>();
        for (TestDecl test : program.tests) {
            if (filter == null || filter.isBlank() || test.name.contains(filter)
                    || test.semanticIdentity.contains(filter)) selected.add(test);
        }
        if (selected.isEmpty()) {
            throw new CompileError("TEST_DISCOVERY_ERROR",
                    filter == null || filter.isBlank() ? "no Moss tests were found"
                            : "no Moss tests matched filter '" + filter + "'",
                    manifest.getRoot().toString(), 0);
        }

        // Check the complete project once, then execute each test body in a fresh
        // Fast Debug state so a failed test cannot contaminate the next one.
        new CheckedProgram(program).run();
        List<ProjectTestResult> results = new ArrayList<>();
        for (TestDecl test : selected) {
            Program executable = executableWithBody(program, test.body, test.sourceFile);
            ByteArrayOutputStream stdout = new ByteArrayOutputStream();
            ByteArrayOutputStream stderr = new ByteArrayOutputStream();
            ProjectTestResult result = new ProjectTestResult();
            result.id = test.semanticIdentity;
            result.name = test.name;
            result.sourceFile = test.sourceFile;
            result.line = test.line;
            try {
                new FastInterpreter(executable, trace,
                        new PrintStream(stdout, true, StandardCharsets.UTF_8),
                        trace ? System.err : new PrintStream(stderr, true, StandardCharsets.UTF_8)).run();
                result.passed = true;
                result.detail = "";
            } catch (RuntimeException e) {
                result.passed = false;
                result.detail = e.getMessage() != null ? e.getMessage() : e.getClass().getSimpleName();
            }
            results.add(result);
        }
        reportProjectTests(manifest, filter, results, json);
        return results.stream().allMatch(r -> r.passed) ? 0 : 1;
    }

    public static int bench(ProjectManifest manifest, String filter, boolean json) throws CompileError {
        verifyImports(manifest);
        Program program = loadProjectDeclarations(manifest, "benches");
        List<BenchDecl> selected = new ArrayList<>();
        for (BenchDecl bench : program.benchmarks) {
            if (filter == null || filter.isBlank() || bench.name.contains(filter)
                    || bench.semanticIdentity.contains(filter)) selected.add(bench);
        }
        if (selected.isEmpty()) {
            throw new CompileError("BENCHMARK_CONFIGURATION_ERROR",
                    filter == null || filter.isBlank() ? "no Moss benchmarks were found"
                            : "no Moss benchmarks matched filter '" + filter + "'",
                    manifest.getRoot().toString(), 0);
        }
        // Reuse the semantic test-body checker for benchmark declarations.
        Program checked = projectCopy(program);
        for (BenchDecl bench : selected) {
            TestDecl asTest = new TestDecl(bench.name, bench.header, bench.line, bench.sourceFile);
            asTest.body = bench.body;
            checked.tests.add(asTest);
        }
        checked.benchmarks.clear();
        new CheckedProgram(checked).run();

        List<ProjectBenchmarkResult> results = new ArrayList<>();
        final int warmup = 5;
        final int samples = 31;
        final int iterations = 100;
        for (BenchDecl bench : selected) {
            Program executable = executableWithBody(program, bench.body, bench.sourceFile);
            for (int i = 0; i < warmup; i++) runInterpreter(executable);
            List<Long> values = new ArrayList<>();
            for (int sample = 0; sample < samples; sample++) {
                long start = System.nanoTime();
                for (int iteration = 0; iteration < iterations; iteration++) runInterpreter(executable);
                values.add((System.nanoTime() - start) / iterations);
            }
            Collections.sort(values);
            ProjectBenchmarkResult result = new ProjectBenchmarkResult();
            result.id = bench.semanticIdentity;
            result.name = bench.name;
            result.sourceFile = bench.sourceFile;
            result.line = bench.line;
            result.medianNs = values.get(values.size() / 2);
            result.p25Ns = values.get(values.size() / 4);
            result.p75Ns = values.get((values.size() * 3) / 4);
            result.samples = samples;
            result.warmup = warmup;
            result.iterations = iterations;
            result.sampleValues = values;
            results.add(result);
        }
        reportProjectBenchmarks(manifest, filter, results, json);
        return 0;
    }

    private static void runInterpreter(Program program) {
        new FastInterpreter(program, false,
                new PrintStream(new ByteArrayOutputStream()),
                new PrintStream(new ByteArrayOutputStream())).run();
    }

    private static Program executableWithBody(Program source, List<Stmt> body, String sourceFile) {
        Program executable = projectCopy(source);
        executable.tests.clear();
        executable.benchmarks.clear();
        executable.main = new MainProc();
        executable.main.body.addAll(body);
        executable.main.sourceFile = sourceFile;
        return executable;
    }

    private static Program projectCopy(Program source) {
        Program copy = new Program();
        copy.sourceFile = source.sourceFile;
        copy.sourceLines.addAll(source.sourceLines);
        copy.moduleName = source.moduleName;
        copy.mainModule = source.mainModule;
        copy.explicitModule = source.explicitModule;
        copy.imports.addAll(source.imports);
        copy.functions.addAll(source.functions);
        copy.traits.addAll(source.traits);
        copy.objects.addAll(source.objects);
        copy.enums.addAll(source.enums);
        copy.domains.addAll(source.domains);
        copy.tests.addAll(source.tests);
        copy.benchmarks.addAll(source.benchmarks);
        copy.main = source.main;
        return copy;
    }

    public static Program loadProjectDeclarations(ProjectManifest manifest, String declarationDirectory) throws CompileError {
        List<Path> paths = new ArrayList<>(manifest.findSourceFiles());
        Path extra = declarationDirectory != null ? manifest.getRoot().resolve(declarationDirectory) : null;
        if (extra != null && Files.isDirectory(extra)) {
            try (Stream<Path> walk = Files.walk(extra)) {
                walk.filter(Files::isRegularFile).filter(f -> f.toString().endsWith(".moss")).forEach(paths::add);
            } catch (IOException e) {
                throw new CompileError("TEST_DISCOVERY_ERROR", "cannot scan " + declarationDirectory + ": " + e.getMessage(), extra.toString(), 0);
            }
        }
        paths.sort(Comparator.comparing(Path::toString));
        Program combined = new Program();
        Set<String> enumNames = new HashSet<>();
        for (Path path : paths) {
            try {
                Program parsed = Parser.parseSource(Files.readString(path, StandardCharsets.UTF_8), path.toString());
                if (combined.sourceFile.isEmpty()) combined.sourceFile = path.toString();
                combined.functions.addAll(parsed.functions);
                combined.traits.addAll(parsed.traits);
                combined.objects.addAll(parsed.objects);
                combined.imports.addAll(parsed.imports);
                for (EnumType value : parsed.enums) if (enumNames.add(value.name)) combined.enums.add(value);
                combined.domains.addAll(parsed.domains);
                combined.tests.addAll(parsed.tests);
                combined.benchmarks.addAll(parsed.benchmarks);
                if (parsed.main != null && combined.main == null) combined.main = parsed.main;
            } catch (IOException e) {
                throw new CompileError("PROJECT_SOURCE_NOT_FOUND", "cannot read Moss source '" + path + "': " + e.getMessage(), path.toString(), 0);
            }
        }
        return combined;
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
        return getExternalCandidateProviders(manifest, null);
    }

    private static Map<String, List<Path>> getExternalCandidateProviders(ProjectManifest manifest, String profile) {
        Map<String, List<Path>> externalCandidateProviders = new HashMap<>();
        List<Path> candidates = SourceFreeAbi.findInterfaceCandidates(manifest != null ? manifest.getRoot() : null);
        String excludedSubdir = null;
        if (profile != null && manifest != null) {
            String opposite = "release".equals(profile) ? "debug" : "release";
            excludedSubdir = manifest.getRoot().resolve("build").resolve(opposite).toAbsolutePath().normalize().toString();
        }
        for (Path f : candidates) {
            Path norm = f.toAbsolutePath().normalize();
            if (excludedSubdir != null && norm.toString().startsWith(excludedSubdir + File.separator)) {
                continue;
            }
            String name = f.getFileName().toString();
            if (name.endsWith(".mossi")) {
                name = name.substring(0, name.length() - 6);
                externalCandidateProviders.computeIfAbsent(name, k -> new ArrayList<>()).add(norm);
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
        Map<String, List<Path>> externalCandidateProviders = getExternalCandidateProviders(manifest, profile);

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
        for (String mod : localModules.keySet()) {
            externalCandidateProviders.remove(mod);
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

            // Explicit module builds previously reached interface/Rust
            // emission without running the semantic checker. Check each module
            // using local declarations and verified imported ABI declarations.
            for (String mod : moduleOrder) {
                List<String> imports = moduleImports.getOrDefault(mod, Collections.emptyList());
                Program checkView = projectCopy(parsedPrograms.get(mod));
                Set<String> included = new LinkedHashSet<>();
                for (String dependency : imports) {
                    if (localModules.containsKey(dependency)) {
                        collectLocalModuleDependencies(dependency, moduleImports, localModules, included);
                    } else {
                        Path providerInterface = buildDir.resolve(dependency + ".mossi");
                        List<Path> candidates = externalCandidateProviders.get(dependency);
                        if (candidates != null && !candidates.isEmpty()) {
                            providerInterface = candidates.get(0);
                        }
                        if (!Files.exists(providerInterface)) {
                            throw new CompileError("MODULE_IMPORT_ERROR",
                                    "imported module '" + dependency + "' has no interface for semantic checking",
                                    localModules.get(mod).toString(), 1);
                        }
                        org.mosslang.compiler.cli.Main.loadMossiFunctions(
                                providerInterface, dependency, checkView);
                    }
                }
                included.remove(mod);
                for (String dependency : included) {
                    Program declarations = parsedPrograms.get(dependency);
                    appendUniqueDeclarations(checkView.functions, declarations.functions, fn -> fn.name);
                    appendUniqueDeclarations(checkView.traits, declarations.traits, trait -> trait.name);
                    appendUniqueDeclarations(checkView.objects, declarations.objects, object -> object.name);
                    appendUniqueDeclarations(checkView.enums, declarations.enums, enumeration -> enumeration.name);
                    appendUniqueDeclarations(checkView.domains, declarations.domains, domain -> domain.name);
                }
                new CheckedProgram(checkView).run();
            }

            for (String mod : moduleOrder) {
                Path srcFile = localModules.get(mod);
                Program prog = parsedPrograms.get(mod);
                List<String> deps = moduleImports.getOrDefault(mod, Collections.emptyList());

                // Generate .mossi interface
                byte[] mossiContent = generateMossiInterface(manifest, mod, prog, toolchain, deps, buildDir, externalCandidateProviders);
                Path mossiPath = buildDir.resolve(mod + ".mossi");
                writeFileIfChanged(mossiPath, mossiContent);
                emittedInterfaces.add(mossiPath);

                // Generate module Rust
                boolean isRoot = mod.equals(rootModuleName);
                String mossiDigest = providerDigest(mossiContent);
                String rustContent = generateModuleRust(buildDir, externalCandidateProviders, prog, mod, deps, mossiDigest, isRoot);
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

    private static void collectLocalModuleDependencies(String module,
                                                       Map<String, List<String>> moduleImports,
                                                       Map<String, Path> localModules,
                                                       Set<String> result) {
        if (!localModules.containsKey(module) || !result.add(module)) return;
        for (String dependency : moduleImports.getOrDefault(module, Collections.emptyList())) {
            collectLocalModuleDependencies(dependency, moduleImports, localModules, result);
        }
    }

    private static <T> void appendUniqueDeclarations(List<T> target, List<T> source,
                                                      java.util.function.Function<T, String> name) {
        Set<String> names = new HashSet<>();
        for (T declaration : target) names.add(name.apply(declaration));
        for (T declaration : source) {
            if (names.add(name.apply(declaration))) target.add(declaration);
        }
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

    static void appendEnumCaseContract(StringBuilder out, EnumCase enumCase) {
        out.append("case ").append(enumCase.name);
        if (enumCase.fields != null) {
            for (Field field : enumCase.fields) {
                out.append(" ").append(field.name).append(":")
                        .append(field.type != null && !field.type.isEmpty()
                                ? field.type.toLowerCase() : "int");
            }
        }
        out.append("\n");
    }

    static void appendEnumCaseSemantic(StringBuilder out, EnumCase enumCase) {
        int fieldCount = enumCase.fields != null ? enumCase.fields.size() : 0;
        out.append("  enum_case \"").append(enumCase.name).append("\" ")
                .append(fieldCount);
        if (enumCase.fields != null) {
            for (Field field : enumCase.fields) {
                out.append(" \"").append(field.name).append("\" \"")
                        .append(field.type != null && !field.type.isEmpty()
                                ? field.type.toLowerCase() : "int")
                        .append("\"");
            }
        }
        out.append("\n");
    }

    static MossiRecord v10ExportedEnumRecord(EnumType enumeration, String module,
                                             String moduleId, int ordinal) {
        String enumName = localTypeName(enumeration.name, module);
        StringBuilder payload = new StringBuilder()
                .append("export enum ").append(enumName).append("\n")
                .append("  declaration_id nominal-enum-v1:")
                .append(moduleId).append(":enum:").append(enumName).append("\n");
        for (EnumCase enumCase : enumeration.cases) appendEnumCaseSemantic(payload, enumCase);
        String id = String.format(Locale.ROOT, "%08d export %s", ordinal, enumName);
        return new MossiRecord(id, payload.toString());
    }

    static String v10ImportRecord(String dependencyName, SourceFreeAbi.MossiInfo dependency,
                                  Path dependencyArtifact) throws CompileError {
        if (dependency == null || dependency.moduleId == null
                || !dependency.moduleId.startsWith("module-v1:")
                || dependency.binaryHash == null || !dependency.binaryHash.startsWith("sha256:")
                || dependency.semanticHash == null || !dependency.semanticHash.startsWith("sha256:")) {
            throw new CompileError("SOURCE_FREE_ABI_REBUILD_REQUIRED",
                    "ABI-v10 provider dependency '" + dependencyName + "' is missing a complete v10 module identity or contract hash; rebuild it with MOSS_STAGED_ABI=v10",
                    dependencyArtifact != null ? dependencyArtifact.toString() : "", 1);
        }
        return dependency.moduleId + " " + dependency.binaryHash + " " + dependency.semanticHash + "\n";
    }

    static void appendExportedObjectFields(StringBuilder out, ObjectType object) {
        for (Field field : object.fields) {
            out.append("  public_representation field ").append(field.name);
            if (field.type != null && !field.type.isEmpty()) {
                out.append(" ").append(field.type);
            }
            out.append("\n");
        }
    }

    static void appendV9ObjectMethodRecord(StringBuilder out, ObjectType object,
                                           Method method, String module) {
        String returnType = method.returnType != null && !method.returnType.isEmpty()
                ? method.returnType : "unit";
        String raiseIdent = raisedErrorIdentity(module, method.body);
        out.append("  object_method ").append(method.name)
                .append(" return=").append(returnType)
                .append(" receiver=").append(method.receiverAccessMode.name().toUpperCase(Locale.ROOT))
                .append(" effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=")
                .append(mayPanic(method.body) ? "1" : "0")
                .append(";raise_set=").append(raiseIdent)
                .append(";may_diverge=0;unresolved=0 params=")
                .append(method.params.size());
        for (int index = 0; index < method.params.size(); index++) {
            Param parameter = method.params.get(index);
            org.mosslang.compiler.types.AccessMode access = index < method.parameterAccessModes.size()
                    ? method.parameterAccessModes.get(index)
                    : org.mosslang.compiler.types.AccessMode.Read;
            out.append(" \"").append(parameter.name).append("\" \"")
                    .append(parameter.type != null ? parameter.type : "")
                    .append("\" ").append(access.name().toUpperCase(Locale.ROOT));
        }
        out.append("\n");
    }

    static void appendV9FunctionRecord(StringBuilder out, Function function, String publicName,
                                       String effects, boolean semanticRecord) {
        String kind = function.generic ? "generic"
                : function.staticDispatch ? "static" : "concrete";
        String semanticBody = function.header + "\n";
        for (Stmt statement : function.body) semanticBody += statement.text + "\n";
        if (function.resultExpression != null) semanticBody += function.resultExpression + "\n";
        String semanticHash = fnv1a64Hex(semanticBody);
        String returnType;
        if (function.returnTypeRef != null) {
            TypeRef checkedReturn = function.returnTypeRef;
            if (checkedReturn.isClosed()) {
                returnType = checkedReturn.toCanonicalString();
            } else if (checkedReturn.kind() == org.mosslang.compiler.types.TypeKind.Builtin
                    || checkedReturn.kind() == org.mosslang.compiler.types.TypeKind.NominalInstance) {
                returnType = checkedReturn.name();
            } else {
                returnType = "_";
            }
        } else if (function.returnType != null && !function.returnType.isEmpty()) {
            returnType = function.returnType;
        } else {
            returnType = "unit";
        }
        if (semanticRecord) {
            out.append("export fn ").append(publicName).append(" kind=").append(kind)
                    .append(" semantic_hash=").append(semanticHash)
                    .append(" return=").append(returnType).append(" ").append(effects)
                    .append("\n");
        } else {
            String symbol = org.mosslang.compiler.backend.DebugMapGenerator.toolingNativeSymbol(
                    "function", function.name, "fn:" + function.name + "@" + function.line);
            out.append("fn ").append(publicName).append(" kind=").append(kind)
                    .append(" return=").append(returnType).append(" ").append(effects)
                    .append(" symbol=").append(symbol).append("\n");
        }
        for (int i = 0; i < function.params.size(); i++) {
            Param parameter = function.params.get(i);
            org.mosslang.compiler.types.AccessMode access = i < function.parameterAccessModes.size()
                    ? function.parameterAccessModes.get(i) : org.mosslang.compiler.types.AccessMode.Read;
            out.append("  param ").append(i).append(" ")
                    .append(parameter.type != null ? parameter.type : "")
                    .append(" mode=").append(access.name().toUpperCase(java.util.Locale.ROOT))
                    .append(" name=\"").append(parameter.name).append("\"\n");
        }
        if (!semanticRecord || (!function.staticDispatch && !function.generic)) return;
        out.append("  open_parameters");
        for (int i = 0; i < function.params.size(); i++) {
            Param parameter = function.params.get(i);
            if (parameter.type == null || parameter.type.isEmpty()) out.append(" ").append(i);
        }
        out.append("\n  requirements\n  specialization_dependencies\n")
                .append("  semantic_ir_body_hash=").append(semanticHash)
                .append("\n  generic_ir_version 1\n  generic_entity_id \"")
                .append("fn:").append(function.name).append("@").append(function.line)
                .append("\"\n  generic_source \"")
                .append(function.sourceFile != null && !function.sourceFile.isEmpty()
                        ? Path.of(function.sourceFile).getFileName() : "")
                .append("\"\n");
        for (int i = 0; i < function.params.size(); i++) {
            Param parameter = function.params.get(i);
            out.append("  generic_param ").append(i).append(" \"").append(parameter.name)
                    .append("\" \"").append(parameter.type != null ? parameter.type : "")
                    .append("\"\n");
        }
        for (Stmt statement : function.body) {
            out.append("  generic_stmt ").append(statement.kind.ordinal()).append(" ")
                    .append(statement.line).append(" ").append(statement.indent).append(" \"")
                    .append(escapeMossiString(statement.text)).append("\" \"")
                    .append(escapeMossiString(statement.a)).append("\" \"")
                    .append(escapeMossiString(statement.b)).append("\" \"")
                    .append(escapeMossiString(statement.semanticType)).append("\" ")
                    .append(statement.args != null ? statement.args.size() : 0);
            if (statement.args != null) {
                for (String argument : statement.args) {
                    out.append(" \"").append(escapeMossiString(argument)).append("\"");
                }
            }
            out.append("\n");
        }
        if (function.resultExpression != null && !function.resultExpression.isEmpty()) {
            out.append("  generic_result ").append(function.resultLine).append(" \"")
                    .append(escapeMossiString(function.resultExpression)).append("\"\n");
        }
        out.append("  generic_ir_end\n");
    }

    private static String escapeMossiString(String value) {
        if (value == null) return "";
        return value.replace("\\", "\\\\").replace("\"", "\\\"");
    }

    private static String providerDigest(byte[] mossiContent) throws CompileError {
        String prefix = "moss-module-interface-v3\nprovider_contract_digest sha256:";
        String text = new String(mossiContent, StandardCharsets.UTF_8);
        if (!text.startsWith(prefix)) {
            throw new CompileError("SOURCE_FREE_CONTRACT_INVALID",
                    "generated ABI-v10 interface is missing its provider digest", "", 1);
        }
        int end = text.indexOf('\n', prefix.length());
        if (end < 0) {
            throw new CompileError("SOURCE_FREE_CONTRACT_INVALID",
                    "generated ABI-v10 provider digest is malformed", "", 1);
        }
        return text.substring(prefix.length(), end).trim();
    }

    static void appendV10FramedRecord(StringBuilder out, String id, String payload) {
        byte[] idBytes = id.getBytes(StandardCharsets.UTF_8);
        byte[] payloadBytes = payload.getBytes(StandardCharsets.UTF_8);
        out.append("record ").append(idBytes.length).append(" ").append(payloadBytes.length).append("\n")
                .append(id).append(payload).append("\n");
    }

    private static String localTypeName(String value, String module) {
        String prefix = module + "__";
        return value != null && value.startsWith(prefix) ? value.substring(prefix.length()) : value;
    }

    private static List<String> splitCanonicalTypeArguments(String body) {
        List<String> result = new ArrayList<>();
        int depth = 0;
        int start = 0;
        for (int i = 0; i < body.length(); i++) {
            char ch = body.charAt(i);
            if (ch == '[') depth++;
            else if (ch == ']') depth--;
            else if (ch == ',' && depth == 0) {
                result.add(body.substring(start, i).trim());
                start = i + 1;
            }
        }
        if (start < body.length()) result.add(body.substring(start).trim());
        return result;
    }

    private static AbiV10Records.WireType v10WireType(String raw, String module,
                                                       String moduleId, Program program) {
        String value = raw == null ? "" : raw.trim();
        String lowered = value.toLowerCase(Locale.ROOT);
        if (lowered.equals("unit")) return AbiV10Records.WireType.unit();
        if (List.of("int", "float", "bool", "string", "char", "range", "rangebatch", "fileio", "executor").contains(lowered)) {
            return AbiV10Records.WireType.scalar(AbiV10Records.WireTypeKind.Primitive, lowered);
        }
        int open = value.indexOf('[');
        if (open > 0 && value.endsWith("]")) {
            String head = value.substring(0, open).trim().toLowerCase(Locale.ROOT);
            AbiV10Records.WireTypeKind kind = switch (head) {
                case "vector", "map", "queue", "set" -> AbiV10Records.WireTypeKind.Builtin;
                default -> null;
            };
            if (kind == null) return null;
            AbiV10Records.WireType result = AbiV10Records.WireType.scalar(kind, head);
            for (String argument : splitCanonicalTypeArguments(value.substring(open + 1, value.length() - 1))) {
                AbiV10Records.WireType parsed = v10WireType(argument, module, moduleId, program);
                if (parsed == null) return null;
                result.arguments.add(parsed);
            }
            int expected = "map".equals(head) ? 2 : 1;
            return result.arguments.size() == expected ? result : null;
        }
        for (ObjectType object : program.objects) {
            String name = localTypeName(object.name, module);
            if (value.equals(object.name) || value.equals(name)) {
                return AbiV10Records.WireType.scalar(AbiV10Records.WireTypeKind.Nominal,
                        "nominal-template-v1:" + moduleId + ":type:" + name);
            }
        }
        for (EnumType enumeration : program.enums) {
            String name = localTypeName(enumeration.name, module);
            if (value.equals(enumeration.name) || value.equals(name)) {
                return AbiV10Records.WireType.scalar(AbiV10Records.WireTypeKind.Enum,
                        "nominal-enum-v1:" + moduleId + ":enum:" + name);
            }
        }
        for (Domain domain : program.domains) {
            String name = localTypeName(domain.name, module);
            if (value.equals(domain.name) || value.equals(name)) {
                return AbiV10Records.WireType.scalar(AbiV10Records.WireTypeKind.Domain,
                        "nominal-domain-v1:" + moduleId + ":domain:" + name);
            }
        }
        return null;
    }

    private static AbiV10Records.NominalTemplate v10NominalTemplate(
            ProjectManifest manifest, String module, String moduleId,
            Program program, ObjectType object,
            List<AbiV10Records.SourceProvenance> provenanceRecords,
            List<MossiRecord> methodTemplateRecords) {
        if (!object.exported || object.sourceFree
                || (object.definingModule != null && !object.definingModule.isEmpty()
                    && !object.definingModule.equals(module))) return null;
        String name = localTypeName(object.name, module);
        String templateId = "nominal-template-v1:" + moduleId + ":type:" + name;
        AbiV10Records.SourceProvenance provenance = AbiV10Records.SourceProvenance.create(
                moduleId, templateId, object.sourceFile, object.line);
        List<AbiV10Records.NominalField> fields = new ArrayList<>();
        int variableOrdinal = 0;
        for (Field field : object.fields) {
            boolean inferred = field.inferred || field.type == null || field.type.isEmpty();
            AbiV10Records.WireType type = inferred
                    ? AbiV10Records.WireType.bound(variableOrdinal++)
                    : v10WireType(field.type, module, moduleId, program);
            if (type == null) return null;
            fields.add(new AbiV10Records.NominalField(field.name, type,
                    field.exported, provenance.provenanceId()));
        }
        List<String> methodIds = new ArrayList<>();
        List<MossiRecord> pendingMethods = new ArrayList<>();
        for (Method method : object.methods) {
            // The Java AST does not yet carry authoritative observable-effect summaries.
            // Emit the canonical record only when the body proves the zero-effect row.
            if (method.resultExpression != null && !method.resultExpression.isEmpty()) return null;
            if (method.body.stream().anyMatch(statement ->
                    statement.kind != org.mosslang.compiler.ast.Stmt.Kind.Pass)) return null;
            if (method.returnType != null && !method.returnType.isEmpty()
                    && !"unit".equalsIgnoreCase(method.returnType)) return null;
            if (method.receiverAccessMode != org.mosslang.compiler.types.AccessMode.Read
                    || method.parameterAccessModes.stream().anyMatch(mode ->
                            mode != org.mosslang.compiler.types.AccessMode.Read)) return null;
            List<AbiV10Records.WireType> parameters = new ArrayList<>();
            List<Integer> parameterAccess = new ArrayList<>();
            for (int index = 0; index < method.params.size(); index++) {
                Param parameter = method.params.get(index);
                AbiV10Records.WireType type = v10WireType(parameter.type, module, moduleId, program);
                if (type == null) return null;
                parameters.add(type);
                org.mosslang.compiler.types.AccessMode mode = index < method.parameterAccessModes.size()
                        ? method.parameterAccessModes.get(index)
                        : org.mosslang.compiler.types.AccessMode.Read;
                parameterAccess.add(mode.ordinal() + 1);
            }
            AbiV10Records.WireType result = method.returnType == null || method.returnType.isEmpty()
                    ? AbiV10Records.WireType.unit()
                    : v10WireType(method.returnType, module, moduleId, program);
            if (result == null) return null;
            String methodId = "method-template-v1:" + moduleId + ":type:" + name
                    + ":method:" + method.name;
            AbiV10Records.MethodTemplate methodTemplate = AbiV10Records.MethodTemplate.create(
                    methodId, templateId, method.name, parameters, parameterAccess, result,
                    method.receiverAccessMode.ordinal() + 1, provenance.provenanceId());
            methodIds.add(methodId);
            pendingMethods.add(methodTemplate.record());
        }
        methodIds.sort(String::compareTo);
        pendingMethods.sort(Comparator.comparing(record -> record.id));
        AbiV10Records.NominalTemplate template = AbiV10Records.NominalTemplate.create(
                moduleId, name, fields, provenance.provenanceId());
        template.methodTemplateIds.addAll(methodIds);
        template.representationHash = template.computeRepresentationHash();
        provenanceRecords.add(provenance);
        methodTemplateRecords.addAll(pendingMethods);
        return template;
    }

    static byte[] generateMossiInterface(ProjectManifest manifest, String module, Program prog, BackendToolchain toolchain, List<String> deps, Path buildDir, Map<String, List<Path>> externalCandidateProviders) {
        StringBuilder contractBuf = new StringBuilder();
        StringBuilder semanticBuf = new StringBuilder();

        for (EnumType e : prog.enums) {
            String enumName = localTypeName(e.name, module);
            contractBuf.append("enum ").append(enumName).append("\n");
            for (EnumCase c : e.cases) {
                appendEnumCaseContract(contractBuf, c);
            }

            if (e.exported) {
                semanticBuf.append("export enum ").append(enumName).append("\n");
                semanticBuf.append("  declaration_id nominal-enum-v1:module-v1:").append(manifest.getName()).append("@").append(manifest.getVersion()).append("::").append(module).append(":enum:").append(enumName).append("\n");
                for (EnumCase c : e.cases) {
                    appendEnumCaseSemantic(semanticBuf, c);
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

            if (d.exported && !d.sourceFree
                    && (d.definingModule == null || d.definingModule.isEmpty()
                        || d.definingModule.equals(module))) {
                semanticBuf.append("export domain ").append(domainName).append(" opaque\n");
                semanticBuf.append("  declaration_id nominal-domain-v1:module-v1:").append(manifest.getName()).append("@").append(manifest.getVersion()).append("::").append(module).append(":domain:").append(domainName).append("\n");
                for (DomainRoute route : d.routes) {
                    semanticBuf.append("  route ").append(route.name).append(": ").append(route.type).append("\n");
                }
                for (Field f : d.state) {
                    semanticBuf.append("  construction_state \"").append(f.name).append("\" \"").append(f.type != null ? f.type.toLowerCase() : "int").append("\" \"").append(f.init != null ? f.init : "").append("\"\n");
                }
                for (Handler h : d.handlers) {
                    emitHandlerMossi(semanticBuf, h, d, module, domainName, prog, true, manifest);
                }
            }
        }

        for (ObjectType obj : prog.objects) {
            String objName = obj.name;
            if (objName.startsWith(module + "__")) {
                objName = objName.substring(module.length() + 2);
            }
            contractBuf.append("object ").append(objName).append("\n");
            for (Method m : obj.methods) {
                appendV9ObjectMethodRecord(contractBuf, obj, m, module);
            }
            semanticBuf.append("export type ").append(objName).append("\n");
            appendExportedObjectFields(semanticBuf, obj);
            for (Method m : obj.methods) {
                appendV9ObjectMethodRecord(semanticBuf, obj, m, module);
            }
        }

        for (Function fn : prog.functions) {
            if (!fn.exported) continue;
            String fnName = fn.name;
            if (fnName.startsWith(module + "__")) {
                fnName = fnName.substring(module.length() + 2);
            }
            String raiseIdent = raisedErrorIdentity(module, fn.body);
            boolean raises = !raiseIdent.equals("-");
            String eff = "effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic="
                    + (mayPanic(fn.body) ? "1" : "0")
                    + ";raise_set=" + (raises ? raiseIdent : "-")
                    + ";may_diverge=0;unresolved=0";
            appendV9FunctionRecord(contractBuf, fn, fnName, eff, false);
            appendV9FunctionRecord(semanticBuf, fn, fnName, eff, true);
        }

        String contractStr = contractBuf.toString();
        String semanticStr = semanticBuf.toString();

        String concreteHash = fnv1a64Hex(contractStr + "\n");
        String interfaceHash = concreteHash;
        String genericSemanticHash = fnv1a64Hex(semanticStr);

        boolean isV10 = "v10".equals(System.getenv("MOSS_STAGED_ABI"));
        if (isV10) {
            String modId = "module-v1:" + manifest.getName() + "@" + manifest.getVersion() + "::" + module;
            List<String> sortedDeps = new ArrayList<>(deps);
            Collections.sort(sortedDeps);
            List<String[]> binRecords = new ArrayList<>();
            List<String[]> semRecords = new ArrayList<>();
            List<MossiRecord> nominalRecords = new ArrayList<>();
            List<MossiRecord> methodTemplateRecords = new ArrayList<>();
            List<MossiRecord> callableSummaryRecords = new ArrayList<>();
            List<MossiRecord> checkedBodyRecords = new ArrayList<>();
            List<AbiV10Records.SourceProvenance> provenanceRecords = new ArrayList<>();
            Set<String> emittedCallableIds = new HashSet<>();
            int exportedDeclarationOrdinal = 0;

            for (EnumType e : prog.enums) {
                String enumName = localTypeName(e.name, module);
                StringBuilder b = new StringBuilder();
                b.append("enum ").append(enumName).append("\n");
                for (EnumCase c : e.cases) {
                    appendEnumCaseContract(b, c);
                }
                binRecords.add(new String[]{"enum:" + enumName, b.toString()});
                if (e.exported && !e.sourceFree
                        && (e.definingModule == null || e.definingModule.isEmpty()
                            || e.definingModule.equals(module))) {
                    MossiRecord record = v10ExportedEnumRecord(e, module, modId,
                            exportedDeclarationOrdinal++);
                    semRecords.add(new String[]{record.id, record.payload});
                }
            }

            for (Domain d : prog.domains) {
                String domainName = d.name;
                if (domainName.startsWith(module + "__")) domainName = domainName.substring(module.length() + 2);
                StringBuilder b = new StringBuilder();
                b.append("domain ").append(domainName).append(" opaque\n");
                for (Field f : d.state) {
                    b.append("  construction_state \"").append(f.name).append("\" \"").append(f.type != null ? f.type.toLowerCase() : "int").append("\" \"").append(f.init != null ? f.init : "").append("\"\n");
                }
                for (Handler h : d.handlers) {
                    emitHandlerMossi(b, h, d, module, domainName, prog, false, manifest);
                }
                binRecords.add(new String[]{"domain:" + domainName, b.toString()});
                if (d.exported && !d.sourceFree
                        && (d.definingModule == null || d.definingModule.isEmpty()
                            || d.definingModule.equals(module))) {
                    StringBuilder semanticDomain = new StringBuilder()
                            .append("export domain ").append(domainName).append(" opaque\n")
                            .append("  declaration_id nominal-domain-v1:").append(modId)
                            .append(":domain:").append(domainName).append("\n");
                    for (DomainRoute route : d.routes) {
                        semanticDomain.append("  route ").append(route.name).append(": ")
                                .append(route.type).append("\n");
                    }
                    for (Field field : d.state) {
                        semanticDomain.append("  construction_state \"").append(field.name)
                                .append("\" \"").append(field.type != null
                                        ? field.type.toLowerCase(Locale.ROOT) : "int")
                                .append("\" \"").append(field.init != null ? field.init : "")
                                .append("\"\n");
                    }
                    for (Handler handler : d.handlers) {
                        emitHandlerMossi(semanticDomain, handler, d, module, domainName,
                                prog, true, manifest);
                    }
                    String id = String.format(Locale.ROOT, "%08d export %s",
                            exportedDeclarationOrdinal++, domainName);
                    semRecords.add(new String[]{id, semanticDomain.toString()});
                }
            }

            for (ObjectType obj : prog.objects) {
                String objName = obj.name;
                if (objName.startsWith(module + "__")) objName = objName.substring(module.length() + 2);
                StringBuilder b = new StringBuilder();
                b.append("type ").append(objName).append(" nominal\n");
                binRecords.add(new String[]{"type:" + objName, b.toString()});

                StringBuilder s = new StringBuilder();
                s.append("export type ").append(objName).append(" nominal\n");
                s.append("  declaration_id nominal-template-v1:").append(modId).append(":type:").append(objName).append("\n");
                appendExportedObjectFields(s, obj);
                for (Method method : obj.methods) {
                    appendV9ObjectMethodRecord(s, obj, method, module);
                }
                semRecords.add(new String[]{"type:" + objName, s.toString()});

                AbiV10Records.NominalTemplate nominal = v10NominalTemplate(
                        manifest, module, modId, prog, obj, provenanceRecords,
                        methodTemplateRecords);
                if (nominal != null) nominalRecords.add(nominal.record());
            }

            for (Function fn : prog.functions) {
                if (!fn.exported) continue;
                String fnName = fn.name;
                if (fnName.startsWith(module + "__")) fnName = fnName.substring(module.length() + 2);
                String raiseIdent = raisedErrorIdentity(module, fn.body);
                boolean raises = !raiseIdent.equals("-");
                String effects = "effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic="
                        + (mayPanic(fn.body) ? "1" : "0")
                        + ";raise_set=" + (raises ? raiseIdent : "-")
                        + ";may_diverge=0;unresolved=0";
                StringBuilder binaryFunction = new StringBuilder();
                StringBuilder semanticFunction = new StringBuilder();
                appendV9FunctionRecord(binaryFunction, fn, fnName, effects, false);
                appendV9FunctionRecord(semanticFunction, fn, fnName, effects, true);
                binRecords.add(new String[]{"fn:" + fnName, binaryFunction.toString()});
                semRecords.add(new String[]{"fn:" + fnName, semanticFunction.toString()});

                boolean localFunction = fn.definingModule == null || fn.definingModule.isEmpty()
                        || fn.definingModule.equals(module);
                boolean sourceBackedFunction = fn.sourceFile == null
                        || !fn.sourceFile.endsWith(".mossi");
                if (localFunction && sourceBackedFunction && fn.semanticCallableId != null
                        && !fn.semanticCallableId.isEmpty()
                        && emittedCallableIds.add(fn.semanticCallableId)) {
                    AbiV10Records.Summary summary = v10DirectReturnSummary(
                            fn, modId, provenanceRecords);
                    CheckedExpr constantReturn = null;
                    CheckedExpr emptyVectorReturn = null;
                    CheckedExpr closedVectorReturn = null;
                    CheckedExpr arithmeticReturn = null;
                    List<CheckedStmt> numericLocalBody = null;
                    List<CheckedStmt> branchReturnBody = null;
                    CheckedExpr floatArithmeticReturn = null;
                    CheckedExpr numericPredicateReturn = null;
                    if (summary == null) {
                        constantReturn = v10ZeroArgLiteralReturn(fn);
                        if (constantReturn != null) {
                            summary = v10ZeroArgLiteralSummary(
                                    fn, constantReturn, modId, provenanceRecords);
                        }
                    }
                    if (summary == null) {
                        emptyVectorReturn = v10EmptyVectorReturn(fn);
                        if (emptyVectorReturn != null) {
                            summary = v10EmptyVectorSummary(
                                    fn, emptyVectorReturn, modId, provenanceRecords);
                        }
                    }
                    if (summary == null) {
                        closedVectorReturn = v10ClosedPrimitiveVectorReturn(fn);
                        if (closedVectorReturn != null) {
                            summary = v10EmptyVectorSummary(
                                    fn, closedVectorReturn, modId, provenanceRecords);
                        }
                    }
                    if (summary == null) {
                        arithmeticReturn = v10ClosedIntExpressionReturn(fn);
                        if (arithmeticReturn != null) {
                            summary = v10ClosedIntExpressionSummary(
                                    fn, arithmeticReturn, modId, provenanceRecords);
                        }
                    }
                    if (summary == null) {
                        numericLocalBody = v10ClosedNumericLocalBody(fn);
                        if (numericLocalBody != null) {
                            summary = v10ClosedBodySummary(
                                    fn, numericLocalBody, modId, provenanceRecords);
                        }
                    }
                    if (summary == null) {
                        branchReturnBody = v10ClosedBranchReturnBody(fn);
                        if (branchReturnBody != null) {
                            summary = v10ClosedBodySummary(
                                    fn, branchReturnBody, modId, provenanceRecords);
                        }
                    }
                    if (summary == null) {
                        floatArithmeticReturn = v10ClosedFloatExpressionReturn(fn);
                        if (floatArithmeticReturn != null) {
                            summary = v10ClosedFloatExpressionSummary(
                                    fn, floatArithmeticReturn, modId, provenanceRecords);
                        }
                    }
                    if (summary == null) {
                        numericPredicateReturn = v10NumericPredicateReturn(fn);
                        if (numericPredicateReturn != null) {
                            summary = v10NumericPredicateSummary(
                                    fn, numericPredicateReturn, modId, provenanceRecords);
                        }
                    }
                    if (summary != null) {
                        callableSummaryRecords.add(summary.record());
                        AbiV10Records.CheckedBodyRecord body = constantReturn != null
                                ? v10SimpleExpressionBody(summary, constantReturn)
                                : emptyVectorReturn != null
                                        ? v10SimpleExpressionBody(summary, emptyVectorReturn)
                                : closedVectorReturn != null
                                        ? v10SimpleExpressionBody(summary, closedVectorReturn)
                                : arithmeticReturn != null
                                        ? v10SimpleExpressionBody(summary, arithmeticReturn)
                                        : numericLocalBody != null
                                                ? v10CheckedBodyRecord(summary, numericLocalBody)
                                        : branchReturnBody != null
                                                ? v10CheckedBodyRecord(summary, branchReturnBody)
                                        : floatArithmeticReturn != null
                                                ? v10SimpleExpressionBody(summary, floatArithmeticReturn)
                                        : numericPredicateReturn != null
                                                ? v10SimpleExpressionBody(summary, numericPredicateReturn)
                                                : v10DirectReturnBody(fn, summary);
                        checkedBodyRecords.add(body.record());
                    }
                }
            }

            binRecords.sort(Comparator.comparing(record -> record[0]));
            semRecords.sort(Comparator.comparing(record -> record[0]));

            MossiInterface mossi = new MossiInterface(modId);
            mossi.compiler = "moss-java-0.1.0";
            mossi.backendFingerprint = toolchain.getFingerprint();
            mossi.backendRustc = toolchain.getVersionVerbose().split("\\R")[0];
            for (String dep : sortedDeps) {
                Path depMossi = buildDir != null ? buildDir.resolve(dep + ".mossi") : null;
                if ((depMossi == null || !Files.exists(depMossi)) && externalCandidateProviders != null) {
                    List<Path> ext = externalCandidateProviders.get(dep);
                    if (ext != null && !ext.isEmpty()) depMossi = ext.get(0);
                }
                SourceFreeAbi.MossiInfo dependency = depMossi != null && Files.exists(depMossi)
                        ? SourceFreeAbi.parseMossiInfo(depMossi) : null;
                if (dependency == null) {
                    throw new CompileError("SOURCE_FREE_ABI_REBUILD_REQUIRED",
                            "ABI-v10 provider dependency '" + dep + "' has no complete interface metadata",
                            depMossi != null ? depMossi.toString() : "", 1);
                }
                mossi.addImport(dependency.moduleId, dependency.binaryHash, dependency.semanticHash);
            }
            for (String[] record : binRecords) {
                mossi.binaryContract.add(new MossiRecord(record[0], record[1]));
            }
            mossi.binaryContract.addAll(nominalRecords);
            mossi.binaryContract.addAll(methodTemplateRecords);
            mossi.binaryContract.addAll(callableSummaryRecords);
            for (String[] record : semRecords) mossi.semanticContract.add(new MossiRecord(record[0], record[1]));
            mossi.privateSemanticDependencies.addAll(checkedBodyRecords);
            for (AbiV10Records.SourceProvenance provenance : provenanceRecords) mossi.provenance.add(provenance.record());
            return CheckedBodyCodec.encodeMossiBytes(mossi);
        }

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
        return (material.substring(0, firstLine + 1) + "provider_contract_digest sha256:" + digest + "\n" + material.substring(firstLine + 1))
                .getBytes(StandardCharsets.UTF_8);
    }

    private static AbiV10Records.Summary v10DirectReturnSummary(
            Function function, String moduleId,
            List<AbiV10Records.SourceProvenance> provenanceRecords) {
        if (function.params.isEmpty() || function.returnTypeRef == null) return null;
        int returnedParameter = v10DirectReturnedParameter(function);
        if (returnedParameter < 0 || !isV10SummaryTypeRepresentable(function.returnTypeRef)
                || !function.returnTypeRef.equals(function.params.get(returnedParameter).checkedTypeRef)) return null;

        Map<org.mosslang.compiler.types.TypeVarId, Integer> ordinals = new LinkedHashMap<>();
        for (Param parameter : function.params) {
            if (!isV10SummaryTypeRepresentable(parameter.checkedTypeRef)) return null;
            collectV10SummaryVariables(parameter.checkedTypeRef, ordinals);
        }
        collectV10SummaryVariables(function.returnTypeRef, ordinals);

        AbiV10Records.Summary summary = new AbiV10Records.Summary();
        summary.callableId = function.semanticCallableId;
        summary.variableCount = ordinals.size();
        for (int index = 0; index < function.params.size(); index++) {
            summary.parameters.add(AbiV10Records.fromTypeRef(
                    function.params.get(index).checkedTypeRef, ordinals));
            org.mosslang.compiler.types.AccessMode access = function.parameterAccessModes.size() > index
                    ? function.parameterAccessModes.get(index)
                    : org.mosslang.compiler.types.AccessMode.Read;
            summary.parameterAccess.add(switch (access) {
                case Read -> AbiV10Records.Access.Read;
                case Write -> AbiV10Records.Access.Write;
                case Consume -> AbiV10Records.Access.Consume;
            });
        }
        summary.result = AbiV10Records.fromTypeRef(function.returnTypeRef, ordinals);
        String bodyBytes = CheckedBodyCodec.encodeCheckedBodyV1(List.of(
                CheckedStmt.returnStmt(CheckedExpr.parameter(returnedParameter,
                        canonicalV10SummaryType(function.params.get(returnedParameter).checkedTypeRef, ordinals)))));
        summary.bodySemanticHash = "body-semantic-v1:sha256:" + CheckedBodyCodec.sha256Hex(bodyBytes);
        AbiV10Records.SourceProvenance provenance = AbiV10Records.SourceProvenance.create(
                moduleId, summary.callableId, function.sourceFile, function.line);
        summary.provenanceId = provenance.provenanceId();
        provenanceRecords.add(provenance);
        return summary;
    }

    private static boolean isV10SummaryTypeRepresentable(TypeRef type) {
        if (type == null) return false;
        return switch (type.kind()) {
            case Primitive, Unit, InferenceVar -> true;
            case Enum, Domain -> type.declaration() != null;
            case Builtin -> type.typeArgs().stream().allMatch(ProjectBuilder::isV10SummaryTypeRepresentable);
            case NominalInstance -> type.declaration() != null
                    && type.fieldSubstitutions().stream()
                            .allMatch(field -> isV10SummaryTypeRepresentable(field.type()));
            case TraitConstraint -> false;
        };
    }

    private static int v10DirectReturnedParameter(Function function) {
        String returned = null;
        if (function.resultExpression != null && !function.resultExpression.isBlank()
                && function.body.isEmpty()) {
            returned = function.resultExpression.trim();
        } else if (function.body.size() == 1
                && function.body.get(0).kind == Stmt.Kind.Return) {
            Stmt statement = function.body.get(0);
            returned = statement.a != null && !statement.a.isBlank() ? statement.a.trim() : "";
            if (returned.isEmpty() && statement.text != null
                    && statement.text.trim().startsWith("return ")) {
                returned = statement.text.trim().substring(7).trim();
            }
        }
        if (returned == null) return -1;
        for (int index = 0; index < function.params.size(); index++) {
            if (returned.equals(function.params.get(index).name)) return index;
        }
        return -1;
    }

    private static CheckedExpr v10ZeroArgLiteralReturn(Function function) {
        if (!function.params.isEmpty() || function.returnTypeRef == null
                || function.returnTypeRef.kind() != org.mosslang.compiler.types.TypeKind.Primitive
                || function.body.size() != 1 || function.body.get(0).kind != Stmt.Kind.Return) return null;
        Stmt statement = function.body.get(0);
        String expression = statement.a != null && !statement.a.isBlank()
                ? statement.a.trim() : "";
        if (expression.isEmpty() && statement.text != null
                && statement.text.trim().startsWith("return ")) {
            expression = statement.text.trim().substring(7).trim();
        }
        TypeRef resultType = canonicalV10SummaryType(function.returnTypeRef, Map.of());
        if ("string".equalsIgnoreCase(function.returnTypeRef.name())
                && expression.matches("\"[^\"\\\\\\r\\n]*\"")) {
            return CheckedExpr.stringLiteral(expression.substring(1, expression.length() - 1), resultType);
        }
        if ("float".equalsIgnoreCase(function.returnTypeRef.name())
                && expression.matches("[-+]?(?:[0-9]+\\.[0-9]+|[0-9]+[eE][+-]?[0-9]+|[0-9]+\\.[0-9]+[eE][+-]?[0-9]+)")) {
            return CheckedExpr.floatLiteral(expression, resultType);
        }
        if ("int".equalsIgnoreCase(function.returnTypeRef.name())
                && expression.matches("-?[0-9](?:_?[0-9])*")) {
            try {
                return CheckedExpr.integerLiteral(Long.parseLong(expression.replace("_", "")), resultType);
            } catch (NumberFormatException ignored) {
                return null;
            }
        }
        if ("bool".equalsIgnoreCase(function.returnTypeRef.name())) {
            if ("true".equals(expression)) return CheckedExpr.boolLiteral(true, resultType);
            if ("false".equals(expression)) return CheckedExpr.boolLiteral(false, resultType);
        }
        return null;
    }

    private static CheckedExpr v10EmptyVectorReturn(Function function) {
        if (!function.params.isEmpty() || function.returnTypeRef == null
                || function.returnTypeRef.kind() != org.mosslang.compiler.types.TypeKind.Builtin
                || !"vector".equalsIgnoreCase(function.returnTypeRef.name())
                || function.returnTypeRef.typeArgs().size() != 1
                || !isV10SummaryTypeRepresentable(function.returnTypeRef)
                || function.body.size() != 1 || function.body.get(0).kind != Stmt.Kind.Return) return null;
        Stmt statement = function.body.get(0);
        String expression = statement.a != null && !statement.a.isBlank()
                ? statement.a.trim() : "";
        if (expression.isEmpty() && statement.text != null
                && statement.text.trim().startsWith("return ")) {
            expression = statement.text.trim().substring(7).trim();
        }
        if (!"[]".equals(expression)) return null;
        Map<org.mosslang.compiler.types.TypeVarId, Integer> ordinals = new LinkedHashMap<>();
        collectV10SummaryVariables(function.returnTypeRef, ordinals);
        if (ordinals.size() > 1) return null;
        CheckedExpr emptyVector = new CheckedExpr();
        emptyVector.kind = org.mosslang.compiler.abi.CheckedExprKind.VectorLiteral;
        emptyVector.type = canonicalV10SummaryType(function.returnTypeRef, ordinals);
        return emptyVector;
    }

    private static CheckedExpr v10ClosedPrimitiveVectorReturn(Function function) {
        if (!function.params.isEmpty() || function.returnTypeRef == null
                || function.returnTypeRef.kind() != org.mosslang.compiler.types.TypeKind.Builtin
                || !"vector".equalsIgnoreCase(function.returnTypeRef.name())
                || function.returnTypeRef.typeArgs().size() != 1
                || !v10ClosedVectorElementSupported(function.returnTypeRef.typeArgs().get(0), 0)
                || function.body.size() != 1 || function.body.get(0).kind != Stmt.Kind.Return) return null;
        Stmt statement = function.body.get(0);
        String expression = statement.a != null && !statement.a.isBlank()
                ? statement.a.trim() : "";
        if (expression.isEmpty() && statement.text != null
                && statement.text.trim().startsWith("return ")) {
            expression = statement.text.trim().substring(7).trim();
        }
        if (expression.length() < 2 || expression.charAt(0) != '['
                || expression.charAt(expression.length() - 1) != ']') return null;
        List<String> elements = org.mosslang.compiler.parser.ParserSupport.splitTopLevel(
                expression.substring(1, expression.length() - 1), ',');
        if (elements.isEmpty() || elements.stream().anyMatch(String::isBlank)) return null;
        return v10LowerClosedVectorLiteral(expression, function.returnTypeRef, function, 0);
    }

    private static boolean v10ClosedVectorElementSupported(TypeRef type, int depth) {
        if (type == null || depth > 16) return false;
        if (type.kind() == org.mosslang.compiler.types.TypeKind.Primitive) {
            return Set.of("int", "float", "bool", "string").contains(
                    type.name().toLowerCase(Locale.ROOT));
        }
        return type.kind() == org.mosslang.compiler.types.TypeKind.Builtin
                && "vector".equalsIgnoreCase(type.name())
                && type.typeArgs().size() == 1
                && v10ClosedVectorElementSupported(type.typeArgs().get(0), depth + 1);
    }

    private static CheckedExpr v10LowerClosedVectorLiteral(
            String source, TypeRef vectorType, Function function, int depth) {
        if (depth > 16 || vectorType == null
                || vectorType.kind() != org.mosslang.compiler.types.TypeKind.Builtin
                || !"vector".equalsIgnoreCase(vectorType.name())
                || vectorType.typeArgs().size() != 1
                || !v10ClosedVectorElementSupported(vectorType.typeArgs().get(0), depth + 1)) return null;
        String expression = source.trim();
        if (expression.length() < 2 || expression.charAt(0) != '['
                || expression.charAt(expression.length() - 1) != ']') return null;
        List<String> elements = org.mosslang.compiler.parser.ParserSupport.splitTopLevel(
                expression.substring(1, expression.length() - 1), ',');
        if (elements.stream().anyMatch(String::isBlank)) return null;
        CheckedExpr vector = new CheckedExpr();
        vector.kind = org.mosslang.compiler.abi.CheckedExprKind.VectorLiteral;
        vector.type = vectorType;
        TypeRef elementType = vectorType.typeArgs().get(0);
        for (String element : elements) {
            CheckedExpr child;
            if (elementType.kind() == org.mosslang.compiler.types.TypeKind.Builtin
                    && "vector".equalsIgnoreCase(elementType.name())) {
                child = v10LowerClosedVectorLiteral(element, elementType, function, depth + 1);
            } else if (TypeRef.primitive("int").equals(elementType)) {
                child = v10LowerClosedIntExpression(element, function, depth + 1);
            } else if (TypeRef.primitive("float").equals(elementType)) {
                child = v10LowerClosedFloatExpression(element, function, depth + 1);
            } else if (TypeRef.primitive("bool").equals(elementType)) {
                child = "true".equals(element.trim()) ? CheckedExpr.boolLiteral(true, elementType)
                        : "false".equals(element.trim()) ? CheckedExpr.boolLiteral(false, elementType) : null;
            } else if (TypeRef.primitive("string").equals(elementType)) {
                String literal = element.trim();
                child = literal.matches("\"[^\"\\\\\\r\\n]*\"")
                        ? CheckedExpr.stringLiteral(literal.substring(1, literal.length() - 1), elementType)
                        : null;
            } else {
                child = null;
            }
            if (child == null) return null;
            vector.operands.add(child);
        }
        return vector;
    }

    private static AbiV10Records.Summary v10EmptyVectorSummary(
            Function function, CheckedExpr expression, String moduleId,
            List<AbiV10Records.SourceProvenance> provenanceRecords) {
        Map<org.mosslang.compiler.types.TypeVarId, Integer> ordinals = new LinkedHashMap<>();
        collectV10SummaryVariables(function.returnTypeRef, ordinals);
        if (ordinals.size() > 1 || !isV10SummaryTypeRepresentable(function.returnTypeRef)) return null;
        AbiV10Records.Summary summary = new AbiV10Records.Summary();
        summary.callableId = function.semanticCallableId;
        summary.variableCount = ordinals.size();
        summary.result = AbiV10Records.fromTypeRef(function.returnTypeRef, ordinals);
        String bodyBytes = CheckedBodyCodec.encodeCheckedBodyV1(List.of(
                CheckedStmt.returnStmt(expression)));
        summary.bodySemanticHash = "body-semantic-v1:sha256:" + CheckedBodyCodec.sha256Hex(bodyBytes);
        AbiV10Records.SourceProvenance provenance = AbiV10Records.SourceProvenance.create(
                moduleId, summary.callableId, function.sourceFile, function.line);
        summary.provenanceId = provenance.provenanceId();
        provenanceRecords.add(provenance);
        return summary;
    }

    private static AbiV10Records.Summary v10ZeroArgLiteralSummary(
            Function function, CheckedExpr expression, String moduleId,
            List<AbiV10Records.SourceProvenance> provenanceRecords) {
        TypeRef resultType = function.returnTypeRef;
        if (!isV10SummaryTypeRepresentable(resultType)) return null;
        AbiV10Records.Summary summary = new AbiV10Records.Summary();
        summary.callableId = function.semanticCallableId;
        summary.result = AbiV10Records.fromTypeRef(resultType, Map.of());
        String bodyBytes = CheckedBodyCodec.encodeCheckedBodyV1(List.of(
                CheckedStmt.returnStmt(expression)));
        summary.bodySemanticHash = "body-semantic-v1:sha256:" + CheckedBodyCodec.sha256Hex(bodyBytes);
        AbiV10Records.SourceProvenance provenance = AbiV10Records.SourceProvenance.create(
                moduleId, summary.callableId, function.sourceFile, function.line);
        summary.provenanceId = provenance.provenanceId();
        provenanceRecords.add(provenance);
        return summary;
    }

    private static CheckedExpr v10ClosedIntExpressionReturn(Function function) {
        if (!isV10IntType(function.returnTypeRef)
                || function.body.size() != 1 || function.body.get(0).kind != Stmt.Kind.Return) return null;
        for (Param parameter : function.params) {
            if (!isV10IntType(parameter.checkedTypeRef)) return null;
        }
        Stmt statement = function.body.get(0);
        String expression = statement.a != null ? statement.a.trim() : "";
        if (expression.isEmpty() && statement.text != null
                && statement.text.trim().startsWith("return ")) {
            expression = statement.text.trim().substring(7).trim();
        }
        return v10LowerClosedIntExpression(expression, function, 0);
    }

    private static boolean isV10IntType(TypeRef type) {
        return type != null && type.kind() == org.mosslang.compiler.types.TypeKind.Primitive
                && "int".equalsIgnoreCase(type.name());
    }

    private static boolean isV10FloatType(TypeRef type) {
        return type != null && type.kind() == org.mosslang.compiler.types.TypeKind.Primitive
                && "float".equalsIgnoreCase(type.name());
    }

    private static boolean isV10BoolType(TypeRef type) {
        return type != null && type.kind() == org.mosslang.compiler.types.TypeKind.Primitive
                && "bool".equalsIgnoreCase(type.name());
    }

    private static CheckedExpr v10ClosedFloatExpressionReturn(Function function) {
        if (!isV10FloatType(function.returnTypeRef)
                || function.body.size() != 1 || function.body.get(0).kind != Stmt.Kind.Return) return null;
        for (Param parameter : function.params) {
            if (!isV10FloatType(parameter.checkedTypeRef)) return null;
        }
        Stmt statement = function.body.get(0);
        String expression = statement.a != null ? statement.a.trim() : "";
        if (expression.isEmpty() && statement.text != null
                && statement.text.trim().startsWith("return ")) {
            expression = statement.text.trim().substring(7).trim();
        }
        return v10LowerClosedFloatExpression(expression, function, 0);
    }

    private static CheckedExpr v10LowerClosedFloatExpression(
            String source, Function function, int depth) {
        return v10LowerClosedFloatExpression(source, function, Map.of(), depth);
    }

    private static CheckedExpr v10LowerClosedFloatExpression(
            String source, Function function, Map<String, Integer> localOrdinals, int depth) {
        if (source == null || depth > 128) return null;
        String expression = source.trim();
        while (v10ExpressionHasWrappingParens(expression)) {
            expression = expression.substring(1, expression.length() - 1).trim();
        }
        TypeRef floatType = TypeRef.primitive("float");
        if (!expression.isEmpty()
                && org.mosslang.compiler.parser.NumericLiteral.numericLiteralStartsAt(expression, 0)) {
            org.mosslang.compiler.parser.NumericLiteral literal =
                    org.mosslang.compiler.parser.NumericLiteral.scanNumericLiteral(expression, 0);
            if (literal.ok && literal.end == expression.length()
                    && literal.kind == org.mosslang.compiler.parser.NumericLiteral.Kind.Float) {
                return CheckedExpr.floatLiteral(literal.canonical, floatType);
            }
        }
        for (int index = 0; index < function.params.size(); index++) {
            if (function.params.get(index).name.equals(expression)) {
                return CheckedExpr.parameter(index, floatType);
            }
        }
        Integer localOrdinal = localOrdinals.get(expression);
        if (localOrdinal != null) return CheckedExpr.localRead(localOrdinal, floatType);
        for (List<String> operators : List.of(List.of("+", "-"), List.of("*", "/"))) {
            org.mosslang.compiler.semantics.ExpressionTyper.BinarySplit split =
                    org.mosslang.compiler.semantics.ExpressionTyper.splitBinary(expression, operators);
            if (split == null) continue;
            CheckedExpr left = v10LowerClosedFloatExpression(split.left(), function, localOrdinals, depth + 1);
            CheckedExpr right = v10LowerClosedFloatExpression(split.right(), function, localOrdinals, depth + 1);
            if (left == null || right == null) return null;
            org.mosslang.compiler.abi.CheckedBinaryOp operation = switch (split.op()) {
                case "+" -> org.mosslang.compiler.abi.CheckedBinaryOp.Add;
                case "-" -> org.mosslang.compiler.abi.CheckedBinaryOp.Sub;
                case "*" -> org.mosslang.compiler.abi.CheckedBinaryOp.Mul;
                case "/" -> org.mosslang.compiler.abi.CheckedBinaryOp.Div;
                default -> null;
            };
            return operation == null ? null : CheckedExpr.binary(operation, left, right, floatType);
        }
        if (expression.startsWith("-") && !expression.equals("-")) {
            CheckedExpr operand = v10LowerClosedFloatExpression(
                    expression.substring(1).trim(), function, localOrdinals, depth + 1);
            return operand == null ? null : CheckedExpr.unary(
                    org.mosslang.compiler.abi.CheckedUnaryOp.Neg, operand, floatType);
        }
        return null;
    }

    private static AbiV10Records.Summary v10ClosedFloatExpressionSummary(
            Function function, CheckedExpr expression, String moduleId,
            List<AbiV10Records.SourceProvenance> provenanceRecords) {
        TypeRef floatType = TypeRef.primitive("float");
        AbiV10Records.Summary summary = new AbiV10Records.Summary();
        summary.callableId = function.semanticCallableId;
        for (int index = 0; index < function.params.size(); index++) {
            summary.parameters.add(AbiV10Records.fromTypeRef(floatType, Map.of()));
            org.mosslang.compiler.types.AccessMode access = function.parameterAccessModes.size() > index
                    ? function.parameterAccessModes.get(index)
                    : org.mosslang.compiler.types.AccessMode.Read;
            summary.parameterAccess.add(switch (access) {
                case Read -> AbiV10Records.Access.Read;
                case Write -> AbiV10Records.Access.Write;
                case Consume -> AbiV10Records.Access.Consume;
            });
        }
        summary.result = AbiV10Records.fromTypeRef(floatType, Map.of());
        String bodyBytes = CheckedBodyCodec.encodeCheckedBodyV1(List.of(CheckedStmt.returnStmt(expression)));
        summary.bodySemanticHash = "body-semantic-v1:sha256:" + CheckedBodyCodec.sha256Hex(bodyBytes);
        AbiV10Records.SourceProvenance provenance = AbiV10Records.SourceProvenance.create(
                moduleId, summary.callableId, function.sourceFile, function.line);
        summary.provenanceId = provenance.provenanceId();
        provenanceRecords.add(provenance);
        return summary;
    }

    private static CheckedExpr v10NumericPredicateReturn(Function function) {
        if (function.returnTypeRef == null
                || function.returnTypeRef.kind() != org.mosslang.compiler.types.TypeKind.Primitive
                || !"bool".equalsIgnoreCase(function.returnTypeRef.name())
                || function.body.size() != 1 || function.body.get(0).kind != Stmt.Kind.Return
                || function.params.isEmpty()) return null;
        boolean intOperands = function.params.stream()
                .allMatch(parameter -> isV10IntType(parameter.checkedTypeRef));
        boolean floatOperands = function.params.stream()
                .allMatch(parameter -> isV10FloatType(parameter.checkedTypeRef));
        if (!intOperands && !floatOperands) return null;
        Stmt statement = function.body.get(0);
        String expression = statement.a != null ? statement.a.trim() : "";
        if (expression.isEmpty() && statement.text != null
                && statement.text.trim().startsWith("return ")) {
            expression = statement.text.trim().substring(7).trim();
        }
        return v10LowerNumericPredicateExpression(expression, function, floatOperands, 0);
    }

    private static CheckedExpr v10LowerNumericPredicateExpression(
            String source, Function function, boolean floatOperands, int depth) {
        if (source == null || depth > 128) return null;
        String expression = source.trim();
        while (v10ExpressionHasWrappingParens(expression)) {
            expression = expression.substring(1, expression.length() - 1).trim();
        }
        TypeRef boolType = TypeRef.primitive("bool");
        if ("true".equals(expression) || "false".equals(expression)) {
            return CheckedExpr.boolLiteral("true".equals(expression), boolType);
        }
        if (expression.startsWith("not ")) {
            CheckedExpr operand = v10LowerNumericPredicateExpression(
                    expression.substring(4), function, floatOperands, depth + 1);
            return operand == null ? null : CheckedExpr.unary(
                    org.mosslang.compiler.abi.CheckedUnaryOp.Not, operand, boolType);
        }
        org.mosslang.compiler.semantics.ExpressionTyper.BinarySplit logical =
                org.mosslang.compiler.semantics.ExpressionTyper.splitBinary(expression,
                        List.of(" or ", " and ", " xor "));
        if (logical != null) {
            CheckedExpr left = v10LowerNumericPredicateExpression(
                    logical.left(), function, floatOperands, depth + 1);
            CheckedExpr right = v10LowerNumericPredicateExpression(
                    logical.right(), function, floatOperands, depth + 1);
            if (left == null || right == null) return null;
            org.mosslang.compiler.abi.CheckedBinaryOp operation = switch (logical.op().trim()) {
                case "and" -> org.mosslang.compiler.abi.CheckedBinaryOp.And;
                case "or" -> org.mosslang.compiler.abi.CheckedBinaryOp.Or;
                case "xor" -> org.mosslang.compiler.abi.CheckedBinaryOp.Xor;
                default -> null;
            };
            return operation == null ? null : CheckedExpr.binary(operation, left, right, boolType);
        }
        org.mosslang.compiler.semantics.ExpressionTyper.BinarySplit comparison =
                org.mosslang.compiler.semantics.ExpressionTyper.splitBinary(expression,
                        List.of("==", "!=", "<=", ">=", "<", ">"));
        if (comparison == null) return null;
        CheckedExpr left = floatOperands
                ? v10LowerClosedFloatExpression(comparison.left(), function, depth + 1)
                : v10LowerClosedIntExpression(comparison.left(), function, depth + 1);
        CheckedExpr right = floatOperands
                ? v10LowerClosedFloatExpression(comparison.right(), function, depth + 1)
                : v10LowerClosedIntExpression(comparison.right(), function, depth + 1);
        if (left == null || right == null) return null;
        org.mosslang.compiler.abi.CheckedBinaryOp operation = switch (comparison.op()) {
            case "==" -> org.mosslang.compiler.abi.CheckedBinaryOp.Equal;
            case "!=" -> org.mosslang.compiler.abi.CheckedBinaryOp.NotEqual;
            case "<" -> org.mosslang.compiler.abi.CheckedBinaryOp.Less;
            case "<=" -> org.mosslang.compiler.abi.CheckedBinaryOp.LessEqual;
            case ">" -> org.mosslang.compiler.abi.CheckedBinaryOp.Greater;
            case ">=" -> org.mosslang.compiler.abi.CheckedBinaryOp.GreaterEqual;
            default -> null;
        };
        return operation == null ? null : CheckedExpr.binary(operation, left, right, boolType);
    }

    private static AbiV10Records.Summary v10NumericPredicateSummary(
            Function function, CheckedExpr expression, String moduleId,
            List<AbiV10Records.SourceProvenance> provenanceRecords) {
        AbiV10Records.Summary summary = new AbiV10Records.Summary();
        summary.callableId = function.semanticCallableId;
        TypeRef operandType = function.params.stream()
                .allMatch(parameter -> isV10FloatType(parameter.checkedTypeRef))
                        ? TypeRef.primitive("float") : TypeRef.primitive("int");
        for (int index = 0; index < function.params.size(); index++) {
            summary.parameters.add(AbiV10Records.fromTypeRef(operandType, Map.of()));
            org.mosslang.compiler.types.AccessMode access = function.parameterAccessModes.size() > index
                    ? function.parameterAccessModes.get(index)
                    : org.mosslang.compiler.types.AccessMode.Read;
            summary.parameterAccess.add(switch (access) {
                case Read -> AbiV10Records.Access.Read;
                case Write -> AbiV10Records.Access.Write;
                case Consume -> AbiV10Records.Access.Consume;
            });
        }
        summary.result = AbiV10Records.fromTypeRef(TypeRef.primitive("bool"), Map.of());
        String bodyBytes = CheckedBodyCodec.encodeCheckedBodyV1(List.of(CheckedStmt.returnStmt(expression)));
        summary.bodySemanticHash = "body-semantic-v1:sha256:" + CheckedBodyCodec.sha256Hex(bodyBytes);
        AbiV10Records.SourceProvenance provenance = AbiV10Records.SourceProvenance.create(
                moduleId, summary.callableId, function.sourceFile, function.line);
        summary.provenanceId = provenance.provenanceId();
        provenanceRecords.add(provenance);
        return summary;
    }

    private static CheckedExpr v10LowerClosedIntExpression(
            String source, Function function, int depth) {
        return v10LowerClosedIntExpression(source, function, Map.of(), depth);
    }

    private static CheckedExpr v10LowerClosedIntExpression(
            String source, Function function, Map<String, Integer> localOrdinals, int depth) {
        if (source == null || depth > 128) return null;
        String expression = source.trim();
        while (v10ExpressionHasWrappingParens(expression)) {
            expression = expression.substring(1, expression.length() - 1).trim();
        }
        TypeRef intType = TypeRef.primitive("int");
        if (expression.matches("[+-]?[0-9](?:_?[0-9])*") ) {
            try {
                return CheckedExpr.integerLiteral(Long.parseLong(expression.replace("_", "")), intType);
            } catch (NumberFormatException ignored) {
                return null;
            }
        }
        for (int index = 0; index < function.params.size(); index++) {
            if (function.params.get(index).name.equals(expression)) {
                return CheckedExpr.parameter(index, intType);
            }
        }
        Integer localOrdinal = localOrdinals.get(expression);
        if (localOrdinal != null) return CheckedExpr.localRead(localOrdinal, intType);
        for (List<String> operators : List.of(List.of("+", "-"), List.of("*", "/", "%"))) {
            org.mosslang.compiler.semantics.ExpressionTyper.BinarySplit split =
                    org.mosslang.compiler.semantics.ExpressionTyper.splitBinary(expression, operators);
            if (split == null) continue;
            CheckedExpr left = v10LowerClosedIntExpression(split.left(), function, localOrdinals, depth + 1);
            CheckedExpr right = v10LowerClosedIntExpression(split.right(), function, localOrdinals, depth + 1);
            if (left == null || right == null) return null;
            org.mosslang.compiler.abi.CheckedBinaryOp operation = switch (split.op()) {
                case "+" -> org.mosslang.compiler.abi.CheckedBinaryOp.Add;
                case "-" -> org.mosslang.compiler.abi.CheckedBinaryOp.Sub;
                case "*" -> org.mosslang.compiler.abi.CheckedBinaryOp.Mul;
                case "/" -> org.mosslang.compiler.abi.CheckedBinaryOp.Div;
                case "%" -> org.mosslang.compiler.abi.CheckedBinaryOp.Mod;
                default -> null;
            };
            return operation == null ? null : CheckedExpr.binary(operation, left, right, intType);
        }
        if (expression.startsWith("-") && !expression.equals("-")) {
            CheckedExpr operand = v10LowerClosedIntExpression(
                    expression.substring(1).trim(), function, localOrdinals, depth + 1);
            return operand == null ? null : CheckedExpr.unary(
                    org.mosslang.compiler.abi.CheckedUnaryOp.Neg, operand, intType);
        }
        return null;
    }

    private static boolean v10ExpressionHasWrappingParens(String expression) {
        if (expression.length() < 2 || expression.charAt(0) != '(' || expression.charAt(expression.length() - 1) != ')') {
            return false;
        }
        int depth = 0;
        for (int index = 0; index < expression.length(); index++) {
            char current = expression.charAt(index);
            if (current == '(') depth++;
            else if (current == ')') {
                depth--;
                if (depth == 0 && index != expression.length() - 1) return false;
                if (depth < 0) return false;
            }
        }
        return depth == 0;
    }

    private static AbiV10Records.Summary v10ClosedIntExpressionSummary(
            Function function, CheckedExpr expression, String moduleId,
            List<AbiV10Records.SourceProvenance> provenanceRecords) {
        TypeRef resultType = TypeRef.primitive("int");
        if (!isV10SummaryTypeRepresentable(resultType)
                || function.params.stream().anyMatch(parameter -> !isV10IntType(parameter.checkedTypeRef))) return null;
        AbiV10Records.Summary summary = new AbiV10Records.Summary();
        summary.callableId = function.semanticCallableId;
        for (int index = 0; index < function.params.size(); index++) {
            summary.parameters.add(AbiV10Records.fromTypeRef(resultType, Map.of()));
            org.mosslang.compiler.types.AccessMode access = function.parameterAccessModes.size() > index
                    ? function.parameterAccessModes.get(index)
                    : org.mosslang.compiler.types.AccessMode.Read;
            summary.parameterAccess.add(switch (access) {
                case Read -> AbiV10Records.Access.Read;
                case Write -> AbiV10Records.Access.Write;
                case Consume -> AbiV10Records.Access.Consume;
            });
        }
        summary.result = AbiV10Records.fromTypeRef(resultType, Map.of());
        String bodyBytes = CheckedBodyCodec.encodeCheckedBodyV1(List.of(CheckedStmt.returnStmt(expression)));
        summary.bodySemanticHash = "body-semantic-v1:sha256:" + CheckedBodyCodec.sha256Hex(bodyBytes);
        AbiV10Records.SourceProvenance provenance = AbiV10Records.SourceProvenance.create(
                moduleId, summary.callableId, function.sourceFile, function.line);
        summary.provenanceId = provenance.provenanceId();
        provenanceRecords.add(provenance);
        return summary;
    }

    private static List<CheckedStmt> v10ClosedNumericLocalBody(Function function) {
        TypeRef valueType = function.returnTypeRef;
        if ((!isV10IntType(valueType) && !isV10FloatType(valueType)) || function.body.size() < 2) return null;
        if (function.params.stream().anyMatch(parameter -> !valueType.equals(parameter.checkedTypeRef))) return null;
        Map<String, Integer> localOrdinals = new LinkedHashMap<>();
        Map<String, Boolean> localMutability = new HashMap<>();
        List<CheckedStmt> checked = new ArrayList<>();
        boolean hasLocal = false;
        for (int index = 0; index < function.body.size(); index++) {
            Stmt statement = function.body.get(index);
            if (statement.kind == Stmt.Kind.Return) {
                if (index != function.body.size() - 1) return null;
                String expression = statement.a != null ? statement.a.trim() : "";
                if (expression.isEmpty() && statement.text != null
                        && statement.text.trim().startsWith("return ")) {
                    expression = statement.text.trim().substring(7).trim();
                }
                CheckedExpr value = v10LowerClosedNumericExpression(
                        expression, function, localOrdinals, valueType, 0);
                if (value == null) return null;
                checked.add(CheckedStmt.returnStmt(value));
                continue;
            }
            if (statement.kind == Stmt.Kind.Pass) {
                CheckedStmt pass = new CheckedStmt();
                pass.kind = org.mosslang.compiler.abi.CheckedStmtKind.Pass;
                checked.add(pass);
                continue;
            }
            if (statement.kind != Stmt.Kind.Let && statement.kind != Stmt.Kind.Var
                    && statement.kind != Stmt.Kind.Assign) return null;
            String name = statement.a != null ? statement.a.trim() : "";
            if (!name.matches("[A-Za-z_][A-Za-z0-9_]*")) return null;
            String sourceExpression = statement.b != null ? statement.b.trim() : "";
            CheckedExpr value = v10LowerClosedNumericExpression(
                    sourceExpression, function, localOrdinals, valueType, 0);
            if (value == null) return null;
            if (statement.kind == Stmt.Kind.Assign && localOrdinals.containsKey(name)) {
                if (!Boolean.TRUE.equals(localMutability.get(name))) return null;
                checked.add(CheckedStmt.assignStmt(localOrdinals.get(name), value));
                hasLocal = true;
                continue;
            }
            if (statement.kind != Stmt.Kind.Assign && localOrdinals.containsKey(name)) return null;
            int ordinal = localOrdinals.size();
            CheckedStmt declaration = statement.kind == Stmt.Kind.Let
                    ? CheckedStmt.letStmt(ordinal, value) : CheckedStmt.varStmt(ordinal, value);
            declaration.type = valueType;
            checked.add(declaration);
            localOrdinals.put(name, ordinal);
            localMutability.put(name, statement.kind != Stmt.Kind.Let);
            hasLocal = true;
        }
        if (!hasLocal || checked.isEmpty()
                || checked.get(checked.size() - 1).kind != org.mosslang.compiler.abi.CheckedStmtKind.Return) return null;
        return checked;
    }

    private static List<CheckedStmt> v10ClosedBranchReturnBody(Function function) {
        TypeRef resultType = function.returnTypeRef;
        boolean intResult = isV10IntType(resultType);
        boolean floatResult = isV10FloatType(resultType);
        boolean boolResult = isV10BoolType(resultType);
        if ((!intResult && !floatResult && !boolResult) || function.body.size() != 4) return null;
        boolean intOperands = function.params.stream()
                .allMatch(parameter -> isV10IntType(parameter.checkedTypeRef));
        boolean floatOperands = function.params.stream()
                .allMatch(parameter -> isV10FloatType(parameter.checkedTypeRef));
        if (!function.params.isEmpty() && !intOperands && !floatOperands) return null;
        if ((intResult && !function.params.isEmpty() && !intOperands)
                || (floatResult && !function.params.isEmpty() && !floatOperands)) return null;
        Stmt ifSource = function.body.get(0);
        Stmt thenSource = function.body.get(1);
        Stmt elseSource = function.body.get(2);
        Stmt elseReturnSource = function.body.get(3);
        if (ifSource.kind != Stmt.Kind.If || thenSource.kind != Stmt.Kind.Return
                || elseSource.kind != Stmt.Kind.Else || elseReturnSource.kind != Stmt.Kind.Return
                || thenSource.indent <= ifSource.indent || elseSource.indent != ifSource.indent
                || elseReturnSource.indent <= elseSource.indent) return null;
        String condition = ifSource.a != null ? ifSource.a.trim() : "";
        CheckedExpr predicate = v10LowerNumericPredicateExpression(condition, function, floatOperands, 0);
        String thenExpression = thenSource.a != null ? thenSource.a.trim() : "";
        String elseExpression = elseReturnSource.a != null ? elseReturnSource.a.trim() : "";
        CheckedExpr thenValue = boolResult
                ? v10LowerNumericPredicateExpression(thenExpression, function, floatOperands, 0)
                : v10LowerClosedNumericExpression(thenExpression, function, Map.of(), resultType, 0);
        CheckedExpr elseValue = boolResult
                ? v10LowerNumericPredicateExpression(elseExpression, function, floatOperands, 0)
                : v10LowerClosedNumericExpression(elseExpression, function, Map.of(), resultType, 0);
        if (predicate == null || thenValue == null || elseValue == null) return null;
        CheckedStmt branch = new CheckedStmt();
        branch.kind = org.mosslang.compiler.abi.CheckedStmtKind.If;
        branch.expr = predicate;
        branch.thenBranch.add(CheckedStmt.returnStmt(thenValue));
        branch.elseBranch.add(CheckedStmt.returnStmt(elseValue));
        return List.of(branch);
    }

    private static CheckedExpr v10LowerClosedNumericExpression(
            String source, Function function, Map<String, Integer> localOrdinals,
            TypeRef valueType, int depth) {
        if (isV10IntType(valueType))
            return v10LowerClosedIntExpression(source, function, localOrdinals, depth);
        if (isV10FloatType(valueType))
            return v10LowerClosedFloatExpression(source, function, localOrdinals, depth);
        return null;
    }

    private static AbiV10Records.Summary v10ClosedBodySummary(
            Function function, List<CheckedStmt> statements, String moduleId,
            List<AbiV10Records.SourceProvenance> provenanceRecords) {
        AbiV10Records.Summary summary = new AbiV10Records.Summary();
        summary.callableId = function.semanticCallableId;
        for (int index = 0; index < function.params.size(); index++) {
            summary.parameters.add(AbiV10Records.fromTypeRef(
                    function.params.get(index).checkedTypeRef, Map.of()));
            org.mosslang.compiler.types.AccessMode access = function.parameterAccessModes.size() > index
                    ? function.parameterAccessModes.get(index)
                    : org.mosslang.compiler.types.AccessMode.Read;
            summary.parameterAccess.add(switch (access) {
                case Read -> AbiV10Records.Access.Read;
                case Write -> AbiV10Records.Access.Write;
                case Consume -> AbiV10Records.Access.Consume;
            });
        }
        summary.result = AbiV10Records.fromTypeRef(function.returnTypeRef, Map.of());
        String bodyBytes = CheckedBodyCodec.encodeCheckedBodyV1(statements);
        summary.bodySemanticHash = "body-semantic-v1:sha256:" + CheckedBodyCodec.sha256Hex(bodyBytes);
        AbiV10Records.SourceProvenance provenance = AbiV10Records.SourceProvenance.create(
                moduleId, summary.callableId, function.sourceFile, function.line);
        summary.provenanceId = provenance.provenanceId();
        provenanceRecords.add(provenance);
        return summary;
    }

    private static AbiV10Records.CheckedBodyRecord v10CheckedBodyRecord(
            AbiV10Records.Summary summary, List<CheckedStmt> statements) {
        AbiV10Records.CheckedBodyRecord body = new AbiV10Records.CheckedBodyRecord();
        body.callableId = summary.callableId;
        body.checkedIrBytes = CheckedBodyCodec.encodeCheckedBodyV1(statements);
        body.semanticHash = summary.bodySemanticHash;
        return body;
    }

    private static AbiV10Records.CheckedBodyRecord v10SimpleExpressionBody(
            AbiV10Records.Summary summary, CheckedExpr expression) {
        List<CheckedStmt> checkedStatements = List.of(CheckedStmt.returnStmt(expression));
        AbiV10Records.CheckedBodyRecord body = new AbiV10Records.CheckedBodyRecord();
        body.callableId = summary.callableId;
        body.checkedIrBytes = CheckedBodyCodec.encodeCheckedBodyV1(checkedStatements);
        body.semanticHash = summary.bodySemanticHash;
        return body;
    }

    private static AbiV10Records.CheckedBodyRecord v10DirectReturnBody(
            Function function, AbiV10Records.Summary summary) {
        Map<org.mosslang.compiler.types.TypeVarId, Integer> ordinals = new LinkedHashMap<>();
        for (Param parameter : function.params) collectV10SummaryVariables(parameter.checkedTypeRef, ordinals);
        collectV10SummaryVariables(function.returnTypeRef, ordinals);
        int returnedParameter = v10DirectReturnedParameter(function);
        if (returnedParameter < 0) throw new CompileError("SOURCE_FREE_SUMMARY_INVALID",
                "direct-return checked body no longer matches its callable summary", function.sourceFile, function.line);
        List<CheckedStmt> checkedStatements = List.of(CheckedStmt.returnStmt(
                CheckedExpr.parameter(returnedParameter, canonicalV10SummaryType(
                        function.params.get(returnedParameter).checkedTypeRef, ordinals))));
        AbiV10Records.CheckedBodyRecord body = new AbiV10Records.CheckedBodyRecord();
        body.callableId = summary.callableId;
        body.checkedIrBytes = CheckedBodyCodec.encodeCheckedBodyV1(checkedStatements);
        body.semanticHash = summary.bodySemanticHash;
        return body;
    }

    private static void collectV10SummaryVariables(TypeRef type,
            Map<org.mosslang.compiler.types.TypeVarId, Integer> ordinals) {
        if (type == null) return;
        if (type.kind() == org.mosslang.compiler.types.TypeKind.InferenceVar) {
            ordinals.computeIfAbsent(type.varId(), ignored -> ordinals.size());
        }
        for (TypeRef argument : type.typeArgs()) collectV10SummaryVariables(argument, ordinals);
        for (TypeRef.FieldSub field : type.fieldSubstitutions())
            collectV10SummaryVariables(field.type(), ordinals);
    }

    private static TypeRef canonicalV10SummaryType(TypeRef type,
            Map<org.mosslang.compiler.types.TypeVarId, Integer> ordinals) {
        if (type.kind() == org.mosslang.compiler.types.TypeKind.InferenceVar) {
            Integer ordinal = ordinals.get(type.varId());
            if (ordinal == null) throw new CompileError("SOURCE_FREE_SUMMARY_INVALID",
                    "checked callable type variable has no summary ordinal", "", 1);
            return TypeRef.var(new org.mosslang.compiler.types.TypeVarId(0, 0, ordinal));
        }
        return switch (type.kind()) {
            case Primitive, Unit, Enum, Domain -> type;
            case Builtin -> TypeRef.builtin(type.name(), type.typeArgs().stream()
                    .map(argument -> canonicalV10SummaryType(argument, ordinals)).toList());
            case NominalInstance -> {
                List<TypeRef.FieldSub> substitutions = type.fieldSubstitutions().stream()
                        .map(field -> new TypeRef.FieldSub(field.name(),
                                canonicalV10SummaryType(field.type(), ordinals))).toList();
                yield TypeRef.nominalBound(type.declaration(), type.name(), substitutions);
            }
            default -> throw new CompileError("SOURCE_FREE_SUMMARY_INVALID",
                    "checked callable type is not representable in ABI-v10", "", 1);
        };
    }

    private static String raisedErrorIdentity(String module, List<Stmt> body) {
        for (Stmt statement : body) {
            if (statement.kind != Stmt.Kind.Raise
                    && (statement.text == null || !statement.text.trim().startsWith("raise "))) continue;
            String value = statement.a != null ? statement.a.trim() : "";
            if (value.isEmpty() && statement.text != null && statement.text.trim().startsWith("raise ")) {
                value = statement.text.trim().substring(6).trim();
            }
            int paren = value.indexOf('(');
            if (paren >= 0) value = value.substring(0, paren).trim();
            int dot = value.lastIndexOf('.');
            if (dot > 0 && dot + 1 < value.length()) {
                String type = value.substring(0, dot).replace('.', '_');
                String variant = value.substring(dot + 1).trim();
                if (!type.contains("__")) type = module + "__" + type;
                return "enum:" + type + ":" + variant;
            }
            return "enum:" + module + "__Fault:" + value;
        }
        return "-";
    }

    private static boolean mayPanic(List<Stmt> body) {
        for (Stmt statement : body) {
            String text = (statement.text == null ? "" : statement.text) + " "
                    + (statement.a == null ? "" : statement.a) + " "
                    + (statement.b == null ? "" : statement.b);
            if (text.matches("(?s).*\\b(assert|panic|unwrap)\\s*\\(.*")
                    || text.matches("(?s).*\\[[^]]+\\].*")) return true;
            if (statement.kind == Stmt.Kind.Assign && statement.b != null
                    && (statement.b.contains("/") || statement.b.contains("%"))) return true;
        }
        return false;
    }

    private static void appendStateEffects(StringBuilder out, String handler,
                                           Set<String> reads, Set<String> writes) {
        out.append("  handler_state_effects \"").append(handler).append("\" ").append(reads.size());
        for (String field : reads) out.append(" \"").append(field).append("\"");
        out.append(" ").append(writes.size());
        for (String field : writes) out.append(" \"").append(field).append("\"");
        out.append(" 0\n");
    }

    private record FailureArmStateEffects(List<String> reads, List<String> writes) {}

    private static FailureArmStateEffects failureArmStateEffects(
            FailureArm arm, Domain domain) {
        Set<String> reads = new java.util.TreeSet<>();
        Set<String> writes = new java.util.TreeSet<>();
        for (Field field : domain.state) {
            for (Stmt statement : arm.body) {
                boolean assignment = statement.kind == Stmt.Kind.Assign;
                String lhs = statement.a != null ? statement.a.trim() : "";
                if (assignment && lhs.equals(field.name)) writes.add(field.name);
                boolean read = false;
                if (assignment || statement.kind == Stmt.Kind.Let
                        || statement.kind == Stmt.Kind.Var) {
                    read = SemanticChecker.expressionUses(statement.b, field.name);
                } else {
                    read = SemanticChecker.expressionUses(statement.a, field.name)
                            || SemanticChecker.expressionUses(statement.b, field.name)
                            || SemanticChecker.expressionUses(statement.text, field.name);
                    if (statement.args != null) {
                        for (String argument : statement.args) {
                            read |= SemanticChecker.expressionUses(argument, field.name);
                        }
                    }
                }
                if (read) reads.add(field.name);
            }
        }
        return new FailureArmStateEffects(List.copyOf(reads), List.copyOf(writes));
    }

    private static void appendFailureArmStateEffects(StringBuilder out,
                                                       FailureArmStateEffects effects) {
        out.append(' ').append(effects.reads().size());
        for (String field : effects.reads()) out.append(" \"").append(field).append("\"");
        out.append(' ').append(effects.writes().size());
        for (String field : effects.writes()) out.append(" \"").append(field).append("\"");
        out.append(" 0"); // no state consumes are inferred by this writer yet
    }

    private static final class AbiOwnedTypeShape {
        final String canonicalType;
        final int kind;
        final List<AbiOwnedTypeShape> children;

        AbiOwnedTypeShape(String canonicalType, int kind, List<AbiOwnedTypeShape> children) {
            this.canonicalType = canonicalType;
            this.kind = kind;
            this.children = children;
        }

        String fingerprint() {
            StringBuilder out = new StringBuilder().append(kind).append(':')
                    .append(canonicalType.length()).append(':').append(canonicalType)
                    .append(':').append(children.size()).append(':');
            for (AbiOwnedTypeShape child : children) out.append(child.fingerprint());
            return out.toString();
        }
    }

    private static AbiOwnedTypeShape abiOwnedTypeShape(String spelling, Program program,
                                                       Set<String> resolving) {
        if (spelling == null || spelling.isBlank()) return null;
        String canonical = spelling.trim();
        String lower = canonical.toLowerCase(Locale.ROOT);
        if (canonical.startsWith("&")) return new AbiOwnedTypeShape(canonical, 5, List.of());
        if (Set.of("fileio", "range", "rangebatch", "mossguard", "protectedread", "protectedwrite", "executor")
                .contains(lower) || lower.startsWith("domainhandle[")) {
            return new AbiOwnedTypeShape(canonical, 4, List.of());
        }
        if (Set.of("int", "float", "bool", "unit").contains(lower)) {
            return new AbiOwnedTypeShape(canonical, 0, List.of());
        }
        if (lower.equals("string") || lower.startsWith("vector[")
                || lower.startsWith("map[") || lower.startsWith("queue[")) {
            List<AbiOwnedTypeShape> children = new ArrayList<>();
            int open = canonical.indexOf('[');
            if (open >= 0 && canonical.endsWith("]")) {
                for (String argument : splitCanonicalTypeArguments(
                        canonical.substring(open + 1, canonical.length() - 1))) {
                    children.add(abiOwnedTypeShape(normalizeOwnedChild(argument), program, resolving));
                }
            }
            if (children.stream().anyMatch(java.util.Objects::isNull)) return null;
            return new AbiOwnedTypeShape(canonical, 3, children);
        }
        if ((lower.startsWith("option[") || lower.startsWith("result[")) && canonical.endsWith("]")) {
            List<AbiOwnedTypeShape> children = new ArrayList<>();
            int open = canonical.indexOf('[');
            for (String argument : splitCanonicalTypeArguments(
                    canonical.substring(open + 1, canonical.length() - 1))) {
                children.add(abiOwnedTypeShape(normalizeOwnedChild(argument), program, resolving));
            }
            if (children.stream().anyMatch(java.util.Objects::isNull)) return null;
            return new AbiOwnedTypeShape(canonical, 1, children);
        }
        String key = canonical.replace('.', '_');
        if (!resolving.add(key)) return new AbiOwnedTypeShape(canonical, 6, List.of());
        try {
            for (EnumType enumeration : program.enums) {
                if (!sameNominalName(canonical, enumeration.name)) continue;
                List<AbiOwnedTypeShape> children = new ArrayList<>();
                for (EnumCase enumCase : enumeration.cases) {
                    for (Field field : enumCase.fields) {
                        children.add(abiOwnedTypeShape(field.type, program, resolving));
                    }
                }
                if (children.stream().anyMatch(java.util.Objects::isNull)) return null;
                return new AbiOwnedTypeShape(canonical, 1, children);
            }
            for (ObjectType object : program.objects) {
                if (!sameNominalName(canonical, object.name)) continue;
                List<AbiOwnedTypeShape> children = new ArrayList<>();
                for (Field field : object.fields) children.add(abiOwnedTypeShape(field.type, program, resolving));
                if (children.stream().anyMatch(java.util.Objects::isNull)) return null;
                return new AbiOwnedTypeShape(canonical, 2, children);
            }
            for (Domain domain : program.domains) {
                if (sameNominalName(canonical, domain.name)) return new AbiOwnedTypeShape(canonical, 4, List.of());
            }
            return null;
        } finally {
            resolving.remove(key);
        }
    }

    private static String normalizeOwnedChild(String spelling) {
        String trimmed = spelling.trim();
        String lower = trimmed.toLowerCase(Locale.ROOT);
        if (Set.of("int", "float", "bool", "unit", "string", "fileio", "range", "rangebatch")
                .contains(lower)) return lower;
        return trimmed;
    }

    private static boolean sameNominalName(String sourceName, String declarationName) {
        if (sourceName.equals(declarationName) || sourceName.endsWith("__" + declarationName)) return true;
        int dot = sourceName.lastIndexOf('.');
        return dot >= 0 && (sourceName.substring(dot + 1).equals(declarationName)
                || sourceName.substring(dot + 1).endsWith("__" + declarationName));
    }

    private static void collectAbiOwnedShapes(AbiOwnedTypeShape shape,
                                              Map<String, AbiOwnedTypeShape> shapes) {
        AbiOwnedTypeShape previous = shapes.putIfAbsent(shape.canonicalType, shape);
        if (previous != null && !previous.fingerprint().equals(shape.fingerprint())) {
            throw new IllegalStateException("inconsistent ABI owned type shape for " + shape.canonicalType);
        }
        for (AbiOwnedTypeShape child : shape.children) collectAbiOwnedShapes(child, shapes);
    }

    private static boolean abiOwnedTypeShapeSafe(AbiOwnedTypeShape shape) {
        if (shape == null || shape.kind == 4 || shape.kind == 5 || shape.kind == 6) return false;
        for (AbiOwnedTypeShape child : shape.children) {
            if (!abiOwnedTypeShapeSafe(child)) return false;
        }
        return true;
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

        Set<String> stateReads = new LinkedHashSet<>();
        Set<String> stateWrites = new LinkedHashSet<>();
        for (Field field : d.state) {
            for (Stmt statement : h.body) {
                boolean assignment = statement.kind == Stmt.Kind.Assign;
                String lhs = statement.a != null ? statement.a.trim() : "";
                if (assignment && lhs.equals(field.name)) stateWrites.add(field.name);
                boolean read = SemanticChecker.expressionUses(statement.b, field.name)
                        || (!assignment && SemanticChecker.expressionUses(statement.a, field.name));
                if (statement.args != null) {
                    for (String arg : statement.args) read |= SemanticChecker.expressionUses(arg, field.name);
                }
                if (!read && statement.kind != Stmt.Kind.Assign && statement.kind != Stmt.Kind.Let
                        && statement.kind != Stmt.Kind.Var) {
                    read = SemanticChecker.expressionUses(statement.text, field.name);
                }
                if (read) stateReads.add(field.name);
            }
        }
        boolean readsState = !stateReads.isEmpty();
        boolean writesState = !stateWrites.isEmpty();

        if (isSemantic) {
            buf.append("  handler ").append(h.name).append(" reply=").append(replyType)
               .append(" effects=local_capture_read=0;local_mutation=0;domain_read=").append(readsState ? "1" : "0").append(";domain_write=")
               .append(writesState ? "1" : "0")
               .append(";message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=")
               .append(raiseIdent != null ? raiseIdent : "-")
               .append(";may_diverge=0;unresolved=0\n");
        }

        appendStateEffects(buf, h.name, stateReads, stateWrites);

        for (Param p : h.params) {
            buf.append("  handler_param \"").append(h.name).append("\" \"").append(p.name).append("\" \"").append(p.type != null ? p.type.toLowerCase() : "int").append("\"\n");
        }

        buf.append("  handler_abi \"").append(h.name).append("\" \"__moss_body_").append(module).append("__").append(domainName).append("_").append(h.name).append("\" \"__MossFailureFrame_").append(module).append("__").append(domainName).append("_").append(h.name).append("\" \"__MossRaisedCarrier_").append(module).append("__").append(domainName).append("_").append(h.name).append("\" 1 1\n");
        buf.append("  handler_outcome_contract \"").append(h.name).append("\" \"__MossBodyOutcome_").append(module).append("__").append(domainName).append("_").append(h.name).append("\" \"").append(replyType).append("\"\n");
        buf.append("  handler_wrapper_contract \"").append(h.name).append("\" nested=propagate root=join,close_fileio,release_guards,acquire_failure_locks,execute_arm,fulfill_reply\n");
        buf.append("  handler_reply_contract \"").append(h.name).append("\" \"").append(replyType).append("\" ").append(isOneWay ? "1" : "0").append(" 1\n");
        List<Map<String, AbiOwnedTypeShape>> captureShapesByArm = new ArrayList<>();
        Map<String, AbiOwnedTypeShape> ownedShapes = new java.util.TreeMap<>();
        if (h.failureArms != null) {
            for (FailureArm arm : h.failureArms) {
                Map<String, AbiOwnedTypeShape> armShapes = new LinkedHashMap<>();
                for (Map.Entry<String, String> capture : arm.checkedCaptures.entrySet()) {
                    AbiOwnedTypeShape shape = abiOwnedTypeShape(capture.getValue(), prog, new HashSet<>());
                    if (!abiOwnedTypeShapeSafe(shape)) {
                        throw new IllegalStateException("on_fail capture '" + capture.getKey()
                                + "' has no owned source-free ABI type shape");
                    }
                    armShapes.put(capture.getKey(), shape);
                    collectAbiOwnedShapes(shape, ownedShapes);
                }
                captureShapesByArm.add(armShapes);
            }
        }
        buf.append("  handler_owned_type_shapes \"").append(h.name).append("\" ")
                .append(ownedShapes.size()).append('\n');
        for (AbiOwnedTypeShape shape : ownedShapes.values()) {
            buf.append("  handler_owned_type_shape \"").append(h.name).append("\" \"")
                    .append(shape.canonicalType).append("\" \"").append(shape.fingerprint()).append("\"\n");
        }

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
                FailureArmStateEffects stateEffects = failureArmStateEffects(arm, d);
                Map<String, AbiOwnedTypeShape> armCaptureShapes = aIdx < captureShapesByArm.size()
                        ? captureShapesByArm.get(aIdx) : Collections.emptyMap();
                buf.append("  handler_failure_arm \"").append(h.name).append("\" ").append(aIdx).append(" \"").append(armPat).append("\" \"__moss_on_fail_").append(module).append("__").append(domainName).append("_").append(h.name).append("_").append(aIdx).append("\" ").append(armCaptureShapes.size()).append(" 1");
                appendFailureArmStateEffects(buf, stateEffects);
                buf.append('\n');
                for (Map.Entry<String, AbiOwnedTypeShape> capture : armCaptureShapes.entrySet()) {
                    String mossType = arm.checkedCaptures.get(capture.getKey());
                    buf.append("  handler_failure_capture \"").append(h.name).append("\" ")
                            .append(aIdx).append(" \"").append(capture.getKey()).append("\" \"")
                            .append(mossType).append("\" \"").append(capture.getKey())
                            .append("\" 1 1 \"").append(capture.getValue().fingerprint()).append("\"\n");
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

    private static String generateModuleRust(Path buildDir, Map<String, List<Path>> externalCandidateProviders, Program prog, String module, List<String> deps, String mossiDigest, boolean isRoot) {
        // Collect generic functions from all deps
        Pattern fnPat = Pattern.compile("\\bfn\\s+([A-Za-z0-9_]+)\\s+kind=generic");
        for (String dep : deps) {
            Path depMossi = buildDir.resolve(dep + ".mossi");
            if (!Files.exists(depMossi) && externalCandidateProviders != null) {
                List<Path> ext = externalCandidateProviders.get(dep);
                if (ext != null && !ext.isEmpty()) depMossi = ext.get(0);
            }
            if (Files.exists(depMossi)) {
                try {
                    String depText = Files.readString(depMossi, StandardCharsets.ISO_8859_1);
                    for (String line : depText.split("\\R")) {
                        Matcher m = fnPat.matcher(line);
                        if (m.find()) {
                            String fnName = m.group(1);
                            prog.importedGenericFunctions.add(dep + "__" + fnName);
                        }
                    }
                } catch (IOException ignored) {}
            }
        }
        String baseRust = NativeGenerator.generate(prog, false);
        StringBuilder extra = new StringBuilder();
        for (String dep : deps) {
            extra.append("#[macro_use]\n");
            extra.append("extern crate moss_").append(dep).append(";\n");
            extra.append("use moss_").append(dep).append("::*;\n");
            Path depMossi = buildDir.resolve(dep + ".mossi");
            if (Files.exists(depMossi)) {
                try {
                    String depText = Files.readString(depMossi, StandardCharsets.ISO_8859_1);
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
        boolean isV10 = "v10".equals(System.getenv("MOSS_STAGED_ABI"));
        String marker = (isV10 ? "MOSS_ABI_V10_CONTRACT::" : "MOSS_ABI_V9_CONTRACT::") + module + "::" + mossiDigest + "::END";
        extra.append("#[used]\n#[no_mangle]\npub static __moss_abi_")
          .append(isV10 ? "v10" : "v9").append("_contract_")
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

    private static boolean writeFileIfChanged(Path file, byte[] content) throws CompileError {
        try {
            if (Files.exists(file) && java.util.Arrays.equals(Files.readAllBytes(file), content)) {
                return false;
            }
            Files.write(file, content);
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
