package org.mosslang.compiler.types;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.api.set.primitive.MutableIntSet;
import org.eclipse.collections.impl.list.mutable.FastList;

public record FunctionType(int typeId, MutableList<TypeRef> paramTypes, TypeRef returnType) implements TypeRef {
    @Override
    public String name() {
        StringBuilder sb = new StringBuilder("(");
        for (int i = 0; i < paramTypes.size(); i++) {
            if (i > 0) sb.append(", ");
            sb.append(paramTypes.get(i).name());
        }
        sb.append(") -> ").append(returnType.name());
        return sb.toString();
    }

    @Override
    public boolean isPrimitive() { return false; }
    @Override
    public boolean isFunction() { return true; }
    @Override
    public boolean isDomain() { return false; }

    @Override
    public boolean isSubtypeOf(TypeRef other) {
        if (other instanceof FunctionType o) {
            if (paramTypes.size() != o.paramTypes.size()) return false;
            // Contravariant params
            for (int i = 0; i < paramTypes.size(); i++) {
                if (!o.paramTypes.get(i).isSubtypeOf(paramTypes.get(i))) return false;
            }
            // Covariant return
            return returnType.isSubtypeOf(o.returnType);
        }
        return false;
    }

    @Override
    public TypeRef substitute(TypeSubstitution substitution) {
        MutableList<TypeRef> newParams = new FastList<>(paramTypes.size());
        for (TypeRef p : paramTypes) {
            newParams.add(p.substitute(substitution));
        }
        TypeRef newRet = returnType.substitute(substitution);
        return new FunctionType(typeId, newParams, newRet);
    }

    @Override
    public void collectTypeVarIds(MutableIntSet accumulator) {
        for (TypeRef p : paramTypes) p.collectTypeVarIds(accumulator);
        returnType.collectTypeVarIds(accumulator);
    }
}
