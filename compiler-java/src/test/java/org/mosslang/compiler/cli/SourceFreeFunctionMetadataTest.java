package org.mosslang.compiler.cli;

import org.mosslang.compiler.ast.Function;
import org.mosslang.compiler.ast.Program;
import org.mosslang.compiler.ast.Stmt;
import org.mosslang.compiler.abi.CheckedBodyCodec;
import org.mosslang.compiler.abi.MossiInterface;
import org.mosslang.compiler.abi.MossiRecord;
import org.mosslang.compiler.types.AccessMode;
import org.mosslang.compiler.parser.Parser;
import org.mosslang.compiler.semantics.SemanticChecker;

import java.nio.file.Files;
import java.nio.file.Path;

public final class SourceFreeFunctionMetadataTest {
    public static void main(String[] args) throws Exception {
        Path scratch = Path.of("tmp");
        Files.createDirectories(scratch);
        Path interfaceFile = Files.createTempFile(scratch, "java-source-free-function-", ".mossi");
        try {
            Files.writeString(interfaceFile, """
                    moss-module-interface-v2
                    module_id provider::service
                    package_version 1.0.0
                    compiler 0.1.0
                    native_abi 9
                    imports
                    contract
                    fn map kind=generic return=vector effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=-;may_diverge=0;unresolved=0 symbol=moss__function__service__map__abcd
                      param 0 vector mode=WRITE name="values"
                    fn consume kind=concrete return=unit effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=-;may_diverge=0;unresolved=0 symbol=moss__function__service__consume__abcd
                      param 0 string mode=CONSUME name="value"
                    fn signal kind=concrete return=unit effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=enum:Fault:Bad;may_diverge=0;unresolved=0 symbol=moss__function__service__signal__abcd
                    fn ping kind=concrete return=unit effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=-;may_diverge=0;unresolved=0 symbol=moss__function__service__ping__abcd
                    semantic_exports
                    export fn map kind=generic semantic_hash=0123456789abcdef return=vector effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=-;may_diverge=0;unresolved=0
                      param 0 vector mode=WRITE name="values"
                      open_parameters 0
                      generic_ir_version 1
                      generic_stmt 3 7 1 "echo 1, 2" "" "" "" 2 "1" "2"
                      generic_result 8 "values"
                      generic_ir_end
                    export fn consume semantic_hash=0123456789abcdef return=unit effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=-;may_diverge=0;unresolved=0
                      param 0 string mode=CONSUME name="value"
                    export fn signal semantic_hash=0123456789abcdef return=unit effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=enum:Fault:Bad;may_diverge=0;unresolved=0
                    export fn ping semantic_hash=0123456789abcdef return=unit effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=-;may_diverge=0;unresolved=0
                    """);

            Program program = new Program();
            Main.loadMossiFunctions(interfaceFile, "service", program);
            Function imported = program.functions.stream()
                    .filter(function -> function.name.equals("service.map"))
                    .findFirst().orElseThrow(() -> new AssertionError("imported function missing"));
            assertTrue(imported.generic && imported.staticDispatch, "generic dispatch facts were not loaded");
            assertEquals(1, imported.params.size(), "parameter count");
            assertEquals("values", imported.params.get(0).name, "parameter name");
            assertEquals("vector", imported.params.get(0).type, "parameter type");
            assertEquals(AccessMode.Write, imported.parameterAccessModes.get(0), "parameter access mode");
            assertEquals(1, imported.body.size(), "generic statement count");
            Stmt statement = imported.body.get(0);
            assertEquals(Stmt.Kind.Call, statement.kind, "statement kind");
            assertEquals(2, statement.args.size(), "call argument count");
            assertEquals("2", statement.args.get(1), "second call argument");
            assertEquals("values", imported.resultExpression, "generic result expression");
            Function v9Consume = program.functions.stream()
                    .filter(function -> function.name.equals("service.consume"))
                    .findFirst().orElseThrow(() -> new AssertionError("ABI-v9 consuming function missing"));
            assertEquals(AccessMode.Consume, v9Consume.parameterAccessModes.get(0),
                    "ABI-v9 consuming function access mode");
            Program v9RaiseConsumer = Parser.parseSource(""
                    + "enum Fault:\n"
                    + "  Bad\n"
                    + "fn forward():\n"
                    + "  service.signal()\n"
                    + "fn main():\n"
                    + "  forward()\n", "source-free-v9-raise-consumer.moss");
            Main.loadMossiFunctions(interfaceFile, "service", v9RaiseConsumer);
            v9RaiseConsumer.functions.removeIf(function -> !function.name.equals("main")
                    && !function.name.equals("forward")
                    && !function.name.equals("service.signal")
                    && !function.name.equals("service__signal"));
            try {
                new SemanticChecker(v9RaiseConsumer).run();
                throw new AssertionError("ABI-v9 source-free raise was allowed to escape main");
            } catch (org.mosslang.compiler.diagnostics.CompileError error) {
                if (!"MOSS_COMPILE_ERROR".equals(error.getCode())) throw error;
            }
            Program handledRaiseConsumer = Parser.parseSource(""
                    + "enum Fault:\n"
                    + "  Bad\n"
                    + "fn forward():\n"
                    + "  service.signal()\n"
                    + "fn main():\n"
                    + "  try:\n"
                    + "    forward()\n"
                    + "  recover Fault.Bad:\n"
                    + "    pass\n", "source-free-v9-handled-raise-consumer.moss");
            Main.loadMossiFunctions(interfaceFile, "service", handledRaiseConsumer);
            handledRaiseConsumer.functions.removeIf(function -> !function.name.equals("main")
                    && !function.name.equals("forward")
                    && !function.name.equals("service.signal")
                    && !function.name.equals("service__signal"));
            new SemanticChecker(handledRaiseConsumer).run();
            String handledRaiseRust = org.mosslang.compiler.backend.NativeGenerator.generate(
                    handledRaiseConsumer, false);
            if (!handledRaiseRust.contains("moss_service::signal().map_err(|e| __moss_raise(format!(\"{:?}\", e)))?")) {
                throw new AssertionError("source-free function was not lowered through its provider module: "
                        + handledRaiseRust);
            }
            if (!handledRaiseRust.contains("fn forward() -> Result<(), __MossRaised>")) {
                throw new AssertionError("source-free raising function was not included in raise closure");
            }
            if (handledRaiseRust.contains("pub fn service.signal()")) {
                throw new AssertionError("source-free function was emitted as an invalid local Rust stub");
            }
            Program raiseAfterRecover = Parser.parseSource(""
                    + "enum Fault:\n"
                    + "  Bad\n"
                    + "fn invoke(flag: Bool) -> Int:\n"
                    + "  try:\n"
                    + "    raise Fault.Bad\n"
                    + "  recover Fault.Bad:\n"
                    + "    pass\n"
                    + "  raise Fault.Bad\n"
                    + "fn main():\n"
                    + "  pass\n", "raise-after-recover.moss");
            new SemanticChecker(raiseAfterRecover).run();
            String raiseAfterRecoverRust = org.mosslang.compiler.backend.NativeGenerator.generate(
                    raiseAfterRecover, false);
            if (!raiseAfterRecoverRust.contains("fn invoke(flag: bool) -> Result<i64, __MossRaised>")) {
                throw new AssertionError("a top-level raise after a completed recovery was not propagated: "
                        + raiseAfterRecoverRust);
            }
            Program raiseLikeText = Parser.parseSource(""
                    + "import service\n"
                    + "fn note():\n"
                    + "  echo \"service.signal()\"\n"
                    + "fn main():\n"
                    + "  note()\n", "source-free-raise-name-in-string.moss");
            Main.loadMossiFunctions(interfaceFile, "service", raiseLikeText);
            raiseLikeText.functions.removeIf(function -> !function.name.equals("main")
                    && !function.name.equals("note")
                    && !function.name.equals("service.signal")
                    && !function.name.equals("service__signal"));
            new SemanticChecker(raiseLikeText).run();
            String raiseLikeTextRust = org.mosslang.compiler.backend.NativeGenerator.generate(
                    raiseLikeText, false);
            if (raiseLikeTextRust.contains("fn note() -> Result<(), __MossRaised>")) {
                throw new AssertionError("raise-like text literal incorrectly made note a raising function");
            }
            Program plainSourceFreeCall = Parser.parseSource(""
                    + "import service\n"
                    + "fn main():\n"
                    + "  service.ping()\n", "source-free-plain-function-call.moss");
            Main.loadMossiFunctions(interfaceFile, "service", plainSourceFreeCall);
            plainSourceFreeCall.functions.removeIf(function -> !function.name.equals("main")
                    && !function.name.equals("service.ping")
                    && !function.name.equals("service__ping"));
            new SemanticChecker(plainSourceFreeCall).run();
            String plainSourceFreeRust = org.mosslang.compiler.backend.NativeGenerator.generate(
                    plainSourceFreeCall, false);
            if (!plainSourceFreeRust.contains("moss_service::ping()")
                    || plainSourceFreeRust.contains("pub fn service.ping()")) {
                throw new AssertionError("ordinary source-free function did not use its provider path: "
                        + plainSourceFreeRust);
            }
            Program v9Consumer = Parser.parseSource(""
                    + "fn main():\n"
                    + "  value = \"moved\"\n"
                    + "  service.consume(value)\n"
                    + "  echo value\n", "source-free-v9-consume-consumer.moss");
            Main.loadMossiFunctions(interfaceFile, "service", v9Consumer);
            v9Consumer.functions.removeIf(function -> !function.name.equals("main")
                    && !function.name.equals("service.consume")
                    && !function.name.equals("service__consume"));
            try {
                new SemanticChecker(v9Consumer).run();
                throw new AssertionError("ABI-v9 source-free CONSUME mode did not consume its argument");
            } catch (org.mosslang.compiler.diagnostics.CompileError error) {
                if (!"OWNERSHIP_USE_AFTER_CONSUME".equals(error.getCode())) throw error;
            }
            Function alias = program.functions.stream()
                    .filter(function -> function.name.equals("service__map"))
                    .findFirst().orElseThrow(() -> new AssertionError("native function alias missing"));
            assertEquals("values", alias.resultExpression, "native alias result expression");

            testV10FunctionMetadata(scratch);
            System.out.println("SourceFreeFunctionMetadataTest PASSED.");
        } finally {
            Files.deleteIfExists(interfaceFile);
        }
    }

