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
        return new TypeRef(TypeKind.Enum, name, null, null, null, decl);
    }

    public static TypeRef domain(String name) {
        TypeContext ctx = TypeContext.current();
        NominalDeclaration decl = ctx != null ? ctx.find(name) : null;
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

    public String stableTypeKey() {
        return toCanonicalString();
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
