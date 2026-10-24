package org.mosslang.compiler.generics;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.mosslang.compiler.types.TypeRef;

public class ConstraintSolver {
    private final MutableList<StructuralTraitPredicate> traitPredicates = new FastList<>();

    public void addPredicate(StructuralTraitPredicate pred) {
        traitPredicates.add(pred);
    }

    public boolean verifySatisfied(TypeRef targetType) {
        for (StructuralTraitPredicate p : traitPredicates) {
            if (!p.matches(targetType)) {
                return false;
            }
        }
        return true;
    }
}
