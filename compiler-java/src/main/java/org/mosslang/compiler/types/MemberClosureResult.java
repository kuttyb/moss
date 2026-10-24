package org.mosslang.compiler.types;

import java.util.ArrayList;
import java.util.List;

public class MemberClosureResult {
    public enum Status {
        Closed,
        Deferred,
        Rejected,
        UnificationFailed
    }

    public Status status = Status.Deferred;
    public List<MemberDispatchRequirement> requirements = new ArrayList<>();
    public Integer failingRequirement = null;
    public String diagnostic = "";

    public MemberClosureResult() {}
}
