package org.mosslang.compiler.abi;

import org.mosslang.compiler.diagnostics.CompileError;
import org.mosslang.compiler.types.TypeKind;
import org.mosslang.compiler.types.TypeRef;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/** Canonical ABI-v10 semantic records shared with src/abi_v10_records.hpp. */
public final class AbiV10Records {
    private static final int MAX_STRING_BYTES = 1 << 20;

    private AbiV10Records() {}

    public enum WireTypeKind { Primitive, Builtin, Nominal, Enum, Domain, BoundVar, Unit }

    public static final class WireType {
        public WireTypeKind kind;
        public String identity = "";
        public List<WireType> arguments = new ArrayList<>();
        public int ordinal;

        public static WireType scalar(WireTypeKind kind, String identity) {
            WireType result = new WireType();
            result.kind = kind;
            result.identity = identity;
            return result;
        }

        public static WireType bound(int ordinal) {
            WireType result = new WireType();
            result.kind = WireTypeKind.BoundVar;
            result.ordinal = ordinal;
            return result;
        }

        public static WireType unit() {
            return scalar(WireTypeKind.Unit, "");
        }

        public String canonicalName() {
            return switch (kind) {
                case Primitive -> identity;
                case Unit -> "unit";
                case BoundVar -> "_" + ordinal;
                case Enum, Domain -> finalName(identity);
                case Nominal -> {
                    String name = finalName(identity);
                    if (arguments.isEmpty()) yield name;
                    yield name + "[" + String.join(", ", arguments.stream()
                            .map(WireType::canonicalName).toList()) + "]";
                }
                case Builtin -> {
                    String name = switch (identity) {
                        case "vector" -> "Vector";
                        case "map" -> "Map";
                        case "queue" -> "Queue";
                        case "set" -> "Set";
                        default -> identity;
                    };
                    if (arguments.isEmpty()) yield name;
                    yield name + "[" + String.join(", ", arguments.stream()
                            .map(WireType::canonicalName).toList()) + "]";
                }
            };
        }

        private static String finalName(String identity) {
            int typeAt = identity.lastIndexOf(":type:");
            int enumAt = identity.lastIndexOf(":enum:");
            int domainAt = identity.lastIndexOf(":domain:");
            int at = Math.max(typeAt, Math.max(enumAt, domainAt));
            return at < 0 ? identity : identity.substring(at + (at == typeAt ? 6 : at == enumAt ? 6 : 8));
        }
    }

    public record NominalField(String name, WireType type, boolean exported,
                               String provenanceId) {}

    public enum Access { Read, Write, Consume }

    public static final class Requirement {
        public WireType receiver;
        public String operation = "";
        public Access access = Access.Read;
        public int parent;
        public String provenanceId = "";
        public List<WireType> arguments = new ArrayList<>();
        public WireType result;
        public List<Access> argumentAccess = new ArrayList<>();
        public List<Integer> dependencies = new ArrayList<>();
    }

    /** Canonical ABI-v10 open callable summary, mirrored from abi_v10_records.hpp. */
    public static final class Summary {
        public String callableId = "";
        public int variableCount;
        public List<WireType> parameters = new ArrayList<>();
        public List<Access> parameterAccess = new ArrayList<>();
        public WireType result = WireType.unit();
        public List<Requirement> requirements = new ArrayList<>();
        public List<Integer> effectDependencies = new ArrayList<>();
        public List<String[]> dependencies = new ArrayList<>();
        public int observableEffectBits;
        public List<String> observableEffectTags = new ArrayList<>();
        public String bodySemanticHash = "";
        public String provenanceId = "";

        public byte[] encode() {
            return encodeVersion(effectDependencies.isEmpty() ? 3 : 4);
        }

