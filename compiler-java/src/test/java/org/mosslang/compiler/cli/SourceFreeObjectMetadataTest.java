package org.mosslang.compiler.cli;

import org.mosslang.compiler.ast.ObjectType;
import org.mosslang.compiler.ast.Program;

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
                + "semantic_exports\nexport type Point\n" + exportedFields;
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
        Files.deleteIfExists(interfaceFile);
        Files.deleteIfExists(scratch);
        System.out.println("SourceFreeObjectMetadataTest PASSED.");
    }
}
