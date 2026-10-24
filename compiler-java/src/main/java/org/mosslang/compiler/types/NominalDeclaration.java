package org.mosslang.compiler.types;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class NominalDeclaration {
    private final DeclarationKind kind;
    private final String programName;
    private final String moduleId;
    private final String declarationName;
    private final String id;
    private final List<NominalFieldSlot> fields;

    public NominalDeclaration(DeclarationKind kind, String programName, String moduleId,
                              String declarationName, String id, List<NominalFieldSlot> fields) {
        this.kind = kind;
        this.programName = programName;
        this.moduleId = moduleId;
        this.declarationName = declarationName;
        this.id = id;
        this.fields = fields != null ? Collections.unmodifiableList(new ArrayList<>(fields)) : Collections.emptyList();
    }

    public DeclarationKind getKind() {
        return kind;
    }

    public String getProgramName() {
        return programName;
    }

    public String getModuleId() {
        return moduleId;
    }

    public String getDeclarationName() {
        return declarationName;
    }

    public String getId() {
        return id;
    }

    public List<NominalFieldSlot> getFields() {
        return fields;
    }

    public int inferredSlotCount() {
        int count = 0;
        for (NominalFieldSlot f : fields) {
            if (f.inferred()) count++;
        }
        return count;
    }
}