        private byte[] encodeVersion(int version) {
            require(!callableId.isEmpty() && variableCount >= 0 && variableCount <= 4096
                            && parameters.size() <= 256 && parameters.size() == parameterAccess.size()
                            && requirements.size() <= 4096 && effectDependencies.size() <= 4096
                            && dependencies.size() <= 4096 && validBodyHash(bodySemanticHash)
                            && !provenanceId.isEmpty() && denseOrdinals(this),
                    "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 summary shape is invalid");
            Writer writer = new Writer();
            writer.byteValue(version);
            writer.string(callableId);
            writer.number(variableCount);
            writer.number(parameters.size());
            for (int i = 0; i < parameters.size(); i++) {
                writeType(writer, parameters.get(i), variableCount, 0);
                writer.byteValue(parametersAccess(i));
            }
            writeType(writer, result, variableCount, 0);
            writer.number(requirements.size());
            for (int i = 0; i < requirements.size(); i++) {
                final int requirementIndex = i;
                Requirement requirement = requirements.get(i);
                List<Integer> deps = requirement.dependencies.isEmpty() && requirement.parent != 0
                        ? List.of(requirement.parent) : requirement.dependencies;
                require(requirement.parent >= 0 && requirement.parent <= i
                                && deps.stream().allMatch(dep -> dep > 0 && dep <= requirementIndex)
                                && deps.equals(deps.stream().distinct().sorted().toList())
                                && ((deps.isEmpty() && requirement.parent == 0)
                                    || (deps.size() == 1 && requirement.parent == deps.get(0))
                                    || (deps.size() > 1 && requirement.parent == 0))
                                && !requirement.operation.isEmpty() && !requirement.provenanceId.isEmpty()
                                && requirement.arguments.size() <= 64
                                && requirement.arguments.size() == requirement.argumentAccess.size(),
                        "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 requirement DAG is invalid");
                writeType(writer, requirement.receiver, variableCount, 0);
                writer.string(requirement.operation);
                writer.byteValue(access(requirement.access));
                writer.number(requirement.parent);
                writer.string(requirement.provenanceId);
                writer.number(requirement.arguments.size());
                for (WireType argument : requirement.arguments) writeType(writer, argument, variableCount, 0);
                writer.byteValue(requirement.result == null ? 0 : 1);
                if (requirement.result != null) writeType(writer, requirement.result, variableCount, 0);
                writer.number(requirement.argumentAccess.size());
                for (Access mode : requirement.argumentAccess) writer.byteValue(access(mode));
                if (version >= 3) {
                    writer.number(deps.size());
                    for (int dep : deps) writer.number(dep);
                } else {
                    require(deps.size() <= 1 && (deps.isEmpty() || requirement.parent == deps.get(0)),
                            "SOURCE_FREE_SUMMARY_INVALID",
                            "legacy ABI-v10 summary cannot encode a multi-edge requirement");
                }
            }
            if (version == 4) {
                require(!effectDependencies.isEmpty(), "SOURCE_FREE_SUMMARY_INVALID",
                        "ABI-v10 summary version 4 requires an effect dependency");
                int previousEffect = 0;
                for (int dependency : effectDependencies) {
                    require(dependency > previousEffect && dependency <= requirements.size()
                                    && "static callable".equals(requirements.get(dependency - 1).operation),
                            "SOURCE_FREE_SUMMARY_INVALID",
                            "ABI-v10 effect dependency does not name a canonical Callable requirement");
                    previousEffect = dependency;
                }
                writer.number(effectDependencies.size());
                for (int dependency : effectDependencies) writer.number(dependency);
            }
            writer.number(dependencies.size());
            String previous = "";
            for (String[] dependency : dependencies) {
                require(dependency.length == 2 && !dependency[0].isEmpty()
                                && previous.compareTo(dependency[0]) < 0 && validBodyHash(dependency[1]),
                        "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 semantic dependencies are not canonical");
                previous = dependency[0];
                writer.string(dependency[0]);
                writer.string(dependency[1]);
            }
            writer.number(observableEffectBits);
            writeTags(writer, observableEffectTags);
            writer.string(bodySemanticHash);
            writer.string(provenanceId);
            return writer.finish();
        }

        public MossiRecord record() {
            byte[] payload = encode();
            return MossiRecord.fromBytes(hash("moss-abi-v10-open-summary", payload), payload);
        }

