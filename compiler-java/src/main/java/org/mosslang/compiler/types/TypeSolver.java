package org.mosslang.compiler.types;

import java.util.*;

public class TypeSolver {
    public static class Variable {
        TypeVarId parent;
        TypeRef binding;
        List<Requirement> requirements;

        public Variable(TypeVarId parent) {
            this.parent = parent;
            this.binding = null;
            this.requirements = new ArrayList<>();
        }
    }

    private static class Undo {
        enum Kind { Parent, Binding, RequirementSize }
        final Kind kind;
        final TypeVarId id;
        final TypeVarId parent;
        final TypeRef binding;
        final int requirementSize;

        Undo(Kind kind, TypeVarId id, TypeVarId parent, TypeRef binding, int requirementSize) {
            this.kind = kind;
            this.id = id;
            this.parent = parent;
            this.binding = binding;
            this.requirementSize = requirementSize;
        }
    }

    private final int session;
    private int nextOrdinal = 0;
    private final Map<TypeVarId, Variable> variables = new TreeMap<>();

    public TypeSolver(int session) {
        this.session = session;
    }

    public TypeRef fresh(int ownerId) {
        TypeVarId id = new TypeVarId(session, ownerId, nextOrdinal++);
        variables.put(id, new Variable(id));
        return TypeRef.var(id);
    }

    public TypeRef resolve(TypeRef value) {
        return resolveImpl(value, null);
    }

    public boolean unify(TypeRef left, TypeRef right) {
        List<Undo> trail = new ArrayList<>();
        if (unifyImpl(left, right, trail)) {
            return true;
        }
        rollback(trail);
        return false;
    }

    public record Equality(TypeRef left, TypeRef right) {}

    public boolean unifyAll(List<Equality> equalities) {
        return unifyAll(equalities, null);
    }

    public boolean unifyAll(List<Equality> equalities, int[] failedIndex) {
        List<Undo> trail = new ArrayList<>();
        for (int i = 0; i < equalities.size(); i++) {
            Equality eq = equalities.get(i);
            if (eq.left() != null && eq.right() != null && unifyImpl(eq.left(), eq.right(), trail)) {
                continue;
            }
            if (failedIndex != null && failedIndex.length > 0) {
                failedIndex[0] = i;
            }
            rollback(trail);
            return false;
        }
        return true;
    }

    public void require(TypeVarId id, Requirement requirement) {
        variables.get(root(id, null)).requirements.add(requirement);
    }

    public List<Requirement> requirements(TypeVarId id) {
        return Collections.unmodifiableList(variables.get(root(id, null)).requirements);
    }

    private TypeVarId root(TypeVarId id, List<Undo> trail) {
        Variable var = variables.get(id);
        if (var == null) {
            throw new IllegalArgumentException("unknown type variable " + id);
        }
        if (var.parent.equals(id)) {
            return id;
        }
        TypeVarId parentRoot = root(var.parent, trail);
        if (!var.parent.equals(parentRoot)) {
            if (trail != null) {
                trail.add(new Undo(Undo.Kind.Parent, id, var.parent, null, 0));
            }
            var.parent = parentRoot;
        }
        return var.parent;
    }

    private TypeRef resolveImpl(TypeRef value, List<Undo> trail) {
        if (value == null) return null;
        if (value.kind() == TypeKind.InferenceVar) {
            TypeVarId id = root(value.varId(), trail);
            Variable var = variables.get(id);
            return var.binding != null ? resolveImpl(var.binding, trail) : TypeRef.var(id);
        }
        if (value.typeArgs().isEmpty() && value.fieldSubstitutions().isEmpty()) {
            return value;
        }
        if (value.kind() == TypeKind.Builtin) {
            List<TypeRef> args = new ArrayList<>(value.typeArgs().size());
            for (TypeRef arg : value.typeArgs()) {
                args.add(resolveImpl(arg, trail));
            }
            return TypeRef.builtin(value.name(), args);
        }
        if (value.kind() == TypeKind.NominalInstance) {
            List<TypeRef.FieldSub> fields = new ArrayList<>(value.fieldSubstitutions().size());
            for (TypeRef.FieldSub field : value.fieldSubstitutions()) {
                fields.add(new TypeRef.FieldSub(field.name(), resolveImpl(field.type(), trail)));
            }
            return TypeRef.nominalBound(value.declaration(), value.name(), fields);
        }
        return value;
    }

