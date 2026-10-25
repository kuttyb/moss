package org.mosslang.compiler.project;

import org.mosslang.compiler.ast.EnumCase;
import org.mosslang.compiler.ast.Field;
import org.mosslang.compiler.ast.Function;
import org.mosslang.compiler.ast.Param;
import org.mosslang.compiler.abi.CheckedBodyCodec;
import org.mosslang.compiler.abi.MossiInterface;
import org.mosslang.compiler.diagnostics.CompileError;
import org.mosslang.compiler.types.AccessMode;

import java.nio.file.Path;

public final class ProjectBuilderMetadataTest {
    public static void main(String[] args) {
        EnumCase payload = new EnumCase("Rgb", 1, "metadata.moss");
        payload.fields.add(new Field("red", "Int", "", "", 1, "metadata.moss", false, true));
        payload.fields.add(new Field("green", "Float", "", "", 1, "metadata.moss", false, true));
        payload.fields.add(new Field("blue", "String", "", "", 1, "metadata.moss", false, true));

        StringBuilder contract = new StringBuilder();
        ProjectBuilder.appendEnumCaseContract(contract, payload);
        assertEquals("case Rgb red:int green:float blue:string\n", contract.toString());

        StringBuilder semantic = new StringBuilder();
        ProjectBuilder.appendEnumCaseSemantic(semantic, payload);
        assertEquals("  enum_case \"Rgb\" 3 \"red\" \"int\" \"green\" \"float\" \"blue\" \"string\"\n",
                semantic.toString());
        testV10ImportRecordRequiresModuleBoundHashes();
        testV10SectionHashValidation();
        testV9FunctionParameterAccessSerialization();
        testExportedObjectFieldSerialization();
        testExportedObjectMethodSerialization();
        System.out.println("ProjectBuilderMetadataTest PASSED.");
    }

    private static void testV9FunctionParameterAccessSerialization() {
        Function function = new Function("provider__f", "fn f(a: Int, b: Vector[Int], c: Vector[Int])", 3,
                "provider.moss", true);
        function.params.add(new Param("a", "Int"));
        function.params.add(new Param("b", "Vector[Int]"));
        function.params.add(new Param("c", "Vector[Int]"));
        function.parameterAccessModes.add(AccessMode.Read);
        function.parameterAccessModes.add(AccessMode.Write);
        function.parameterAccessModes.add(AccessMode.Consume);
        StringBuilder record = new StringBuilder();
        ProjectBuilder.appendV9FunctionRecord(record, function, "f", "effects=may_panic=0", false);
        assertContains(record.toString(), "param 0 Int mode=READ name=\"a\"");
        assertContains(record.toString(), "param 1 Vector[Int] mode=WRITE name=\"b\"");
        assertContains(record.toString(), "param 2 Vector[Int] mode=CONSUME name=\"c\"");
    }

    private static void testExportedObjectFieldSerialization() {
        org.mosslang.compiler.ast.ObjectType object =
                new org.mosslang.compiler.ast.ObjectType("Point", "type Point", 1,
                        "provider.moss", true);
        object.fields.add(new Field("x", "float", "", "", 2,
                "provider.moss", false, true));
        object.fields.add(new Field("label", "string", "", "", 3,
                "provider.moss", false, true));
        StringBuilder fields = new StringBuilder();
        ProjectBuilder.appendExportedObjectFields(fields, object);
        assertEquals("  public_representation field x float\n"
                        + "  public_representation field label string\n",
                fields.toString());
    }

    private static void testExportedObjectMethodSerialization() {
        org.mosslang.compiler.ast.ObjectType object =
                new org.mosslang.compiler.ast.ObjectType("Point", "type Point", 1,
                        "provider.moss", true);
        object.fields.add(new Field("x", "float", "", "", 2,
                "provider.moss", false, true));
        org.mosslang.compiler.ast.Method method = new org.mosslang.compiler.ast.Method(
                "Point", "shift", 4, "provider.moss");
        method.setReturnType("float");
        method.receiverAccessMode = AccessMode.Write;
        method.params.add(new Param("amount", "float"));
        method.parameterAccessModes.add(AccessMode.Consume);
        method.body.add(new org.mosslang.compiler.ast.Stmt(
                org.mosslang.compiler.ast.Stmt.Kind.Assign, 5, 0,
                "x = x + amount", "x", "x + amount"));
        StringBuilder record = new StringBuilder();
        ProjectBuilder.appendV9ObjectMethodRecord(record, object, method, "provider");
        assertContains(record.toString(), "object_method shift return=float receiver=WRITE");
        assertContains(record.toString(), "params=1 \"amount\" \"float\" CONSUME");
    }

    private static void testV10ImportRecordRequiresModuleBoundHashes() {
        SourceFreeAbi.MossiInfo dependency = new SourceFreeAbi.MossiInfo();
        dependency.moduleId = "module-v1:provider@1.0.0::service";
        dependency.binaryHash = "sha256:" + "1".repeat(64);
        dependency.semanticHash = "sha256:" + "2".repeat(64);
        assertEquals(dependency.moduleId + " " + dependency.binaryHash + " "
                        + dependency.semanticHash + "\n",
                ProjectBuilder.v10ImportRecord("service", dependency, Path.of("service.mossi")));

        dependency.binaryHash = null;
        try {
            ProjectBuilder.v10ImportRecord("service", dependency, Path.of("service.mossi"));
            throw new AssertionError("ABI-v10 imports require both provider contract hashes");
        } catch (CompileError error) {
            if (!"SOURCE_FREE_ABI_REBUILD_REQUIRED".equals(error.getCode())) {
                throw new AssertionError("expected SOURCE_FREE_ABI_REBUILD_REQUIRED, got " + error.getCode(), error);
            }
        }
    }

    private static void testV10SectionHashValidation() {
        MossiInterface valid = new MossiInterface("module-v1:sample@1.0.0::provider");
        String encoded = CheckedBodyCodec.encodeMossi(valid);
        String corrupted = encoded.replaceFirst("semantic_contract_hash sha256:[0-9a-f]{64}",
                "semantic_contract_hash sha256:" + "0".repeat(64));
        int digestLineEnd = corrupted.indexOf('\n', corrupted.indexOf("provider_contract_digest "));
        String material = "moss-module-interface-v3\n" + corrupted.substring(digestLineEnd + 1);
        String digest = CheckedBodyCodec.sha256Hex(material);
        corrupted = "moss-module-interface-v3\nprovider_contract_digest sha256:" + digest
                + corrupted.substring(digestLineEnd);
        try {
            SourceFreeAbi.validateMossiText(Path.of("provider.mossi"), corrupted, true);
            throw new AssertionError("an invalid ABI-v10 section hash must be rejected");
        } catch (CompileError error) {
            if (!"SOURCE_FREE_CONTRACT_INVALID".equals(error.getCode())) {
                throw new AssertionError("expected SOURCE_FREE_CONTRACT_INVALID, got " + error.getCode(), error);
            }
        }
    }

    private static void assertEquals(String expected, String actual) {
        if (!expected.equals(actual)) {
            throw new AssertionError("expected <" + expected + "> but got <" + actual + ">");
        }
    }

    private static void assertContains(String value, String expected) {
        if (!value.contains(expected)) throw new AssertionError("missing <" + expected + "> from <" + value + ">");
    }
}