        public static Summary decode(MossiRecord record) {
            Reader reader = new Reader(record.payloadBytes());
            int version = reader.byteValue();
            require(version == 2 || version == 3 || version == 4,
                    "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 summary record version is unsupported");
            Summary value = new Summary();
            value.callableId = reader.string();
            value.variableCount = reader.number();
            require(value.variableCount <= 4096, "SOURCE_FREE_SUMMARY_INVALID",
                    "ABI-v10 summary has too many variables");
            int count = reader.count(256, "summary parameter count");
            for (int i = 0; i < count; i++) {
                value.parameters.add(readType(reader, value.variableCount, 0));
                value.parameterAccess.add(readAccess(reader));
            }
            value.result = readType(reader, value.variableCount, 0);
            count = reader.count(4096, "summary requirement count");
            for (int i = 0; i < count; i++) {
                Requirement req = new Requirement();
                req.receiver = readType(reader, value.variableCount, 0);
                req.operation = reader.string();
                req.access = readAccess(reader);
                req.parent = reader.number();
                req.provenanceId = reader.string();
                int args = reader.count(64, "requirement argument count");
                for (int a = 0; a < args; a++) req.arguments.add(readType(reader, value.variableCount, 0));
                int hasResult = reader.byteValue();
                require(hasResult <= 1, "SOURCE_FREE_SUMMARY_INVALID",
                        "ABI-v10 requirement result presence tag is invalid");
                if (hasResult == 1) req.result = readType(reader, value.variableCount, 0);
                int accesses = reader.count(64, "requirement access count");
                require(accesses == args, "SOURCE_FREE_SUMMARY_INVALID",
                        "ABI-v10 requirement argument/access counts disagree");
                for (int a = 0; a < accesses; a++) req.argumentAccess.add(readAccess(reader));
                if (version >= 3) {
                    int deps = reader.count(4096, "requirement dependency count");
                    for (int d = 0; d < deps; d++) req.dependencies.add(reader.number());
                } else if (req.parent != 0) req.dependencies.add(req.parent);
                value.requirements.add(req);
            }
            if (version >= 4) {
                count = reader.count(4096, "effect dependency count");
                for (int i = 0; i < count; i++) value.effectDependencies.add(reader.number());
            }
            count = reader.count(4096, "semantic dependency count");
            for (int i = 0; i < count; i++) value.dependencies.add(new String[]{reader.string(), reader.string()});
            value.observableEffectBits = reader.number();
            value.observableEffectTags = readTags(reader);
            value.bodySemanticHash = reader.string();
            value.provenanceId = reader.string();
            byte[] canonicalPayload = value.encodeVersion(version);
            require(reader.done() && Arrays.equals(canonicalPayload, record.payloadBytes())
                            && value.record().id.equals(record.id),
                    "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 summary is noncanonical or forged");
            return value;
        }

        private int parametersAccess(int index) { return access(parameterAccess.get(index)); }
    }

    public static final class CheckedBodyRecord {
        public enum DependencyKind { Callable, NominalType }
        public record Dependency(DependencyKind kind, String targetId, String expectedHash) {}

        public String callableId = "";
        public String semanticHash = "";
        public String checkedIrBytes = "";
        public List<Dependency> dependencies = new ArrayList<>();

        public byte[] encode() {
            require(callableId != null && !callableId.isEmpty()
                            && checkedIrBytes != null && checkedIrBytes.startsWith(CheckedBodyCodec.HEADER)
                            && validBodyHash(semanticHash)
                            && semanticHash.equals("body-semantic-v1:sha256:"
                                    + CheckedBodyCodec.sha256Hex(checkedIrBytes))
                            && dependencies.size() <= 4096,
                    "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 checked body has invalid version or semantic hash");
            Writer writer = new Writer();
            writer.byteValue(1);
            writer.string("checked-body-v1");
            writer.string(callableId);
            writer.string(semanticHash);
            writer.string(checkedIrBytes);
            writer.number(dependencies.size());
            String previous = "";
            for (Dependency dependency : dependencies) {
                require(dependency != null && dependency.kind() != null
                                && dependency.targetId() != null && !dependency.targetId().isEmpty()
                                && previous.compareTo(dependency.targetId()) < 0
                                && (dependency.kind() == DependencyKind.Callable
                                    ? validBodyHash(dependency.expectedHash())
                                    : validNominalDependency(dependency.targetId(), dependency.expectedHash())),
                        "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 checked body dependency is invalid");
                previous = dependency.targetId();
                writer.byteValue(dependency.kind().ordinal() + 1);
                writer.string(dependency.targetId());
                writer.string(dependency.expectedHash());
            }
            return writer.finish();
        }

        public MossiRecord record() {
            return MossiRecord.fromBytes("checked-body-v1:" + callableId, encode());
        }

        public static CheckedBodyRecord decode(MossiRecord record) {
            Reader reader = new Reader(record.payloadBytes());
            require(reader.byteValue() == 1 && "checked-body-v1".equals(reader.string()),
                    "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 checked body record version is unsupported");
            CheckedBodyRecord value = new CheckedBodyRecord();
            value.callableId = reader.string();
            value.semanticHash = reader.string();
            value.checkedIrBytes = reader.string();
            int count = reader.count(4096, "checked-body dependency count");
            for (int i = 0; i < count; i++) {
                int kind = reader.byteValue();
                require(kind >= 1 && kind <= DependencyKind.values().length,
                        "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 checked body dependency kind is unknown");
                value.dependencies.add(new Dependency(DependencyKind.values()[kind - 1],
                        reader.string(), reader.string()));
            }
            require(reader.done() && Arrays.equals(value.encode(), record.payloadBytes())
                            && record.id.equals("checked-body-v1:" + value.callableId),
                    "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 checked body is noncanonical or forged");
            // The semantic hash binds exact checked IR bytes; also require the IR
            // itself to be a canonical Java checked-body encoding.
            List<CheckedStmt> statements = CheckedBodyCodec.decodeCheckedBody(value.checkedIrBytes);
            require(CheckedBodyCodec.encodeCheckedBodyV1(statements).equals(value.checkedIrBytes),
                    "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 checked body IR is noncanonical");
            return value;
        }
    }

