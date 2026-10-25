package org.mosslang.compiler.cli;

import org.mosslang.compiler.ast.Function;
import org.mosslang.compiler.ast.Program;
import org.mosslang.compiler.ast.Stmt;
import org.mosslang.compiler.abi.CheckedBodyCodec;
import org.mosslang.compiler.abi.MossiInterface;
import org.mosslang.compiler.abi.MossiRecord;
import org.mosslang.compiler.types.AccessMode;

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
                    semantic_exports
                    export fn map kind=generic semantic_hash=0123456789abcdef return=vector effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=-;may_diverge=0;unresolved=0
                      param 0 vector mode=WRITE name="values"
                      open_parameters 0
                      generic_ir_version 1
                      generic_stmt 3 7 1 "echo 1, 2" "" "" "" 2 "1" "2"
                      generic_result 8 "values"
                      generic_ir_end
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
