package org.mosslang.compiler.project;

import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Comparator;

public final class ProjectExecutionTest {
    public static void main(String[] args) throws Exception {
        Path scratch = Path.of("tmp");
        Files.createDirectories(scratch);
        Path root = Files.createTempDirectory(scratch, "java-project-execution-");
        try {
            Files.createDirectories(root.resolve("src"));
            Files.createDirectories(root.resolve("tests"));
            Files.createDirectories(root.resolve("benches"));
            Files.writeString(root.resolve("moss.toml"), ""
                    + "[package]\n"
                    + "name = \"execution-fixture\"\n"
                    + "version = \"0.1.0\"\n"
                    + "[build]\n"
                    + "source = \"src\"\n");
            Files.writeString(root.resolve("src/main.moss"), ""
                    + "fn doubled(value: Int) -> Int:\n"
                    + "  return value * 2\n"
                    + "fn main():\n"
                    + "  pass\n");
            Files.writeString(root.resolve("tests/arithmetic.moss"), ""
                    + "test \"passes actual assertion\":\n"
                    + "  assertEqual(doubled(21), 42)\n"
                    + "test \"reports failed assertion\":\n"
                    + "  assertEqual(doubled(20), 42)\n");
            Files.writeString(root.resolve("benches/arithmetic.moss"), ""
                    + "bench \"runs declared body\":\n"
                    + "  values = [1, 2, 3]\n"
                    + "  values |> sum\n");
            ProjectManifest manifest = ProjectManifest.load(root);

            CapturedResult passing = capture(() ->
                    ProjectBuilder.test(manifest, "passes actual assertion", false, true));
            assertEquals(0, passing.status, "passing test status");
            if (!passing.output.contains("\"passed\": 1, \"failed\": 0")
                    || !passing.output.contains("\"status\": \"pass\"")) {
                throw new AssertionError("project test did not execute and report the passing assertion: "
                        + passing.output);
            }

            CapturedResult failing = capture(() ->
                    ProjectBuilder.test(manifest, "reports failed assertion", false, true));
            assertEquals(1, failing.status, "failing test status");
            if (!failing.output.contains("\"passed\": 0, \"failed\": 1")
                    || !failing.output.contains("TEST_ASSERTION_FAILED")) {
                throw new AssertionError("project test did not report the failed assertion: "
                        + failing.output);
            }

            CapturedResult benchmark = capture(() -> ProjectBuilder.bench(manifest, "runs declared body", true));
            assertEquals(0, benchmark.status, "benchmark status");
            if (!benchmark.output.contains("\"name\": \"runs declared body\"")
                    || !benchmark.output.contains("\"samples\": 31")
                    || !benchmark.output.contains("\"sample_values_ns\": [")) {
                throw new AssertionError("project benchmark did not execute and report measured samples: "
                        + benchmark.output);
            }
            System.out.println("ProjectExecutionTest PASSED.");
        } finally {
            try (var walk = Files.walk(root)) {
                walk.sorted(Comparator.reverseOrder()).forEach(path -> {
                    try { Files.deleteIfExists(path); }
                    catch (Exception error) { throw new RuntimeException(error); }
                });
            }
        }
    }

    private static CapturedResult capture(CheckedOperation operation) throws Exception {
        PrintStream original = System.out;
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (PrintStream output = new PrintStream(bytes, true, StandardCharsets.UTF_8)) {
            System.setOut(output);
            int status = operation.run();
            output.flush();
            return new CapturedResult(status, bytes.toString(StandardCharsets.UTF_8));
        } finally {
            System.setOut(original);
        }
    }

    private static void assertEquals(int expected, int actual, String label) {
        if (expected != actual) {
            throw new AssertionError(label + ": expected " + expected + ", got " + actual);
        }
    }

    @FunctionalInterface
    private interface CheckedOperation {
        int run() throws Exception;
    }

    private record CapturedResult(int status, String output) {}
}