    private static boolean validNominalDependency(String targetId, String expectedHash) {
        return (validTemplateId(targetId) || (targetId != null && targetId.startsWith("nominal-enum-v1:")))
                && expectedHash != null && expectedHash.startsWith("sha256:")
                && isHexHash(expectedHash);
    }

    private static int access(Access access) {
        require(access != null, "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 access mode is missing");
        return access.ordinal() + 1;
    }

    private static Access readAccess(Reader reader) {
        int tag = reader.byteValue();
        require(tag >= 1 && tag <= Access.values().length,
                "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 access mode is invalid");
        return Access.values()[tag - 1];
    }

    private static boolean denseOrdinals(Summary summary) {
        boolean[] used = new boolean[summary.variableCount];
        for (WireType type : summary.parameters) collectOrdinals(type, used, 0);
        collectOrdinals(summary.result, used, 0);
        for (Requirement requirement : summary.requirements) {
            collectOrdinals(requirement.receiver, used, 0);
            for (WireType argument : requirement.arguments) collectOrdinals(argument, used, 0);
            if (requirement.result != null) collectOrdinals(requirement.result, used, 0);
        }
        for (boolean present : used) if (!present) return false;
        return true;
    }

    private static void collectOrdinals(WireType type, boolean[] used, int depth) {
        require(type != null && depth < 64 && type.arguments.size() <= 64,
                "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 type expression is too deep");
        if (type.kind == WireTypeKind.BoundVar) {
            require(type.ordinal >= 0 && type.ordinal < used.length,
                    "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 bound variable escapes its scope");
            used[type.ordinal] = true;
        }
        for (WireType argument : type.arguments) collectOrdinals(argument, used, depth + 1);
    }

    private static void writeTags(Writer writer, List<String> tags) {
        require(tags.size() <= 4096, "SOURCE_FREE_SUMMARY_INVALID",
                "ABI-v10 effect set exceeds the limit");
        writer.number(tags.size());
        String previous = "";
        for (String tag : tags) {
            require(tag != null && !tag.isEmpty() && previous.compareTo(tag) < 0,
                    "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 effect tags are not canonical");
            writer.string(tag);
            previous = tag;
        }
    }

    private static List<String> readTags(Reader reader) {
        int count = reader.count(4096, "effect tag count");
        List<String> tags = new ArrayList<>(count);
        for (int i = 0; i < count; i++) tags.add(reader.string());
        return tags;
    }

    private static boolean validBodyHash(String value) {
        String prefix = "body-semantic-v1:";
        return value != null && value.startsWith(prefix)
                && isHexHash(value.substring(prefix.length()));
    }

    private static boolean isHexHash(String value) {
        if (value == null || value.length() != 71 || !value.startsWith("sha256:")) return false;
        for (int i = 7; i < value.length(); i++) {
            char ch = value.charAt(i);
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
        }
        return true;
    }

    public static final class MethodTemplate {
        public String methodId;
        public String templateId;
        public String methodName;
        public boolean exported;
        public int variableCount;
        public List<WireType> parameters = new ArrayList<>();
        public List<Integer> parameterAccess = new ArrayList<>();
        public WireType result;
        public int receiverAccess = 1;
        public List<String> requirements = new ArrayList<>();
        public List<String[]> dependencies = new ArrayList<>();
        public int observableEffectBits;
        public List<String> observableEffectTags = new ArrayList<>();
        public String provenanceId;

        public static MethodTemplate create(String methodId, String templateId,
                                            String methodName, List<WireType> parameters,
                                            List<Integer> parameterAccess, WireType result,
                                            int receiverAccess, String provenanceId) {
            MethodTemplate value = new MethodTemplate();
            value.methodId = methodId;
            value.templateId = templateId;
            value.methodName = methodName;
            value.exported = true;
            value.parameters.addAll(parameters);
            value.parameterAccess.addAll(parameterAccess);
            value.result = result;
            value.receiverAccess = receiverAccess;
            value.provenanceId = provenanceId;
            return value;
        }

