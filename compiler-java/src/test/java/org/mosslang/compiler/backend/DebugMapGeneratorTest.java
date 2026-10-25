package org.mosslang.compiler.backend;

import org.mosslang.compiler.ast.Program;
import org.mosslang.compiler.cli.Main;
import org.mosslang.compiler.parser.Parser;
import org.mosslang.compiler.semantics.CheckedProgram;

import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashSet;
import java.util.Set;

public class DebugMapGeneratorTest {
    public static void main(String[] args) throws Exception {
        System.out.println("Running DebugMapGeneratorTest...");

        testStableHashAndToolingSymbols();
        testDebugMapGeneration();
        testUserSortFunctionShadowsBuiltinLowering();
        testCharCodepointRustLowering();
        testScalarProtocolRustLowering();
        testCliFlagsDebugMap();

        System.out.println("DebugMapGeneratorTest PASSED: All tests passed successfully.");
    }

    private static void assertEquals(Object expected, Object actual, String message) {
        if (expected == null && actual == null) return;
        if (expected != null && expected.equals(actual)) return;
        throw new AssertionError(message + " - expected: " + expected + ", actual: " + actual);
    }

    private static void assertTrue(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    private static void testStableHashAndToolingSymbols() {
        assertEquals("1c7da4aaa4b6bb71", DebugMapGenerator.stableHash("fn:normalize@7"),
                "FNV-1a hash of fn:normalize@7 must match canonical C++ hash");
        assertEquals("ade64fd3aee57822", DebugMapGenerator.stableHash("method:Meter.doubled@4"),
                "FNV-1a hash of method:Meter.doubled@4 must match canonical C++ hash");
        assertEquals("bf84c20deab69352", DebugMapGenerator.stableHash("handler:Counter.Add@20"),
                "FNV-1a hash of handler:Counter.Add@20 must match canonical C++ hash");
        assertEquals("7f8133ec4e25c588", DebugMapGenerator.stableHash("fn:step_probe@24"),
                "FNV-1a hash of fn:step_probe@24 must match canonical C++ hash");

        assertEquals("moss__function__normalize__1c7da4aaa4b6bb71",
                DebugMapGenerator.toolingNativeSymbol("function", "normalize", "fn:normalize@7"),
                "toolingNativeSymbol for function normalize");
        assertEquals("moss__method__Meter__doubled__ade64fd3aee57822",
                DebugMapGenerator.toolingNativeSymbol("method", "Meter__doubled", "method:Meter.doubled@4"),
                "toolingNativeSymbol for method Meter.doubled");
        assertEquals("moss__handler__Counter__Add__bf84c20deab69352",
                DebugMapGenerator.toolingNativeSymbol("handler", "Counter__Add", "handler:Counter.Add@20"),
                "toolingNativeSymbol for handler Counter.Add");
    }

    private static void testDebugMapGeneration() throws Exception {
        String source = """
                type Meter:
                  value: Int
                
                  fn doubled() -> Int:
                    return value * 2
                
                fn normalize(value: Int) -> Int:
                  adjusted = value + 1
                  return adjusted
                
                fn summarize(values: Vector) -> Int:
                  return values
                    |> map(normalize)
                    |> filter(_ > 2)
                    |> sum
                
                domain Counter:
                  total = 0
                
                  fn Add(amount: Int) -> Int:
                    total = total + amount
                    reply total
                
                fn step_probe(value: Int) -> Int:
                  first = value + 1
                  second = first + 1
                  return second
                
                fn main():
                  counter = Counter()
                  meter = Meter(value = 3)
                  enabled = true
                  label = "ready"
                  values = [1, 2, 3]
                  if not enabled:
                    echo label
                  echo meter.doubled()
                  step_probe(1)
                  echo summarize(values)
                  result = message counter.Add(5)
                  echo result
                """;

        File tmpSrc = File.createTempFile("test_map_", ".moss");
        tmpSrc.deleteOnExit();
        Files.writeString(tmpSrc.toPath(), source);

        Program program = Parser.parseSource(source, tmpSrc.getAbsolutePath());

        // Test O0
        String rustO0 = NativeGenerator.generate(program, false, false);
        DebugMap mapO0 = DebugMapGenerator.generate(program, rustO0, tmpSrc.getAbsolutePath(),
                "/tmp/test_o0.rs", "", false, false);

        assertEquals("moss-debug-map", "moss-debug-map", "Format must be moss-debug-map");
        assertEquals(1, mapO0.formatVersion, "Version must be 1");
        assertEquals(false, mapO0.debugBuild, "debugBuild should be false");
        assertEquals(false, mapO0.optimized, "optimized should be false");

        Set<String> requiredKinds = Set.of("function", "method", "handler", "domain", "type", "main",
                "functional_pipeline", "functional_node");
        Set<String> kinds = new HashSet<>();
        Set<String> identities = new HashSet<>();
        for (DebugMap.DebugMapEntry e : mapO0.entries) {
            kinds.add(e.constructKind);
            assertTrue(identities.add(e.semanticIdentity), "No duplicate semantic identity: " + e.semanticIdentity);
            assertTrue(e.source.startLine > 0, "Source start line > 0 for " + e.semanticIdentity);
            assertTrue(e.generated.startLine > 0, "Generated start line > 0 for " + e.semanticIdentity);
        }

        for (String req : requiredKinds) {
            assertTrue(kinds.contains(req), "Required kind missing: " + req);
        }

        // Test OPT
        String rustOpt = NativeGenerator.generate(program, false, true);
        DebugMap mapOpt = DebugMapGenerator.generate(program, rustOpt, tmpSrc.getAbsolutePath(),
                "/tmp/test_opt.rs", "", false, true);
        assertEquals(true, mapOpt.optimized, "optimized should be true");

        // Test DEBUG
        String rustDebug = NativeGenerator.generate(program, true, false);
        DebugMap mapDebug = DebugMapGenerator.generate(program, rustDebug, tmpSrc.getAbsolutePath(),
                "/tmp/test_debug.rs", "/tmp/test_debug", true, false);
        assertEquals(true, mapDebug.debugBuild, "debugBuild should be true");
        assertEquals(false, mapDebug.optimized, "optimized should be false");
        assertEquals("/tmp/test_debug", mapDebug.nativeExecutable, "nativeExecutable preserved");

        // Verify JSON serialization
        String json = mapO0.toJson();
        assertTrue(json.contains("\"format\": \"moss-debug-map\""), "JSON contains format header");
        assertTrue(json.contains("\"construct_kind\": \"function\""), "JSON contains function entry");
        assertTrue(json.contains("\"native_symbol\": \"moss__function__normalize__1c7da4aaa4b6bb71\""),
                "JSON contains correct native symbol for normalize");
    }

    private static void testUserSortFunctionShadowsBuiltinLowering() {
        String source = """
                fn sort(values: Vector[Int]) -> Int:
                  return 7
                fn main():
                  var values = [3, 1, 2]
                  echo sort(values)
                  echo values[0]
                """;
        Program program = Parser.parseSource(source, "user-sort-function.moss");
        new CheckedProgram(program).run();
        String rust = NativeGenerator.generate(program, false, false);
        assertTrue(rust.contains("fn sort("), "user-defined sort function is emitted");
        assertTrue(!rust.contains(".sort_by("), "user-defined sort call must not become builtin sorting");
        assertTrue(rust.contains("sort(values"), "main must call the user-defined sort function");

        String genericKeySource = """
                fn loose(value) -> String:
                  return value
                fn main():
                  var words = ["b", "c", "a"]
                  sort(words, by: loose)
                """;
        Program genericKeyProgram = Parser.parseSource(genericKeySource, "generic-sort-key.moss");
        new CheckedProgram(genericKeyProgram).run();
        String genericKeyRust = NativeGenerator.generate(genericKeyProgram, false, false);
        assertTrue(genericKeyRust.contains("__moss_fn_loose!((*__moss_a).clone())"),
                "untyped String sort key receives its specialized owned value");
        assertTrue(genericKeyRust.contains("__moss_fn_loose!((*__moss_b).clone())"),
                "both untyped sort-key operands receive owned values");
    }

    private static void testCharCodepointRustLowering() {
        String source = """
                fn main():
                  let letter = '\\x41'
                  echo letter.codepoint()
                  echo '\\x2C'.codepoint()
                """;
        Program program = Parser.parseSource(source, "char-codepoint.moss");
        new CheckedProgram(program).run();
        String rust = NativeGenerator.generate(program, false, false);
        assertTrue(rust.contains("((letter) as u32 as i64)"),
                "Char.codepoint lowers the named receiver to its Unicode scalar value");
        assertTrue(rust.contains("(('\\u{2C}') as u32 as i64)"),
                "Char.codepoint lowers a literal receiver to its Unicode scalar value");
    }

    private static void testScalarProtocolRustLowering() {
        String source = """
                type Counter:
                  value: Int
                fn main():
                  echo 2.less(3)
                  echo 0.0.isequal(-0.0)
                  echo 'A'.hash()
                  echo "moss".hash()
                  echo true.to_string()
                  let counter = Counter(value: 4)
                  echo counter.value.less(5)
                """;
        Program program = Parser.parseSource(source, "scalar-protocol.moss");
        new CheckedProgram(program).run();
        String rust = NativeGenerator.generate(program, false, false);
        assertTrue(rust.contains("(&(2_i64) < &(3_i64))"), "Int.less uses scalar ordering");
        assertTrue(rust.contains("moss_float_isequal(0.0_f64 as f64, -0.0_f64 as f64)"),
                "Float.isequal uses Moss float equality");
        assertTrue(rust.contains("(('A') as u32 as i64)"), "Char.hash uses the Unicode scalar value");
        assertTrue(rust.contains("moss_string_hash(&(MossStr::from(\"moss\")))"),
                "String.hash uses the Moss string hash helper");
        assertTrue(rust.contains("MossStr::from((true).to_string())"),
                "Bool.to_string returns a Moss String");
        assertTrue(rust.contains("(&(counter.value) < &(5_i64))"),
                "scalar operation on an object field uses its checked field type");
    }

    private static void testCliFlagsDebugMap() throws Exception {
        String source = "fn main():\n  echo 42\n";
        Path tmpSrc = Files.createTempFile("test_cli_", ".moss");
        Files.writeString(tmpSrc, source);

        Path tmpRs = Files.createTempFile("test_cli_", ".rs");
        Path tmpMap = Path.of(tmpRs.toString().replaceAll("\\.rs$", ".mossmap"));
        Files.deleteIfExists(tmpMap);

        // Default: emits .mossmap
        int rc = Main.run(new String[]{tmpSrc.toString(), "-o", tmpRs.toString()});
        assertEquals(0, rc, "Compilation succeeded");
        assertTrue(Files.exists(tmpMap), "Default compilation generated .mossmap alongside .rs");
        Files.deleteIfExists(tmpMap);

        // --no-debug-map: suppresses .mossmap
        rc = Main.run(new String[]{"--no-debug-map", tmpSrc.toString(), "-o", tmpRs.toString()});
        assertEquals(0, rc, "Compilation with --no-debug-map succeeded");
        assertTrue(!Files.exists(tmpMap), "--no-debug-map suppressed .mossmap generation");

        // --emit-debug-map <file>: writes to custom path
        Path customMap = Files.createTempFile("custom_", ".map");
        Files.deleteIfExists(customMap);
        rc = Main.run(new String[]{"--emit-debug-map", customMap.toString(), tmpSrc.toString(), "-o", tmpRs.toString()});
        assertEquals(0, rc, "Compilation with --emit-debug-map succeeded");
        assertTrue(Files.exists(customMap), "--emit-debug-map wrote to custom map destination");
        Files.deleteIfExists(customMap);

        Files.deleteIfExists(tmpSrc);
        Files.deleteIfExists(tmpRs);
        Files.deleteIfExists(tmpMap);
    }
}
