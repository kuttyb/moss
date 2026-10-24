package org.mosslang.compiler.project;

import org.mosslang.compiler.diagnostics.CompileError;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;

public class BackendToolchain {
    private final String rustcExecutable;
    private final String versionVerbose;
    private final String profile;
    private final List<String> compileFlags;
    private final String fingerprint;

    public BackendToolchain(String rustcExecutable, String versionVerbose, String profile, List<String> compileFlags, String fingerprint) {
        this.rustcExecutable = rustcExecutable;
        this.versionVerbose = versionVerbose;
        this.profile = profile;
        this.compileFlags = compileFlags;
        this.fingerprint = fingerprint;
    }

    public String getRustcExecutable() {
        return rustcExecutable;
    }

    public String getVersionVerbose() {
        return versionVerbose;
    }

    public String getProfile() {
        return profile;
    }

    public List<String> getCompileFlags() {
        return compileFlags;
    }

    public String getFingerprint() {
        return fingerprint;
    }

    public static BackendToolchain inspect(boolean optimized, boolean testMode) throws CompileError {
        String rustc = System.getenv("RUSTC");
        if (rustc == null || rustc.isBlank()) {
            rustc = "rustc";
        }

        // Run rustc --version --verbose
        String versionVerbose;
        try {
            Process process = new ProcessBuilder(rustc, "--version", "--verbose").start();
            StringBuilder sb = new StringBuilder();
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    sb.append(line).append("\n");
                }
            }
            int code = process.waitFor();
            if (code != 0 || sb.toString().trim().isEmpty()) {
                throw new CompileError("BUILD_BACKEND_ERROR", "could not identify the configured Rust compiler with 'rustc --version --verbose'", "", 0);
            }
            versionVerbose = sb.toString().trim();
        } catch (Exception e) {
            throw new CompileError("BUILD_TOOL_NOT_FOUND", "rustc was not found; install Rust or set RUSTC to an executable: " + e.getMessage(), "", 0);
        }

        String profile = optimized ? "release" : (testMode ? "test-debug" : "debug");
        List<String> flags = new ArrayList<>(List.of("--edition=2021", "-D", "warnings"));
        if (optimized) {
            flags.add("-O");
            flags.add("-C");
            flags.add("debuginfo=1");
        } else {
            flags.add("-g");
            flags.add("-C");
            flags.add("opt-level=0");
        }

        StringBuilder payload = new StringBuilder();
        payload.append(rustc).append("\n");
        payload.append(versionVerbose).append("\n");
        payload.append(profile).append("\n");
        for (String f : flags) {
            payload.append(f).append("\n");
        }

        String fingerprint = sha256(payload.toString());
        return new BackendToolchain(rustc, versionVerbose, profile, flags, fingerprint);
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