        public byte[] encode() {
            require(methodId != null && !methodId.isEmpty() && validTemplateId(templateId)
                            && methodName != null && !methodName.isEmpty()
                            && variableCount >= 0 && variableCount <= 4096
                            && parameters.size() <= 256 && parameters.size() == parameterAccess.size()
                            && receiverAccess >= 1 && receiverAccess <= 3
                            && provenanceId != null && !provenanceId.isEmpty()
                            && requirements.isEmpty() && dependencies.isEmpty()
                            && observableEffectBits == 0 && observableEffectTags.isEmpty(),
                    "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 method template shape is invalid");
            Writer writer = new Writer();
            writer.byteValue(1);
            writer.string(methodId);
            writer.string(templateId);
            writer.string(methodName);
            writer.byteValue(exported ? 1 : 0);
            writer.number(variableCount);
            writer.number(parameters.size());
            for (int i = 0; i < parameters.size(); i++) {
                writeType(writer, parameters.get(i), variableCount, 0);
                int access = parameterAccess.get(i);
                require(access >= 1 && access <= 3, "SOURCE_FREE_CONTRACT_INVALID",
                        "ABI-v10 parameter access mode is invalid");
                writer.byteValue(access);
            }
            writeType(writer, result, variableCount, 0);
            writer.byteValue(receiverAccess);
            writer.number(0); // requirements
            writer.number(0); // dependencies
            writer.number(observableEffectBits);
            writer.number(0); // observable effect tags
            writer.string(provenanceId);
            return writer.finish();
        }

        public MossiRecord record() { return MossiRecord.fromBytes(methodId, encode()); }

        public static MethodTemplate decode(MossiRecord record) {
            Reader reader = new Reader(record.payloadBytes());
            require(reader.byteValue() == 1, "SOURCE_FREE_CONTRACT_INVALID",
                    "ABI-v10 method template version is unsupported");
            MethodTemplate value = new MethodTemplate();
            value.methodId = reader.string();
            value.templateId = reader.string();
            value.methodName = reader.string();
            int exported = reader.byteValue();
            require(exported <= 1, "SOURCE_FREE_CONTRACT_INVALID",
                    "ABI-v10 method visibility is invalid");
            value.exported = exported == 1;
            value.variableCount = reader.number();
            require(value.variableCount <= 4096, "SOURCE_FREE_CONTRACT_INVALID",
                    "ABI-v10 method variable count exceeds the limit");
            int parameterCount = reader.count(256, "method parameter count");
            for (int i = 0; i < parameterCount; i++) {
                value.parameters.add(readType(reader, value.variableCount, 0));
                int access = reader.byteValue();
                require(access >= 1 && access <= 3, "SOURCE_FREE_CONTRACT_INVALID",
                        "ABI-v10 parameter access mode is invalid");
                value.parameterAccess.add(access);
            }
            value.result = readType(reader, value.variableCount, 0);
            value.receiverAccess = reader.byteValue();
            require(value.receiverAccess >= 1 && value.receiverAccess <= 3,
                    "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 receiver access mode is invalid");
            int requirementCount = reader.count(4096, "method requirement count");
            require(requirementCount == 0, "SOURCE_FREE_CONTRACT_INVALID",
                    "ABI-v10 method requirement projection is unsupported");
            int dependencyCount = reader.count(4096, "method dependency count");
            require(dependencyCount == 0, "SOURCE_FREE_CONTRACT_INVALID",
                    "ABI-v10 method dependency projection is unsupported");
            value.observableEffectBits = reader.number();
            int tagCount = reader.count(4096, "method effect tag count");
            require(tagCount == 0 && value.observableEffectBits == 0,
                    "SOURCE_FREE_CONTRACT_INVALID",
                    "ABI-v10 method effect projection is unsupported");
            value.provenanceId = reader.string();
            require(reader.done() && Arrays.equals(value.encode(), record.payloadBytes())
                            && value.methodId.equals(record.id),
                    "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 method template is noncanonical");
            return value;
        }
    }

    public static final class NominalTemplate {
        public String templateId;
        public String moduleId;
        public String declarationName;
        public int variableCount;
        public List<NominalField> fields = new ArrayList<>();
        public String representationHash;
        public List<String> methodTemplateIds = new ArrayList<>();
        public String provenanceId;

        public static NominalTemplate create(String moduleId, String name,
                                             List<NominalField> fields,
                                             String provenanceId) {
            NominalTemplate result = new NominalTemplate();
            result.moduleId = moduleId;
            result.declarationName = name;
            result.templateId = "nominal-template-v1:" + moduleId + ":type:" + name;
            result.fields.addAll(fields);
            result.provenanceId = provenanceId;
            for (NominalField field : fields) {
                if (field.type.kind == WireTypeKind.BoundVar)
                    result.variableCount = Math.max(result.variableCount, field.type.ordinal + 1);
            }
            result.representationHash = result.computeRepresentationHash();
            return result;
        }

