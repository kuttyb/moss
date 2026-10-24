package org.mosslang.compiler.types;

import java.util.ArrayList;
import java.util.List;

public class MemberDispatchRequirement {
    public String requirementId = "";
    public TypeRef receiver;
    public String memberName = "";
    public List<TypeRef> argumentTypes = new ArrayList<>();
    public TypeRef result;
    public AccessMode receiverAccess = AccessMode.Read;
    public List<AccessMode> argumentAccess = new ArrayList<>();
    public String admittedTemplateOrBuiltinFamily;
    public String selectedCallableId;
    public RequirementOrigin origin = new RequirementOrigin();
    public String parentRequirementId;

    public MemberDispatchRequirement() {}

    public MemberDispatchRequirement copy() {
        MemberDispatchRequirement r = new MemberDispatchRequirement();
        r.requirementId = this.requirementId;
        r.receiver = this.receiver;
        r.memberName = this.memberName;
        r.argumentTypes = new ArrayList<>(this.argumentTypes);
        r.result = this.result;
        r.receiverAccess = this.receiverAccess;
        r.argumentAccess = new ArrayList<>(this.argumentAccess);
        r.admittedTemplateOrBuiltinFamily = this.admittedTemplateOrBuiltinFamily;
        r.selectedCallableId = this.selectedCallableId;
        r.origin = this.origin;
        r.parentRequirementId = this.parentRequirementId;
        return r;
    }
}
