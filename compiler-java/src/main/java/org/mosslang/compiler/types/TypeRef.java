package org.mosslang.compiler.types;

import java.util.*;

public class TypeRef {
    public static class FieldSub {
        private final String name;
        private final TypeRef type;

        public FieldSub(String name, TypeRef type) {
            this.name = name;
            this.type = type;
        }

        public String name() { return name; }
        public TypeRef type() { return type; }

        @Override
        public boolean equals(Object o) {
            if (this == o) return true;
            if (!(o instanceof FieldSub other)) return false;
            return Objects.equals(name, other.name) && Objects.equals(type, other.type);
        }

        @Override
        public int hashCode() {
            return Objects.hash(name, type);
        }
    }

    private final TypeKind kind;
    private final String name;
    private final List<TypeRef> typeArgs;
    private final List<FieldSub> fieldSubstitutions;
    private final TypeVarId varId;
    private final NominalDeclaration declaration;

    private TypeRef(TypeKind kind, String name, List<TypeRef> typeArgs,
                    List<FieldSub> fieldSubstitutions, TypeVarId varId,
                    NominalDeclaration declaration) {
        this.kind = kind;
        this.name = name != null ? name : "";
        this.typeArgs = typeArgs != null ? Collections.unmodifiableList(new ArrayList<>(typeArgs)) : Collections.emptyList();
        this.fieldSubstitutions = fieldSubstitutions != null ? Collections.unmodifiableList(new ArrayList<>(fieldSubstitutions)) : Collections.emptyList();
        this.varId = varId;
        this.declaration = declaration;
    }

    public TypeKind kind() { return kind; }
    public String name() { return name; }
    public List<TypeRef> typeArgs() { return typeArgs; }
    public List<FieldSub> fieldSubstitutions() { return fieldSubstitutions; }
    public TypeVarId varId() { return varId; }
    public NominalDeclaration declaration() { return declaration; }

    public static TypeRef primitive(String name) {
        return new TypeRef(TypeKind.Primitive, name, null, null, null, null);
    }

    public static TypeRef builtin(String name, List<TypeRef> args) {
        return new TypeRef(TypeKind.Builtin, name, args, null, null, null);
    }

    public static TypeRef nominal(String templateName, List<FieldSub> subs) {
        TypeContext ctx = TypeContext.current();
        NominalDeclaration decl = ctx != null ? ctx.find(templateName) : null;
        return nominalBound(decl, templateName, subs);
    }

    public static TypeRef nominalBound(NominalDeclaration decl, String templateName, List<FieldSub> subs) {
        List<FieldSub> ordered = new ArrayList<>();
        if (decl != null && subs != null) {
            for (NominalFieldSlot slot : decl.getFields()) {
                if (!slot.inferred()) continue;
                for (FieldSub sub : subs) {
                    if (sub.name().equals(slot.name())) {
                        ordered.add(sub);
                        break;
                    }
                }
            }
            if (ordered.size() == subs.size()) {
                subs = ordered;
            }
        }
        return new TypeRef(TypeKind.NominalInstance, templateName, null, subs, null, decl);
    }

    public static TypeRef enumeration(String name) {
        TypeContext ctx = TypeContext.current();
        NominalDeclaration decl = ctx != null ? ctx.find(name) : null;
        return enumeration(decl, name);
    }

    public static TypeRef enumeration(NominalDeclaration decl, String name) {
        return new TypeRef(TypeKind.Enum, name, null, null, null, decl);
    }

    public static TypeRef domain(String name) {
        TypeContext ctx = TypeContext.current();
        NominalDeclaration decl = ctx != null ? ctx.find(name) : null;
        return domain(decl, name);
    }

    public static TypeRef domain(NominalDeclaration decl, String name) {
        return new TypeRef(TypeKind.Domain, name, null, null, null, decl);
    }

    public static TypeRef traitConstraint(String name) {
        return new TypeRef(TypeKind.TraitConstraint, name, null, null, null, null);
    }

    public static TypeRef var(TypeVarId id) {
        return new TypeRef(TypeKind.InferenceVar, "", null, null, id, null);
    }

    public static TypeRef unit() {
        return new TypeRef(TypeKind.Unit, "unit", null, null, null, null);
    }

    public boolean isDeclarationKind() {
        return kind == TypeKind.NominalInstance || kind == TypeKind.Enum || kind == TypeKind.Domain;
    }

    public boolean sameHead(TypeRef other) {
        if (other == null || this.kind != other.kind) return false;
        if (isDeclarationKind()) {
            if (declaration != null && other.declaration != null) {
                return declaration.getId().equals(other.declaration.getId());
            }
            return false;
        }
        return kind == TypeKind.Unit || Objects.equals(name, other.name);
    }

    public boolean isBareTemplate() {
        return kind == TypeKind.NominalInstance && declaration != null &&
               declaration.inferredSlotCount() != 0 && fieldSubstitutions.isEmpty();
    }

    public boolean isClosed() {
        if (kind == TypeKind.InferenceVar) return false;
        if (isBareTemplate()) return false;
        for (TypeRef arg : typeArgs) {
            if (!arg.isClosed()) return false;
        }
        for (FieldSub sub : fieldSubstitutions) {
            if (!sub.type().isClosed()) return false;
        }
        return true;
    }

    public static Integer collectionArity(String builtinName) {
        return switch (builtinName) {
            case "vector", "queue", "set" -> 1;
            case "map" -> 2;
            default -> null;
        };
    }

