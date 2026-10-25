package org.mosslang.compiler.abi;

import org.mosslang.compiler.diagnostics.CompileError;
import org.mosslang.compiler.types.TypeContext;
import org.mosslang.compiler.types.TypeKind;
import org.mosslang.compiler.types.TypeRef;

import java.io.IOException;
import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.StandardCharsets;
import java.nio.charset.CodingErrorAction;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.*;

public class CheckedBodyCodec {
    public static final String HEADER = "moss-checked-body-v1\n";
    public static final String MOSSI_HEADER = "moss-module-interface-v3\n";
    private static final int MAX_MOSSI_BYTES = 16 * 1024 * 1024;
    private static final int MAX_MOSSI_RECORDS = 100_000;
    private static final int MAX_MOSSI_RECORD_BYTES = 4 * 1024 * 1024;

    private static int parseBoundedCount(String value, int maximum, String what) {
        try {
            int count = Integer.parseInt(value);
            if (count < 0 || count > maximum) throw new NumberFormatException();
            return count;
        } catch (NumberFormatException ex) {
            throw new CompileError(0, "ABI-v10 " + what + " is malformed or exceeds its limit", "SOURCE_FREE_CONTRACT_INVALID");
        }
    }

    public static void encodeField(StringBuilder out, String value) {
        String val = value != null ? value : "";
        out.append(val.length()).append(":").append(val);
    }

    public static void encodeType(StringBuilder out, TypeRef type) {
        if (type == null) {
            throw new IllegalArgumentException("checked body node has no TypeRef");
        }
        encodeField(out, String.valueOf(type.kind().ordinal()));
        if (type.kind() == TypeKind.InferenceVar) {
            if (type.varId().sessionId() != 0 || type.varId().ownerId() != 0) {
                throw new IllegalArgumentException("checked body has non-summary type variable");
            }
            encodeField(out, String.valueOf(type.varId().ordinal()));
        } else if (type.kind() == TypeKind.NominalInstance || type.kind() == TypeKind.Enum || type.kind() == TypeKind.Domain) {
            if (type.declaration() == null || type.declaration().getId().isEmpty()) {
                throw new IllegalArgumentException("checked body has unresolved declaration");
            }
            encodeField(out, type.declaration().getId());
        } else {
            encodeField(out, type.name());
        }

        encodeField(out, String.valueOf(type.typeArgs().size()));
        for (TypeRef arg : type.typeArgs()) {
            encodeType(out, arg);
        }

        encodeField(out, String.valueOf(type.fieldSubstitutions().size()));
        for (TypeRef.FieldSub fs : type.fieldSubstitutions()) {
            encodeField(out, fs.name());
            encodeType(out, fs.type());
        }
    }

    public static void encodeExpr(StringBuilder out, CheckedExpr expr) {
        if (expr == null || expr.type == null) {
            throw new IllegalArgumentException("checked body expr has no TypeRef");
        }
        encodeField(out, String.valueOf(expr.kind.tag));
        encodeType(out, expr.type);

        switch (expr.kind) {
            case Parameter -> {
                encodeField(out, String.valueOf(expr.ordinal));
            }
            case IntegerLiteral -> {
                encodeField(out, expr.literalValue);
            }
            case FloatLiteral, StringLiteral -> {
                encodeField(out, expr.literalValue);
            }
            case BoolLiteral -> {
                encodeField(out, "true".equals(expr.literalValue) ? "1" : "0");
            }
            case LocalRead -> {
                encodeField(out, String.valueOf(expr.ordinal));
            }
            case Add -> {
                encodeField(out, "2");
                for (CheckedExpr child : expr.operands) {
                    encodeExpr(out, child);
                }
            }
            case Unary -> {
                encodeField(out, String.valueOf(expr.unaryOp.tag));
                encodeExpr(out, expr.operands.get(0));
            }
            case Binary -> {
                encodeField(out, String.valueOf(expr.binaryOp.tag));
                encodeExpr(out, expr.operands.get(0));
                encodeExpr(out, expr.operands.get(1));
            }
            case Field -> {
                encodeField(out, expr.semanticTargetId);
                encodeField(out, String.valueOf(expr.memberOrdinal));
            }
            default -> {
                encodeField(out, expr.literalValue != null ? expr.literalValue : "");
            }
        }
    }

