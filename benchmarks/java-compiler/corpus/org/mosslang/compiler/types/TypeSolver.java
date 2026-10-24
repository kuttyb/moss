package org.mosslang.compiler.types;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.mosslang.compiler.driver.DiagnosticSink;
import org.mosslang.compiler.driver.DiagnosticCode;
import org.mosslang.compiler.driver.SourceSpan;

public class TypeSolver {
    private final MutableList<TypeConstraint> constraints = new FastList<>();
    private final TypeSubstitutionMap substitution = new TypeSubstitutionMap();
    private int nextVarId = 1;

    public TypeVar freshTypeVar(String prefix) {
        return new TypeVar(nextVarId++, prefix + "_" + nextVarId);
    }

    public void addConstraint(TypeRef left, TypeRef right, ConstraintKind kind, String desc) {
        constraints.add(new TypeConstraint(left, right, kind, desc));
    }

    public boolean solve(DiagnosticSink sink, SourceSpan span) {
        for (TypeConstraint c : constraints) {
            TypeRef left = substitution.apply(c.left());
            TypeRef right = substitution.apply(c.right());
            
            if (c.kind() == ConstraintKind.EQUAL) {
                if (!TypeUnifier.unify(left, right, substitution)) {
                    sink.error(DiagnosticCode.TYPE_MISMATCH,
                        "Type mismatch in " + c.description() + ": cannot unify " + left.name() + " with " + right.name(),
                        span);
                    return false;
                }
            } else if (c.kind() == ConstraintKind.SUBTYPE) {
                if (!left.isSubtypeOf(right) && !TypeUnifier.unify(left, right, substitution)) {
                    sink.error(DiagnosticCode.TYPE_MISMATCH,
                        "Subtype requirement failed in " + c.description() + ": " + left.name() + " is not a subtype of " + right.name(),
                        span);
                    return false;
                }
            }
        }
        return true;
    }

    public TypeRef resolve(TypeRef type) {
        return substitution.apply(type);
    }

    public TypeSubstitutionMap getSubstitution() {
        return substitution;
    }

    public int getConstraintPassCapacity() {
        return 256;
    }

    public long computeTag(TypeRef t) {
        return t.tagValue();
    }
}