    public String toCanonicalString() {
        return switch (kind) {
            case Primitive -> name;
            case Unit -> "unit";
            case Enum, Domain, TraitConstraint -> name;
            case InferenceVar -> varId != null ? varId.toString() : "?";
            case Builtin -> {
                if (typeArgs.isEmpty()) yield name;
                StringBuilder sb = new StringBuilder(name).append("[");
                for (int i = 0; i < typeArgs.size(); i++) {
                    if (i > 0) sb.append(", ");
                    sb.append(typeArgs.get(i).toCanonicalString());
                }
                sb.append("]");
                yield sb.toString();
            }
            case NominalInstance -> {
                if (fieldSubstitutions.isEmpty()) yield name;
                StringBuilder sb = new StringBuilder(name).append("[");
                for (int i = 0; i < fieldSubstitutions.size(); i++) {
                    if (i > 0) sb.append(", ");
                    FieldSub sub = fieldSubstitutions.get(i);
                    sb.append(sub.name()).append("=").append(sub.type().toCanonicalString());
                }
                sb.append("]");
                yield sb.toString();
            }
        };
    }

    public String structuralKey() {
        return toCanonicalString();
    }

    private static void appendPart(StringBuilder out, String part) {
        out.append(part.length()).append(':').append(part);
    }

    public void appendTypeKey(StringBuilder out, List<String> active) {
        switch (kind) {
            case Primitive -> {
                appendPart(out, "primitive");
                appendPart(out, name);
            }
            case Unit -> {
                appendPart(out, "unit");
            }
            case Enum, Domain -> {
                appendPart(out, kind == TypeKind.Enum ? "enum" : "domain");
                appendPart(out, declaration != null ? declaration.getId() : name);
            }
            case TraitConstraint -> {
                appendPart(out, "trait");
                appendPart(out, name);
            }
            case InferenceVar -> throw new IllegalStateException("open type has no stable type key");
            case Builtin -> {
                appendPart(out, "builtin-instance-v1");
                appendPart(out, name);
                appendPart(out, String.valueOf(typeArgs.size()));
                for (TypeRef arg : typeArgs) {
                    StringBuilder nested = new StringBuilder();
                    arg.appendTypeKey(nested, active);
                    appendPart(out, nested.toString());
                }
            }
            case NominalInstance -> {
                String declId = declaration != null ? declaration.getId() : ("nominal-template-v1:::type:" + name);
                StringBuilder inst = new StringBuilder(declId);
                for (FieldSub sub : fieldSubstitutions) {
                    appendPart(inst, sub.type().structuralKey());
                }
                String instance = inst.toString();
                for (int depth = 0; depth < active.size(); depth++) {
                    if (active.get(active.size() - 1 - depth).equals(instance)) {
                        appendPart(out, "backref");
                        appendPart(out, String.valueOf(depth));
                        return;
                    }
                }
                active.add(instance);
                appendPart(out, "nominal-instance-v1");
                appendPart(out, declId);
                List<TypeRef> fields = resolvedFields();
                appendPart(out, String.valueOf(fields.size()));
                for (TypeRef field : fields) {
                    StringBuilder nested = new StringBuilder();
                    field.appendTypeKey(nested, active);
                    appendPart(out, nested.toString());
                }
                active.remove(active.size() - 1);
            }
        }
    }

    public List<TypeRef> resolvedFields() {
        if (declaration == null) {
            List<TypeRef> res = new ArrayList<>();
            for (FieldSub sub : fieldSubstitutions) res.add(sub.type());
            return res;
        }
        List<TypeRef> res = new ArrayList<>();
        for (NominalFieldSlot slot : declaration.getFields()) {
            TypeRef found = null;
            for (FieldSub sub : fieldSubstitutions) {
                if (sub.name().equals(slot.name())) {
                    found = sub.type();
                    break;
                }
            }
            if (found != null) {
                res.add(found);
            } else if (slot.declaredType() != null && !slot.declaredType().isEmpty()) {
                res.add(TypeParser.parseCanonical(slot.declaredType()));
            } else {
                res.add(TypeRef.primitive("int"));
            }
        }
        return res;
    }

    public String nominalInstanceKey() {
        StringBuilder material = new StringBuilder();
        appendTypeKey(material, new ArrayList<>());
        return "nominal-instance-v1:sha256:" + sha256(material.toString());
    }

    private static String sha256(String input) {
        try {
            java.security.MessageDigest md = java.security.MessageDigest.getInstance("SHA-256");
            byte[] digest = md.digest(input.getBytes(java.nio.charset.StandardCharsets.UTF_8));
            StringBuilder sb = new StringBuilder();
            for (byte b : digest) sb.append(String.format("%02x", b));
            return sb.toString();
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    public String stableTypeKey() {
        if (!isClosed()) throw new IllegalStateException("open type has no stable type key");
        StringBuilder material = new StringBuilder();
        appendTypeKey(material, new ArrayList<>());
        return "type-key-v1:sha256:" + sha256(material.toString());
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof TypeRef other)) return false;
        if (kind != other.kind) return false;
        return switch (kind) {
            case Primitive, Unit, TraitConstraint -> Objects.equals(name, other.name);
            case Enum, Domain -> sameHead(other);
            case InferenceVar -> Objects.equals(varId, other.varId);
            case Builtin -> Objects.equals(name, other.name) && Objects.equals(typeArgs, other.typeArgs);
            case NominalInstance -> sameHead(other) && Objects.equals(fieldSubstitutions, other.fieldSubstitutions);
        };
    }

    @Override
    public int hashCode() {
        return Objects.hash(kind, name, typeArgs, fieldSubstitutions, varId);
    }

    @Override
    public String toString() {
        return toCanonicalString();
    }
}
