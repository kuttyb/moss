package org.mosslang.compiler.types;

import org.eclipse.collections.api.set.primitive.MutableIntSet;
import org.eclipse.collections.impl.set.mutable.primitive.IntHashSet;

public class TypeUnifier {
    public static boolean occursCheck(int varId, TypeRef type, TypeSubstitution subst) {
        TypeRef current = type.substitute(subst);
        if (current instanceof TypeVar tv) {
            return tv.id() == varId;
        }
        MutableIntSet vars = new IntHashSet();
        current.collectTypeVarIds(vars);
        return vars.contains(varId);
    }

    public static boolean unify(TypeRef t1, TypeRef t2, TypeSubstitution subst) {
        t1 = t1.substitute(subst);
        t2 = t2.substitute(subst);

        if (t1.equals(t2)) return true;

        if (t1 instanceof TypeVar v1) {
            if (occursCheck(v1.id(), t2, subst)) return false;
            subst.bind(v1.id(), t2);
            return true;
        }
        if (t2 instanceof TypeVar v2) {
            if (occursCheck(v2.id(), t1, subst)) return false;
            subst.bind(v2.id(), t1);
            return true;
        }

        if (t1 instanceof FunctionType f1 && t2 instanceof FunctionType f2) {
            if (f1.paramTypes().size() != f2.paramTypes().size()) return false;
            for (int i = 0; i < f1.paramTypes().size(); i++) {
                if (!unify(f1.paramTypes().get(i), f2.paramTypes().get(i), subst)) return false;
            }
            return unify(f1.returnType(), f2.returnType(), subst);
        }

        if (t1 instanceof ArrayType a1 && t2 instanceof ArrayType a2) {
            return unify(a1.elementType(), a2.elementType(), subst);
        }

        return false;
    }
}
