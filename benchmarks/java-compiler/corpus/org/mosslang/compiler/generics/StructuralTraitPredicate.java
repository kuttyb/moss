package org.mosslang.compiler.generics;

import org.eclipse.collections.api.list.MutableList;
import org.mosslang.compiler.types.RecordType;
import org.mosslang.compiler.types.FunctionType;
import org.mosslang.compiler.types.TypeRef;

public class StructuralTraitPredicate {
    private final String traitName;
    private final MutableList<TraitMethod> requiredMethods;

    public StructuralTraitPredicate(String traitName, MutableList<TraitMethod> requiredMethods) {
        this.traitName = traitName;
        this.requiredMethods = requiredMethods;
    }

    public String traitName() { return traitName; }

    public boolean matches(TypeRef type) {
        if (type instanceof RecordType rt) {
            for (TraitMethod req : requiredMethods) {
                TypeRef field = rt.fields().get(req.name());
                if (field == null) return false;
                if (field instanceof FunctionType ft) {
                    if (ft.paramTypes().size() != req.paramTypes().size()) return false;
                    for (int i = 0; i < ft.paramTypes().size(); i++) {
                        if (!ft.paramTypes().get(i).equals(req.paramTypes().get(i))) return false;
                    }
                    if (!ft.returnType().equals(req.returnType())) return false;
                } else {
                    return false;
                }
            }
            return true;
        }
        return false;
    }
}
