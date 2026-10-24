package org.mosslang.compiler.types;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class Requirement {
    private final String id;
    private final RequirementKind kind;
    private final String targetId;
    private final TypeRef subject;
    private final List<TypeRef> arguments;
    private final TypeRef result;
    private final AccessMode access;
    private final List<AccessMode> argumentAccess;

    public Requirement(String id, RequirementKind kind, String targetId, TypeRef subject,
                       List<TypeRef> arguments, TypeRef result, AccessMode access,
                       List<AccessMode> argumentAccess) {
        this.id = id;
        this.kind = kind;
        this.targetId = targetId;
        this.subject = subject;
        this.arguments = arguments != null ? Collections.unmodifiableList(new ArrayList<>(arguments)) : Collections.emptyList();
        this.result = result;
        this.access = access;
        this.argumentAccess = argumentAccess != null ? Collections.unmodifiableList(new ArrayList<>(argumentAccess)) : Collections.emptyList();
    }

    public String id() { return id; }
    public RequirementKind kind() { return kind; }
    public String targetId() { return targetId; }
    public TypeRef subject() { return subject; }
    public List<TypeRef> arguments() { return arguments; }
    public TypeRef result() { return result; }
    public AccessMode access() { return access; }
    public List<AccessMode> argumentAccess() { return argumentAccess; }
}