    public static void encodeStmt(StringBuilder out, CheckedStmt stmt) {
        if (stmt == null) return;
        encodeField(out, String.valueOf(stmt.kind.tag));
        encodeField(out, String.valueOf(stmt.localOrdinal));

        if (stmt.expr != null) {
            encodeField(out, "1");
            encodeExpr(out, stmt.expr);
        } else {
            encodeField(out, "0");
        }

        encodeField(out, String.valueOf(stmt.operands.size()));
        for (CheckedExpr op : stmt.operands) {
            encodeExpr(out, op);
        }

        encodeField(out, String.valueOf(stmt.body.size()));
        for (CheckedStmt s : stmt.body) {
            encodeStmt(out, s);
        }
    }

    public static String encodeCheckedBodyV1(List<CheckedStmt> statements) {
        StringBuilder out = new StringBuilder();
        out.append(HEADER);
        encodeField(out, String.valueOf(statements != null ? statements.size() : 0));
        if (statements != null) {
            for (CheckedStmt s : statements) {
                encodeStmt(out, s);
            }
        }
        return out.toString();
    }

    public static List<CheckedStmt> decodeCheckedBody(String bytes) {
        return decodeCheckedBody(bytes, null);
    }

    public static List<CheckedStmt> decodeCheckedBody(String bytes, TypeContext context) {
        H3BodyReader reader = new H3BodyReader(bytes, context);
        return reader.read();
    }

    public static String computeCheckedBodyHashV1(List<CheckedStmt> statements) {
        String encoded = encodeCheckedBodyV1(statements);
        return "body-semantic-v1:sha256:" + sha256Hex(encoded);
    }

    // --- SHA-256 and Domain Hashing ---

