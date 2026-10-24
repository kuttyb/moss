package org.mosslang.compiler.project;

import org.mosslang.compiler.diagnostics.CompileError;

import java.io.BufferedReader;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.stream.Stream;

public class ProjectManifest {
    private final Path root;
    private final Path manifestFile;
    private final String name;
    private final String version;
    private final Path source;

    public ProjectManifest(Path root, Path manifestFile, String name, String version, Path source) {
        this.root = root.toAbsolutePath().normalize();
        this.manifestFile = manifestFile.toAbsolutePath().normalize();
        this.name = name;
        this.version = version;
        this.source = source != null ? source : Paths.get("src");
    }

    public Path getRoot() {
        return root;
    }

    public Path getManifestFile() {
        return manifestFile;
    }

    public String getName() {
        return name;
    }

    public String getVersion() {
        return version;
    }

    public Path getSource() {
        return source;
    }

    public Path getSourceDirectory() {
        return root.resolve(source).normalize();
    }

    public static Path findProjectRoot(Path start) {
        Path curr = start.toAbsolutePath().normalize();
        if (!Files.isDirectory(curr)) {
            curr = curr.getParent();
        }
        while (curr != null) {
            if (Files.isRegularFile(curr.resolve("Moss.toml")) || Files.isRegularFile(curr.resolve("moss.toml"))) {
                return curr;
            }
            curr = curr.getParent();
        }
        return null;
    }

    public static ProjectManifest load(Path start) throws CompileError {
        Path root = findProjectRoot(start);
        if (root == null) {
            throw new CompileError("PROJECT_MANIFEST_ERROR", "no Moss.toml was found in this directory or any parent", start.toString(), 0);
        }
        Path manifestPath = Files.isRegularFile(root.resolve("Moss.toml")) ? root.resolve("Moss.toml") : root.resolve("moss.toml");
        String name = "";
        String version = "";
        Path source = Paths.get("src");

        try (BufferedReader reader = Files.newBufferedReader(manifestPath)) {
            String line;
            String section = "";
            int lineNumber = 0;
            while ((line = reader.readLine()) != null) {
                lineNumber++;
                boolean inString = false;
                int commentIndex = -1;
                for (int i = 0; i < line.length(); i++) {
                    char c = line.charAt(i);
                    if (c == '"') {
                        inString = !inString;
                    } else if (c == '#' && !inString) {
                        commentIndex = i;
                        break;
                    }
                }
                if (commentIndex != -1) {
                    line = line.substring(0, commentIndex);
                }
                line = line.trim();
                if (line.isEmpty()) {
                    continue;
                }
                if (line.startsWith("[") && line.endsWith("]")) {
                    section = line.substring(1, line.length() - 1).trim();
                    if (!section.equals("project") && !section.equals("package") &&
                        !section.equals("build") && !section.equals("dependencies")) {
                        throw new CompileError("PROJECT_MANIFEST_ERROR", "unknown manifest section '[" + section + "]'", manifestPath.toString(), lineNumber);
                    }
                    continue;
                }
                if (section.equals("dependencies")) {
                    continue;
                }
                int eq = line.indexOf('=');
                if (eq == -1 || section.isEmpty()) {
                    throw new CompileError("PROJECT_MANIFEST_ERROR", "expected a key = \"value\" inside a manifest section", manifestPath.toString(), lineNumber);
                }
                String key = line.substring(0, eq).trim();
                String rawVal = line.substring(eq + 1).trim();
                if (rawVal.length() < 2 || !rawVal.startsWith("\"") || !rawVal.endsWith("\"")) {
                    throw new CompileError("PROJECT_MANIFEST_ERROR", "manifest values must be quoted strings", manifestPath.toString(), lineNumber);
                }
                String val = rawVal.substring(1, rawVal.length() - 1);
                if (val.contains("\"")) {
                    throw new CompileError("PROJECT_MANIFEST_ERROR", "manifest string contains an unsupported quote", manifestPath.toString(), lineNumber);
                }

                if ((section.equals("project") || section.equals("package")) && key.equals("name")) {
                    name = val;
                } else if ((section.equals("project") || section.equals("package")) && key.equals("version")) {
                    version = val;
                } else if (section.equals("build") && key.equals("source")) {
                    source = Paths.get(val);
                } else {
                    throw new CompileError("PROJECT_MANIFEST_ERROR", "unknown manifest key '" + section + "." + key + "'", manifestPath.toString(), lineNumber);
                }
            }
        } catch (IOException e) {
            throw new CompileError("PROJECT_MANIFEST_ERROR", "cannot read Moss.toml: " + e.getMessage(), manifestPath.toString(), 0);
        }

        if (name.isEmpty()) {
            throw new CompileError("PROJECT_MANIFEST_ERROR", "[package].name (or legacy [project].name) is required", manifestPath.toString(), 0);
        }
        if (version.isEmpty()) {
            throw new CompileError("PROJECT_MANIFEST_ERROR", "[package].version (or legacy [project].version) is required", manifestPath.toString(), 0);
        }
        if (source.toString().isEmpty() || source.isAbsolute()) {
            throw new CompileError("PROJECT_MANIFEST_ERROR", "[build].source must be a non-empty project-relative path", manifestPath.toString(), 0);
        }
        for (Path part : source) {
            if (part.toString().equals("..")) {
                throw new CompileError("PROJECT_MANIFEST_ERROR", "[build].source may not escape the project root", manifestPath.toString(), 0);
            }
        }

        return new ProjectManifest(root, manifestPath, name, version, source);
    }

    public List<Path> findSourceFiles() throws CompileError {
        Path srcDir = getSourceDirectory();
        if (Files.isRegularFile(srcDir)) {
            if (srcDir.toString().endsWith(".moss")) {
                return List.of(srcDir.normalize());
            }
            return List.of();
        }
        if (!Files.isDirectory(srcDir)) {
            throw new CompileError("PROJECT_SOURCE_NOT_FOUND", "project source was not found at '" + srcDir + "'", srcDir.toString(), 0);
        }

        List<Path> files = new ArrayList<>();
        try (Stream<Path> stream = Files.walk(srcDir)) {
            stream.filter(Files::isRegularFile)
                  .filter(p -> p.toString().endsWith(".moss"))
                  .forEach(p -> files.add(p.toAbsolutePath().normalize()));
        } catch (IOException e) {
            throw new CompileError("PROJECT_SOURCE_NOT_FOUND", "cannot scan project source directory '" + srcDir + "': " + e.getMessage(), srcDir.toString(), 0);
        }

        if (files.isEmpty()) {
            throw new CompileError("PROJECT_SOURCE_NOT_FOUND", "project source contains no .moss files at '" + srcDir + "'", srcDir.toString(), 0);
        }
        Collections.sort(files);
        return files;
    }
}
