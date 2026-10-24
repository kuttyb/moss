package org.mosslang.compiler.optimizer;

import java.util.ArrayList;
import java.util.List;

public class FunctionalSemanticRewrite {
    public String name = "";
    public String detail = "";
    public List<String> provenance = new ArrayList<>();

    public FunctionalSemanticRewrite() {}

    public FunctionalSemanticRewrite(String name, String detail, List<String> provenance) {
        this.name = name != null ? name : "";
        this.detail = detail != null ? detail : "";
        this.provenance = provenance != null ? new ArrayList<>(provenance) : new ArrayList<>();
    }

    @Override
    public String toString() {
        return "FunctionalSemanticRewrite{" + name + ": " + detail + "}";
    }
}