        public String computeRepresentationHash() {
            Writer writer = new Writer();
            writer.string(templateId);
            writer.number(variableCount);
            for (NominalField field : fields) {
                writer.string(field.name);
                writer.byteValue(field.exported ? 1 : 0);
                writer.rawString(encodeType(field.type, variableCount));
            }
            return hash("moss-abi-v10-nominal-representation", writer.finish());
        }

        public byte[] encode() {
            validate();
            Writer writer = new Writer();
            writer.byteValue(1);
            writer.string(templateId);
            writer.string(moduleId);
            writer.string(declarationName);
            writer.number(variableCount);
            writer.number(fields.size());
            Set<String> names = new HashSet<>();
            for (NominalField field : fields) {
                require(!field.name.isEmpty() && names.add(field.name)
                        && !field.provenanceId.isEmpty(), "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                        "ABI-v10 nominal field identity is invalid");
                writer.string(field.name);
                writeType(writer, field.type, variableCount, 0);
                writer.byteValue(field.exported ? 1 : 0);
                writer.string(field.provenanceId);
            }
            writer.string(representationHash);
            writer.number(methodTemplateIds.size());
            String previous = "";
            for (String methodId : methodTemplateIds) {
                require(!methodId.isEmpty() && (previous.isEmpty() || previous.compareTo(methodId) < 0),
                        "SOURCE_FREE_TYPE_IDENTITY_MISMATCH", "ABI-v10 method IDs are not canonical");
                previous = methodId;
                writer.string(methodId);
            }
            writer.string(provenanceId);
            return writer.finish();
        }

        public MossiRecord record() {
            return MossiRecord.fromBytes(templateId, encode());
        }

        public void validate() {
            require(validTemplateId(templateId) && moduleId.equals(moduleFromTemplateId(templateId))
                            && templateId.equals("nominal-template-v1:" + moduleId + ":type:" + declarationName)
                            && !declarationName.isEmpty() && variableCount >= 0 && variableCount <= 4096
                            && fields.size() <= 4096 && representationHash != null
                            && representationHash.matches("sha256:[0-9a-f]{64}")
                            && !provenanceId.isEmpty() && representationHash.equals(computeRepresentationHash()),
                    "SOURCE_FREE_TYPE_IDENTITY_MISMATCH", "ABI-v10 nominal template shape is invalid");
        }

        public static NominalTemplate decode(MossiRecord record) {
            Reader reader = new Reader(record.payloadBytes());
            require(reader.byteValue() == 1, "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                    "ABI-v10 nominal record version is unsupported");
            NominalTemplate value = new NominalTemplate();
            value.templateId = reader.string();
            value.moduleId = reader.string();
            value.declarationName = reader.string();
            value.variableCount = reader.number();
            int count = reader.count(4096, "nominal field count");
            require(value.variableCount <= 4096, "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                    "ABI-v10 nominal variable count exceeds the limit");
            for (int i = 0; i < count; i++) {
                String name = reader.string();
                WireType type = readType(reader, value.variableCount, 0);
                int exported = reader.byteValue();
                require(exported <= 1, "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                        "ABI-v10 field visibility is invalid");
                value.fields.add(new NominalField(name, type, exported == 1, reader.string()));
            }
            value.representationHash = reader.string();
            int methodCount = reader.count(4096, "nominal method count");
            for (int i = 0; i < methodCount; i++) value.methodTemplateIds.add(reader.string());
            value.provenanceId = reader.string();
            require(reader.done() && Arrays.equals(value.encode(), record.payloadBytes())
                            && value.templateId.equals(record.id),
                    "SOURCE_FREE_TYPE_IDENTITY_MISMATCH", "ABI-v10 nominal template is noncanonical");
            return value;
        }
    }

    public record SourceProvenance(String provenanceId, String moduleId,
                                   String logicalSourceId, String sourceFileDisplay,
                                   int startLine, int startColumn, int endLine,
                                   int endColumn, String declarationId) {
        public static SourceProvenance create(String moduleId, String declarationId,
                                              String sourceFile, int line) {
            String display = sourceFile == null || sourceFile.isEmpty() ? ""
                    : java.nio.file.Path.of(sourceFile).getFileName().toString();
            String logical = hash("moss-abi-v10-logical-source", moduleId + "\n" + display);
            int safeLine = Math.max(line, 1);
            String id = hash("moss-abi-v10-provenance", declarationId + "\n" + logical + "\n" + safeLine);
            return new SourceProvenance(id, moduleId, logical, display, safeLine, 1, safeLine, 1, declarationId);
        }

        public byte[] encode() {
            require(!provenanceId.isEmpty() && validModuleId(moduleId) && !logicalSourceId.isEmpty()
                            && startLine > 0 && endLine >= startLine
                            && (endLine != startLine || endColumn >= startColumn),
                    "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 source provenance is invalid");
            Writer writer = new Writer();
            writer.byteValue(1);
            writer.string(provenanceId);
            writer.string(moduleId);
            writer.string(logicalSourceId);
            writer.string(sourceFileDisplay);
            writer.number(startLine);
            writer.number(startColumn);
            writer.number(endLine);
            writer.number(endColumn);
            writer.string(declarationId);
            return writer.finish();
        }

        public MossiRecord record() { return MossiRecord.fromBytes(provenanceId, encode()); }

        public static SourceProvenance decode(MossiRecord record) {
            Reader reader = new Reader(record.payloadBytes());
            require(reader.byteValue() == 1, "SOURCE_FREE_CONTRACT_INVALID",
                    "ABI-v10 source provenance version is unsupported");
            SourceProvenance value = new SourceProvenance(reader.string(), reader.string(),
                    reader.string(), reader.string(), reader.number(), reader.number(),
                    reader.number(), reader.number(), reader.string());
            require(reader.done() && Arrays.equals(value.encode(), record.payloadBytes())
                            && value.provenanceId.equals(record.id),
                    "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 source provenance is noncanonical");
            return value;
        }
    }

    public static WireType fromTypeRef(TypeRef type) {
        if (type == null) throw invalid("ABI-v10 field has no resolved type");
        return switch (type.kind()) {
            case Primitive -> WireType.scalar(WireTypeKind.Primitive, type.name());
            case Unit -> WireType.unit();
            case Builtin -> {
                WireType result = WireType.scalar(WireTypeKind.Builtin, type.name());
                for (TypeRef arg : type.typeArgs()) result.arguments.add(fromTypeRef(arg));
                yield result;
            }
            case Enum -> WireType.scalar(WireTypeKind.Enum, type.declaration().getId());
            case Domain -> WireType.scalar(WireTypeKind.Domain, type.declaration().getId());
            case NominalInstance -> {
                if (type.declaration() == null) throw invalid("ABI-v10 nominal field type has no declaration");
                WireType result = WireType.scalar(WireTypeKind.Nominal, type.declaration().getId());
                for (TypeRef.FieldSub sub : type.fieldSubstitutions())
                    result.arguments.add(fromTypeRef(sub.type()));
                yield result;
            }
            default -> throw invalid("ABI-v10 nominal field type is not closed");
        };
    }

    public static String hash(String domain, String text) {
        return hash(domain, text.getBytes(StandardCharsets.UTF_8));
    }

    public static String hash(String domain, byte[] payload) {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            digest.update(domain.getBytes(StandardCharsets.UTF_8));
            digest.update((byte) 0);
            digest.update(payload);
            return "sha256:" + java.util.HexFormat.of().formatHex(digest.digest());
        } catch (NoSuchAlgorithmException ex) {
            throw new IllegalStateException(ex);
        }
    }

    private static byte[] encodeType(WireType type, int variables) {
        Writer writer = new Writer();
        writeType(writer, type, variables, 0);
        return writer.finish();
    }

    private static void writeType(Writer writer, WireType type, int variables, int depth) {
        require(type != null && depth < 64 && type.arguments.size() <= 64,
                "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 type expression is too deep");
        writer.byteValue(type.kind.ordinal() + 1);
        if (type.kind == WireTypeKind.BoundVar) {
            require(type.ordinal >= 0 && type.ordinal < variables && type.identity.isEmpty()
                            && type.arguments.isEmpty(), "SOURCE_FREE_SUMMARY_INVALID",
                    "ABI-v10 bound variable escapes its scope");
            writer.number(type.ordinal);
            return;
        }
        if (type.kind == WireTypeKind.Unit) {
            require(type.identity.isEmpty() && type.arguments.isEmpty(),
                    "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 unit type has payload");
            return;
        }
        require(type.identity != null && !type.identity.isEmpty(),
                "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 type identity is missing");
        if (type.kind == WireTypeKind.Nominal)
            require(validTemplateId(type.identity), "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                    "ABI-v10 nominal TypeExpr has a noncanonical template ID");
        if (type.kind != WireTypeKind.Builtin && type.kind != WireTypeKind.Nominal)
            require(type.arguments.isEmpty(), "SOURCE_FREE_CONTRACT_INVALID",
                    "ABI-v10 scalar type has arguments");
        writer.string(type.identity);
        writer.number(type.arguments.size());
        for (WireType arg : type.arguments) writeType(writer, arg, variables, depth + 1);
    }

    private static WireType readType(Reader reader, int variables, int depth) {
        require(depth < 64, "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 type expression is too deep");
        int tag = reader.byteValue();
        require(tag >= 1 && tag <= 7, "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 type tag is unknown");
        WireTypeKind kind = WireTypeKind.values()[tag - 1];
        if (kind == WireTypeKind.BoundVar) {
            int ordinal = reader.number();
            require(ordinal < variables, "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 bound variable escapes its scope");
            return WireType.bound(ordinal);
        }
        if (kind == WireTypeKind.Unit) return WireType.unit();
        WireType type = WireType.scalar(kind, reader.string());
        int count = reader.count(64, "type argument count");
        require((kind == WireTypeKind.Builtin || kind == WireTypeKind.Nominal) || count == 0,
                "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 scalar type has arguments");
        if (kind == WireTypeKind.Nominal)
            require(validTemplateId(type.identity), "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                    "ABI-v10 nominal TypeExpr has a noncanonical template ID");
        for (int i = 0; i < count; i++) type.arguments.add(readType(reader, variables, depth + 1));
        return type;
    }

    private static boolean validTemplateId(String id) {
        if (id == null || !id.startsWith("nominal-template-v1:")) return false;
        int marker = id.lastIndexOf(":type:");
        return marker > "nominal-template-v1:".length() && marker + 6 < id.length()
                && validModuleId(id.substring("nominal-template-v1:".length(), marker));
    }

    private static String moduleFromTemplateId(String id) {
        int marker = id.lastIndexOf(":type:");
        return id.substring("nominal-template-v1:".length(), marker);
    }

    private static boolean validModuleId(String id) {
        return CheckedBodyCodec.isModuleIdValid(id);
    }

    private static void require(boolean condition, String code, String message) {
        if (!condition) throw new CompileError(0, message, code);
    }

    private static CompileError invalid(String message) {
        return new CompileError(0, message, "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");
    }

    private static final class Writer {
        private final ByteArrayOutputStream out = new ByteArrayOutputStream();
        void byteValue(int value) { out.write(value); }
        void number(int value) {
            require(value >= 0, "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 integer is negative");
            out.write((value >>> 24) & 0xff); out.write((value >>> 16) & 0xff);
            out.write((value >>> 8) & 0xff); out.write(value & 0xff);
        }
        void string(String value) {
            byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
            require(bytes.length <= MAX_STRING_BYTES, "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 string is oversized");
            number(bytes.length); out.writeBytes(bytes);
        }
        void rawString(byte[] bytes) {
            require(bytes.length <= MAX_STRING_BYTES, "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 string is oversized");
            number(bytes.length); out.writeBytes(bytes);
        }
        byte[] finish() { return out.toByteArray(); }
    }

    private static final class Reader {
        private final byte[] bytes;
        private int position;
        Reader(byte[] bytes) { this.bytes = bytes; }
        int byteValue() {
            require(position < bytes.length, "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 semantic record is truncated");
            return bytes[position++] & 0xff;
        }
        int number() {
            long result = ((long) byteValue() << 24) | ((long) byteValue() << 16)
                    | ((long) byteValue() << 8) | byteValue();
            require(result <= Integer.MAX_VALUE, "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 number exceeds Java bounds");
            return (int) result;
        }
        int count(int maximum, String label) {
            int count = number();
            require(count <= maximum && count <= bytes.length,
                    "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 " + label + " exceeds the limit");
            return count;
        }
        String string() {
            int length = number();
            require(length <= MAX_STRING_BYTES && length <= bytes.length - position,
                    "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 semantic string is truncated");
            try {
                String value = StandardCharsets.UTF_8.newDecoder()
                        .onMalformedInput(CodingErrorAction.REPORT)
                        .onUnmappableCharacter(CodingErrorAction.REPORT)
                        .decode(ByteBuffer.wrap(bytes, position, length)).toString();
                position += length;
                return value;
            } catch (CharacterCodingException ex) {
                throw new CompileError(0, "ABI-v10 semantic string is not UTF-8", "SOURCE_FREE_CONTRACT_INVALID");
            }
        }
        boolean done() { return position == bytes.length; }
    }
}
