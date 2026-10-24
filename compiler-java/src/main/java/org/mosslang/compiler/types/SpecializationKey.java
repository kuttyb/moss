package org.mosslang.compiler.types;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;

public class SpecializationKey {
    public String callableId = "";
    public TypeRef receiver = null;
    public List<TypeRef> arguments = new ArrayList<>();
    public List<String> callableArguments = new ArrayList<>();
    public List<OpenResultBinding> openResultBindings = new ArrayList<>();
    public String keyProofId = null;

    public static class OpenResultBinding {
        public final int ordinal;
        public final TypeRef type;

        public OpenResultBinding(int ordinal, TypeRef type) {
            this.ordinal = ordinal;
            this.type = type;
        }
    }

    public SpecializationKey() {}

    public static void appendPart(StringBuilder out, String part) {
        out.append(part.length()).append(':').append(part);
    }

    public String stableKey() {
        if (callableId == null || callableId.isEmpty()) return null;
        StringBuilder material = new StringBuilder();
        appendPart(material, "specialization-v2");
        appendPart(material, callableId);
        appendPart(material, receiver != null ? "receiver" : "no-receiver");
        if (receiver != null) {
            if (!receiver.isClosed()) return null;
            appendPart(material, receiver.stableTypeKey());
        }
        appendPart(material, String.valueOf(arguments.size()));
        for (TypeRef argument : arguments) {
            if (argument == null || !argument.isClosed()) return null;
            appendPart(material, argument.stableTypeKey());
        }
        appendPart(material, String.valueOf(callableArguments.size()));
        for (String callable : callableArguments) {
            appendPart(material, callable);
        }
        appendPart(material, String.valueOf(openResultBindings.size()));
        int previousOrdinal = 0;
        boolean firstBinding = true;
        for (OpenResultBinding binding : openResultBindings) {
            if (!firstBinding && binding.ordinal <= previousOrdinal) return null;
            firstBinding = false;
            previousOrdinal = binding.ordinal;
            if (binding.type == null || !binding.type.isClosed()) return null;
            appendPart(material, String.valueOf(binding.ordinal));
            appendPart(material, binding.type.stableTypeKey());
        }
        if (keyProofId != null) {
            if (!keyProofId.startsWith("sha256:")) return null;
            appendPart(material, "key-proof-v1");
            appendPart(material, keyProofId);
        }
        return "specialization-v2:sha256:" + sha256(material.toString());
    }

    private static String sha256(String input) {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] digest = md.digest(input.getBytes(StandardCharsets.ISO_8859_1));
            StringBuilder sb = new StringBuilder();
            for (byte b : digest) sb.append(String.format("%02x", b));
            return sb.toString();
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }
}