    public static String sha256Hex(String content) {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] digest = md.digest(content.getBytes(StandardCharsets.UTF_8));
            return HexFormat.of().formatHex(digest);
        } catch (NoSuchAlgorithmException e) {
            throw new RuntimeException("SHA-256 not available", e);
        }
    }

    private static byte[] sha256(byte[] content) {
        try {
            return MessageDigest.getInstance("SHA-256").digest(content);
        } catch (NoSuchAlgorithmException e) {
            throw new RuntimeException("SHA-256 not available", e);
        }
    }

    public static String domainHash(String domain, String content) {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            md.update(domain.getBytes(StandardCharsets.UTF_8));
            md.update((byte) 0);
            md.update(content.getBytes(StandardCharsets.UTF_8));
            return "sha256:" + HexFormat.of().formatHex(md.digest());
        } catch (NoSuchAlgorithmException e) {
            throw new RuntimeException("SHA-256 not available", e);
        }
    }

    public static boolean isHexHash(String value) {
        if (value == null || value.length() != 71 || !value.startsWith("sha256:")) {
            return false;
        }
        for (int i = 7; i < value.length(); i++) {
            char c = value.charAt(i);
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                return false;
            }
        }
        return true;
    }

    public static boolean isModuleIdValid(String id) {
        if (id == null || !id.startsWith("module-v1:")) {
            return false;
        }
        String rest = id.substring("module-v1:".length());
        int at = rest.indexOf('@');
        int split = rest.indexOf("::");
        if (at <= 0 || split <= at + 1 || split + 2 >= rest.length()) {
            return false;
        }
        String pkg = rest.substring(0, at);
        String ver = rest.substring(at + 1, split);
        String mod = rest.substring(split + 2);
        return isValidIdentPart(pkg) && isValidIdentPart(ver) && isValidIdentPart(mod);
    }

    private static boolean isValidIdentPart(String s) {
        if (s == null || s.isEmpty()) return false;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-' || c == '.')) {
                return false;
            }
        }
        return true;
    }

    // --- .mossi Serialization and Parsing ---

    private static String frameImports(List<MossiImport> imports) {
        List<MossiImport> sorted = new ArrayList<>(imports != null ? imports : Collections.emptyList());
        sorted.sort(Comparator.comparing(a -> a.moduleId));
        StringBuilder sb = new StringBuilder();
        sb.append("imports\ncount ").append(sorted.size()).append("\n");
        for (MossiImport imp : sorted) {
            sb.append(imp.moduleId).append(" ")
              .append(imp.binaryHash).append(" ")
              .append(imp.semanticHash).append("\n");
        }
        return sb.toString();
    }

    private static void requireUniqueImports(List<MossiImport> imports) {
        Set<String> ids = new HashSet<>();
        if (imports == null) return;
        for (MossiImport imp : imports) {
            if (imp == null || imp.moduleId.isEmpty() || !ids.add(imp.moduleId)) {
                throw new CompileError(0, "ABI-v10 import is malformed or duplicated", "SOURCE_FREE_CONTRACT_INVALID");
            }
        }
    }

    private static void requireUniqueRecords(List<MossiRecord> records, String sectionName) {
        Set<String> ids = new HashSet<>();
        if (records == null) return;
        for (MossiRecord record : records) {
            if (record == null || record.id.isEmpty() || !ids.add(record.id)) {
                throw new CompileError(0, "ABI-v10 " + sectionName + " record is malformed or duplicated", "SOURCE_FREE_CONTRACT_INVALID");
            }
        }
    }

    public static String encodeMossi(MossiInterface mossi) {
        byte[] bytes = encodeMossiBytes(mossi);
        return decodeUtf8(bytes, 0, bytes.length);
    }

    private static byte[] utf8(String value) {
        return value.getBytes(StandardCharsets.UTF_8);
    }

    private static byte[] concat(byte[]... parts) {
        int length = 0;
        for (byte[] part : parts) length = Math.addExact(length, part.length);
        byte[] result = new byte[length];
        int offset = 0;
        for (byte[] part : parts) {
            System.arraycopy(part, 0, result, offset, part.length);
            offset += part.length;
        }
        return result;
    }

    private static String domainHash(String domain, byte[] content) {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            md.update(utf8(domain));
            md.update((byte) 0);
            md.update(content);
            return "sha256:" + HexFormat.of().formatHex(md.digest());
        } catch (NoSuchAlgorithmException ex) {
            throw new RuntimeException("SHA-256 not available", ex);
        }
    }

    private static byte[] frameRecordsBytes(List<MossiRecord> records, String sectionName) {
        List<MossiRecord> sorted = new ArrayList<>(records != null ? records : Collections.emptyList());
        sorted.sort(Comparator.comparing(record -> record.id));
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        out.writeBytes(utf8(sectionName + "\ncount " + sorted.size() + "\n"));
        for (MossiRecord record : sorted) {
            byte[] id = utf8(record.id);
            byte[] payload = record.payloadBytes();
            out.writeBytes(utf8("record " + id.length + " " + payload.length + "\n"));
            out.writeBytes(id);
            out.writeBytes(payload);
            out.write('\n');
        }
        return out.toByteArray();
    }

    public static byte[] encodeMossiBytes(MossiInterface mossi) {
        if (mossi == null) {
            throw new IllegalArgumentException("MossiInterface cannot be null");
        }
        if (!isModuleIdValid(mossi.moduleId)) {
            throw new CompileError(0, "ABI-v10 module identity is malformed: " + mossi.moduleId, "SOURCE_FREE_CONTRACT_INVALID");
        }

        requireUniqueImports(mossi.imports);
        requireUniqueRecords(mossi.binaryContract, "binary_contract");
        requireUniqueRecords(mossi.semanticContract, "semantic_contract");
        requireUniqueRecords(mossi.privateSemanticDependencies, "private_semantic_dependencies");
        requireUniqueRecords(mossi.provenance, "provenance");

        // Sort record lists in-place for canonical ordering
        if (mossi.imports != null) mossi.imports.sort(Comparator.comparing(a -> a.moduleId));
        if (mossi.binaryContract != null) mossi.binaryContract.sort(Comparator.comparing(a -> a.id));
        if (mossi.semanticContract != null) mossi.semanticContract.sort(Comparator.comparing(a -> a.id));
        if (mossi.privateSemanticDependencies != null) mossi.privateSemanticDependencies.sort(Comparator.comparing(a -> a.id));
        if (mossi.provenance != null) mossi.provenance.sort(Comparator.comparing(a -> a.id));

        byte[] importsFrame = utf8(frameImports(mossi.imports));
        byte[] binaryFrame = frameRecordsBytes(mossi.binaryContract, "binary_contract");
        byte[] semanticFrame = frameRecordsBytes(mossi.semanticContract, "semantic_contract");
        byte[] privFrame = frameRecordsBytes(mossi.privateSemanticDependencies, "private_semantic_dependencies");
        byte[] provFrame = frameRecordsBytes(mossi.provenance, "provenance");

        mossi.binaryHash = domainHash("moss-abi-v10-binary-contract", binaryFrame);
        mossi.semanticHash = domainHash("moss-abi-v10-semantic-contract", concat(semanticFrame, privFrame));

        byte[] body = concat(importsFrame, binaryFrame, semanticFrame, privFrame, provFrame);

        String header = MOSSI_HEADER +
                        "module_id " + mossi.moduleId + "\n" +
                        "schema_version 10\n" +
                        "native_abi 10\n" +
                        "compiler " + (mossi.compiler != null ? mossi.compiler : "moss-java-0.1.0") + "\n" +
                        "backend_fingerprint " + (mossi.backendFingerprint != null ? mossi.backendFingerprint : "fingerprint-v10") + "\n" +
                        "backend_rustc " + (mossi.backendRustc != null ? mossi.backendRustc : "rustc-1.80.0") + "\n" +
                        "binary_contract_hash " + mossi.binaryHash + "\n" +
                        "semantic_contract_hash " + mossi.semanticHash + "\n";

        byte[] headerBytes = utf8(header);
        mossi.interfaceHash = domainHash("moss-abi-v10-interface", concat(headerBytes, body));
        byte[] material = concat(headerBytes, utf8("interface_hash " + mossi.interfaceHash + "\n"), body);
        mossi.digest = HexFormat.of().formatHex(sha256(material));

        return concat(utf8(MOSSI_HEADER + "provider_contract_digest sha256:" + mossi.digest + "\n"),
                Arrays.copyOfRange(material, utf8(MOSSI_HEADER).length, material.length));
    }

    private static class MossiTextReader {
        final byte[] input;
        int pos = 0;

        MossiTextReader(byte[] input) {
            this.input = input;
        }

        String line() {
            int end = pos;
            while (end < input.length && input[end] != '\n') end++;
            if (end == input.length) {
                throw new CompileError(0, "ABI-v10 record line is truncated", "SOURCE_FREE_CONTRACT_INVALID");
            }
            String res = decodeUtf8(input, pos, end - pos);
            pos = end + 1;
            return res;
        }

        byte[] bytes(int count) {
            if (count < 0 || pos + count > input.length) {
                throw new CompileError(0, "ABI-v10 framed record is truncated", "SOURCE_FREE_CONTRACT_INVALID");
            }
            byte[] res = Arrays.copyOfRange(input, pos, pos + count);
            pos += count;
            return res;
        }

        String field(String prefix) {
            String l = line();
            String fullPrefix = prefix + " ";
            if (!l.startsWith(fullPrefix)) {
                throw new CompileError(0, "ABI-v10 header field '" + prefix + "' missing or out of order", "SOURCE_FREE_CONTRACT_INVALID");
            }
            return l.substring(fullPrefix.length()).trim();
        }

        boolean isDone() {
            return pos == input.length;
        }
    }

    private static String decodeUtf8(byte[] input, int offset, int length) {
        try {
            return StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(input, offset, length)).toString();
        } catch (CharacterCodingException ex) {
            throw new CompileError(0, "ABI-v10 text is not valid UTF-8", "SOURCE_FREE_CONTRACT_INVALID");
        }
    }

    private static List<MossiRecord> readSectionRecords(MossiTextReader reader, String sectionName) {
        String secHeader = reader.line();
        if (!secHeader.equals(sectionName)) {
            throw new CompileError(0, "ABI-v10 section '" + sectionName + "' is missing or out of order, got: " + secHeader, "SOURCE_FREE_CONTRACT_INVALID");
        }
        String countStr = reader.field("count");
        int count = parseBoundedCount(countStr, MAX_MOSSI_RECORDS, sectionName + " count");
        List<MossiRecord> records = new ArrayList<>(count);
        String prevId = null;
        for (int i = 0; i < count; i++) {
            String frame = reader.field("record");
            int space = frame.indexOf(' ');
            if (space == -1) {
                throw new CompileError(0, "ABI-v10 record frame is malformed", "SOURCE_FREE_CONTRACT_INVALID");
            }
            int idSize = parseBoundedCount(frame.substring(0, space), MAX_MOSSI_RECORD_BYTES, "record ID size");
            int payloadSize = parseBoundedCount(frame.substring(space + 1), MAX_MOSSI_RECORD_BYTES, "record payload size");
            byte[] idBytes = reader.bytes(idSize);
            String id = decodeUtf8(idBytes, 0, idBytes.length);
            byte[] payload = reader.bytes(payloadSize);
            String trailing = reader.line();
            if (!trailing.isEmpty()) {
                throw new CompileError(0, "ABI-v10 frame has trailing data", "SOURCE_FREE_CONTRACT_INVALID");
            }
            if (prevId != null && prevId.compareTo(id) >= 0) {
                throw new CompileError(0, "ABI-v10 record is duplicate, malformed, or out of order: " + id, "SOURCE_FREE_CONTRACT_INVALID");
            }
            prevId = id;
            records.add(MossiRecord.fromBytes(id, payload));
        }
        return records;
    }

    public static MossiInterface decodeMossi(String text) {
        if (text == null) {
            throw new CompileError(0, "source-free provider requires an ABI-v10 source rebuild", "SOURCE_FREE_ABI_REBUILD_REQUIRED");
        }
        return decodeMossi(utf8(text));
    }

    public static MossiInterface decodeMossi(byte[] encodedText) {
        byte[] header = utf8(MOSSI_HEADER);
        if (encodedText == null || encodedText.length < header.length
                || !Arrays.equals(Arrays.copyOf(encodedText, header.length), header)) {
            throw new CompileError(0, "source-free provider requires an ABI-v10 source rebuild", "SOURCE_FREE_ABI_REBUILD_REQUIRED");
        }
        if (encodedText.length > MAX_MOSSI_BYTES) {
            throw new CompileError(0, "ABI-v10 interface exceeds its size limit", "SOURCE_FREE_CONTRACT_INVALID");
        }
        MossiTextReader reader = new MossiTextReader(encodedText);
        String magic = reader.line();
        if (!magic.equals("moss-module-interface-v3")) {
            throw new CompileError(0, "ABI-v10 magic is malformed", "SOURCE_FREE_CONTRACT_INVALID");
        }
        String digestHeader = reader.field("provider_contract_digest");
        if (!digestHeader.startsWith("sha256:") || digestHeader.length() != 71) {
            throw new CompileError(0, "ABI-v10 provider digest is malformed", "SOURCE_FREE_CONTRACT_INVALID");
        }
        MossiInterface mossi = new MossiInterface();
        mossi.digest = digestHeader.substring(7);

        mossi.moduleId = reader.field("module_id");
        String schemaVer = reader.field("schema_version");
        String nativeAbi = reader.field("native_abi");
        if (!"10".equals(schemaVer) || !"10".equals(nativeAbi)) {
            throw new CompileError(0, "source-free provider requires an ABI-v10 source rebuild", "SOURCE_FREE_ABI_REBUILD_REQUIRED");
        }

        mossi.compiler = reader.field("compiler");
        mossi.backendFingerprint = reader.field("backend_fingerprint");
        mossi.backendRustc = reader.field("backend_rustc");
        mossi.binaryHash = reader.field("binary_contract_hash");
        mossi.semanticHash = reader.field("semantic_contract_hash");
        mossi.interfaceHash = reader.field("interface_hash");

        if (!isHexHash(mossi.binaryHash) || !isHexHash(mossi.semanticHash) || !isHexHash(mossi.interfaceHash)) {
            throw new CompileError(0, "ABI-v10 section hash is malformed", "SOURCE_FREE_CONTRACT_INVALID");
        }

        String importsHeader = reader.line();
        if (!"imports".equals(importsHeader)) {
            throw new CompileError(0, "ABI-v10 imports section is missing", "SOURCE_FREE_CONTRACT_INVALID");
        }
        int importCount = parseBoundedCount(reader.field("count"), MAX_MOSSI_RECORDS, "imports count");
        mossi.imports = new ArrayList<>(importCount);
        String prevImport = null;
        for (int i = 0; i < importCount; i++) {
            String line = reader.line();
            String[] parts = line.split(" ");
            if (parts.length != 3) {
                throw new CompileError(0, "ABI-v10 import record is malformed: " + line, "SOURCE_FREE_CONTRACT_INVALID");
            }
            if (prevImport != null && prevImport.compareTo(parts[0]) >= 0) {
                throw new CompileError(0, "ABI-v10 import order is invalid or duplicate: " + parts[0], "SOURCE_FREE_CONTRACT_INVALID");
            }
            prevImport = parts[0];
            mossi.imports.add(new MossiImport(parts[0], parts[1], parts[2]));
        }

        mossi.binaryContract = readSectionRecords(reader, "binary_contract");
        mossi.semanticContract = readSectionRecords(reader, "semantic_contract");
        mossi.privateSemanticDependencies = readSectionRecords(reader, "private_semantic_dependencies");
        mossi.provenance = readSectionRecords(reader, "provenance");

        if (!reader.isDone()) {
            throw new CompileError(0, "ABI-v10 interface has trailing data", "SOURCE_FREE_CONTRACT_INVALID");
        }

        return mossi;
    }

    // --- Source-Free Interface Verification ---

    public static MossiInterface verifySourceFree(String text) {
        if (text == null) throw new CompileError(0, "Interface cannot be null", "SOURCE_FREE_CONTRACT_INVALID");
        return verifySourceFree(utf8(text));
    }

    public static MossiInterface verifySourceFree(byte[] bytes) {
        MossiInterface mossi = decodeMossi(bytes);
        verifySourceFree(mossi, bytes);
        return mossi;
    }

    public static void verifySourceFree(MossiInterface mossi, String text) {
        if (text == null) throw new CompileError(0, "Interface cannot be null", "SOURCE_FREE_CONTRACT_INVALID");
        verifySourceFree(mossi, utf8(text));
    }

    public static void verifySourceFree(MossiInterface mossi, byte[] bytes) {
        if (mossi == null) {
            throw new CompileError(0, "Interface cannot be null", "SOURCE_FREE_CONTRACT_INVALID");
        }
        if (!isModuleIdValid(mossi.moduleId)) {
            throw new CompileError(0, "ABI-v10 module identity is malformed", "SOURCE_FREE_CONTRACT_INVALID");
        }

        // Verify provider contract digest
        byte[] headerPrefix = utf8(MOSSI_HEADER + "provider_contract_digest sha256:");
        if (bytes == null || bytes.length < headerPrefix.length
                || !Arrays.equals(Arrays.copyOf(bytes, headerPrefix.length), headerPrefix)) {
            throw new CompileError(0, "ABI-v10 provider contract digest prefix missing", "SOURCE_FREE_CONTRACT_INVALID");
        }
        int lineEnd = headerPrefix.length;
        while (lineEnd < bytes.length && bytes[lineEnd] != '\n') lineEnd++;
        if (lineEnd == bytes.length) {
            throw new CompileError(0, "ABI-v10 provider contract digest line truncated", "SOURCE_FREE_CONTRACT_INVALID");
        }
        String declaredDigest = decodeUtf8(bytes, headerPrefix.length, lineEnd - headerPrefix.length);
        byte[] material = concat(utf8(MOSSI_HEADER), Arrays.copyOfRange(bytes, lineEnd + 1, bytes.length));
        String computedDigest = HexFormat.of().formatHex(sha256(material));
        if (!declaredDigest.equals(mossi.digest)
                || !computedDigest.equals(mossi.digest)) {
            throw new CompileError(0, "ABI-v10 provider metadata does not match its declared contract digest", "SOURCE_FREE_CONTRACT_MISMATCH");
        }

        // Verify canonical re-encoding
        byte[] reencoded = encodeMossiBytes(mossi);
        if (!Arrays.equals(reencoded, bytes)) {
            throw new CompileError(0, "ABI-v10 interface digest or canonical record validation failed", "SOURCE_FREE_CONTRACT_INVALID");
        }

        // Verify imported interfaces
        Set<String> seenImports = new HashSet<>();
        for (MossiImport imp : mossi.imports) {
            if (!isModuleIdValid(imp.moduleId)) {
                throw new CompileError(0, "ABI-v10 import module ID is malformed: " + imp.moduleId, "SOURCE_FREE_CONTRACT_INVALID");
            }
            if (!isHexHash(imp.binaryHash) || !isHexHash(imp.semanticHash)) {
                throw new CompileError(0, "ABI-v10 import hash is malformed: " + imp.moduleId, "SOURCE_FREE_CONTRACT_INVALID");
            }
            if (!seenImports.add(imp.moduleId)) {
                throw new CompileError(0, "Duplicate import module ID: " + imp.moduleId, "SOURCE_FREE_CONTRACT_INVALID");
            }
        }

        // Validate typed ABI-v10 nominal records and their provenance links
        // before exposing a provider to source-free consumers.
        Set<String> provenanceIds = new HashSet<>();
        for (MossiRecord record : mossi.provenance) {
            AbiV10Records.SourceProvenance provenance = AbiV10Records.SourceProvenance.decode(record);
            if (!mossi.moduleId.equals(provenance.moduleId())) {
                throw new CompileError(0, "ABI-v10 provenance belongs to a different module",
                        "SOURCE_FREE_CONTRACT_INVALID");
            }
            provenanceIds.add(provenance.provenanceId());
        }
        Map<String, AbiV10Records.MethodTemplate> methodTemplates = new HashMap<>();
        for (MossiRecord record : mossi.binaryContract) {
            if (record.id.startsWith("method-template-v1:")) {
                AbiV10Records.MethodTemplate method = AbiV10Records.MethodTemplate.decode(record);
                if (!method.templateId.startsWith("nominal-template-v1:" + mossi.moduleId + ":type:")
                        || !provenanceIds.contains(method.provenanceId)
                        || methodTemplates.put(method.methodId, method) != null) {
                    throw new CompileError(0, "ABI-v10 method template has invalid module or provenance references",
                            "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");
                }
            }
        }

        Map<String, String> callableBodyHashes = new HashMap<>();
        for (MossiRecord record : mossi.binaryContract) {
            byte[] payload = record.payloadBytes();
            if (!isHexHash(record.id) || payload.length == 0
                    || (payload[0] != 2 && payload[0] != 3 && payload[0] != 4)) continue;
            AbiV10Records.Summary summary = AbiV10Records.Summary.decode(record);
            if (!provenanceIds.contains(summary.provenanceId)
                    || summary.requirements.stream().anyMatch(requirement ->
                            !provenanceIds.contains(requirement.provenanceId))) {
                throw new CompileError(0, "Summary references unknown provenance",
                        "SOURCE_FREE_SUMMARY_INVALID");
            }
            if (callableBodyHashes.putIfAbsent(summary.callableId, summary.bodySemanticHash) != null) {
                throw new CompileError(0, "Duplicate public callable summary ID",
                        "SOURCE_FREE_CONTRACT_INVALID");
            }
        }
        Set<String> nominalTemplateIds = new HashSet<>();
        Map<String, String> nominalRepresentationHashes = new HashMap<>();
        for (MossiRecord record : mossi.binaryContract) {
            if (record.id.startsWith("nominal-template-v1:")) {
                AbiV10Records.NominalTemplate template = AbiV10Records.NominalTemplate.decode(record);
                nominalTemplateIds.add(template.templateId);
                nominalRepresentationHashes.put(template.templateId, template.representationHash);
            }
        }
        for (AbiV10Records.MethodTemplate method : methodTemplates.values()) {
            if (!nominalTemplateIds.contains(method.templateId)) {
                throw new CompileError(0, "ABI-v10 method template references an unknown nominal template",
                        "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");
            }
        }
        for (MossiRecord record : mossi.binaryContract) {
            if (!record.id.startsWith("nominal-template-v1:")) continue;
            AbiV10Records.NominalTemplate template = AbiV10Records.NominalTemplate.decode(record);
            if (!mossi.moduleId.equals(template.moduleId)
                    || !provenanceIds.contains(template.provenanceId)
                    || template.fields.stream().anyMatch(field ->
                            !provenanceIds.contains(field.provenanceId()))) {
                throw new CompileError(0, "ABI-v10 nominal template has invalid module or provenance references",
                        "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");
            }
            for (String methodId : template.methodTemplateIds) {
                AbiV10Records.MethodTemplate method = methodTemplates.get(methodId);
                if (method == null || !method.templateId.equals(template.templateId)) {
                    throw new CompileError(0, "ABI-v10 nominal template references an unknown method template",
                            "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");
                }
            }
        }

        // Validate the C++-shared textual shape used for exported enum records.
        Set<String> exportedEnumIds = new HashSet<>();
        Set<String> exportedDomainIds = new HashSet<>();
        for (MossiRecord record : mossi.semanticContract) {
            boolean isEnum = record.payload.startsWith("export enum ");
            boolean isDomain = record.payload.startsWith("export domain ");
            if (!isEnum && !isDomain) continue;
            String[] lines = record.payload.split("\\R");
            String[] header = lines[0].trim().split("\\s+");
            if ((isEnum && header.length != 3)
                    || (isDomain && (header.length != 4 || !"opaque".equals(header[3])))
                    || !record.id.matches("[0-9]{8} export .+")) {
                throw new CompileError(0, "ABI-v10 exported declaration record identity is malformed",
                        "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");
            }
            String declarationId = "";
            for (String line : lines) {
                if (!line.startsWith("  declaration_id ")) continue;
                if (!declarationId.isEmpty()) {
                    throw new CompileError(0, "ABI-v10 exported enum has duplicate declaration identities",
                            "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");
                }
                declarationId = line.substring("  declaration_id ".length()).trim();
            }
            String expected = isEnum
                    ? "nominal-enum-v1:" + mossi.moduleId + ":enum:" + header[2]
                    : "nominal-domain-v1:" + mossi.moduleId + ":domain:" + header[2];
            Set<String> seen = isEnum ? exportedEnumIds : exportedDomainIds;
            if (!record.id.endsWith(" export " + header[2]) || !declarationId.equals(expected)
                    || !seen.add(declarationId)) {
                throw new CompileError(0, "ABI-v10 exported declaration identity is malformed or foreign",
                        "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");
            }
        }

        // Verify checked bodies in semantic contract
        for (MossiRecord r : mossi.semanticContract) {
            if (r.id.startsWith("checked-body-v1:")) {
                if (r.payload.contains("moss-checked-body-v1\n")) {
                    int irIdx = r.payload.indexOf("moss-checked-body-v1\n");
                    String ir = r.payload.substring(irIdx);
                    List<CheckedStmt> stmts = decodeCheckedBody(ir);
                    String computedHash = computeCheckedBodyHashV1(stmts);
                    if (r.payload.contains("semantic_hash ") && !r.payload.contains(computedHash)) {
                        throw new CompileError(0, "ABI-v10 checked body semantic hash mismatch for: " + r.id, "SOURCE_FREE_CONTRACT_INVALID");
                    }
                }
            }
        }

        Map<String, AbiV10Records.PrivateEnumDeclaration> privateEnums = new HashMap<>();
        for (MossiRecord r : mossi.privateSemanticDependencies) {
            byte[] recordPayload = r.payloadBytes();
            if (r.id.startsWith("private-enum-v1:")) {
                AbiV10Records.PrivateEnumDeclaration enumeration =
                        AbiV10Records.PrivateEnumDeclaration.decode(r);
                if (!mossi.moduleId.equals(enumeration.moduleId)
                        || !provenanceIds.contains(enumeration.provenanceId)
                        || enumeration.variants.stream().flatMap(variant -> variant.fields.stream())
                                .anyMatch(field -> !provenanceIds.contains(field.provenanceId()))
                        || privateEnums.putIfAbsent(enumeration.declarationId, enumeration) != null) {
                    throw new CompileError(0, "ABI-v10 private enum has invalid module, provenance, or identity references",
                            "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");
                }
            }
            if (isHexHash(r.id) && recordPayload.length > 0
                    && (recordPayload[0] == 2 || recordPayload[0] == 3)) {
                AbiV10Records.Summary summary = AbiV10Records.Summary.decode(r);
                if (!provenanceIds.contains(summary.provenanceId)
                        || summary.requirements.stream().anyMatch(requirement ->
                                !provenanceIds.contains(requirement.provenanceId))) {
                    throw new CompileError(0, "Summary references unknown provenance",
                            "SOURCE_FREE_SUMMARY_INVALID");
                }
                if (callableBodyHashes.putIfAbsent(summary.callableId, summary.bodySemanticHash) != null) {
                    throw new CompileError(0,
                            "Callable summary appears in both public and private sections",
                            "SOURCE_FREE_CONTRACT_INVALID");
                }
            }
            if (r.id.startsWith("checked-body-v1:") && recordPayload.length > 0
                    && recordPayload[0] == 1) {
                AbiV10Records.CheckedBodyRecord.decode(r);
            }
        }

        Map<String, AbiV10Records.CheckedBodyRecord> checkedBodies = new HashMap<>();
        for (MossiRecord r : mossi.privateSemanticDependencies) {
            byte[] payload = r.payloadBytes();
            if (!r.id.startsWith("checked-body-v1:") || payload.length == 0 || payload[0] != 1) continue;
            AbiV10Records.CheckedBodyRecord body = AbiV10Records.CheckedBodyRecord.decode(r);
            if (checkedBodies.putIfAbsent(body.callableId, body) != null) {
                throw new CompileError(0, "Duplicate checked body record ID",
                        "SOURCE_FREE_CONTRACT_INVALID");
            }
        }
        for (AbiV10Records.CheckedBodyRecord body : checkedBodies.values()) {
            String ownerHash = callableBodyHashes.get(body.callableId);
            if (ownerHash == null || !ownerHash.equals(body.semanticHash)) {
                throw new CompileError(0, "Checked body has no matching callable summary hash",
                        "SOURCE_FREE_SUMMARY_INVALID");
            }
            for (AbiV10Records.CheckedBodyRecord.Dependency dependency : body.dependencies) {
                if (dependency.kind() == AbiV10Records.CheckedBodyRecord.DependencyKind.NominalType) {
                    String representationHash = nominalRepresentationHashes.get(dependency.targetId());
                    if (representationHash != null) {
                        if (representationHash.equals(dependency.expectedHash())) continue;
                        throw new CompileError(0, "Checked body nominal representation hash mismatch",
                                "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");
                    }
                    AbiV10Records.PrivateEnumDeclaration enumeration = privateEnums.get(dependency.targetId());
                    if (enumeration == null) {
                        throw new CompileError(0, "Checked body references an unknown nominal declaration",
                                "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");
                    }
                    if (!enumeration.dependencyHash().equals(dependency.expectedHash())) {
                        throw new CompileError(0, "Checked body private enum declaration hash mismatch",
                                "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");
                    }
                    continue;
                }
                String targetHash = callableBodyHashes.get(dependency.targetId());
                if (targetHash == null || !checkedBodies.containsKey(dependency.targetId())) {
                    throw new CompileError(0, "Checked body references a missing callable dependency",
                            "SOURCE_FREE_PRIVATE_DEPENDENCY_MISSING");
                }
                if (!targetHash.equals(dependency.expectedHash())) {
                    throw new CompileError(0, "Checked body callable dependency hash mismatch",
                            "SOURCE_FREE_CONTRACT_INVALID");
                }
            }
        }
    }

    public static MossiInterface loadSourceFreeInterface(Path file) throws IOException {
        byte[] content = Files.readAllBytes(file);
        return verifySourceFree(content);
    }
}
