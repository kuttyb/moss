package org.mosslang.compiler.ast;

import org.mosslang.compiler.types.SpecializationKey;
import org.mosslang.compiler.types.TypeRef;
import java.util.ArrayList;
import java.util.List;

public class FunctionSpecialization {
    public String generatedName = "";
    public List<String> parameterTypes = new ArrayList<>();
    public String returnType = "";
    public SpecializationKey semanticKey = new SpecializationKey();
    public List<TypeRef> parameterTypeRefs = new ArrayList<>();
    public TypeRef returnTypeRef = null;
    public String stableTypeKey = "";
    public String resultCallKey = "";
    public List<String> callableArguments = new ArrayList<>();
    public List<SpecializationKey.OpenResultBinding> openResultBindings = new ArrayList<>();

    public static class MemberReq {
        public String requirementId = "";
        public String parentRequirementId = null;
        public String memberName = "length";
        public String receiverKind = "primitive"; // "primitive" or "nominal"
        public String receiverId = "string";
        public String resultKind = "primitive";
        public String resultId = "int";
        public String receiverAccess = "READ";
        public String admittedTemplate = "builtin:string";
        public String selectedCallableId = "builtin:string.length";
        public int line = 0;
        public String sourceFile = "";
        public String enclosingCallable = "main";
        public String callExpressionId = "";
    }
    public List<MemberReq> memberRequirements = new ArrayList<>();
}
