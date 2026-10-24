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
import java.util.List;

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
        if ("agent".equals(first) && args.length > 1 && "bootstrap".equals(args[1])) {
            System.out.println("{\"result\":{\"compiler\":\"moss-java\",\"language_version\":\"" + LANGUAGE_VERSION + "\"}}");
            return 0;
        }

        boolean checkOnly = false;
        boolean jsonOutput = false;
        String sourcePath = null;
        String outputPath = null;

        for (int i = 0; i < args.length; i++) {
            String a = args[i];
            if ("check".equals(a) || "--check".equals(a)) {
                checkOnly = true;
            } else if ("--json".equals(a)) {
                jsonOutput = true;
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

            CheckedProgram checker = new CheckedProgram(program);
            checker.run();

            if (!jsonOutput) {
                for (Warning w : checker.warnings()) {
                    System.err.println("moss:" + w.line() + ": warning: " + w.message());
                }
            }

            if (checkOnly) {
                if (jsonOutput) {
                    DiagnosticFormatter.writeStructuredSuccess(System.out, "check", COMPILER_VERSION, absPath);
                } else {
                    System.out.println(sourcePath + ": ok");
                }
                return 0;
            }

            // Default compilation: generate native Rust artifact
            if (outputPath == null) {
                outputPath = sourcePath.replaceAll("\\.moss$", "") + ".rs";
            }
            String rustCode = org.mosslang.compiler.backend.NativeGenerator.generate(program, false);
            Files.writeString(Paths.get(outputPath), rustCode, StandardCharsets.UTF_8);
            System.out.println("generated " + outputPath);
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
