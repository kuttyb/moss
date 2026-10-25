package org.mosslang.compiler.cli;

import org.mosslang.compiler.ast.ObjectType;
import org.mosslang.compiler.ast.Program;
import org.mosslang.compiler.ast.Function;
import org.mosslang.compiler.ast.Param;
import org.mosslang.compiler.backend.NativeGenerator;
import org.mosslang.compiler.diagnostics.CompileError;
import org.mosslang.compiler.parser.Parser;
import org.mosslang.compiler.semantics.SemanticChecker;

import java.nio.file.Files;
import java.nio.file.Path;

public final class SourceFreeObjectMetadataTest {
    public static void main(String[] args) throws Exception {
        Path scratch = Path.of("tmp", "java-source-free-object-metadata");
        Files.createDirectories(scratch);
        Path interfaceFile = scratch.resolve("provider.mossi");
        String exportedFields = "  public_representation field x float\n"
                + "  public_representation field label string\n";

        String content = "moss-module-interface-v2\n"
                + "provider_contract_digest sha256:" + "0".repeat(64) + "\n"
                + "module_id sample::provider\ncontract\nobject Point\n"
                + "semantic_exports\nexport type Point\n" + exportedFields
                + "  object_method shift return=float receiver=WRITE "
                + "effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=-;may_diverge=0;unresolved=0 "
                + "params=1 \"amount\" \"float\" READ\n"
                + "  object_method replace return=unit receiver=WRITE "
                + "effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=-;may_diverge=0;unresolved=0 "
                + "params=1 \"next\" \"Vector[Int]\" CONSUME\n";
        Files.writeString(interfaceFile, content);
        Program program = new Program();
        Main.loadMossiFunctions(interfaceFile, "provider", program);
        ObjectType imported = program.objects.stream()
                .filter(object -> object.name.equals("provider__Point"))
                .findFirst().orElseThrow(() -> new AssertionError(
                        "source-free object declaration was not imported"));
        if (imported.fields.size() != 2
                || !"float".equals(imported.fields.get(0).type)
                || !"string".equals(imported.fields.get(1).type)) {
            throw new AssertionError("source-free fields were not preserved: "
                    + imported.fields);
        }
        if (program.objects.stream().noneMatch(object -> object.name.equals("provider.Point"))) {
            throw new AssertionError("qualified source type alias was not imported");
        }
        if (imported.methods.size() != 2
                || !"shift".equals(imported.methods.get(0).name)
                || !"float".equals(imported.methods.get(0).returnType)
                || imported.methods.get(0).params.size() != 1
                || !"amount".equals(imported.methods.get(0).params.get(0).name)
                || !"float".equals(imported.methods.get(0).params.get(0).type)
                || imported.methods.get(0).receiverAccessMode
                        != org.mosslang.compiler.types.AccessMode.Write
                || imported.methods.get(0).parameterAccessModes.size() != 1
                || imported.methods.get(0).parameterAccessModes.get(0)
                        != org.mosslang.compiler.types.AccessMode.Read
                || imported.methods.get(1).parameterAccessModes.size() != 1
                || imported.methods.get(1).parameterAccessModes.get(0)
                        != org.mosslang.compiler.types.AccessMode.Consume) {
            throw new AssertionError("source-free method signature was not restored");
        }
        Function consumer = new Function("consume", "fn consume(value: provider__Point)",
                1, "consumer.moss", false);
        consumer.params.add(new Param("value", "provider__Point"));
        consumer.setReturnType("unit");
        program.functions.add(consumer);
        String rust = NativeGenerator.generate(program, false);
        if (!rust.contains("value: &moss_provider::Point")) {
            throw new AssertionError("source-free nominal type did not lower to its provider crate path: " + rust);
        }
        if (rust.contains("pub struct provider__Point")
                || rust.contains("impl provider__Point")) {
            throw new AssertionError("source-free nominal was incorrectly redefined locally");
        }
        Program checkedConsumer = Parser.parseSource(""
                + "fn use_point(point: provider__Point) -> Float:\n"
                + "  return point.shift(2.0)\n"
                + "fn main():\n"
                + "  pass\n", "consumer.moss");
        Main.loadMossiFunctions(interfaceFile, "provider", checkedConsumer);
        new SemanticChecker(checkedConsumer).run();
        Program consumingConsumer = Parser.parseSource(""
                + "fn use_point(point: provider__Point, values: Vector[Int]) -> Vector[Int]:\n"
                + "  point.replace(values)\n"
                + "  return values\n"
                + "fn main():\n"
                + "  pass\n", "consumer-ownership.moss");
        Main.loadMossiFunctions(interfaceFile, "provider", consumingConsumer);
        try {
            new SemanticChecker(consumingConsumer).run();
            throw new AssertionError("source-free method CONSUME mode must consume its argument");
        } catch (CompileError error) {
            if (!"OWNERSHIP_USE_AFTER_CONSUME".equals(error.getCode())) {
                throw new AssertionError("expected OWNERSHIP_USE_AFTER_CONSUME, got "
                        + error.getCode(), error);
            }
        }
        Files.deleteIfExists(interfaceFile);
        Files.deleteIfExists(scratch);
        System.out.println("SourceFreeObjectMetadataTest PASSED.");
    }
}
