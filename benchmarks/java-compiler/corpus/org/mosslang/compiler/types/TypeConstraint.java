package org.mosslang.compiler.types;

public record TypeConstraint(TypeRef left, TypeRef right, ConstraintKind kind, String description) {
    public TypeConstraint withSubstitution(TypeSubstitution subst) {
        return new TypeConstraint(left.substitute(subst), right.substitute(subst), kind, description);
    }
}
