package org.mosslang.compiler.types;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Function;

public class MemberClosure {

    public static MemberDispatchRequirement resolveMemberRequirement(
            TypeSolver solver, MemberDispatchRequirement requirement) {
        MemberDispatchRequirement result = requirement.copy();
        if (result.receiver != null) {
            result.receiver = solver.resolve(result.receiver);
        }
        for (int i = 0; i < result.argumentTypes.size(); i++) {
            TypeRef arg = result.argumentTypes.get(i);
            if (arg != null) {
                result.argumentTypes.set(i, solver.resolve(arg));
            }
        }
        if (result.result != null) {
            result.result = solver.resolve(result.result);
        }
        return result;
    }

    public static boolean isClosed(MemberDispatchRequirement req) {
        if (req == null) return false;
        if (req.receiver == null || !req.receiver.isClosed()) return false;
        if (req.argumentAccess.size() != req.argumentTypes.size()) return false;
        if (req.result == null || !req.result.isClosed()) return false;
        if (req.admittedTemplateOrBuiltinFamily == null || req.selectedCallableId == null) return false;
        for (TypeRef arg : req.argumentTypes) {
            if (arg == null || !arg.isClosed()) return false;
        }
        return true;
    }

    public static MemberClosureResult closeMemberRequirements(
            TypeSolver solver, List<MemberDispatchRequirement> requirements,
            Function<MemberDispatchRequirement, MemberAdmission> admitMember) {
        MemberClosureResult closure = new MemberClosureResult();
        closure.requirements = new ArrayList<>();
        for (MemberDispatchRequirement r : requirements) {
            closure.requirements.add(r.copy());
        }

        boolean[] completed = new boolean[closure.requirements.size()];
        int remaining = completed.length;

        while (remaining > 0) {
            boolean progress = false;
            for (int index = 0; index < completed.length; index++) {
                if (completed[index]) continue;
                MemberDispatchRequirement current = resolveMemberRequirement(solver, closure.requirements.get(index));
                MemberAdmission admission = admitMember.apply(current);
                if (admission == null || admission.status == MemberAdmission.Status.Deferred) {
                    continue;
                }
                if (admission.status == MemberAdmission.Status.Rejected) {
                    closure.status = MemberClosureResult.Status.Rejected;
                    closure.failingRequirement = index;
                    closure.diagnostic = admission.diagnostic;
                    return closure;
                }
                if (admission.admittedTemplateOrBuiltinFamily == null ||
                        admission.selectedCallableId == null ||
                        admission.argumentAccess.size() != current.argumentTypes.size()) {
                    closure.status = MemberClosureResult.Status.Rejected;
                    closure.failingRequirement = index;
                    closure.diagnostic = "member admission omitted its target or access contract";
                    return closure;
                }

                List<TypeSolver.Equality> equalities = new ArrayList<>(admission.contributes);
                Integer resultEquality = null;
                if (current.result != null && admission.result != null) {
                    resultEquality = equalities.size();
                    equalities.add(new TypeSolver.Equality(current.result, admission.result));
                }

                int[] failedEquality = new int[1];
                if (!solver.unifyAll(equalities, failedEquality)) {
                    closure.status = MemberClosureResult.Status.UnificationFailed;
                    closure.failingRequirement = index;
                    closure.diagnostic = (resultEquality != null && failedEquality[0] == resultEquality)
                            ? "member result conflicts with its expected type"
                            : "member admission contributed incompatible types";
                    return closure;
                }

                current.admittedTemplateOrBuiltinFamily = admission.admittedTemplateOrBuiltinFamily;
                current.selectedCallableId = admission.selectedCallableId;
                current.receiverAccess = admission.receiverAccess;
                current.argumentAccess = new ArrayList<>(admission.argumentAccess);
                if (current.result == null) {
                    current.result = admission.result;
                }
                closure.requirements.set(index, current);
                completed[index] = true;
                remaining--;
                progress = true;
            }

            if (!progress) {
                closure.status = MemberClosureResult.Status.Deferred;
                for (int index = 0; index < completed.length; index++) {
                    if (!completed[index]) {
                        closure.failingRequirement = index;
                        break;
                    }
                }
                closure.diagnostic = "member receiver remains unresolved";
                return closure;
            }
        }

        for (int index = 0; index < closure.requirements.size(); index++) {
            closure.requirements.set(index, resolveMemberRequirement(solver, closure.requirements.get(index)));
            if (!isClosed(closure.requirements.get(index))) {
                closure.status = MemberClosureResult.Status.Deferred;
                closure.failingRequirement = index;
                closure.diagnostic = "member requirement remains open after admission";
                return closure;
            }
        }

        closure.status = MemberClosureResult.Status.Closed;
        return closure;
    }
}
