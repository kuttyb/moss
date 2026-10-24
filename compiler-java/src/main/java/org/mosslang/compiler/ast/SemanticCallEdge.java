package org.mosslang.compiler.ast;

import org.mosslang.compiler.types.TypeRef;
import java.util.ArrayList;
import java.util.List;

public class SemanticCallEdge {
    public String source = "";
    public String target = "";
    public int line = 0;
    public List<String> argumentTypes = new ArrayList<>();
    public String sourceFile = "";
    public String invocationKind = "ordinary_call";
    public String receiver = "";
    public String sourceSpecializationKey = "";
    public String specializationKey = "";
    public TypeRef returnTypeRef = null;
    public int occurrence = 0;
    public int programOccurrence = 0;

    public String callSiteId() {
        String base = "entity-v1:call:" + source + ":" + target;
        if (programOccurrence != 0) {
            return base + ":occurrence:" + (programOccurrence + 1);
        }
        return base;
    }

    public String callSiteSemanticIdentity() {
        String id = source +
                (sourceSpecializationKey != null && !sourceSpecializationKey.isEmpty() ? "#" + sourceSpecializationKey : "") +
                "@" + line + ":call:" + target;
        if (occurrence != 0) {
            id += ":occurrence:" + (occurrence + 1);
        }
        return id;
    }
}
