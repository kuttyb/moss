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
}