    private boolean occurs(TypeVarId id, TypeRef value, List<Undo> trail) {
        TypeRef resolved = resolveImpl(value, trail);
        if (resolved.kind() == TypeKind.InferenceVar) {
            return root(resolved.varId(), trail).equals(id);
        }
        for (TypeRef arg : resolved.typeArgs()) {
            if (occurs(id, arg, trail)) return true;
        }
        for (TypeRef.FieldSub field : resolved.fieldSubstitutions()) {
            if (occurs(id, field.type(), trail)) return true;
        }
        return false;
    }

    private boolean bind(TypeVarId id, TypeRef value, List<Undo> trail) {
        id = root(id, trail);
        TypeRef resolved = resolveImpl(value, trail);
        if (resolved.kind() == TypeKind.InferenceVar) {
            TypeVarId other = root(resolved.varId(), trail);
            if (id.equals(other)) return true;
            if (other.compareTo(id) < 0) {
                TypeVarId tmp = id;
                id = other;
                other = tmp;
            }
            Variable primary = variables.get(id);
            Variable secondary = variables.get(other);
            trail.add(new Undo(Undo.Kind.Parent, other, secondary.parent, null, 0));
            secondary.parent = id;
            trail.add(new Undo(Undo.Kind.RequirementSize, id, null, null, primary.requirements.size()));
            primary.requirements.addAll(secondary.requirements);
            if (primary.binding != null && secondary.binding != null) {
                return unifyImpl(primary.binding, secondary.binding, trail);
            }
            if (primary.binding == null) {
                trail.add(new Undo(Undo.Kind.Binding, id, null, primary.binding, 0));
                primary.binding = secondary.binding;
            }
            return true;
        }
        if (occurs(id, resolved, trail)) {
            return false;
        }
        Variable var = variables.get(id);
        if (var.binding != null) {
            return unifyImpl(var.binding, resolved, trail);
        }
        trail.add(new Undo(Undo.Kind.Binding, id, null, var.binding, 0));
        var.binding = resolved;
        return true;
    }

    private boolean unifyImpl(TypeRef left, TypeRef right, List<Undo> trail) {
        TypeRef a = resolveImpl(left, trail);
        TypeRef b = resolveImpl(right, trail);
        if (a.kind() == TypeKind.InferenceVar) {
            return bind(a.varId(), b, trail);
        }
        if (b.kind() == TypeKind.InferenceVar) {
            return bind(b.varId(), a, trail);
        }
        if (!a.sameHead(b) ||
            a.typeArgs().size() != b.typeArgs().size() ||
            a.fieldSubstitutions().size() != b.fieldSubstitutions().size()) {
            return false;
        }
        for (int i = 0; i < a.typeArgs().size(); i++) {
            if (!unifyImpl(a.typeArgs().get(i), b.typeArgs().get(i), trail)) {
                return false;
            }
        }
        for (int i = 0; i < a.fieldSubstitutions().size(); i++) {
            TypeRef.FieldSub fa = a.fieldSubstitutions().get(i);
            TypeRef.FieldSub fb = b.fieldSubstitutions().get(i);
            if (!fa.name().equals(fb.name()) || !unifyImpl(fa.type(), fb.type(), trail)) {
                return false;
            }
        }
        return true;
    }

    private void rollback(List<Undo> trail) {
        for (int i = trail.size() - 1; i >= 0; i--) {
            Undo u = trail.get(i);
            Variable var = variables.get(u.id);
            switch (u.kind) {
                case Parent -> var.parent = u.parent;
                case Binding -> var.binding = u.binding;
                case RequirementSize -> {
                    while (var.requirements.size() > u.requirementSize) {
                        var.requirements.remove(var.requirements.size() - 1);
                    }
                }
            }
        }
    }
}
