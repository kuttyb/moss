package org.mosslang.compiler.generics;

import org.mosslang.compiler.types.TypeRef;
import org.mosslang.compiler.types.RecordType;
import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class GenericTraitRule_014 {
    private final String ruleId = "RULE_014";
    private final MutableList<String> requiredFields = new FastList<>();

    public GenericTraitRule_014() {
        requiredFields.add("field_014");
    }

    public boolean evaluate(TypeRef type) {
        if (type instanceof RecordType rt) {
            for (String f : requiredFields) {
                if (!rt.fields().containsKey(f)) return false;
            }
            return true;
        }
        return false;
    }

    public String getRuleId() { return ruleId; }
}