    private static void testV10FunctionMetadata(Path scratch) throws Exception {
        String effects = "effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=-;may_diverge=0;unresolved=0";
        String contract = "fn map kind=generic return=vector " + effects + " symbol=moss__function__service__map__abcd\n"
                + "  param 0 vector mode=WRITE name=\"values\"\n";
        String semantic = "export fn map kind=generic semantic_hash=0123456789abcdef return=vector " + effects + "\n"
                + "  param 0 vector mode=WRITE name=\"values\"\n"
                + "  open_parameters 0\n  requirements\n  specialization_dependencies\n"
                + "  semantic_ir_body_hash=0123456789abcdef\n  generic_ir_version 1\n"
                + "  generic_stmt 3 7 1 \"echo 1, 2\" \"\" \"\" \"\" 2 \"1\" \"2\"\n"
                + "  generic_result 8 \"values\"\n  generic_ir_end\n";
        MossiInterface iface = new MossiInterface("module-v1:provider@1.0.0::service");
        iface.binaryContract.add(new MossiRecord("fn:map", contract));
        iface.semanticContract.add(new MossiRecord("fn:map", semantic));
        iface.semanticContract.add(new MossiRecord("fn:make", ""
                + "export fn make return=Vector[Int] " + effects + "\n"));
        iface.semanticContract.add(new MossiRecord("fn:consume", ""
                + "export fn consume return=unit " + effects + "\n"
                + "  param 0 string mode=CONSUME name=\"value\"\n"));
        iface.semanticContract.add(new MossiRecord("fn:mutate_pair", ""
                + "export fn mutate_pair return=unit " + effects + "\n"
                + "  param 0 string mode=WRITE name=\"left\"\n"
                + "  param 1 string mode=READ name=\"right\"\n"));
        Path interfaceFile = Files.createTempFile(scratch, "java-source-free-v10-function-", ".mossi");
        try {
            Files.writeString(interfaceFile, CheckedBodyCodec.encodeMossi(iface));
            Program program = new Program();
            Main.loadMossiFunctions(interfaceFile, "service", program);
            Function imported = program.functions.stream()
                    .filter(function -> function.name.equals("service.map"))
                    .findFirst().orElseThrow(() -> new AssertionError("ABI-v10 imported function missing"));
            assertTrue(imported.generic && imported.staticDispatch, "ABI-v10 generic dispatch facts were not loaded");
            assertEquals(1, imported.params.size(), "ABI-v10 parameter count");
            assertEquals(AccessMode.Write, imported.parameterAccessModes.get(0), "ABI-v10 access mode");
            assertEquals(2, imported.body.get(0).args.size(), "ABI-v10 generic call arguments");
            assertEquals("values", imported.resultExpression, "ABI-v10 generic result expression");
            Function consume = program.functions.stream()
                    .filter(function -> function.name.equals("service.consume"))
                    .findFirst().orElseThrow(() -> new AssertionError("ABI-v10 consuming function missing"));
            assertEquals(AccessMode.Consume, consume.parameterAccessModes.get(0),
                    "ABI-v10 consuming function access mode");
            Function make = program.functions.stream()
                    .filter(function -> function.name.equals("service.make"))
                    .findFirst().orElseThrow(() -> new AssertionError("ABI-v10 concrete function missing"));
            assertEquals("Vector[Int]", make.returnType, "ABI-v10 concrete collection return type");
            Program consumer = Parser.parseSource(""
                    + "fn forward(value: String):\n"
                    + "  service.consume(value)\n"
                    + "fn main():\n"
                    + "  value = \"moved\"\n"
                    + "  forward(value)\n"
                    + "  echo value\n", "source-free-return-consumer.moss");
            MossiInterface makeOnly = new MossiInterface("module-v1:provider@1.0.0::service");
            makeOnly.semanticContract.add(new MossiRecord("fn:make", ""
                    + "export fn make return=Vector[Int] " + effects + "\n"));
            makeOnly.semanticContract.add(new MossiRecord("fn:consume", ""
                    + "export fn consume return=unit " + effects + "\n"
                    + "  param 0 string mode=CONSUME name=\"value\"\n"));
            makeOnly.semanticContract.add(new MossiRecord("fn:mutate_pair", ""
                    + "export fn mutate_pair return=unit " + effects + "\n"
                    + "  param 0 string mode=WRITE name=\"left\"\n"
                    + "  param 1 string mode=READ name=\"right\"\n"));
            Path makeOnlyFile = Files.createTempFile(scratch, "java-source-free-v10-return-", ".mossi");
            try {
                Files.write(makeOnlyFile, CheckedBodyCodec.encodeMossiBytes(makeOnly));
                Program sourceFreeV10Call = Parser.parseSource(""
                        + "import service\n"
                        + "fn main():\n"
                        + "  values = service.make()\n", "source-free-v10-function-call.moss");
                Main.loadMossiFunctions(makeOnlyFile, "service", sourceFreeV10Call);
                sourceFreeV10Call.functions.removeIf(function -> !function.name.equals("main")
                        && !function.name.equals("service.make")
                        && !function.name.equals("service__make"));
                new SemanticChecker(sourceFreeV10Call).run();
                String sourceFreeV10Rust = org.mosslang.compiler.backend.NativeGenerator.generate(
                        sourceFreeV10Call, false);
                if (!sourceFreeV10Rust.contains("moss_service::make()")
                        || sourceFreeV10Rust.contains("pub fn service.make()")) {
                    throw new AssertionError("ABI-v10 source-free function did not use its provider path: "
                            + sourceFreeV10Rust);
                }

                Main.loadMossiFunctions(makeOnlyFile, "service", consumer);
                Function importedConsume = consumer.functions.stream()
                        .filter(function -> function.name.equals("service.consume"))
                        .findFirst().orElseThrow(() -> new AssertionError(
                                "source-free consuming function was not imported"));
                assertEquals(AccessMode.Consume, importedConsume.parameterAccessModes.get(0),
                        "source-free consuming access mode");
                assertTrue(importedConsume.sourceFreeAbiVersion > 0,
                        "source-free ABI version was not retained");
                try {
                    new SemanticChecker(consumer).run();
                    throw new AssertionError("source-free CONSUME mode did not consume its argument");
                } catch (org.mosslang.compiler.diagnostics.CompileError error) {
                    if (!"OWNERSHIP_USE_AFTER_CONSUME".equals(error.getCode())) throw error;
                }

                Program writeConsumer = Parser.parseSource(""
                        + "fn main():\n"
                        + "  value = \"shared\"\n"
                        + "  service.mutate_pair(value, value)\n",
                        "source-free-write-consumer.moss");
                Main.loadMossiFunctions(makeOnlyFile, "service", writeConsumer);
                try {
                    new SemanticChecker(writeConsumer).run();
                    throw new AssertionError("source-free WRITE/READ overlap was not rejected");
                } catch (org.mosslang.compiler.diagnostics.CompileError error) {
                    if (!"OWNERSHIP_CONFLICTING_ACCESS".equals(error.getCode())) throw error;
                }
            } finally {
                Files.deleteIfExists(makeOnlyFile);
            }
        } finally {
            Files.deleteIfExists(interfaceFile);
        }
    }

    private static void assertEquals(Object expected, Object actual, String message) {
        if (!expected.equals(actual)) {
            throw new AssertionError(message + ": expected " + expected + ", got " + actual);
        }
    }

    private static void assertTrue(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }
}
