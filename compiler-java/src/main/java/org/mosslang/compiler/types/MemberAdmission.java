package org.mosslang.compiler.types;

import java.util.ArrayList;
import java.util.List;

public class MemberAdmission {
    public enum Status {
        Admitted,
        Deferred,
        Rejected
    }

    public Status status = Status.Deferred;
    public String admittedTemplateOrBuiltinFamily;
    public String selectedCallableId;
    public AccessMode receiverAccess = AccessMode.Read;
    public List<AccessMode> argumentAccess = new ArrayList<>();
    public TypeRef result;
    public List<TypeSolver.Equality> contributes = new ArrayList<>();
    public String diagnostic = "";

    public MemberAdmission() {}

    public static MemberAdmission deferred() {
        MemberAdmission a = new MemberAdmission();
        a.status = Status.Deferred;
        return a;
    }

    public static MemberAdmission rejected(String diagnostic) {
        MemberAdmission a = new MemberAdmission();
        a.status = Status.Rejected;
        a.diagnostic = diagnostic != null ? diagnostic : "";
        return a;
    }

    public static MemberAdmission admitted(String family, String callableId, AccessMode receiverAccess,
                                           List<AccessMode> argAccess, TypeRef result,
                                           List<TypeSolver.Equality> contributes) {
        MemberAdmission a = new MemberAdmission();
        a.status = Status.Admitted;
        a.admittedTemplateOrBuiltinFamily = family;
        a.selectedCallableId = callableId;
        a.receiverAccess = receiverAccess;
        a.argumentAccess = argAccess != null ? argAccess : new ArrayList<>();
        a.result = result;
        if (contributes != null) {
            a.contributes.addAll(contributes);
        }
        return a;
    }
}
