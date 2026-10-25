package org.mosslang.compiler.cli;

import org.mosslang.compiler.ast.ObjectType;
import org.mosslang.compiler.ast.EnumType;
import org.mosslang.compiler.ast.Program;
import org.mosslang.compiler.ast.Function;
import org.mosslang.compiler.ast.Param;
import org.mosslang.compiler.backend.NativeGenerator;
import org.mosslang.compiler.abi.CheckedBodyCodec;
import org.mosslang.compiler.abi.MossiInterface;
import org.mosslang.compiler.abi.MossiRecord;
import org.mosslang.compiler.abi.AbiV10Records;
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
                + "module_id sample::provider\ncontract\nobject Point\ncomment Café\n"
                + "semantic_exports\n"
                + "export enum LegacyResult\n"
                + "  declaration_id nominal-enum-v1:module-v1:sample@0.1.0::provider:enum:LegacyResult\n"
                + "  enum_case \"Failed\" 2 \"code\" \"int\" \"message\" \"string\"\n"
                + "export type Point\n" + exportedFields
                + "  object_method shift return=float receiver=WRITE "
                + "effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=-;may_diverge=0;unresolved=0 "
                + "params=1 \"amount\" \"float\" READ\n"
                + "  object_method replace return=unit receiver=WRITE "
                + "effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=-;may_diverge=0;unresolved=0 "
                + "params=1 \"next\" \"Vector[Int]\" CONSUME\n";
        Files.writeString(interfaceFile, content);
        if (!Main.readMossiText(interfaceFile).contains("comment CafÃ©")) {
            throw new AssertionError("ABI-v9 interface decoding no longer preserves its legacy byte mapping");
        }
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
        EnumType legacyResult = program.enums.stream()
                .filter(enumeration -> enumeration.name.equals("provider__LegacyResult"))
                .findFirst().orElseThrow(() -> new AssertionError(
                        "ABI-v9 source-free enum was not imported"));
        if (!legacyResult.sourceFree || legacyResult.cases.size() != 1
                || legacyResult.cases.get(0).fields.size() != 2) {
            throw new AssertionError("ABI-v9 enum payload facts were not imported");
        }
        Program legacyEnumConsumer = Parser.parseSource(""
                + "fn failure() -> provider__LegacyResult:\n"
                + "  return provider__LegacyResult.Failed(code=3, message=\"bad\")\n"
                + "fn main():\n"
                + "  pass\n", "legacy-enum-consumer.moss");
        Main.loadMossiFunctions(interfaceFile, "provider", legacyEnumConsumer);
        new SemanticChecker(legacyEnumConsumer).run();
        String legacyEnumRust = NativeGenerator.generate(legacyEnumConsumer, false);
        if (!legacyEnumRust.contains("moss_provider::LegacyResult::Failed")
                || legacyEnumRust.contains("pub enum provider__LegacyResult")) {
            throw new AssertionError("ABI-v9 source-free enum did not use its provider Rust path: "
                    + legacyEnumRust);
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
        if (!"unit".equals(imported.methods.get(1).returnType)) {
            throw new AssertionError("source-free unit method return type was not restored");
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
                + "fn replace_values(point: provider__Point, values: Vector[Int]):\n"
                + "  point.replace(values)\n"
                + "fn main():\n"
                + "  pass\n", "consumer.moss");
        Main.loadMossiFunctions(interfaceFile, "provider", checkedConsumer);
        new SemanticChecker(checkedConsumer).run();
        String checkedRust = NativeGenerator.generate(checkedConsumer, false);
        if (!checkedRust.contains("fn replace_values(point: &mut moss_provider::Point, values: std::vec::Vec<i64>)")) {
            throw new AssertionError("source-free WRITE/CONSUME modes did not reach Rust parameter lowering: "
                    + checkedRust);
        }
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
        Program wrongReturnConsumer = Parser.parseSource(""
                + "fn wrong_return(point: provider__Point, values: Vector[Int]) -> Int:\n"
                + "  return point.replace(values)\n"
                + "fn main():\n"
                + "  pass\n", "consumer-return-type.moss");
        Main.loadMossiFunctions(interfaceFile, "provider", wrongReturnConsumer);
        try {
            new SemanticChecker(wrongReturnConsumer).run();
            throw new AssertionError("unit-returning source-free method cannot satisfy Int result");
        } catch (CompileError error) {
            if (!"MOSS_COMPILE_ERROR".equals(error.getCode())) {
                throw new AssertionError("expected return-type rejection, got " + error.getCode(), error);
            }
        }
        MossiInterface v10 = new MossiInterface("module-v1:sample@0.1.0::provider");
        v10.compiler = "moss-java-é";
        v10.binaryContract.add(new MossiRecord("type:Point", "type Point nominal\n"));
        AbiV10Records.SourceProvenance pointProvenance = AbiV10Records.SourceProvenance.create(
                v10.moduleId, "nominal-template-v1:" + v10.moduleId + ":type:Point",
                "provider.moss", 1);
        AbiV10Records.NominalTemplate pointTemplate = AbiV10Records.NominalTemplate.create(
                v10.moduleId, "Point", java.util.List.of(new AbiV10Records.NominalField(
                        "x", AbiV10Records.WireType.scalar(
                                AbiV10Records.WireTypeKind.Primitive, "float"),
                        true, pointProvenance.provenanceId())), pointProvenance.provenanceId());
        v10.binaryContract.add(pointTemplate.record());
        v10.provenance.add(pointProvenance.record());
        v10.semanticContract.add(new MossiRecord("type:Point", ""
                + "export type Point nominal\n"
                + "  declaration_id nominal-template-v1:module-v1:sample@0.1.0::provider:type:Point\n"
                + "  object_method replace return=unit receiver=WRITE "
                + "effects=local_capture_read=0;local_mutation=0;domain_read=0;domain_write=0;message=0;external_io=0;fileio=0;fileio_unknown=0;may_panic=0;raise_set=-;may_diverge=0;unresolved=0 "
                + "params=1 \"next\" \"Vector[Int]\" CONSUME\n"));
        v10.semanticContract.add(new MossiRecord("00000000 export Outcome", ""
                + "export enum Outcome\n"
                + "  declaration_id nominal-enum-v1:module-v1:sample@0.1.0::provider:enum:Outcome\n"
                + "  enum_case \"Ready\" 1 \"code\" \"int\"\n"
                + "  enum_case \"Done\" 0\n"));
        Path v10File = scratch.resolve("provider-v10.mossi");
        String encodedV10 = CheckedBodyCodec.encodeMossi(v10);
        Files.writeString(v10File, encodedV10, java.nio.charset.StandardCharsets.UTF_8);
        if (!encodedV10.equals(Main.readMossiText(v10File))) {
            throw new AssertionError("ABI-v10 interface was not decoded as UTF-8");
        }
        Program v10Consumer = new Program();
        Main.loadMossiFunctions(v10File, "provider", v10Consumer);
        ObjectType v10Point = v10Consumer.objects.stream()
                .filter(object -> object.name.equals("provider__Point"))
                .findFirst().orElseThrow();
        if (v10Point.methods.size() != 1
                || v10Point.fields.size() != 1
                || !"x".equals(v10Point.fields.get(0).name)
                || !"float".equals(v10Point.fields.get(0).type)
                || !"unit".equals(v10Point.methods.get(0).returnType)
                || v10Point.methods.get(0).receiverAccessMode
                        != org.mosslang.compiler.types.AccessMode.Write
                || v10Point.methods.get(0).parameterAccessModes.size() != 1
                || v10Point.methods.get(0).parameterAccessModes.get(0)
                        != org.mosslang.compiler.types.AccessMode.Consume) {
            throw new AssertionError("ABI-v10 source-free object method facts were not restored");
        }
        EnumType v10Outcome = v10Consumer.enums.stream()
                .filter(enumeration -> enumeration.name.equals("provider__Outcome"))
                .findFirst().orElseThrow(() -> new AssertionError(
                        "ABI-v10 source-free enum was not imported"));
        if (!v10Outcome.sourceFree || v10Outcome.cases.size() != 2
                || !"Ready".equals(v10Outcome.cases.get(0).name)
                || v10Outcome.cases.get(0).fields.size() != 1
                || !"code".equals(v10Outcome.cases.get(0).fields.get(0).name)) {
            throw new AssertionError("ABI-v10 source-free enum payload was not restored");
        }
        Program enumConsumer = Parser.parseSource(""
                + "fn choose() -> provider__Outcome:\n"
                + "  return provider__Outcome.Ready(code=1)\n"
                + "fn main():\n"
                + "  pass\n", "enum-consumer.moss");
        Main.loadMossiFunctions(v10File, "provider", enumConsumer);
        new SemanticChecker(enumConsumer).run();
        String enumRust = NativeGenerator.generate(enumConsumer, false);
        if (!enumRust.contains("moss_provider::Outcome::Ready")
                || enumRust.contains("pub enum provider__Outcome")) {
            throw new AssertionError("source-free enum did not lower to its provider crate: " + enumRust);
        }
        Files.deleteIfExists(interfaceFile);
        Files.deleteIfExists(v10File);
        Files.deleteIfExists(scratch);
        System.out.println("SourceFreeObjectMetadataTest PASSED.");
    }
}
