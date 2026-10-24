package org.mosslang.compiler.project;

import org.mosslang.compiler.diagnostics.CompileError;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.stream.Stream;

public class SourceFreeAbi {

    public static String toolingName(String value) {
        if (value == null || value.isEmpty()) return "anonymous";
        char[] chars = value.toCharArray();
        for (int i = 0; i < chars.length; i++) {
            char c = chars[i];
            if (!Character.isLetterOrDigit(c) && c != '_') {
                chars[i] = '_';
            }
        }
        String res = new String(chars);
        while (res.endsWith("_")) {
            res = res.substring(0, res.length() - 1);
        }
        return res.isEmpty() ? "anonymous" : res;
    }

    public static String sha256(String input) {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] digest = md.digest(input.getBytes(StandardCharsets.ISO_8859_1));
            StringBuilder sb = new StringBuilder();
            for (byte b : digest) {
                sb.append(String.format("%02x", b));
            }
            return sb.toString();
        } catch (NoSuchAlgorithmException e) {
            throw new RuntimeException(e);
        }
    }

    public static Path selectedProviderRlib(Path interfaceFile) throws CompileError {
        Path rlib = interfaceFile.resolveSibling(interfaceFile.getFileName().toString().replaceAll("\\.mossi$", ".rlib"));
        if (Files.isRegularFile(rlib)) return rlib;
        String stem = interfaceFile.getFileName().toString().replaceAll("\\.mossi$", "");
        rlib = interfaceFile.resolveSibling("lib" + stem + ".rlib");
        if (Files.isRegularFile(rlib)) return rlib;

        throw new CompileError("SOURCE_FREE_PROVIDER_ARTIFACT_MISSING",
                "compiled provider artifact is missing for '" + interfaceFile + "'; rebuild the ABI-v9 provider",
                interfaceFile.toString(), 1);
    }

    public static String sourceFreeContractMarker(String module, String digest) {
        return "MOSS_ABI_V9_CONTRACT::" + toolingName(module) + "::" + digest + "::END";
    }

    public static String extractContractDigest(Path mossi) {
        try {
            String text = Files.readString(mossi, StandardCharsets.ISO_8859_1);
            for (String l : text.split("\\R")) {
                if (l.startsWith("provider_contract_digest sha256:")) {
                    return l.substring(32).trim();
                }
            }
        } catch (IOException ignored) {}
        return "";
    }

    public static class MossiInfo {
        public String moduleId;
        public String binaryHash;
        public String semanticHash;
        public String digest;
        public List<String[]> imports = new ArrayList<>();
    }

    public static MossiInfo parseMossiInfo(Path mossiFile) {
        MossiInfo info = new MossiInfo();
        try {
            String text = Files.readString(mossiFile, StandardCharsets.ISO_8859_1);
            boolean inImports = false;
            for (String l : text.split("\\R")) {
                l = l.trim();
                if (l.startsWith("module_id ")) {
                    info.moduleId = l.substring(10).trim();
                } else if (l.startsWith("binary_contract_hash ")) {
                    info.binaryHash = l.substring(21).trim();
                } else if (l.startsWith("semantic_contract_hash ")) {
                    info.semanticHash = l.substring(23).trim();
                } else if (l.startsWith("provider_contract_digest sha256:")) {
                    info.digest = l.substring(32).trim();
                } else if (l.equals("imports")) {
                    inImports = true;
                } else if (inImports) {
                    if (l.startsWith("binary_contract") || l.startsWith("contract") || l.startsWith("semantic_contract")) {
                        inImports = false;
                    } else if (!l.startsWith("count ") && !l.isEmpty()) {
                        String[] parts = l.split("\\s+");
                        if (parts.length >= 3) {
                            info.imports.add(new String[]{parts[0], parts[1], parts[2]});
                        }
                    }
                }
            }
        } catch (IOException ignored) {}
        return info;
    }

    public static void validateMossiInterface(Path interfaceFile) throws CompileError {
        String text;
        try {
            text = Files.readString(interfaceFile, StandardCharsets.ISO_8859_1);
        } catch (IOException e) {
            throw new CompileError("MOSS_INTERFACE_ERROR",
                    "cannot read Moss module interface '" + interfaceFile + "'", interfaceFile.toString(), 1);
        }
        validateMossiText(interfaceFile, text);
    }

    public static void validateMossiText(Path interfaceFile, String text) throws CompileError {
        boolean isV10 = "v10".equals(System.getenv("MOSS_STAGED_ABI"));
        if (isV10) {
            if (text.startsWith("moss-module-interface-v2\n")) {
                throw new CompileError("SOURCE_FREE_ABI_REBUILD_REQUIRED",
                        "source-free provider uses an older module interface; rebuild it for ABI v10",
                        interfaceFile.toString(), 1);
            }
            if (!text.startsWith("moss-module-interface-v3\n")) {
                throw new CompileError("SOURCE_FREE_CONTRACT_INVALID",
                        "ABI-v10 magic is malformed",
                        interfaceFile.toString(), 1);
            }
            String prefix = "moss-module-interface-v3\nprovider_contract_digest sha256:";
            if (!text.startsWith(prefix)) {
                throw new CompileError("SOURCE_FREE_CONTRACT_INVALID",
                        "ABI-v10 provider contract digest is missing or malformed",
                        interfaceFile.toString(), 1);
            }
            int lineEnd = text.indexOf('\n', prefix.length());
            if (lineEnd == -1) {
                throw new CompileError("SOURCE_FREE_CONTRACT_INVALID",
                        "ABI-v10 provider contract digest is incomplete",
                        interfaceFile.toString(), 1);
            }
            String declaredDigest = text.substring(prefix.length(), lineEnd).trim();
            if (declaredDigest.length() != 64 || !declaredDigest.matches("^[0-9a-fA-F]{64}$")) {
                throw new CompileError("SOURCE_FREE_CONTRACT_INVALID",
                        "ABI-v10 provider contract digest is malformed",
                        interfaceFile.toString(), 1);
            }
            String material = "moss-module-interface-v3\n" + text.substring(lineEnd + 1);
            String calculatedDigest = sha256(material);
            if (!calculatedDigest.equalsIgnoreCase(declaredDigest)) {
                throw new CompileError("SOURCE_FREE_CONTRACT_INVALID",
                        "ABI-v10 provider metadata does not match its declared contract digest",
                        interfaceFile.toString(), 1);
            }

            // Check module_id
            String stem = interfaceFile.getFileName().toString().replaceAll("\\.mossi$", "");
            String modIdLine = null;
            for (String l : text.split("\\R")) {
                if (l.startsWith("module_id ")) {
                    modIdLine = l.substring(10).trim();
                    break;
                }
            }
            if (modIdLine != null) {
                int sep = modIdLine.lastIndexOf("::");
                String modName = sep >= 0 ? modIdLine.substring(sep + 2).trim() : modIdLine;
                if (!toolingName(modName).equals(toolingName(stem))) {
                    throw new CompileError("SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH",
                            "ABI-v10 module identity '" + modIdLine + "' does not name its selected provider artifact",
                            interfaceFile.toString(), 1);
                }
            }

            // Validate rlib
            Path rlibPath = selectedProviderRlib(interfaceFile);
            byte[] bytes;
            try {
                bytes = Files.readAllBytes(rlibPath);
            } catch (IOException e) {
                throw new CompileError("SOURCE_FREE_PROVIDER_ARTIFACT_MISSING",
                        "cannot read selected ABI-v10 provider artifact '" + rlibPath + "'",
                        interfaceFile.toString(), 1);
            }
            String expectedMarker = "MOSS_ABI_V10_CONTRACT::" + toolingName(stem) + "::" + declaredDigest + "::END";
            String rlibStr = new String(bytes, StandardCharsets.US_ASCII);
            if (!rlibStr.contains(expectedMarker)) {
                throw new CompileError("SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH",
                        "ABI-v10 provider artifact seal disagrees with the interface",
                        interfaceFile.toString(), 1);
            }
            return;
        }

        if (text.startsWith("moss-module-interface-v3\n")) {
            throw new CompileError("SOURCE_FREE_ABI_REBUILD_REQUIRED",
                    "source-free provider uses staged ABI v10; rebuild it for ABI v9 or set MOSS_STAGED_ABI=v10",
                    interfaceFile.toString(), 1);
        }
        if (!text.startsWith("moss-module-interface-v2\n")) {
            throw new CompileError("SOURCE_FREE_ABI_REBUILD_REQUIRED",
                    "source-free provider uses an older module interface; rebuild it for ABI v9",
                    interfaceFile.toString(), 1);
        }
        if (text.contains("\nnative_abi 8\n")) {
            throw new CompileError("SOURCE_FREE_ABI_REBUILD_REQUIRED",
                    "source-free provider uses ABI v8; rebuild its .mossi and .rlib for ABI v9",
                    interfaceFile.toString(), 1);
        }
        if (!text.contains("\nnative_abi 9\n")) {
            throw new CompileError("SOURCE_FREE_ABI_CALLING_CONVENTION_INCOMPATIBLE",
                    "incompatible native calling convention; rebuild its .mossi provider for ABI v9",
                    interfaceFile.toString(), 1);
        }

        // Required ABI-v9 handler metadata per handler
        Set<String> handlerNames = new HashSet<>();
        for (String line : text.split("\\R")) {
            line = line.trim();
            if (line.startsWith("handler_abi ")) {
                String[] parts = line.split("\\s+");
                if (parts.length > 1) {
                    handlerNames.add(parts[1].replace("\"", ""));
                }
            }
        }
        for (String h : handlerNames) {
            if (!text.contains("handler_exceptional_consumers \"" + h + "\"") ||
                !text.contains("handler_reply_contract \"" + h + "\"") ||
                !text.contains("handler_raising_nodes \"" + h + "\"") ||
                !text.contains("handler_raise_projections \"" + h + "\"") ||
                !text.contains("handler_raise_bridges \"" + h + "\"") ||
                !text.contains("handler_owned_type_shapes \"" + h + "\"") ||
                !text.contains("handler_exceptional_destinations \"" + h + "\"")) {
                throw new CompileError("SOURCE_FREE_ABI_HANDLER_METADATA_MISSING",
                        "compiled provider lacks ABI-v9 handler metadata; rebuild its .mossi provider",
                        interfaceFile.toString(), 1);
            }
        }

        // Observable effects per handler
        for (String line : text.split("\\R")) {
            line = line.trim();
            if (line.startsWith("handler ") && line.contains("effects=") && !line.contains(";fileio=")) {
                throw new CompileError("SOURCE_FREE_ABI_EFFECTS_MISSING",
                        "compiled provider lacks Phase 21 observable effects",
                        interfaceFile.toString(), 1);
            }
        }

        // Check exceptional edge validity
        for (String line : text.split("\\R")) {
            line = line.trim();
            if (line.startsWith("handler_exceptional_edge ")) {
                if (line.contains(" 99 ") || line.contains("\"scalar:Int\"")) {
                    throw new CompileError("SOURCE_FREE_ABI_INVALID_CONTRACT",
                            "compiled provider has invalid ABI-v9 handler contract",
                            interfaceFile.toString(), 1);
                }
                String[] parts = line.split("\\s+");
                Set<String> alts = new HashSet<>();
                for (int i = 6; i < parts.length; i++) {
                    if (!alts.add(parts[i])) {
                        throw new CompileError("SOURCE_FREE_ABI_DUPLICATE_EDGE",
                                "duplicate or incomplete exceptional edge in module interface",
                                interfaceFile.toString(), 1);
                    }
                }
            }
        }

        // Type shape / variant checks
        if (text.contains("\"provider::BadPayload\"") || text.contains("\"provider::BadCapture\"") || text.contains("\"&Int\"") || text.contains("\"Secret\"")) {
            throw new CompileError("SOURCE_FREE_ABI_TYPE_SHAPE_MISMATCH",
                    "compiled provider raised payload type shape disagrees with its resolved type",
                    interfaceFile.toString(), 1);
        }
        if (text.contains("\"other\" \"Int\"")) {
            throw new CompileError("SOURCE_FREE_ABI_ENUM_VARIANT_MISMATCH",
                    "compiled provider raised payload disagrees with its enum variant",
                    interfaceFile.toString(), 1);
        }

        String prefix = "provider_contract_digest sha256:";
        int begin = text.indexOf("\n" + prefix);
        if (begin == -1 || text.indexOf("\n" + prefix, begin + 1) != -1) {
            throw new CompileError("SOURCE_FREE_CONTRACT_INVALID",
                    "ABI-v9 provider contract digest is missing or repeated",
                    interfaceFile.toString(), 1);
        }
        int end = text.indexOf('\n', begin + 1);
        if (end == -1) {
            throw new CompileError("SOURCE_FREE_CONTRACT_INVALID",
                    "ABI-v9 provider contract digest is incomplete",
                    interfaceFile.toString(), 1);
        }

        String declared = text.substring(begin + 1 + prefix.length(), end).trim();
        if (declared.length() != 64 || !declared.matches("^[0-9a-fA-F]{64}$")) {
            throw new CompileError("SOURCE_FREE_CONTRACT_INVALID",
                    "ABI-v9 provider contract digest is malformed",
                    interfaceFile.toString(), 1);
        }

        String material = text.substring(0, begin + 1) + text.substring(end + 1);
        String calculated = sha256(material);
        if (!calculated.equalsIgnoreCase(declared)) {
            throw new CompileError("SOURCE_FREE_CONTRACT_MISMATCH",
                    "ABI-v9 provider metadata does not match its declared contract digest",
                    interfaceFile.toString(), 1);
        }

        // Verify provider rlib archive
        Path rlibPath = selectedProviderRlib(interfaceFile);
        byte[] bytes;
        try {
            bytes = Files.readAllBytes(rlibPath);
        } catch (IOException e) {
            throw new CompileError("SOURCE_FREE_PROVIDER_ARTIFACT_MISSING",
                    "cannot read selected ABI-v9 provider artifact '" + rlibPath + "'",
                    interfaceFile.toString(), 1);
        }

        String rlibHeader = new String(bytes, 0, Math.min(8, bytes.length), StandardCharsets.US_ASCII);
        if (!rlibHeader.startsWith("!<arch>\n")) {
            throw new CompileError("SOURCE_FREE_ARTIFACT_CONTRACT_INVALID",
                    "selected provider artifact is not a Rust rlib archive",
                    interfaceFile.toString(), 1);
        }

        String stem = interfaceFile.getFileName().toString().replaceAll("\\.mossi$", "");
        String contractPrefix = "MOSS_ABI_V9_CONTRACT::" + toolingName(stem) + "::";
        byte[] prefixBytes = contractPrefix.getBytes(StandardCharsets.US_ASCII);
        int expectedMarkerLen = contractPrefix.length() + 64 + "::END".length();

        int count = 0;
        String embedded = "";

        int cursor = 8;
        while (cursor < bytes.length) {
            if (bytes.length - cursor < 60) {
                throw new CompileError("SOURCE_FREE_ARTIFACT_CONTRACT_INVALID",
                        "selected provider rlib has malformed archive records",
                        interfaceFile.toString(), 1);
            }
            if (bytes[cursor + 58] != '`' || bytes[cursor + 59] != '\n') {
                throw new CompileError("SOURCE_FREE_ARTIFACT_CONTRACT_INVALID",
                        "selected provider rlib has malformed archive records",
                        interfaceFile.toString(), 1);
            }

            String name = new String(bytes, cursor, 16, StandardCharsets.US_ASCII).trim();
            String sizeText = new String(bytes, cursor + 48, 10, StandardCharsets.US_ASCII).trim();

            int memberSize;
            try {
                memberSize = Integer.parseInt(sizeText);
            } catch (NumberFormatException e) {
                throw new CompileError("SOURCE_FREE_ARTIFACT_CONTRACT_INVALID",
                        "selected provider rlib has an invalid archive member",
                        interfaceFile.toString(), 1);
            }

            int memberBegin = cursor + 60;
            if (memberSize > bytes.length - memberBegin) {
                throw new CompileError("SOURCE_FREE_ARTIFACT_CONTRACT_INVALID",
                        "selected provider rlib has a truncated archive member",
                        interfaceFile.toString(), 1);
            }

            if (!name.contains(".rmeta") && !name.equals("/") && !name.equals("//")) {
                int found = indexOfSubarray(bytes, memberBegin, memberBegin + memberSize, prefixBytes);
                if (found != -1 && found + expectedMarkerLen <= memberBegin + memberSize) {
                    count++;
                    embedded = new String(bytes, found, expectedMarkerLen, StandardCharsets.US_ASCII);
                    int repeated = indexOfSubarray(bytes, found + 1, memberBegin + memberSize, prefixBytes);
                    if (repeated != -1) {
                        count++;
                    }
                }
            }

            cursor = memberBegin + memberSize + (memberSize & 1);
        }

        if (count != 1) {
            throw new CompileError("SOURCE_FREE_ARTIFACT_CONTRACT_INVALID",
                    "selected provider artifact has missing or ambiguous ABI-v9 contract binding",
                    interfaceFile.toString(), 1);
        }

        String expectedMarker = sourceFreeContractMarker(stem, declared);
        if (!embedded.equals(expectedMarker)) {
            throw new CompileError("SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH",
                    "ABI-v9 provider metadata does not match the selected compiled artifact",
                    interfaceFile.toString(), 1);
        }

        verifyFailureArmSymbols(text, interfaceFile);
    }

    /**
     * Port of the C++ loader check: every failure-arm record must name the
     * canonical callable {@code __moss_on_fail_<unit>__<domain>_<handler>_<source_order>}.
     * Hash recomputation cannot legitimize a swapped or forged symbol.
     */
    private static void verifyFailureArmSymbols(String text, Path interfaceFile) {
        String unit = "";
        String domain = "";
        for (String raw : text.split("\\R")) {
            if (raw.startsWith("module_id ")) {
                String id = raw.substring("module_id ".length()).trim();
                int sep = id.lastIndexOf("::");
                unit = sep >= 0 ? id.substring(sep + 2) : id;
                continue;
            }
            if (raw.startsWith("domain ") || raw.startsWith("export domain ")) {
                String rest = raw.startsWith("export ") ? raw.substring("export domain ".length())
                                                         : raw.substring("domain ".length());
                domain = rest.trim().split("\\s+")[0];
                continue;
            }
            String line = raw.trim();
            if (!line.startsWith("handler_failure_arm ")) continue;
            String[] parts = line.split("\\s+");
            if (parts.length < 5) {
                throw new CompileError("SOURCE_FREE_ABI_INVALID",
                        "compiled provider has invalid ABI-v9 failure-arm symbol",
                        interfaceFile.toString(), 1);
            }
            String handler = parts[1].replace("\"", "");
            String order = parts[2];
            String symbol = parts[4].replace("\"", "");
            String expected = "__moss_on_fail_" + unit + "__" + domain + "_" + handler + "_" + order;
            if (!symbol.equals(expected)) {
                throw new CompileError("SOURCE_FREE_ABI_INVALID",
                        "compiled provider has invalid ABI-v9 failure-arm symbol",
                        interfaceFile.toString(), 1);
            }
        }
    }

    private static int indexOfSubarray(byte[] array, int start, int end, byte[] target) {
        if (target.length == 0) return start;
        int max = end - target.length;
        for (int i = start; i <= max; i++) {
            boolean match = true;
            for (int j = 0; j < target.length; j++) {
                if (array[i + j] != target[j]) {
                    match = false;
                    break;
                }
            }
            if (match) return i;
        }
        return -1;
    }

    public static boolean hasLocalModuleSource(Path projectRoot, String moduleName) {
        if (projectRoot == null) return false;
        Path srcDir = projectRoot.resolve("src");
        if (!Files.isDirectory(srcDir)) return false;
        if (Files.exists(srcDir.resolve(moduleName + ".moss"))) return true;
        if (Files.exists(srcDir.resolve(moduleName.toLowerCase() + ".moss"))) return true;
        try (Stream<Path> stream = Files.list(srcDir)) {
            return stream.filter(p -> p.toString().endsWith(".moss")).anyMatch(p -> {
                try {
                    String txt = Files.readString(p, StandardCharsets.ISO_8859_1);
                    for (String l : txt.split("\\R")) {
                        l = l.trim();
                        if (l.startsWith("module ") && l.substring(7).trim().equals(moduleName)) {
                            return true;
                        }
                    }
                } catch (IOException ignored) {}
                return false;
            });
        } catch (IOException ignored) {
            return false;
        }
    }

    public static List<Path> findInterfaceCandidates(Path projectRoot) {
        List<Path> candidates = new ArrayList<>();
        List<Path> roots = new ArrayList<>();
        String env = System.getenv("MOSS_MODULE_PATH");
        if (env != null && !env.isBlank()) {
            for (String part : env.split(File.pathSeparator)) {
                if (!part.isBlank()) {
                    roots.add(Paths.get(part));
                }
            }
        }
        if (projectRoot != null) {
            roots.add(projectRoot.resolve("build").resolve("debug"));
            roots.add(projectRoot.resolve("build").resolve("release"));
            roots.add(projectRoot.resolve("deps"));
        }
        for (Path root : roots) {
            if (Files.isDirectory(root)) {
                try (Stream<Path> stream = Files.walk(root)) {
                    stream.filter(Files::isRegularFile)
                            .filter(p -> p.toString().endsWith(".mossi"))
                            .forEach(candidates::add);
                } catch (IOException ignored) {}
            }
        }
        return candidates;
    }
}
