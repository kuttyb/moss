package org.mosslang.compiler.backend;

import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.semantics.ExpressionTyper;
import org.mosslang.compiler.semantics.SemanticChecker;
import org.mosslang.compiler.types.TypeKind;
import java.util.*;

public class NativeGenerator {
    private static final ThreadLocal<Set<String>> currentRaisingFunctions = ThreadLocal.withInitial(HashSet::new);
    private static final ThreadLocal<Set<String>> currentWriteFunctions = ThreadLocal.withInitial(HashSet::new);
    private static final ThreadLocal<Map<String, Set<Integer>>> currentWriteParamIndices = ThreadLocal.withInitial(HashMap::new);
    private static final ThreadLocal<Set<String>> currentWriteMethods = ThreadLocal.withInitial(HashSet::new);
    private static final ThreadLocal<Set<String>> currentWriteParams = ThreadLocal.withInitial(HashSet::new);
    private static final ThreadLocal<Set<String>> currentGenericFunctions = ThreadLocal.withInitial(HashSet::new);
    private static final ThreadLocal<Boolean> currentInTry = ThreadLocal.withInitial(() -> false);
    private static final ThreadLocal<Program> currentProgram = new ThreadLocal<>();
    private static final ThreadLocal<String> currentEnclosingIdentity = new ThreadLocal<>();

    public static class HandlerSyncPlan {
        public final Set<String> readFields = new HashSet<>();
        public final Set<String> writeFields = new HashSet<>();
        public final List<Integer> lockClasses = new ArrayList<>();
        public final Map<Integer, Boolean> classIsWrite = new HashMap<>();
    }

    private static HandlerSyncPlan analyzeHandler(Handler h, Domain d) {
        HandlerSyncPlan plan = new HandlerSyncPlan();
        for (Stmt s : h.body) {
            analyzeStmtEffects(s, d, plan);
        }
        for (int i = 0; i < d.state.size(); i++) {
            Field f = d.state.get(i);
            boolean writes = plan.writeFields.contains(f.name);
            boolean reads = plan.readFields.contains(f.name);
            if (writes || reads) {
                plan.lockClasses.add(i);
                plan.classIsWrite.put(i, writes);
            }
        }
        Collections.sort(plan.lockClasses);
        return plan;
    }

    private static void analyzeStmtEffects(Stmt s, Domain d, HandlerSyncPlan plan) {
        if (s == null) return;
        if (s.kind == Stmt.Kind.Assign) {
            String target = s.a != null ? s.a.trim() : "";
            for (Field f : d.state) {
                if (target.equals(f.name) || target.startsWith(f.name + ".")) {
                    plan.writeFields.add(f.name);
                }
            }
            analyzeReadEffects(s.b, d, plan);
        } else {
            String txt = s.text != null ? s.text.trim() : "";
            for (Field f : d.state) {
                if (txt.contains(f.name + ".append(") || txt.contains(f.name + ".reserve(")) {
                    plan.writeFields.add(f.name);
                }
                for (String wf : currentWriteFunctions.get()) {
                    if (txt.contains(wf + "(" + f.name + ")") || txt.contains("." + wf + "(" + f.name + ")")) {
                        plan.writeFields.add(f.name);
                    }
                }
            }
            analyzeReadEffects(s.text, d, plan);
            analyzeReadEffects(s.a, d, plan);
            analyzeReadEffects(s.b, d, plan);
            for (String arg : s.args) {
                analyzeReadEffects(arg, d, plan);
            }
        }
    }

    private static boolean fnMutatesParameter(Function fn, String name) {
        List<String> bodies = new ArrayList<>();
        if (fn.resultExpression != null) bodies.add(fn.resultExpression);
        for (Stmt stmt : fn.body) if (stmt != null && stmt.text != null) bodies.add(stmt.text);
        for (String body : bodies) {
            String text = body.trim();
            if (text.startsWith(name + " =") || text.startsWith(name + "=") ||
                    text.startsWith(name + "[") || text.contains(name + ".push(") ||
                    text.contains(name + ".pop(") || text.contains(name + ".delete(") ||
                    text.contains(name + ".append(") || text.contains(name + ".reserve(")) return true;
        }
        return false;
    }

    private static boolean callPassesParameter(String body, String callee, int argIndex, String param) {
        String marker = callee + "(";
        int at = body.indexOf(marker);
        while (at >= 0) {
            int begin = at + marker.length();
            int depth = 1;
            boolean inString = false;
            int end = begin;
            for (; end < body.length(); end++) {
                char ch = body.charAt(end);
                if (ch == '"' && (end == 0 || body.charAt(end - 1) != '\\')) inString = !inString;
                if (!inString) {
                    if (ch == '(') depth++;
                    else if (ch == ')' && --depth == 0) break;
                }
            }
            if (end < body.length()) {
                List<String> args = splitTopLevel(body.substring(begin, end), ',');
                if (argIndex < args.size()) {
                    String value = args.get(argIndex).trim();
                    int colon = value.indexOf(':');
                    if (colon >= 0) value = value.substring(colon + 1).trim();
                    if (value.equals(param)) return true;
                }
            }
            at = body.indexOf(marker, at + marker.length());
        }
        return false;
    }

    private static void analyzeReadEffects(String text, Domain d, HandlerSyncPlan plan) {
        if (text == null || text.isEmpty()) return;
        for (Field f : d.state) {
            if (SemanticChecker.expressionUses(text, f.name)) {
                plan.readFields.add(f.name);
            }
        }
    }

    public static String generate(Program program, boolean debugBuild) {
        return generate(program, debugBuild, false);
    }

    public static String generate(Program program, boolean debugBuild, boolean optimized) {
        currentProgram.set(program);
        StringBuilder o = new StringBuilder();
        o.append("// Generated by Moss v0.1. Do not edit by hand.\n");
        o.append("// Synchronous domain calls enter compiler-planned handler-level 2PL.\n");
        o.append("#![allow(non_snake_case)]\n");
        o.append("#![allow(non_camel_case_types)]\n");
        o.append("#![allow(dead_code)]\n");
        o.append("#![allow(unused_imports)]\n");
        o.append("#![allow(unused_mut)]\n");
        o.append("#![allow(unused_variables)]\n");
        o.append("#![allow(unused_assignments)]\n");
        o.append("#![allow(unused_parens)]\n");
        o.append("#![allow(unreachable_code)]\n");
        o.append("#![allow(unconditional_panic)]\n\n");

        o.append("fn __moss_require_send<T: Send>() {}\n\n");
        o.append("#[inline(always)]\n");
        o.append("fn __moss_shl(value: i64, shift: i64) -> i64 {\n");
        o.append("    if shift < 0 || shift >= 64 { 0 } else { value.wrapping_shl(shift as u32) }\n");
        o.append("}\n\n");
        o.append("#[inline(always)]\n");
        o.append("fn __moss_shr(value: i64, shift: i64) -> i64 {\n");
        o.append("    if shift < 0 || shift >= 64 { if value < 0 { -1 } else { 0 } } else { value.wrapping_shr(shift as u32) }\n");
        o.append("}\n\n");

        o.append(MossRuntimeSupport.FLOAT_RUNTIME).append("\n");
        o.append(MossRuntimeSupport.EXECUTOR_RUNTIME).append("\n");
        o.append(MossRuntimeSupport.HANDLER_RUNTIME).append("\n");
        o.append(MossRuntimeSupport.STRING_RUNTIME).append("\n");
        o.append(MossRuntimeSupport.DISPLAY_RUNTIME).append("\n");

        Map<String, Domain> domains = new HashMap<>();
        for (Domain d : program.domains) {
            domains.put(d.name, d);
        }

        Map<String, ObjectType> objects = new HashMap<>();
        for (ObjectType obj : program.objects) {
            objects.put(obj.name, obj);
        }

        Set<String> raisingFunctions = new HashSet<>();
        for (Function f : program.functions) {
            if (f.resultExpression != null && f.resultExpression.contains(".poke(")) {
                raisingFunctions.add(f.name);
            }
            if (!f.sourceFreeRaiseVariants.isEmpty()) raisingFunctions.add(f.name);
            boolean hasTry = false;
            for (Stmt s : f.body) {
                if (s.kind == Stmt.Kind.Try) {
                    hasTry = true;
                }
                if (s.kind == Stmt.Kind.Raise) {
                    String rVal = s.a != null ? s.a.trim() : (s.text != null && s.text.startsWith("raise ") ? s.text.substring(6).trim() : "");
                    if (!hasTry || rVal.isEmpty() || rVal.equals("raise")) {
                        raisingFunctions.add(f.name);
                        break;
                    }
                } else if (!hasTry && s.text != null && (s.text.trim().startsWith("raise ") || s.text.contains(".poke("))) {
                    raisingFunctions.add(f.name);
                    break;
                }
                if (expressionCallsRaisingMethod(statementExpressionForGenerator(s), f.name)) {
                    raisingFunctions.add(f.name);
                    break;
                }
            }
            if (!raisingFunctions.contains(f.name)
                    && expressionCallsRaisingMethod(f.resultExpression, f.name)) {
                raisingFunctions.add(f.name);
            }
        }
        // A function must return the same raised carrier as any local
        // callable it invokes. Compute the transitive closure before emitting
        // signatures so call propagation cannot silently unwrap a callee's
        // failure into the function's default result.
        boolean addedRaisingFunction;
        do {
            addedRaisingFunction = false;
            for (Function f : program.functions) {
                if (raisingFunctions.contains(f.name)) continue;
                boolean callsRaisingFunction = expressionCallsRaisingFunction(
                        f.resultExpression, raisingFunctions)
                        || expressionCallsRaisingFunction(f.resultExpression, Collections.singleton("fail"));
                for (Stmt statement : f.body) {
                    callsRaisingFunction |= expressionCallsRaisingFunction(statement.text, raisingFunctions)
                            || expressionCallsRaisingFunction(statement.a, raisingFunctions)
                            || expressionCallsRaisingFunction(statement.b, raisingFunctions)
                            || expressionCallsRaisingFunction(statement.text, Collections.singleton("fail"))
                            || expressionCallsRaisingFunction(statement.a, Collections.singleton("fail"))
                            || expressionCallsRaisingFunction(statement.b, Collections.singleton("fail"));
                    if (statement.args != null) {
                        for (String argument : statement.args) {
                            callsRaisingFunction |= expressionCallsRaisingFunction(argument, raisingFunctions)
                                    || expressionCallsRaisingFunction(argument, Collections.singleton("fail"));
                        }
                    }
                }
                for (Stmt statement : f.body) {
                    callsRaisingFunction |= expressionCallsRaisingMethod(
                            statementExpressionForGenerator(statement), f.name);
                }
                callsRaisingFunction |= expressionCallsRaisingMethod(f.resultExpression, f.name);
                if (callsRaisingFunction) {
                    raisingFunctions.add(f.name);
                    addedRaisingFunction = true;
                }
            }
        } while (addedRaisingFunction);
        currentRaisingFunctions.set(raisingFunctions);

        Set<String> writeFunctions = new HashSet<>();
        for (Function f : program.functions) {
            for (Param p : f.params) {
                if (f.resultExpression != null) {
                    String re = f.resultExpression.trim();
                    if (re.contains(p.name + ".push(") || re.contains(p.name + ".pop(") || re.contains(p.name + ".delete(")) {
                        writeFunctions.add(f.name);
                    }
                    if (re.contains(p.name + ".append(") || re.contains(p.name + ".reserve(")) writeFunctions.add(f.name);
                }
                for (Stmt s : f.body) {
                    if (s.kind == Stmt.Kind.Assign && p.name.equals(s.a != null ? s.a.trim() : "")) {
                        writeFunctions.add(f.name);
                    }
                    if (s.text != null) {
                        String st = s.text.trim();
                        if (st.startsWith(p.name + " =") || st.startsWith(p.name + "=")
                                || st.contains(p.name + ".push(") || st.contains(p.name + ".pop(") || st.contains(p.name + ".delete(")
                                || st.contains(p.name + ".append(") || st.contains(p.name + ".reserve(")) {
                            writeFunctions.add(f.name);
                        }
                    }
                }
            }
        }
        for (int pass = 0; pass < 2; pass++) {
            for (Function f : program.functions) {
                for (Param p : f.params) {
                    if (f.resultExpression != null) {
                        for (String wf : new ArrayList<>(writeFunctions)) {
                            if (f.resultExpression.contains(wf + "(" + p.name + ")")) {
                                writeFunctions.add(f.name);
                            }
                        }
                    }
                    for (Stmt s : f.body) {
                        String txt = s.text != null ? s.text : "";
                        for (String wf : new ArrayList<>(writeFunctions)) {
                            if (txt.contains(wf + "(" + p.name + ")")) {
                                writeFunctions.add(f.name);
                            }
                        }
                    }
                }
            }
        }
        currentWriteFunctions.set(writeFunctions);

        Map<String, Set<Integer>> writeParamIndices = new HashMap<>();
        for (Function fn : program.functions) {
            Set<Integer> indices = new HashSet<>();
            for (int pi = 0; pi < fn.params.size(); pi++) {
                String name = fn.params.get(pi).name;
                boolean checkerWritesParameter = pi < fn.parameterAccessModes.size()
                        && fn.parameterAccessModes.get(pi)
                                == org.mosslang.compiler.types.AccessMode.Write;
                if (checkerWritesParameter || fnMutatesParameter(fn, name)) indices.add(pi);
            }
            if (!indices.isEmpty()) writeParamIndices.put(fn.name, indices);
        }
        for (int pass = 0; pass < program.functions.size(); pass++) {
            boolean changed = false;
            for (Function fn : program.functions) {
                Set<Integer> indices = writeParamIndices.computeIfAbsent(fn.name, ignored -> new HashSet<>());
                List<String> bodies = new ArrayList<>();
                if (fn.resultExpression != null) bodies.add(fn.resultExpression);
                for (Stmt stmt : fn.body) if (stmt != null && stmt.text != null) bodies.add(stmt.text);
                for (Function callee : program.functions) {
                    Set<Integer> calleeWrites = writeParamIndices.get(callee.name);
                    if (calleeWrites == null || calleeWrites.isEmpty()) continue;
                    for (String body : bodies) {
                        for (int ai : calleeWrites) {
                            if (ai >= fn.params.size()) continue;
                            String param = fn.params.get(ai).name;
                            if (callPassesParameter(body, callee.name, ai, param) && indices.add(ai)) changed = true;
                        }
                    }
                }
                if (indices.isEmpty()) writeParamIndices.remove(fn.name);
            }
            if (!changed) break;
        }
        currentWriteParamIndices.set(writeParamIndices);

        Set<String> genericObjects = new HashSet<>();
        for (ObjectType obj : program.objects) {
            for (Field f : obj.fields) {
                if (f.type == null || f.type.isEmpty()) {
                    genericObjects.add(obj.name);
                    break;
                }
            }
        }

        Set<String> genericFunctions = new HashSet<>();
        if (program.importedGenericFunctions != null) {
            genericFunctions.addAll(program.importedGenericFunctions);
        }
        for (Function f : program.functions) {
            boolean hasGeneric = f.generic;
            for (Param p : f.params) {
                if (p.type == null || p.type.isEmpty() || p.type.contains("?") || genericObjects.contains(p.type)) {
                    hasGeneric = true;
                    break;
                }
            }
            if (!hasGeneric && (f.returnType == null || f.returnType.isEmpty() || f.returnType.contains("?"))) {
                if (f.resultExpression != null && (f.resultExpression.trim().equals("[]") || genericFunctions.contains(f.resultExpression.trim().replace("()", "")))) {
                    hasGeneric = true;
                }
                for (Stmt s : f.body) {
                    if (s.kind == Stmt.Kind.Return && s.a != null) {
                        String ra = s.a.trim();
                        if (ra.equals("[]") || genericFunctions.contains(ra.replace("()", ""))) {
                            hasGeneric = true;
                        }
                    }
                }
            }
            if (hasGeneric) {
                genericFunctions.add(f.name);
            }
        }
        // Second pass for transitive open-result functions (e.g. relay() -> fresh(), make(flag), outer() -> middle())
        for (int pass = 0; pass < 3; pass++) {
            for (Function f : program.functions) {
                if (f.returnType == null || f.returnType.isEmpty()) {
                    for (Stmt s : f.body) {
                        String txt = s.text != null ? s.text : (s.a != null ? s.a : "");
                        for (String gf : new ArrayList<>(genericFunctions)) {
                            if (txt.contains(gf + "(")) {
                                genericFunctions.add(f.name);
                            }
                        }
                    }
                }
            }
        }
        currentGenericFunctions.set(genericFunctions);
        currentProgram.set(program);

        Set<String> writeMethods = new HashSet<>();
        for (ObjectType obj : program.objects) {
            for (Method m : obj.methods) {
                for (Param p : m.params) {
                    for (Stmt s : m.body) {
                        if (s.kind == Stmt.Kind.Assign && p.name.equals(s.a != null ? s.a.trim() : "")) {
                            writeMethods.add(m.name);
                        }
                        if (s.text != null && (s.text.trim().startsWith(p.name + " =") || s.text.trim().startsWith(p.name + "="))) {
                            writeMethods.add(m.name);
                        }
                    }
                }
            }
        }
        currentWriteMethods.set(writeMethods);

        // Enums
        for (EnumType e : program.enums) {
            if (e.sourceFree) continue;
            String enumIdentity = "enum:" + e.name + "@" + e.line;
            o.append("// Moss tooling begin|enum|").append(enumIdentity).append("|").append(e.name).append("|\n");
            o.append("// Moss line ").append(e.line).append(": enum ").append(e.name).append(":\n");
            o.append("#[derive(Clone, Debug, PartialEq, Eq)]\n");
            o.append("pub enum ").append(e.name).append(" {\n");
            for (EnumCase c : e.cases) {
                String caseIdentity = "enum_case:" + e.name + "." + c.name + "@" + c.line;
                o.append("    // Moss tooling begin|enum_case|").append(caseIdentity).append("|").append(e.name).append("::").append(c.name).append("|\n");
                o.append("    // Moss line ").append(c.line).append(": ").append(c.name).append("\n");
                o.append("    ").append(c.name);
                if (!c.fields.isEmpty()) {
                    o.append(" { ");
                    for (int fi = 0; fi < c.fields.size(); fi++) {
                        if (fi > 0) o.append(", ");
                        Field f = c.fields.get(fi);
                        o.append(f.name).append(": ").append(rustType(f.type));
                    }
                    o.append(" }");
                }
                o.append(",\n");
                o.append("    // Moss tooling end|").append(caseIdentity).append("\n");
            }
            o.append("}\n");
            boolean allUnitCases = !e.cases.isEmpty();
            for (EnumCase c : e.cases) {
                if (!c.fields.isEmpty()) {
                    allUnitCases = false;
                    break;
                }
            }
            if (allUnitCases) {
                o.append("impl ").append(e.name).append(" {\n");
                o.append("    pub fn __moss_from_any<T: std::fmt::Debug>(other: T) -> Self {\n");
                o.append("        let s = format!(\"{:?}\", other);\n");
                for (int ci = 0; ci < e.cases.size(); ci++) {
                    EnumCase c = e.cases.get(ci);
                    if (ci < e.cases.size() - 1) {
                        o.append("        if s.contains(\"").append(c.name).append("\") { return Self::").append(c.name).append("; }\n");
                    } else {
                        o.append("        Self::").append(c.name).append("\n");
                    }
                }
                o.append("    }\n");
                o.append("}\n");
            }
            o.append("impl std::fmt::Display for ").append(e.name).append(" {\n");
            o.append("    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {\n");
            o.append("        write!(formatter, \"{:?}\", self)\n");
            o.append("    }\n");
            o.append("}\n");
            o.append("// Moss tooling end|").append(enumIdentity).append("\n\n");
        }

        // Objects
        for (ObjectType obj : program.objects) {
            if (obj.sourceFree) continue;
            String typeIdentity = "type:" + obj.name + "@" + obj.line;
            o.append("// Moss tooling begin|type|").append(typeIdentity).append("|").append(obj.name).append("|\n");
            o.append("// Moss line ").append(obj.line).append(": type ").append(obj.name).append(":\n");
            o.append("#[derive(Clone, Debug)]\n");
            List<String> openGenericParams = new ArrayList<>();
            int openIdx = 0;
            for (Field f : obj.fields) {
                if (f.type == null || f.type.isEmpty()) {
                    openGenericParams.add("__T" + (openIdx++));
                }
            }
            o.append("pub struct ").append(obj.name);
            if (!openGenericParams.isEmpty()) {
                o.append("<");
                for (int gi = 0; gi < openGenericParams.size(); gi++) {
                    if (gi > 0) o.append(", ");
                    o.append(openGenericParams.get(gi)).append(" = i64");
                }
                o.append(">");
            }
            o.append(" {\n");
            openIdx = 0;
            for (Field f : obj.fields) {
                o.append("    // Moss line ").append(f.line).append(": ").append(f.name).append(": ").append(f.type).append("\n");
                String fType = (f.type == null || f.type.isEmpty()) ? openGenericParams.get(openIdx++) : rustType(f.type);
                o.append("    pub ").append(f.name).append(": ").append(fType).append(",\n");
            }
            o.append("}\n");
            o.append("// Moss tooling end|").append(typeIdentity).append("\n\n");

            boolean hasIsequal = obj.methods.stream().anyMatch(m -> m.name.equals("isequal"));
            boolean hasHash = obj.methods.stream().anyMatch(m -> m.name.equals("hash"));
            if (hasIsequal && hasHash) {
                o.append("impl PartialEq for ").append(obj.name).append(" {\n");
                o.append("    fn eq(&self, other: &Self) -> bool {\n");
                o.append("        self.isequal(other.clone())\n");
                o.append("    }\n");
                o.append("}\n");
                o.append("impl Eq for ").append(obj.name).append(" {}\n");
                o.append("impl PartialOrd for ").append(obj.name).append(" {\n");
                o.append("    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {\n");
                o.append("        Some(self.cmp(other))\n");
                o.append("    }\n");
                o.append("}\n");
                o.append("impl Ord for ").append(obj.name).append(" {\n");
                o.append("    fn cmp(&self, other: &Self) -> std::cmp::Ordering {\n");
                o.append("        self.hash().cmp(&other.hash())\n");
                o.append("    }\n");
                o.append("}\n");
                o.append("impl std::hash::Hash for ").append(obj.name).append(" {\n");
                o.append("    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {\n");
                o.append("        self.hash().hash(state);\n");
                o.append("    }\n");
                o.append("}\n\n");
            } else if (openGenericParams.isEmpty() && obj.fields.stream().allMatch(f -> !"float".equalsIgnoreCase(f.type))) {
                o.append("impl PartialEq for ").append(obj.name).append(" {\n");
                o.append("    fn eq(&self, other: &Self) -> bool {\n");
                if (obj.fields.isEmpty()) {
                    o.append("        true\n");
                } else {
                    List<String> eqs = new ArrayList<>();
                    for (Field f : obj.fields) eqs.add("self." + f.name + " == other." + f.name);
                    o.append("        ").append(String.join(" && ", eqs)).append("\n");
                }
                o.append("    }\n");
                o.append("}\n");
                o.append("impl Eq for ").append(obj.name).append(" {}\n");
                o.append("impl PartialOrd for ").append(obj.name).append(" {\n");
                o.append("    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {\n");
                o.append("        Some(self.cmp(other))\n");
                o.append("    }\n");
                o.append("}\n");
                o.append("impl Ord for ").append(obj.name).append(" {\n");
                o.append("    fn cmp(&self, other: &Self) -> std::cmp::Ordering {\n");
                if (obj.fields.isEmpty()) {
                    o.append("        std::cmp::Ordering::Equal\n");
                } else {
                    o.append("        self.").append(obj.fields.get(0).name).append(".cmp(&other.").append(obj.fields.get(0).name).append(")\n");
                }
                o.append("    }\n");
                o.append("}\n");
                o.append("impl std::hash::Hash for ").append(obj.name).append(" {\n");
                o.append("    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {\n");
                for (Field f : obj.fields) {
                    o.append("        self.").append(f.name).append(".hash(state);\n");
                }
                o.append("    }\n");
                o.append("}\n\n");
            }

            // Methods on object
            for (Method m : obj.methods) {
                String methodIdentity = "method:" + obj.name + "." + m.name + "@" + m.line;
                String methodNative = DebugMapGenerator.toolingNativeSymbol("method", obj.name + "__" + m.name, methodIdentity);
                o.append("// Moss tooling begin|method|").append(methodIdentity).append("|").append(obj.name).append("::").append(m.name).append("|").append(methodNative).append("\n");
                o.append("impl ").append(obj.name).append(" {\n");
                o.append("    // Moss line ").append(m.line).append(": fn ").append(m.name).append("\n");
                if (debugBuild) o.append("    #[inline(never)]\n");
                o.append("    #[export_name = \"").append(methodNative).append("\"]\n");
                boolean mRaises = false;
                for (Stmt s : m.body) {
                    if (s.kind == Stmt.Kind.Raise || (s.text != null && s.text.trim().startsWith("raise "))) {
                        mRaises = true;
                        break;
                    }
                }
                String inferredRet = null;
                if (m.returnTypeRef != null) {
                    inferredRet = m.returnTypeRef.kind() == TypeKind.Unit
                            ? "()" : rustType(m.returnTypeRef.toCanonicalString());
                } else if (m.returnType != null && !m.returnType.isEmpty()) {
                    inferredRet = m.returnType.equalsIgnoreCase("unit")
                            ? "()" : rustType(m.returnType);
                } else {
                    for (Stmt s : m.body) {
                        if (s.kind == Stmt.Kind.Return && s.a != null && !s.a.trim().isEmpty() && !s.a.trim().equals("return")) {
                            inferredRet = "i64";
                            break;
                        }
                    }
                    if (inferredRet == null && m.resultExpression != null && !m.resultExpression.isEmpty()) {
                        inferredRet = "i64";
                    }
                }
                String ret = inferredRet != null ? inferredRet : "()";
                if (mRaises) {
                    ret = "Result<" + ret + ", __MossRaised>";
                }
                boolean consumesSelf = m.receiverAccessMode
                        == org.mosslang.compiler.types.AccessMode.Consume;
                boolean mutatesSelf = m.receiverAccessMode
                        == org.mosslang.compiler.types.AccessMode.Write;
                for (Stmt s : m.body) {
                    String txt = s.text != null ? s.text : "";
                    for (Field f : obj.fields) {
                        if (txt.matches(".*\\b" + f.name + "\\s*(?<!=)=(?!=).*")) {
                            mutatesSelf = true;
                            break;
                        }
                    }
                }
                String selfArg = consumesSelf ? "self" : (mutatesSelf ? "&mut self" : "&self");
                o.append("    pub fn ").append(m.name).append("(").append(selfArg);
                Set<String> declared = new HashSet<>();
                declared.add("self");
                for (Field f : obj.fields) {
                    declared.add(f.name);
                }
                Set<String> writeParams = new HashSet<>();
                for (int paramIndex = 0; paramIndex < m.params.size(); paramIndex++) {
                    Param p = m.params.get(paramIndex);
                    boolean paramMutated = paramIndex < m.parameterAccessModes.size()
                            && m.parameterAccessModes.get(paramIndex)
                                    == org.mosslang.compiler.types.AccessMode.Write;
                    for (Stmt s : m.body) {
                        if (s.kind == Stmt.Kind.Assign && p.name.equals(s.a != null ? s.a.trim() : "")) {
                            paramMutated = true;
                            break;
                        }
                        if (s.text != null && (s.text.trim().startsWith(p.name + " =") || s.text.trim().startsWith(p.name + "="))) {
                            paramMutated = true;
                            break;
                        }
                    }
                    String pType = rustType(p.type);
                    String simpleObjName = obj.name.contains("__") ? obj.name.substring(obj.name.lastIndexOf("__") + 2) : obj.name;
                    if (p.type != null && (p.type.equals(obj.name) || p.type.equals(simpleObjName))) {
                        pType = "Self";
                    }
                    if (paramMutated) {
                        writeParams.add(p.name);
                        o.append(", ").append(p.name).append(": &mut ").append(pType);
                    } else {
                        o.append(", ").append(p.name).append(": ").append(pType);
                    }
                    declared.add(p.name);
                }
                o.append(") -> ").append(ret).append(" {\n");

                Set<String> objFields = new HashSet<>();
                for (Field f : obj.fields) {
                    objFields.add(f.name);
                }

                currentWriteParams.set(writeParams);
                genBlockStmts(o, m.body, "        ", declared, Collections.emptySet(), objFields, domains, objects, false, true, mRaises);
                currentWriteParams.set(Collections.emptySet());
                if (m.resultExpression != null) {
                    if (mRaises) {
                        o.append("        return Ok(").append(lowerObjExpr(m.resultExpression, objFields, domains, objects)).append(");\n");
                    } else {
                        o.append("        return ").append(lowerObjExpr(m.resultExpression, objFields, domains, objects)).append(";\n");
                    }
                }
                o.append("    }\n");
                o.append("}\n");
                o.append("// Moss tooling end|").append(methodIdentity).append("\n\n");
            }
        }

        // Generic function macros (emitted before Domains so handlers can invoke them)
        for (Function fn : program.functions) {
            if (genericFunctions.contains(fn.name)) {
                emitGenericMacro(o, fn, writeFunctions, domains, objects);
            }
        }

        // Domains
        for (Domain d : program.domains) {
            if (d.sourceFree) continue;
            Set<String> stateFields = new HashSet<>();
            for (Field f : d.state) {
                stateFields.add(f.name);
            }
            for (DomainRoute r : d.routes) {
                stateFields.add(r.name);
            }

            String domainIdentity = "domain:" + d.name + "@" + d.line;
            o.append("// Moss tooling begin|domain|").append(domainIdentity).append("|").append(d.name).append("Runtime|\n");
            o.append("// Moss line ").append(d.line).append(": domain ").append(d.name).append(":\n");
            for (Field f : d.state) {
                o.append("// Moss line ").append(f.line).append(": ").append(f.name).append(" = ").append(f.init).append("\n");
            }

            // Fine-grained Synchronization Classes
            for (int i = 0; i < d.state.size(); i++) {
                Field f = d.state.get(i);
                o.append("pub struct ").append(d.name).append("Class").append(i).append(" {\n");
                o.append("    pub __moss_storage_").append(f.name.length()).append("_").append(f.name)
                 .append(": ").append(rustFieldType(f)).append(",\n");
                o.append("}\n\n");
            }
            o.append("pub struct ").append(d.name).append("Immutable {\n}\n\n");

            // Runtime struct
            o.append("pub struct ").append(d.name).append("Runtime {\n");
            for (int i = 0; i < d.state.size(); i++) {
                o.append("    pub class").append(i).append(": std::sync::RwLock<").append(d.name).append("Class").append(i).append(">,\n");
            }
            o.append("    pub immutable: ").append(d.name).append("Immutable,\n");
            o.append("    #[cfg(any(test, moss_perf))] pub identity: &'static str,\n");
            o.append("    #[cfg(any(test, moss_perf))] pub rank: usize,\n");
            for (Handler h : d.handlers) {
                o.append("    #[cfg(any(test, moss_perf))] pub handler_").append(h.name).append(": &'static str,\n");
            }
            o.append("}\n");
            o.append("// Moss tooling end|").append(domainIdentity).append("\n\n");

            // Ref struct
            o.append("#[derive(Clone)]\n");
            o.append("pub struct ").append(d.name).append("Ref {\n");
            o.append("    pub state: std::sync::Arc<").append(d.name).append("Runtime>,\n");
            for (DomainRoute r : d.routes) {
                o.append("    pub ").append(r.name).append(": ").append(routeRustType(r.type, domains)).append("Ref,\n");
            }
            o.append("}\n\n");

            // Handler state projections and bodies
            for (Handler h : d.handlers) {
                HandlerSyncPlan plan = analyzeHandler(h, d);
                String replyTypeMoss = (h.replyType != null && !h.replyType.isEmpty())
                        ? h.replyType
                        : (h.replyTypeRef != null ? h.replyTypeRef.toCanonicalString() : "unit");
                String replyType = (!replyTypeMoss.equals("unit") && !replyTypeMoss.isEmpty()) ? rustType(replyTypeMoss) : "()";

                // Handler state projection struct
                o.append("pub struct ").append(d.name).append(h.name).append("State<'a> {\n");
                for (int clsIdx : plan.lockClasses) {
                    Field f = d.state.get(clsIdx);
                    boolean isWrite = plan.classIsWrite.get(clsIdx);
                    o.append("    pub ").append(f.name).append(": &'a ")
                     .append(isWrite ? "mut " : "").append(rustFieldType(f)).append(",\n");
                }
                for (DomainRoute r : d.routes) {
                    o.append("    pub ").append(r.name).append(": &'a ").append(routeRustType(r.type, domains)).append("Ref,\n");
                }
                o.append("    pub __lifetime: std::marker::PhantomData<&'a ()>,\n");
                o.append("}\n\n");

                boolean hRaises = false;
                for (Stmt s : h.body) {
                    if (s.kind == Stmt.Kind.Raise || (s.text != null && s.text.trim().startsWith("raise ")) || !h.failureArms.isEmpty()) {
                        hRaises = true;
                        break;
                    }
                }
                String internalRet = hRaises ? ("Result<Option<" + replyType + ">, __MossRaised>") : replyType;

                String handlerIdentity = "handler:" + d.name + "." + h.name + "@" + h.line;
                String handlerNative = DebugMapGenerator.toolingNativeSymbol("handler", d.name + "__" + h.name, handlerIdentity);
                o.append("// Moss tooling begin|handler|").append(handlerIdentity).append("|__moss_body_").append(d.name).append("_").append(h.name).append("|").append(handlerNative).append("\n");
                o.append("// Moss line ").append(h.line).append(": fn ").append(h.name).append("\n");
                if (debugBuild) o.append("#[inline(never)]\n");
                o.append("#[allow(unused_labels, unused_variables, unreachable_code)]\n");
                o.append("#[export_name = \"").append(handlerNative).append("\"]\n");
                o.append("pub fn __moss_body_").append(d.name).append("_").append(h.name).append("(");
                o.append("state: &mut ").append(d.name).append(h.name).append("State<'_>");
                Set<String> declared = new HashSet<>();
                for (Param p : h.params) {
                    o.append(", ").append(p.name).append(": ").append(rustType(p.type));
                    declared.add(p.name);
                }
                o.append(") -> ").append(internalRet).append(" {\n");
                for (String varName : stringLocalsUsedByRecovery(h)) {
                    if (!stateFields.contains(varName)) {
                        o.append("    let mut ").append(varName).append(" = MossStr::from(\"\");\n");
                        declared.add(varName);
                    }
                }
                o.append("    let mut __moss_reply: Option<").append(replyType).append("> = None;\n");
                o.append("    'handler: {\n");
                Map<String, String> failureCaptureTypes = handlerFailureCaptureTypes(h, declared, stateFields);
                genBlockStmts(o, h.body, "        ", declared, stateFields, Collections.emptySet(),
                        domains, objects, true, false, false, hRaises, failureCaptureTypes);
                o.append("    }\n");
                if (hRaises) {
                    o.append("    Ok(__moss_reply)\n");
                } else if (replyType.equals("()")) {
                    o.append("    ()\n");
                } else {
                    o.append("    __moss_reply.unwrap_or_else(|| std::process::abort())\n");
                }
                o.append("}\n");
                o.append("// Moss tooling end|").append(handlerIdentity).append("\n\n");

                if (!h.failureArms.isEmpty()) {
                    o.append("pub struct ").append(d.name).append(h.name).append("ProviderFailState<'a> {\n");
                    for (int clsIdx = 0; clsIdx < d.state.size(); clsIdx++) {
                        Field f = d.state.get(clsIdx);
                        o.append("    pub ").append(f.name).append(": &'a mut ").append(rustFieldType(f)).append(",\n");
                    }
                    for (DomainRoute r : d.routes) {
                        o.append("    pub ").append(r.name).append(": &'a ").append(routeRustType(r.type, domains)).append("Ref,\n");
                    }
                    o.append("    pub __lifetime: std::marker::PhantomData<&'a ()>,\n");
                    o.append("}\n\n");

                    for (int armIdx = 0; armIdx < h.failureArms.size(); armIdx++) {
                        FailureArm arm = h.failureArms.get(armIdx);
                        o.append("#[allow(unreachable_patterns, unused_labels, unused_variables, unreachable_code)]\n");
                        o.append("pub fn __moss_on_fail_").append(d.name).append("_").append(h.name).append("_").append(armIdx)
                         .append("(state: &mut ").append(d.name).append(h.name).append("ProviderFailState<'_>, __moss_error: __MossRaised) -> ").append(replyType).append(" {\n");
                        String pat = arm.patterns.isEmpty() ? "" : arm.patterns.get(0);
                        if (pat.contains("(") && pat.contains(")")) {
                            String varName = pat.substring(pat.indexOf('(') + 1, pat.indexOf(')')).trim();
                            o.append("    let ").append(varName).append(" = __moss_error.get_code();\n");
                        }
                        for (String capture : failureArmLocalUses(arm, declared)) {
                            if (stateFields.contains(capture)) continue;
                            String mossType = failureCaptureTypes.get(capture);
                            if (mossType == null || mossType.isEmpty()) {
                                throw new IllegalStateException("missing checked type for on_fail capture '"
                                        + capture + "' in " + d.name + "." + h.name);
                            }
                            o.append("    let ").append(capture)
                             .append(" = __moss_error.get_capture::<")
                             .append(rustType(mossType)).append(">(\"")
                             .append(capture).append("\");\n");
                        }
                        o.append("    let mut __moss_failure_reply: Option<").append(replyType).append("> = None;\n");
                        o.append("    'handler: {\n");
                        Set<String> armDeclared = new HashSet<>(declared);
                        genBlockStmts(o, arm.body, "        ", armDeclared, stateFields,
                                Collections.emptySet(), domains, objects, true, true,
                                false, false, failureCaptureTypes);
                        o.append("    }\n");
                        if (replyType.equals("()")) {
                            o.append("    ()\n");
                        } else {
                            o.append("    __moss_failure_reply.unwrap_or_else(|| std::process::abort())\n");
                        }
                        o.append("}\n\n");
                    }
                }
            }

            // Domain methods on Ref
            o.append("impl ").append(d.name).append("Ref {\n");
            for (Handler h : d.handlers) {
                HandlerSyncPlan plan = analyzeHandler(h, d);
                String replyTypeMoss = (h.replyType != null && !h.replyType.isEmpty())
                        ? h.replyType
                        : (h.replyTypeRef != null ? h.replyTypeRef.toCanonicalString() : "unit");
                String replyType = (!replyTypeMoss.equals("unit") && !replyTypeMoss.isEmpty()) ? rustType(replyTypeMoss) : "()";

                boolean hRaises = false;
                for (Stmt s : h.body) {
                    if (s.kind == Stmt.Kind.Raise || (s.text != null && s.text.trim().startsWith("raise ")) || !h.failureArms.isEmpty()) {
                        hRaises = true;
                        break;
                    }
                }
                String internalRet = hRaises ? ("Result<Option<" + replyType + ">, __MossRaised>") : replyType;

                o.append("    pub fn ").append(h.name).append("_shared(&self");
                for (Param p : h.params) {
                    o.append(", ").append(p.name).append(": ").append(rustType(p.type));
                }
                o.append(") -> ").append(internalRet).append(" {\n");
                o.append("        #[cfg(any(test, moss_perf))] moss_lock_event(\"handler_enter\", self.state.identity, self.state.handler_").append(h.name).append(", self.state.rank, usize::MAX, false);\n");

                // Acquire locks in ascending class rank order
                for (int clsIdx : plan.lockClasses) {
                    boolean isWrite = plan.classIsWrite.get(clsIdx);
                    o.append("        #[cfg(any(test, moss_perf))] moss_lock_event(\"lock_acquire\", self.state.identity, self.state.handler_").append(h.name).append(", self.state.rank, ").append(clsIdx).append(", ").append(isWrite).append(");\n");
                    if (isWrite) {
                        o.append("        let mut __class").append(clsIdx).append(" = moss_write_or_abort(&self.state.class").append(clsIdx).append(");\n");
                    } else {
                        o.append("        let __class").append(clsIdx).append(" = moss_read_or_abort(&self.state.class").append(clsIdx).append(");\n");
                    }
                    o.append("        #[cfg(any(test, moss_perf))] moss_lock_event(\"lock_acquired\", self.state.identity, self.state.handler_").append(h.name).append(", self.state.rank, ").append(clsIdx).append(", ").append(isWrite).append(");\n");
                }

                // Construct projection
                o.append("        let mut state = ").append(d.name).append(h.name).append("State {\n");
                for (int clsIdx : plan.lockClasses) {
                    Field f = d.state.get(clsIdx);
                    boolean isWrite = plan.classIsWrite.get(clsIdx);
                    o.append("            ").append(f.name).append(": ")
                     .append(isWrite ? "&mut " : "&")
                     .append("__class").append(clsIdx).append(".__moss_storage_").append(f.name.length()).append("_").append(f.name).append(",\n");
                }
                for (DomainRoute r : d.routes) {
                    o.append("            ").append(r.name).append(": &self.").append(r.name).append(",\n");
                }
                o.append("            __lifetime: std::marker::PhantomData,\n");
                o.append("        };\n");

                // Invoke handler body
                o.append("        let result = __moss_body_").append(d.name).append("_").append(h.name).append("(&mut state");
                for (Param p : h.params) {
                    o.append(", ").append(p.name);
                }
                o.append(");\n");

                // Release locks in reverse order
                o.append("        #[cfg(any(test, moss_perf))] moss_lock_event(\"handler_complete\", self.state.identity, self.state.handler_").append(h.name).append(", self.state.rank, usize::MAX, false);\n");
                for (int idx = plan.lockClasses.size() - 1; idx >= 0; idx--) {
                    int clsIdx = plan.lockClasses.get(idx);
                    boolean isWrite = plan.classIsWrite.get(clsIdx);
                    o.append("        drop(__class").append(clsIdx).append(");\n");
                    o.append("        #[cfg(any(test, moss_perf))] moss_lock_event(\"lock_release\", self.state.identity, self.state.handler_").append(h.name).append(", self.state.rank, ").append(clsIdx).append(", ").append(isWrite).append(");\n");
                }
                o.append("        #[cfg(any(test, moss_perf))] moss_lock_event(\"handler_exit\", self.state.identity, self.state.handler_").append(h.name).append(", self.state.rank, usize::MAX, false);\n");
                o.append("        result\n");
                o.append("    }\n");

                // Root wrapper
                o.append("    pub fn ").append(h.name).append("_root(&self");
                for (Param p : h.params) {
                    o.append(", ").append(p.name).append(": ").append(rustType(p.type));
                }
                o.append(") -> ").append(replyType).append(" {\n");
                if (!h.failureArms.isEmpty()) {
                    o.append("        let __moss_attempt = self.").append(h.name).append("_shared(");
                    for (int pIdx = 0; pIdx < h.params.size(); pIdx++) {
                        if (pIdx > 0) o.append(", ");
                        o.append(h.params.get(pIdx).name);
                    }
                    o.append(");\n");
                    o.append("        match __moss_attempt {\n");
                    if (replyType.equals("()")) {
                        o.append("            Ok(_) => (),\n");
                    } else {
                        o.append("            Ok(__val) => __val.unwrap_or_else(|| std::process::abort()),\n");
                    }
                    o.append("            Err(__moss_error) => {\n");
                    for (int i = 0; i < d.state.size(); i++) {
                        o.append("                let mut __class").append(i).append(" = moss_write_or_abort(&self.state.class").append(i).append(");\n");
                    }
                    o.append("                let mut failure_state = ").append(d.name).append(h.name).append("ProviderFailState {\n");
                    for (int i = 0; i < d.state.size(); i++) {
                        Field f = d.state.get(i);
                        o.append("                    ").append(f.name).append(": &mut __class").append(i).append(".__moss_storage_").append(f.name.length()).append("_").append(f.name).append(",\n");
                    }
                    for (DomainRoute r : d.routes) {
                        o.append("                    ").append(r.name).append(": &self.").append(r.name).append(",\n");
                    }
                    o.append("                    __lifetime: std::marker::PhantomData,\n");
                    o.append("                };\n");
                    if (h.failureArms.size() == 1) {
                        o.append("                let __res = __moss_on_fail_").append(d.name).append("_").append(h.name).append("_0(&mut failure_state, __moss_error);\n");
                    } else {
                        o.append("                let __res = ");
                        for (int armIdx = 0; armIdx < h.failureArms.size(); armIdx++) {
                            FailureArm arm = h.failureArms.get(armIdx);
                            if (armIdx == h.failureArms.size() - 1 || arm.patterns.isEmpty()) {
                                if (armIdx > 0) o.append(" else {\n                    ");
                                o.append("__moss_on_fail_").append(d.name).append("_").append(h.name).append("_").append(armIdx).append("(&mut failure_state, __moss_error)\n");
                                if (armIdx > 0) o.append("                };\n");
                                break;
                            } else {
                                if (armIdx > 0) o.append(" else ");
                                List<String> checks = new ArrayList<>();
                                for (String pat : arm.patterns) {
                                    String clean = pat.trim();
                                    if (clean.contains("(")) clean = clean.substring(0, clean.indexOf('(')).trim();
                                    String rustPat = clean.replace(".", "::");
                                    String shortVar = clean.contains(".") ? clean.substring(clean.lastIndexOf('.') + 1) : clean;
                                    checks.add("__moss_error.message.contains(\"" + rustPat + "\") || __moss_error.message.contains(\"" + clean + "\") || __moss_error.message.contains(\"::" + shortVar + "\")");
                                }
                                o.append("if ").append(String.join(" || ", checks)).append(" {\n");
                                o.append("                    __moss_on_fail_").append(d.name).append("_").append(h.name).append("_").append(armIdx).append("(&mut failure_state, __moss_error)\n");
                                o.append("                }");
                            }
                        }
                    }
                    for (int i = d.state.size() - 1; i >= 0; i--) {
                        o.append("                drop(__class").append(i).append(");\n");
                    }
                    o.append("                __res\n");
                    o.append("            }\n");
                    o.append("        }\n");
                } else if (hRaises) {
                    o.append("        let __moss_attempt = self.").append(h.name).append("_shared(");
                    for (int pIdx = 0; pIdx < h.params.size(); pIdx++) {
                        if (pIdx > 0) o.append(", ");
                        o.append(h.params.get(pIdx).name);
                    }
                    o.append(");\n");
                    o.append("        match __moss_attempt {\n");
                    if (replyType.equals("()")) {
                        o.append("            Ok(_) => (),\n");
                        o.append("            Err(_) => std::process::abort(),\n");
                    } else {
                        o.append("            Ok(__val) => __val.unwrap_or_else(|| std::process::abort()),\n");
                        o.append("            Err(_) => std::process::abort(),\n");
                    }
                    o.append("        }\n");
                } else {
                    o.append("        self.").append(h.name).append("_shared(");
                    for (int pIdx = 0; pIdx < h.params.size(); pIdx++) {
                        if (pIdx > 0) o.append(", ");
                        o.append(h.params.get(pIdx).name);
                    }
                    o.append(")\n");
                }
                o.append("    }\n");
                o.append("    #[inline]\n");
                o.append("    pub fn ").append(h.name).append("(&self");
                for (Param p : h.params) {
                    o.append(", ").append(p.name).append(": ").append(rustType(p.type));
                }
                o.append(") -> ").append(replyType).append(" {\n");
                o.append("        self.").append(h.name).append("_root(");
                for (int pIdx = 0; pIdx < h.params.size(); pIdx++) {
                    if (pIdx > 0) o.append(", ");
                    o.append(h.params.get(pIdx).name);
                }
                o.append(")\n");
                o.append("    }\n");
            }
            o.append("}\n\n");

            // Constructors
            o.append("pub fn construct_").append(d.name).append("(");
            int pCount = 0;
            for (DomainRoute r : d.routes) {
                if (pCount++ > 0) o.append(", ");
                o.append(r.name).append(": ").append(routeRustType(r.type, domains)).append("Ref");
            }
            for (Field f : d.state) {
                if (pCount++ > 0) o.append(", ");
                o.append(f.name).append(": ").append(rustFieldType(f));
            }
            o.append(") -> ").append(d.name).append("Ref {\n");
            o.append("    construct_").append(d.name).append("_with_rank(");
            for (DomainRoute r : d.routes) {
                o.append(r.name).append(", ");
            }
            for (Field f : d.state) {
                o.append(f.name).append(", ");
            }
            o.append("\"main::").append(d.name.toLowerCase()).append("\", 0)\n");
            o.append("}\n\n");

            o.append("pub fn construct_").append(d.name).append("_with_rank(");
            pCount = 0;
            for (DomainRoute r : d.routes) {
                if (pCount++ > 0) o.append(", ");
                o.append(r.name).append(": ").append(routeRustType(r.type, domains)).append("Ref");
            }
            for (Field f : d.state) {
                if (pCount++ > 0) o.append(", ");
                o.append(f.name).append(": ").append(rustFieldType(f));
            }
            if (pCount > 0) o.append(", ");
            o.append("__identity: &'static str, __rank: usize) -> ").append(d.name).append("Ref {\n");
            o.append("    ").append(d.name).append("Ref {\n");
            o.append("        state: std::sync::Arc::new(").append(d.name).append("Runtime {\n");
            for (int i = 0; i < d.state.size(); i++) {
                Field f = d.state.get(i);
                o.append("            class").append(i).append(": std::sync::RwLock::new(").append(d.name).append("Class").append(i).append(" { __moss_storage_").append(f.name.length()).append("_").append(f.name).append(": ").append(f.name).append(" }),\n");
            }
            o.append("            immutable: ").append(d.name).append("Immutable {},\n");
            o.append("            #[cfg(any(test, moss_perf))] identity: __identity,\n");
            o.append("            #[cfg(any(test, moss_perf))] rank: __rank,\n");
            for (Handler h : d.handlers) {
                o.append("            #[cfg(any(test, moss_perf))] handler_").append(h.name).append(": \"domain-specialization:").append(d.name).append("::handler:").append(h.name).append("\",\n");
            }
            o.append("        }),\n");
            for (DomainRoute r : d.routes) {
                o.append("        ").append(r.name).append(",\n");
            }
            o.append("    }\n");
            o.append("}\n\n");
        }

        // Functions
        for (Function fn : program.functions) {
            if (genericFunctions.contains(fn.name) || isExternalSourceFreeFunction(fn, genericFunctions)) {
                continue;
            }
            String fnIdentity;
            String fnGeneratedSymbol = fn.name;
            fnIdentity = "fn:" + fn.name + "@" + fn.line;
            String fnNative = DebugMapGenerator.toolingNativeSymbol("function", fn.name, fnIdentity);
            o.append("// Moss tooling begin|function|").append(fnIdentity).append("|").append(fnGeneratedSymbol).append("|").append(fnNative).append("\n");
            o.append("// Moss line ").append(fn.line).append(": fn ").append(fn.name).append("\n");
            if (debugBuild) o.append("#[inline(never)]\n");
            o.append("#[export_name = \"").append(fnNative).append("\"]\n");
            if (fn.exported) {
                o.append("pub ");
            }
            o.append("fn ").append(fnGeneratedSymbol).append("(");
            Set<String> declared = new HashSet<>();
            Set<String> writeParams = new HashSet<>();
            for (int i = 0; i < fn.params.size(); i++) {
                if (i > 0) o.append(", ");
                Param p = fn.params.get(i);
                String pType = (p.type != null && !p.type.isEmpty())
                        ? rustType(p.type)
                        : (p.typeRef != null ? rustType(p.typeRef.toCanonicalString()) : "i64");
                boolean checkerConsumesParameter = i < fn.parameterAccessModes.size()
                        && fn.parameterAccessModes.get(i)
                                == org.mosslang.compiler.types.AccessMode.Consume;
                boolean paramMutated = currentWriteParamIndices.get()
                        .getOrDefault(fn.name, Collections.emptySet()).contains(i);
                for (Stmt s : fn.body) {
                    if (s.kind == Stmt.Kind.Assign && p.name.equals(s.a != null ? s.a.trim() : "")) {
                        paramMutated = true;
                        break;
                    }
                    if (s.text != null && (s.text.trim().startsWith(p.name + " =") || s.text.trim().startsWith(p.name + "="))) {
                        paramMutated = true;
                        break;
                    }
                }
                if (paramMutated) {
                    writeParams.add(p.name);
                    pType = "&mut " + pType;
                } else if (!checkerConsumesParameter && !fn.exported
                        && p.type != null && p.type.equalsIgnoreCase("vector")) {
                    pType = "&std::vec::Vec<i64>";
                } else if (!checkerConsumesParameter && !fn.exported
                        && p.type != null && objects.containsKey(p.type)) {
                    pType = "&" + pType;
                } else if (!checkerConsumesParameter && !fn.exported
                        && (objects.containsKey(p.type) || objects.containsKey(rustType(p.type))
                        || objects.keySet().stream().anyMatch(k -> k.endsWith("__" + p.type) || k.equals(p.type)))) {
                    pType = "&" + pType;
                }
                o.append(p.name).append(": ").append(pType);
                declared.add(p.name);
            }
            o.append(")");
            boolean fnRaises = raisingFunctions.contains(fn.name);
            boolean isVoidWriteFn = writeFunctions.contains(fn.name) && fn.resultExpression != null
                    && !fn.resultExpression.contains(".pop(") && !fn.resultExpression.contains(".delete(");
            String fnRetMoss = (fn.returnType != null && !fn.returnType.isEmpty())
                    ? fn.returnType
                    : (fn.returnTypeRef != null ? fn.returnTypeRef.toCanonicalString() : (fn.resultExpression != null && !isVoidWriteFn ? "i64" : "unit"));
            String fnRetRust = (!fnRetMoss.equals("unit") && !fnRetMoss.isEmpty()) ? rustType(fnRetMoss) : "()";
            if (fnRaises) {
                o.append(" -> Result<").append(fnRetRust).append(", __MossRaised>");
            } else if (!fnRetRust.equals("()")) {
                o.append(" -> ").append(fnRetRust);
            }
            o.append(" {\n");

            currentWriteParams.set(writeParams);
            currentEnclosingIdentity.set(fnIdentity);
            genBlockStmts(o, fn.body, "    ", declared, Collections.emptySet(), Collections.emptySet(), domains, objects, false, false, false, fnRaises);
            if (fn.resultExpression != null) {
                emitFunctionalPipelineMarkers(o, fnIdentity, fn.resultLine, fn.resultExpression, "    ");
                currentInTry.set(fnRaises);
                String retVal = lowerExpr(fn.resultExpression, Collections.emptySet(), Collections.emptySet(), domains, objects, false);
                currentInTry.set(false);
                if (fnRaises) {
                    o.append("    return Ok(").append(retVal).append(");\n");
                } else if (fnRetRust.equals("()")) {
                    o.append("    ").append(retVal).append(";\n");
                } else {
                    o.append("    return ").append(retVal).append(";\n");
                }
            } else if (fnRaises && fnRetRust.equals("()")) {
                o.append("    Ok(())\n");
            }
            currentWriteParams.set(Collections.emptySet());
            currentEnclosingIdentity.remove();
            o.append("}\n");
            o.append("// Moss tooling end|").append(fnIdentity).append("\n\n");

            if (!fnGeneratedSymbol.equals(fn.name)) {
                o.append("#[inline(always)]\n");
                o.append("fn ").append(fn.name).append("(values: std::vec::Vec<i64>) -> i64 {\n");
                o.append("    ").append(fnGeneratedSymbol).append("(&values)\n");
                o.append("}\n\n");
            }
        }

        // Main
        if (program.main != null) {
            String mainIdentity = "main@" + program.main.line;
            o.append("// Moss tooling begin|main|").append(mainIdentity).append("|main|moss__main\n");
            o.append("// Moss line ").append(program.main.line).append(": fn main():\n");
            if (debugBuild) o.append("#[inline(never)]\n");
            o.append("#[export_name = \"moss__main\"]\n");
            o.append("fn main() {\n");
            Set<String> declared = new HashSet<>();
            currentEnclosingIdentity.set(mainIdentity);
            genBlockStmts(o, program.main.body, "    ", declared, Collections.emptySet(), Collections.emptySet(), domains, objects, false, false, false, false);
            currentEnclosingIdentity.remove();
            o.append("}\n");
            o.append("// Moss tooling end|").append(mainIdentity).append("\n");
        } else {
            o.append("fn main() {}\n");
        }

        currentRaisingFunctions.remove();
        return o.toString();
    }

    private static boolean expressionCallsRaisingFunction(String expression, Set<String> raisingFunctions) {
        if (expression == null || expression.isEmpty() || raisingFunctions.isEmpty()) return false;
        for (int i = 0; i < expression.length();) {
            char ch = expression.charAt(i);
            if (ch == '"' || ch == '\'') {
                char quote = ch;
                i++;
                while (i < expression.length()) {
                    char current = expression.charAt(i++);
                    if (current == '\\' && i < expression.length()) i++;
                    else if (current == quote) break;
                }
                continue;
            }
            if (!Character.isLetter(ch) && ch != '_') {
                i++;
                continue;
            }
            int start = i++;
            while (i < expression.length()
                    && (Character.isLetterOrDigit(expression.charAt(i)) || expression.charAt(i) == '_')) i++;
            String identifier = expression.substring(start, i);
            int next = i;
            while (next < expression.length() && Character.isWhitespace(expression.charAt(next))) next++;
            StringBuilder qualified = new StringBuilder(identifier);
            while (next < expression.length() && expression.charAt(next) == '.') {
                int member = next + 1;
                while (member < expression.length() && Character.isWhitespace(expression.charAt(member))) member++;
                if (member >= expression.length()
                        || (!Character.isLetter(expression.charAt(member)) && expression.charAt(member) != '_')) {
                    break;
                }
                int memberEnd = member + 1;
                while (memberEnd < expression.length()
                        && (Character.isLetterOrDigit(expression.charAt(memberEnd))
                                || expression.charAt(memberEnd) == '_')) memberEnd++;
                qualified.append('.').append(expression, member, memberEnd);
                next = memberEnd;
                while (next < expression.length() && Character.isWhitespace(expression.charAt(next))) next++;
            }
            if (next < expression.length() && expression.charAt(next) == '('
                    && raisingFunctions.contains(qualified.toString())) {
                return true;
            }
        }
        return false;
    }

    private static void emitFunctionalPipelineMarkers(StringBuilder out, String enclosingIdentity,
                                                      int sourceLine, String expression, String indent) {
        if (expression == null || !expression.contains("|>")) return;
        List<String> stages = splitPipeline(expression);
        if (stages.size() < 2 || sourceLine <= 0) return;
        String owner = enclosingIdentity != null ? enclosingIdentity : "main";
        int at = owner.indexOf('@');
        if (at >= 0) owner = owner.substring(0, at);
        String identity = owner + "@" + sourceLine + ":expression:0";
        List<String> provenance = new ArrayList<>();
        provenance.add(identity + ":source");
        for (int i = 1; i < stages.size(); i++) provenance.add(identity + ":stage:" + i);
        out.append(indent).append("// Moss backend: EAGER FUNCTIONAL PIPELINE semantics (semantic ")
                .append(identity).append("; provenance ").append(String.join(" ", provenance)).append(")\n");
        for (String origin : provenance) {
            out.append(indent).append("// Moss functional origin|").append(origin).append("\n");
        }
    }

    private static void emitGenericMacro(StringBuilder o, Function fn, Set<String> writeFunctions,
                                         Map<String, Domain> domains, Map<String, ObjectType> objects) {
        o.append("#[macro_export]\nmacro_rules! __moss_fn_").append(fn.name).append(" {\n    (");
        for (int i = 0; i < fn.params.size(); i++) {
            if (i > 0) o.append(", ");
            o.append("$").append(fn.params.get(i).name).append(":expr");
        }
        o.append(") => {{\n");
        Set<String> mutatedParams = new HashSet<>();
        for (int i = 0; i < fn.params.size(); i++) {
            String pName = fn.params.get(i).name;
            if (i == 0 && writeFunctions.contains(fn.name)) {
                mutatedParams.add(pName);
            }
            if (fn.resultExpression != null && fn.resultExpression.contains(".delete(")) {
                int dp = fn.resultExpression.indexOf(".delete(");
                String inside = fn.resultExpression.substring(dp + 8, fn.resultExpression.lastIndexOf(')')).trim();
                List<String> dArgs = splitTopLevel(inside, ',');
                if (dArgs.size() >= 3 && dArgs.get(2).trim().equals(pName)) {
                    mutatedParams.add(pName);
                }
            }
            for (Stmt s : fn.body) {
                if (s.text != null && s.text.contains(".delete(")) {
                    int dp = s.text.indexOf(".delete(");
                    String inside = s.text.substring(dp + 8, s.text.lastIndexOf(')')).trim();
                    List<String> dArgs = splitTopLevel(inside, ',');
                    if (dArgs.size() >= 3 && dArgs.get(2).trim().equals(pName)) {
                        mutatedParams.add(pName);
                    }
                }
            }
        }
        Set<String> declared = new HashSet<>();
        for (Param p : fn.params) {
            declared.add(p.name);
            if (!mutatedParams.contains(p.name)) {
                o.append("        #[allow(unused_mut)]\n        let mut ").append(p.name).append(" = ($").append(p.name).append(").clone();\n");
            }
        }
        StringBuilder bodySb = new StringBuilder();
        genBlockStmts(bodySb, fn.body, "        ", declared, Collections.emptySet(), Collections.emptySet(), domains, objects, false, false, false, false);
        if (fn.resultExpression != null && !fn.resultExpression.isEmpty()) {
            String resLowered = lowerExpr(fn.resultExpression, Collections.emptySet(), domains, objects);
            boolean isVoidWrite = writeFunctions.contains(fn.name)
                    && !fn.resultExpression.contains(".pop(") && !fn.resultExpression.contains(".delete(");
            if (isVoidWrite) {
                bodySb.append("        ").append(resLowered).append(";\n");
            } else {
                bodySb.append("        ").append(resLowered).append("\n");
            }
        }
        String bodyStr = bodySb.toString();
        bodyStr = bodyStr.replaceAll("(?m)^(\\s*)return\\s+([^;]+);\\s*$", "$1$2");
        for (String mp : mutatedParams) {
            bodyStr = bodyStr.replaceAll("\\b" + mp + "\\b", "(\\$" + mp + ")");
        }
        o.append(bodyStr);
        o.append("    }};\n}\n\n");
    }

    private static String rustFieldType(Field f) {
        if (f.type != null && !f.type.isEmpty()) {
            return rustType(f.type);
        }
        if (f.init != null && !f.init.isEmpty()) {
            String init = f.init.trim();
            if (init.matches("^-?\\d+$")) return "i64";
            if (init.matches("^-?\\d+\\.\\d+$")) return "f64";
            if (init.startsWith("\"")) return "MossStr";
            if (init.equals("true") || init.equals("false")) return "bool";
            if (init.startsWith("Vector[") && init.endsWith("]()")) {
                String inner = init.substring(7, init.length() - 3).trim();
                return "std::vec::Vec<" + rustType(inner) + ">";
            }
        }
        return "i64";
    }

    private static String rustType(String mossType) {
        if (mossType == null || mossType.isEmpty()) return "()";
        if (mossType.toLowerCase().startsWith("option[") && mossType.endsWith("]")) {
            String inner = mossType.substring(7, mossType.length() - 1).trim();
            return "Option<" + rustType(inner) + ">";
        }
        if (mossType.toLowerCase().startsWith("vector[") && mossType.endsWith("]")) {
            String inner = mossType.substring(7, mossType.length() - 1).trim();
            return "std::vec::Vec<" + rustType(inner) + ">";
        }
        if (mossType.toLowerCase().startsWith("map[") && mossType.endsWith("]")) {
            List<String> kv = splitTopLevel(mossType.substring(4, mossType.length() - 1).trim(), ',');
            String k = kv.size() > 0 ? rustType(kv.get(0)) : "MossStr";
            String v = kv.size() > 1 ? rustType(kv.get(1)) : "i64";
            return "std::collections::BTreeMap<" + k + ", " + v + ">";
        }
        if (mossType.toLowerCase().startsWith("queue[") && mossType.endsWith("]")) {
            String inner = mossType.substring(6, mossType.length() - 1).trim();
            return "std::collections::VecDeque<" + rustType(inner) + ">";
        }
        if (mossType.endsWith("]") && mossType.contains("[")) {
            int br = mossType.indexOf('[');
            String base = mossType.substring(0, br).trim();
            String inside = mossType.substring(br + 1, mossType.length() - 1).trim();
            Program prog = currentProgram.get();
            boolean isGeneric = true;
            if (prog != null) {
                for (ObjectType obj : prog.objects) {
                    if (obj.name.equals(base)) {
                        boolean hasOpen = obj.fields.stream().anyMatch(f -> f.type == null || f.type.isEmpty());
                        if (!hasOpen) {
                            isGeneric = false;
                        }
                        break;
                    }
                }
            }
            if (!isGeneric) {
                return sourceFreeRustName(base, prog);
            }
            List<String> parts = splitTopLevel(inside, ',');
            List<String> loweredParts = new ArrayList<>();
            for (String p : parts) {
                p = p.trim();
                if (p.contains("=")) {
                    p = p.split("=", 2)[1].trim();
                }
                loweredParts.add(rustType(p));
            }
            return sourceFreeRustName(base, prog) + "<" + String.join(", ", loweredParts) + ">";
        }
        String sourceFreeName = sourceFreeRustName(mossType, currentProgram.get());
        if (!sourceFreeName.equals(mossType)) return sourceFreeName;
        if (mossType.contains(".")) {
            return mossType.replace(".", "__");
        }
        return switch (mossType) {
            case "int", "Int" -> "i64";
            case "float", "Float" -> "f64";
            case "bool", "Bool" -> "bool";
            case "string", "String" -> "MossStr";
            case "char", "Char" -> "char";
            case "unit" -> "()";
            case "vector", "Vector" -> "std::vec::Vec<i64>";
            case "range", "Range" -> "__MossRange";
            case "FileIO" -> "__MossFile";
            case "Executor" -> "__MossExecutor";
            default -> mossType.endsWith("Ref") ? mossType : mossType;
        };
    }

    private static String sourceFreeRustName(String mossType, Program program) {
        if (program == null || mossType == null) return mossType;
        for (ObjectType object : program.objects) {
            if (!object.sourceFree || !object.name.equals(mossType)) continue;
            if (object.definingModule == null || object.definingModule.isEmpty()
                    || object.declarationName == null || object.declarationName.isEmpty()) {
                return mossType;
            }
            return "moss_" + object.definingModule + "::" + object.declarationName;
        }
        for (EnumType enumeration : program.enums) {
            if (!enumeration.sourceFree || !enumeration.name.equals(mossType)) continue;
            if (enumeration.definingModule == null || enumeration.definingModule.isEmpty()
                    || enumeration.declarationName == null || enumeration.declarationName.isEmpty()) {
                return mossType;
            }
            return "moss_" + enumeration.definingModule + "::" + enumeration.declarationName;
        }
        for (Domain domain : program.domains) {
            if (!domain.sourceFree || !domain.name.equals(mossType)) continue;
            if (domain.definingModule == null || domain.definingModule.isEmpty()
                    || domain.declarationName == null || domain.declarationName.isEmpty()) {
                return mossType;
            }
            return "moss_" + domain.definingModule + "::" + domain.declarationName;
        }
        return mossType;
    }

    private static boolean isExternalSourceFreeFunction(Function function,
                                                        Set<String> genericFunctions) {
        return function.sourceFreeAbiVersion > 0 && !genericFunctions.contains(function.name);
    }

    private static String sourceFreeFunctionRustPath(Function function, String fallbackName,
                                                     Set<String> genericFunctions) {
        if (function == null || !isExternalSourceFreeFunction(function, genericFunctions)) {
            return fallbackName;
        }
        String module = function.definingModule;
        String declaration = function.declarationName;
        if (module == null || module.isBlank()) {
            int separator = function.name.indexOf('.');
            if (separator < 0) separator = function.name.indexOf("__");
            if (separator > 0) module = function.name.substring(0, separator);
        }
        if (declaration == null || declaration.isBlank()) {
            String prefix = module == null ? "" : module + ".";
            if (!prefix.isEmpty() && function.name.startsWith(prefix)) {
                declaration = function.name.substring(prefix.length());
            } else {
                prefix = module == null ? "" : module + "__";
                declaration = !prefix.isEmpty() && function.name.startsWith(prefix)
                        ? function.name.substring(prefix.length()) : function.name;
            }
        }
        if (module == null || module.isBlank() || declaration == null || declaration.isBlank()) {
            return fallbackName;
        }
        return "moss_" + module + "::" + declaration;
    }

    private static void genBlockStmts(StringBuilder o, List<Stmt> stmts, String indent,
                                      Set<String> declared, Set<String> stateFields,
                                      Set<String> objFields, Map<String, Domain> domains,
                                      Map<String, ObjectType> objects,
                                      boolean inHandler, boolean inMethod, boolean canRaise) {
        genBlockStmts(o, stmts, indent, declared, stateFields, objFields, domains,
                objects, inHandler, false, inMethod, canRaise, Collections.emptyMap());
    }

    private static void genBlockStmts(StringBuilder o, List<Stmt> stmts, String indent,
                                      Set<String> declared, Set<String> stateFields,
                                      Set<String> objFields, Map<String, Domain> domains,
                                      Map<String, ObjectType> objects,
                                      boolean inHandler, boolean inFailureArm, boolean inMethod, boolean canRaise) {
        genBlockStmts(o, stmts, indent, declared, stateFields, objFields, domains,
                objects, inHandler, inFailureArm, inMethod, canRaise, Collections.emptyMap());
    }

    private static void genBlockStmts(StringBuilder o, List<Stmt> stmts, String indent,
                                      Set<String> declared, Set<String> stateFields,
                                      Set<String> objFields, Map<String, Domain> domains,
                                      Map<String, ObjectType> objects,
                                      boolean inHandler, boolean inFailureArm, boolean inMethod,
                                      boolean canRaise, Map<String, String> failureCaptureTypes) {
        if (stmts == null) return;
        Deque<Integer> blockIndents = new ArrayDeque<>();
        Deque<Boolean> blockIsMatch = new ArrayDeque<>();
        int caseIndent = -1;
        boolean inTry = false;
        for (Stmt s : stmts) {
            if (s == null) continue;
            if (caseIndent >= 0 && s.indent <= caseIndent) {
                o.append(indent).append("    }\n");
                caseIndent = -1;
            }
            while (!blockIndents.isEmpty() && s.indent <= blockIndents.peek()) {
                blockIndents.pop();
                boolean wasMatch = blockIsMatch.pop();
                if (wasMatch) {
                    o.append(indent).append("    _ => {}\n");
                }
                o.append(indent).append("}\n");
            }
            currentInTry.set(inTry || canRaise);
            if (s.line > 0) {
                o.append(indent).append("// Moss line ").append(s.line).append(": ")
                 .append(s.text != null ? s.text : s.kind.name().toLowerCase()).append("\n");
            }
            if (s.kind == Stmt.Kind.Let || s.kind == Stmt.Kind.Var || s.kind == Stmt.Kind.Assign) {
                emitFunctionalPipelineMarkers(o, currentEnclosingIdentity.get(), s.line, s.b, indent);
            } else if (s.kind == Stmt.Kind.Return || s.kind == Stmt.Kind.Reply) {
                emitFunctionalPipelineMarkers(o, currentEnclosingIdentity.get(), s.line, s.a, indent);
            } else if (s.kind == Stmt.Kind.Echo) {
                for (String arg : s.args) emitFunctionalPipelineMarkers(o, currentEnclosingIdentity.get(), s.line, arg, indent);
            } else if (s.kind == Stmt.Kind.Call) {
                emitFunctionalPipelineMarkers(o, currentEnclosingIdentity.get(), s.line, s.text, indent);
            }
            if (s.kind == Stmt.Kind.If) {
                String cond = s.a != null ? s.a.trim() : (s.text != null && s.text.contains("if ") ? s.text.substring(s.text.indexOf("if ") + 3).trim() : "");
                if (cond.endsWith(":")) cond = cond.substring(0, cond.length() - 1).trim();
                if (cond.startsWith("not ")) cond = "!(" + cond.substring(4).trim() + ")";
                o.append(indent).append("if ").append(lowerExpr(cond, stateFields, objFields, domains, objects)).append(" {\n");
                blockIndents.push(s.indent);
                blockIsMatch.push(false);
            } else if (s.kind == Stmt.Kind.For) {
                String loopVar = s.a != null ? s.a.trim() : "_i";
                String iterExpr = s.b != null ? s.b.trim() : "";
                declared.add(loopVar);
                if (iterExpr.startsWith("range(") && iterExpr.endsWith(")")) {
                    List<String> rArgs = splitTopLevel(iterExpr.substring(6, iterExpr.length() - 1).trim(), ',');
                    String start = rArgs.size() > 0 ? lowerExpr(rArgs.get(0).trim(), stateFields, objFields, domains, objects) : "0_i64";
                    String end = rArgs.size() > 1 ? lowerExpr(rArgs.get(1).trim(), stateFields, objFields, domains, objects) : "0_i64";
                    o.append(indent).append("for ").append(loopVar).append(" in (").append(start).append(")..(").append(end).append(") {\n");
                } else {
                    o.append(indent).append("for ").append(loopVar).append(" in (").append(lowerExpr(iterExpr, stateFields, objFields, domains, objects)).append(").clone() {\n");
                }
                blockIndents.push(s.indent);
                blockIsMatch.push(false);
            } else if (s.kind == Stmt.Kind.Else) {
                o.append(indent).append("else {\n");
                blockIndents.push(s.indent);
                blockIsMatch.push(false);
            } else if (s.kind == Stmt.Kind.Match) {
                String scrut = s.a != null ? s.a.trim() : "";
                String loweredScrut = lowerExpr(scrut, stateFields, objFields, domains, objects);
                String matchValue = "consume".equals(s.b) ? loweredScrut : "&(" + loweredScrut + ")";
                o.append(indent).append("match ").append(matchValue).append(" {\n");
                blockIndents.push(s.indent);
                blockIsMatch.push(true);
            } else if (s.kind == Stmt.Kind.Case) {
                String caseName = s.a != null ? s.a.trim() : "";
                String rustEnum = s.semanticType != null
                        ? sourceFreeRustName(s.semanticType.replace(".", "__"), currentProgram.get()) : "";
                o.append(indent).append("    ").append(rustEnum).append("::").append(caseName);
                if (s.args != null && !s.args.isEmpty()) {
                    o.append(" { ");
                    for (int ai = 0; ai < s.args.size(); ai++) {
                        if (ai > 0) o.append(", ");
                        String binder = s.args.get(ai);
                        if ("consume".equals(s.b)) {
                            o.append(binder).append(": ").append(binder);
                        } else {
                            String refName = "__moss_ref_" + binder;
                            o.append(binder).append(": ").append(refName);
                        }
                    }
                    o.append(" }");
                }
                o.append(" => {\n");
                if (!"consume".equals(s.b)) {
                    for (String binder : s.args) {
                        o.append(indent).append("        let ").append(binder).append(" = (*__moss_ref_")
                         .append(binder).append(").clone();\n");
                    }
                }
                if (s.args != null) declared.addAll(s.args);
                caseIndent = s.indent;
            } else if (s.kind == Stmt.Kind.Try) {
                inTry = true;
                currentInTry.set(true);
                o.append(indent).append("let mut __moss_try_res = (|| -> Result<(), __MossRaised> {\n");
            } else if (s.kind == Stmt.Kind.Recover) {
                if (inTry) {
                    inTry = false;
                    currentInTry.set(canRaise);
                    o.append(indent).append("    Ok(())\n");
                    o.append(indent).append("})();\n");
                }
                String armPat = s.a != null ? s.a.trim() : (s.text != null ? s.text.trim() : "");
                List<String> variantChecks = new ArrayList<>();
                for (String subPat : armPat.split(",")) {
                    subPat = subPat.trim();
                    if (subPat.contains("(")) subPat = subPat.substring(0, subPat.indexOf('(')).trim();
                    if (!subPat.isEmpty()) {
                        String rustPat = subPat.replace(".", "::");
                        variantChecks.add("__moss_err.message.contains(\"" + rustPat + "\") || __moss_err.message.contains(\"" + subPat + "\")");
                    }
                }
                String guard = variantChecks.isEmpty() ? "" : (" if " + String.join(" || ", variantChecks));
                o.append(indent).append("if let (Err(ref __moss_err), true) = (&__moss_try_res, match &__moss_try_res { Err(__moss_err)").append(guard).append(" => true, _ => false }) {\n");
                if (armPat.contains("(") && armPat.contains(")")) {
                    String binding = armPat.substring(armPat.indexOf('(') + 1, armPat.lastIndexOf(')')).trim();
                    if (!binding.isEmpty()) {
                        declared.add(binding);
                        o.append(indent).append("    let ").append(binding).append(" = __moss_err.get_code();\n");
                    }
                }
                blockIndents.push(s.indent);
                blockIsMatch.push(false);
            } else if (s.kind == Stmt.Kind.Raise) {
                if (!inTry && !canRaise) {
                    o.append(indent).append("std::process::abort();\n");
                    continue;
                }
                String val = s.a != null ? s.a.trim() : (s.text != null && s.text.startsWith("raise ") ? s.text.substring(6).trim() : "");
                if (val.isEmpty() || val.equals("raise")) {
                    o.append(indent).append("return Err(__moss_err.clone());\n");
                    continue;
                }
                if (val.contains("(") && val.endsWith(")")) {
                    String openPart = val.substring(0, val.indexOf('(')).trim();
                    String argPart = val.substring(val.indexOf('(') + 1, val.length() - 1).trim();
                    String fieldVal = argPart.contains(":") ? argPart.split(":", 2)[1].trim() : argPart;
                    if (!fieldVal.matches("^-?\\d+$") && !fieldVal.startsWith("\"")) {
                        String loweredField = inMethod ? lowerObjExpr(fieldVal, objFields, domains, objects)
                                                       : lowerExpr(fieldVal, stateFields, objFields, domains, objects);
                        o.append(indent).append("let mut __moss_raised = __moss_raise(format!(\"").append(openPart.replace(".", "::")).append(" {{ code: {} }}\", ").append(loweredField).append("));\n");
                        o.append(indent).append("return Err(__moss_raised);\n");
                        continue;
                    }
                }
                String enumVal = lowerEnumExpr(val, stateFields, domains, objects);
                String raised = enumVal != null ? enumVal : lowerExpr(val, stateFields, objFields, domains, objects);
                o.append(indent).append("let mut __moss_raised = __moss_raise(\"").append(raised).append("\");\n");
                if (inHandler) {
                    for (String dVar : failureCaptureTypes.keySet()) {
                        if (declared.contains(dVar) && !stateFields.contains(dVar)
                                && !objFields.contains(dVar) && !dVar.equals("self")) {
                            o.append(indent).append("__moss_raised.captures.insert(\"")
                             .append(dVar).append("\".to_string(), Box::new(")
                             .append(dVar).append(".clone()));\n");
                        }
                    }
                }
                o.append(indent).append("return Err(__moss_raised);\n");
            } else if (s.kind == Stmt.Kind.Reply) {
                String val = s.a != null ? s.a.trim() : (s.text != null && s.text.startsWith("reply ") ? s.text.substring(6).trim() : "");
                if (inFailureArm) {
                    if (val.isEmpty()) {
                        o.append(indent).append("__moss_failure_reply = Some(()); break 'handler;\n");
                    } else {
                        o.append(indent).append("__moss_failure_reply = Some(").append(lowerExpr(val, stateFields, objFields, domains, objects)).append("); break 'handler;\n");
                    }
                } else if (inHandler) {
                    if (val.isEmpty()) {
                        o.append(indent).append("__moss_reply = Some(()); break 'handler;\n");
                    } else {
                        o.append(indent).append("__moss_reply = Some(").append(lowerExpr(val, stateFields, objFields, domains, objects)).append("); break 'handler;\n");
                    }
                } else {
                    o.append(indent).append("return ").append(lowerExpr(val, stateFields, objFields, domains, objects)).append(";\n");
                }
            } else if (s.kind == Stmt.Kind.Return) {
                String val = s.a != null ? s.a.trim() : (s.text != null && s.text.startsWith("return ") ? s.text.substring(7).trim() : "");
                if (!val.isEmpty() && !val.equals("return")) {
                    String lowered = lowerExpr(val, stateFields, objFields, domains, objects);
                    if (canRaise) {
                        o.append(indent).append("return Ok(").append(lowered).append(");\n");
                    } else {
                        o.append(indent).append("return ").append(lowered).append(";\n");
                    }
                } else {
                    if (canRaise) {
                        o.append(indent).append("return Ok(None);\n");
                    } else {
                        o.append(indent).append("return;\n");
                    }
                }
            } else if (s.kind == Stmt.Kind.Message) {
                String receiver = s.a != null ? s.a.trim() : "";
                String handler = s.b != null ? s.b.trim() : "";
                String target = receiver;
                if (!stateFields.isEmpty() || domains.containsKey(receiver)) {
                    target = "state." + receiver;
                }
                boolean targetRaises = false;
                for (Domain d : domains.values()) {
                    for (Handler h : d.handlers) {
                        if (h.name.equals(handler)) {
                            targetRaises |= h.sourceFreeMayRaise;
                            for (Stmt bs : h.body) {
                                if (bs.kind == Stmt.Kind.Raise || (bs.text != null && bs.text.trim().startsWith("raise "))) {
                                    targetRaises = true;
                                    break;
                                }
                            }
                            if (!h.failureArms.isEmpty()) targetRaises = true;
                        }
                    }
                }
                StringBuilder call = new StringBuilder();
                if (inTry && targetRaises) {
                    call.append(target).append(".").append(handler).append("_shared().map_err(|e| __moss_raise(format!(\"{:?}\", e)))?.unwrap_or_default()");
                } else if (inTry) {
                    call.append(target).append(".").append(handler).append("_shared()");
                } else {
                    call.append(target).append(".").append(handler).append("_root(");
                    for (int i = 0; i < s.args.size(); i++) {
                        if (i > 0) call.append(", ");
                        call.append(lowerExpr(s.args.get(i), stateFields, domains, objects));
                    }
                    call.append(")");
                }
                if (s.messageResult != null && !s.messageResult.isEmpty()) {
                    if (declared.add(s.messageResult)) {
                        o.append(indent).append("let mut ").append(s.messageResult).append(" = ").append(call).append(";\n");
                    } else {
                        o.append(indent).append(s.messageResult).append(" = ").append(call).append(";\n");
                    }
                } else {
                    o.append(indent).append(call).append(";\n");
                }
            } else if (inMethod) {
                genMethodStmt(o, s, indent, declared, objFields, domains, objects);
            } else {
                genStmt(o, s, indent, declared, stateFields, domains, objects);
            }
        }
        if (caseIndent >= 0) {
            o.append(indent).append("    }\n");
        }
        while (!blockIndents.isEmpty()) {
            blockIndents.pop();
            boolean wasMatch = blockIsMatch.pop();
            if (wasMatch) {
                o.append(indent).append("    _ => {}\n");
            }
            o.append(indent).append("}\n");
        }
        currentInTry.set(false);
    }

    /** Domain constructor arguments are copied (route handles are shared Arc capabilities). */
    private static String cloneIfIdent(String source, String lowered) {
        if (source != null && source.trim().matches("[A-Za-z_][A-Za-z0-9_]*")
                && !source.trim().equals("true") && !source.trim().equals("false")) {
            return lowered + ".clone()";
        }
        return lowered;
    }

    /** Resolve a domainroutes target type to the generated Rust domain name. */
    private static String routeRustType(String type, Map<String, Domain> domains) {
        if (type == null) return "";
        String t = type.trim();
        if (domains.containsKey(t)) {
            Domain domain = domains.get(t);
            return domain.sourceFree ? sourceFreeRustName(t, currentProgram.get()) : t;
        }
        String canonical = t.replace(".", "__");
        if (domains.containsKey(canonical)) {
            Domain domain = domains.get(canonical);
            return domain.sourceFree
                    ? sourceFreeRustName(canonical, currentProgram.get()) : canonical;
        }
        for (String k : domains.keySet()) {
            if (k.endsWith("__" + t)) {
                Domain domain = domains.get(k);
                return domain != null && domain.sourceFree
                        ? sourceFreeRustName(k, currentProgram.get()) : k;
            }
        }
        return t.contains(".") ? canonical : t;
    }

    private static String lowerEnumExpr(String expr, Set<String> stateFields, Map<String, Domain> domains,
                                        Map<String, ObjectType> objects) {
        if (expr == null || expr.isEmpty()) return null;
        String callee = expr;
        String args = null;
        if (expr.endsWith(")") && expr.contains("(")) {
            int op = expr.indexOf('(');
            callee = expr.substring(0, op).trim();
            args = expr.substring(op + 1, expr.length() - 1).trim();
        }
        int lastDot = callee.lastIndexOf('.');
        if (lastDot > 0) {
            String enumName = callee.substring(0, lastDot).trim();
            String caseName = callee.substring(lastDot + 1).trim();
            int subDot = enumName.lastIndexOf('.');
            String simpleEnum = subDot >= 0 ? enumName.substring(subDot + 1) : enumName;
            if (simpleEnum.contains("__")) {
                simpleEnum = simpleEnum.substring(simpleEnum.lastIndexOf("__") + 2);
            }
            if (!caseName.isEmpty() && Character.isUpperCase(caseName.charAt(0))
                    && !simpleEnum.isEmpty() && Character.isUpperCase(simpleEnum.charAt(0))) {
                String rustEnum = sourceFreeRustName(enumName.replace(".", "__"), currentProgram.get());
                StringBuilder res = new StringBuilder();
                res.append(rustEnum).append("::").append(caseName);
                if (args != null && !args.isEmpty()) {
                    res.append(" { ");
                    List<String> parts = splitTopLevel(args, ',');
                    for (int i = 0; i < parts.size(); i++) {
                        if (i > 0) res.append(", ");
                        String p = parts.get(i).trim();
                        if (p.contains(":")) {
                            String[] kv = p.split(":", 2);
                            res.append(kv[0].trim()).append(": ").append(lowerExpr(kv[1].trim(), stateFields, domains, objects));
                        } else if (p.contains("=")) {
                            String[] kv = p.split("=", 2);
                            res.append(kv[0].trim()).append(": ").append(lowerExpr(kv[1].trim(), stateFields, domains, objects));
                        } else {
                            res.append("code: ").append(lowerExpr(p, stateFields, domains, objects));
                        }
                    }
                    res.append(" }");
                }
                return res.toString();
            }
        }
        return null;
    }

    private static void genMethodStmt(StringBuilder o, Stmt s, String indent, Set<String> declared,
                                      Set<String> objFields, Map<String, Domain> domains,
                                      Map<String, ObjectType> objects) {
        if (s == null) return;
        if (s.line > 0) {
            o.append(indent).append("// Moss line ").append(s.line).append(": ")
             .append(s.text != null ? s.text : "").append("\n");
        }
        String t = s.text != null ? s.text.trim() : "";
        if (t.startsWith("echo ")) {
            String arg = t.substring(5).trim();
            o.append(indent).append("println!(\"{}\", ").append(lowerObjExpr(arg, objFields, domains, objects)).append(");\n");
            return;
        }
        switch (s.kind) {
            case Reply, Return -> {
                String expr = s.a != null ? s.a.trim() : "";
                if (!expr.isEmpty() && !expr.equals("return") && !expr.equals("reply")) {
                    o.append(indent).append("return ").append(lowerObjExpr(expr, objFields, domains, objects)).append(";\n");
                } else {
                    o.append(indent).append("return;\n");
                }
            }
            case Assign -> {
                String varName = s.a != null ? s.a.trim() : "";
                String expr = s.b != null ? s.b.trim() : "";
                if (objFields.contains(varName)) {
                    o.append(indent).append("self.").append(varName).append(" = ")
                            .append(lowerObjExpr(expr, objFields, domains, objects)).append(";\n");
                } else if (currentWriteParams.get().contains(varName)) {
                    o.append(indent).append("*").append(varName).append(" = ")
                            .append(lowerObjExpr(expr, objFields, domains, objects)).append(";\n");
                } else if (!varName.contains(".") && declared.add(varName)) {
                    o.append(indent).append("let mut ").append(varName).append(" = ")
                            .append(lowerObjExpr(expr, objFields, domains, objects)).append(";\n");
                } else {
                    o.append(indent).append(varName).append(" = ")
                            .append(lowerObjExpr(expr, objFields, domains, objects)).append(";\n");
                }
            }
            default -> {
                genStmt(o, s, indent, declared, Collections.emptySet(), domains, objects);
            }
        }
    }

    private static String lowerObjExpr(String expr, Set<String> objFields, Map<String, Domain> domains,
                                       Map<String, ObjectType> objects) {
        if (expr == null) return "";
        expr = expr.trim();
        for (String f : objFields) {
            expr = expr.replaceAll("(?<!\\.)\\b" + f + "\\b", "self." + f);
        }
        return lowerExpr(expr, Collections.emptySet(), domains, objects);
    }

    private static void genStmt(StringBuilder o, Stmt s, String indent, Set<String> declared,
                                Set<String> stateFields, Map<String, Domain> domains,
                                Map<String, ObjectType> objects) {
        if (s == null) return;
        if (s.line > 0) {
            o.append(indent).append("// Moss line ").append(s.line).append(": ")
             .append(s.text != null ? s.text : s.kind.name().toLowerCase()).append("\n");
        }
        switch (s.kind) {
            case Pass -> {
                // pass is a no-op in generated Rust
            }
            case Try -> {
                o.append(indent).append("{\n");
            }
            case Recover -> {
                o.append(indent).append("}\n");
            }
            case Echo -> {
                if (s.args.isEmpty()) {
                    o.append(indent).append("println!();\n");
                } else {
                    o.append(indent).append("println!(\"");
                    for (int i = 0; i < s.args.size(); i++) {
                        if (i > 0) o.append(" ");
                        o.append("{}");
                    }
                    o.append("\"");
                    for (String arg : s.args) {
                        o.append(", (&(").append(lowerExpr(arg, stateFields, domains, objects)).append(")).__moss_display()");
                    }
                    o.append(");\n");
                }
            }
            case Let, Var -> {
                String varName = s.a != null ? s.a.trim() : "";
                String expr = s.b != null ? s.b.trim() : "";
                if (varName.endsWith("]") && varName.contains("[")) {
                    int openBr = varName.indexOf('[');
                    String base = varName.substring(0, openBr).trim();
                    String idx = varName.substring(openBr + 1, varName.length() - 1).trim();
                    o.append(indent).append(base).append(".insert(")
                            .append(lowerExpr(idx, stateFields, domains, objects)).append(", ")
                            .append(lowerExpr(expr, stateFields, domains, objects)).append(");\n");
                    break;
                }
                if (varName.contains(":")) {
                    varName = varName.split(":", 2)[0].trim();
                }
                if (declared.contains(varName)) {
                    o.append(indent).append(varName).append(" = ")
                            .append(lowerExpr(expr, stateFields, domains, objects)).append(";\n");
                } else {
                    declared.add(varName);
                    String typeAnn = (s.semanticType != null && !s.semanticType.isEmpty() && s.semanticType.startsWith("vector[")) ? ": " + rustType(s.semanticType) : "";
                    o.append(indent).append("let mut ").append(varName).append(typeAnn).append(" = ")
                            .append(lowerExpr(expr, stateFields, domains, objects)).append(";\n");
                }
            }
            case Assign -> {
                String varName = s.a != null ? s.a.trim() : "";
                String expr = s.b != null ? s.b.trim() : "";
                if (varName.endsWith("]") && varName.contains("[")) {
                    int openBr = varName.indexOf('[');
                    String base = varName.substring(0, openBr).trim();
                    String idx = varName.substring(openBr + 1, varName.length() - 1).trim();
                    o.append(indent).append(base).append(".insert(")
                            .append(lowerExpr(idx, stateFields, domains, objects)).append(", ")
                            .append(lowerExpr(expr, stateFields, domains, objects)).append(");\n");
                } else if (stateFields.contains(varName)) {
                    o.append(indent).append("(*state.").append(varName).append(") = ")
                            .append(lowerExpr(expr, stateFields, domains, objects)).append(";\n");
                } else if (currentWriteParams.get().contains(varName)) {
                    o.append(indent).append("*").append(varName).append(" = ")
                            .append(lowerExpr(expr, stateFields, domains, objects)).append(";\n");
                } else if (varName.contains(".")) {
                    String root = varName.substring(0, varName.indexOf('.'));
                    if (stateFields.contains(root)) {
                        o.append(indent).append("state.").append(varName).append(" = ")
                                .append(lowerExpr(expr, stateFields, domains, objects)).append(";\n");
                    } else {
                        o.append(indent).append(varName).append(" = ")
                                .append(lowerExpr(expr, stateFields, domains, objects)).append(";\n");
                    }
                } else if (!varName.contains(".") && declared.add(varName)) {
                    String typeAnn = (s.semanticType != null && !s.semanticType.isEmpty() && s.semanticType.startsWith("vector[")) ? ": " + rustType(s.semanticType) : "";
                    o.append(indent).append("let mut ").append(varName).append(typeAnn).append(" = ")
                            .append(lowerExpr(expr, stateFields, domains, objects)).append(";\n");
                } else {
                    o.append(indent).append(varName).append(" = ")
                            .append(lowerExpr(expr, stateFields, domains, objects)).append(";\n");
                }
            }
            case Message -> {
                String receiver = s.a != null ? s.a.trim() : "";
                String handler = s.b != null ? s.b.trim() : "";
                String target = receiver;
                if (!stateFields.isEmpty()) {
                    target = "state." + receiver;
                }
                StringBuilder call = new StringBuilder();
                call.append(target).append(".").append(handler).append("_shared(");
                for (int i = 0; i < s.args.size(); i++) {
                    if (i > 0) call.append(", ");
                    call.append(lowerExpr(s.args.get(i), stateFields, domains, objects));
                }
                call.append(")");
                if (s.messageResult != null && !s.messageResult.isEmpty()) {
                    if (declared.add(s.messageResult)) {
                        o.append(indent).append("let mut ").append(s.messageResult).append(" = ").append(call).append(";\n");
                    } else {
                        o.append(indent).append(s.messageResult).append(" = ").append(call).append(";\n");
                    }
                } else {
                    o.append(indent).append(call).append(";\n");
                }
            }
            case Reply, Return -> {
                String expr = s.a != null ? s.a.trim() : "";
                if (!expr.isEmpty() && !expr.equals("return") && !expr.equals("reply")) {
                    String val = lowerExpr(expr, stateFields, domains, objects);
                    while (hasOuterParens(val)) {
                        val = val.substring(1, val.length() - 1).trim();
                    }
                    o.append(indent).append("return ").append(val).append(";\n");
                } else {
                    o.append(indent).append("return;\n");
                }
            }
            case Raw -> {
                String t = s.text != null ? s.text.trim() : "";
                if (t.equals("try:") || t.startsWith("try:")) {
                    o.append(indent).append("{\n");
                } else if (t.equals("recover:") || t.startsWith("recover:") || t.startsWith("recover ")) {
                    o.append(indent).append("}\n");
                } else if (t.equals("pass")) {
                    // pass
                } else if (t.startsWith("echo ")) {
                    String arg = t.substring(5).trim();
                    if (arg.contains(",")) {
                        List<String> printArgs = new ArrayList<>();
                        List<String> formatSpec = new ArrayList<>();
                        for (String part : splitTopLevel(arg, ',')) {
                            formatSpec.add("{}");
                            printArgs.add("(&(" + lowerExpr(part.trim(), stateFields, domains, objects) + ")).__moss_display()");
                        }
                        o.append(indent).append("println!(\"").append(String.join(" ", formatSpec)).append("\", ")
                                .append(String.join(", ", printArgs)).append(");\n");
                    } else {
                        o.append(indent).append("println!(\"{}\", (&(").append(lowerExpr(arg, stateFields, domains, objects)).append(")).__moss_display());\n");
                    }
                } else if (t.contains("=") && !t.contains("==")) {
                    int eq = t.indexOf('=');
                    String lhs = t.substring(0, eq).trim();
                    String rhs = t.substring(eq + 1).trim();
                    if (stateFields.contains(lhs)) {
                        o.append(indent).append("(*state.").append(lhs).append(") = ")
                                .append(lowerExpr(rhs, stateFields, domains, objects)).append(";\n");
                    } else if (lhs.contains(".")) {
                        String root = lhs.substring(0, lhs.indexOf('.'));
                        if (stateFields.contains(root)) {
                            o.append(indent).append("state.").append(lhs).append(" = ")
                                    .append(lowerExpr(rhs, stateFields, domains, objects)).append(";\n");
                        } else {
                            o.append(indent).append(lhs).append(" = ")
                                    .append(lowerExpr(rhs, stateFields, domains, objects)).append(";\n");
                        }
                    } else if (declared.add(lhs)) {
                        String typeAnn = (s.semanticType != null && !s.semanticType.isEmpty() && s.semanticType.startsWith("vector[")) ? ": " + rustType(s.semanticType) : "";
                        o.append(indent).append("let mut ").append(lhs).append(typeAnn).append(" = ")
                                .append(lowerExpr(rhs, stateFields, domains, objects)).append(";\n");
                    } else {
                        o.append(indent).append(lhs).append(" = ")
                                .append(lowerExpr(rhs, stateFields, domains, objects)).append(";\n");
                    }
                } else if (!t.isEmpty()) {
                    o.append(indent).append(lowerExpr(t, stateFields, domains, objects)).append(";\n");
                }
            }
            default -> {
                String t = s.text != null ? s.text.trim() : "";
                if (!t.isEmpty()) {
                    o.append(indent).append(lowerExpr(t, stateFields, domains, objects)).append(";\n");
                }
            }
        }
    }

    private static List<String> splitPipeline(String expression) {
        List<String> stages = new ArrayList<>();
        int depth = 0, start = 0;
        boolean inString = false, inChar = false, escaped = false;
        for (int i = 0; i < expression.length(); i++) {
            char ch = expression.charAt(i);
            if (escaped) { escaped = false; continue; }
            if ((inString || inChar) && ch == '\\') { escaped = true; continue; }
            if (!inChar && ch == '"') { inString = !inString; continue; }
            if (!inString && ch == '\'') { inChar = !inChar; continue; }
            if (inString || inChar) continue;
            if (ch == '(' || ch == '[' || ch == '{') depth++;
            else if (ch == ')' || ch == ']' || ch == '}') depth--;
            else if (depth == 0 && ch == '|' && i + 1 < expression.length() && expression.charAt(i + 1) == '>') {
                stages.add(expression.substring(start, i).trim());
                start = i + 2;
                i++;
            }
        }
        stages.add(expression.substring(start).trim());
        return stages;
    }

    public static List<String> splitTopLevel(String str, char delimiter) {
        List<String> result = new ArrayList<>();
        int depth = 0;
        boolean inQuotes = false;
        StringBuilder cur = new StringBuilder();
        for (int i = 0; i < str.length(); i++) {
            char c = str.charAt(i);
            if (c == '"' && (i == 0 || str.charAt(i - 1) != '\\')) {
                inQuotes = !inQuotes;
            }
            if (!inQuotes) {
                if (c == '(' || c == '[' || c == '{') depth++;
                else if (c == ')' || c == ']' || c == '}') depth--;
                else if (c == delimiter && depth == 0) {
                    result.add(cur.toString().trim());
                    cur.setLength(0);
                    continue;
                }
            }
            cur.append(c);
        }
        if (cur.length() > 0) {
            result.add(cur.toString().trim());
        }
        return result;
    }

    private static boolean isKnownFunction(String name, String simpleName) {
        Program program = currentProgram.get();
        if (program == null) return false;
        for (Function function : program.functions) {
            if (function.name.equals(name) || function.name.equals(simpleName)
                    || function.declarationName.equals(name)
                    || function.declarationName.equals(simpleName)) return true;
        }
        return false;
    }

    private static boolean isStandardErrorWriteParam(Function function, int index) {
        if (function == null || index < 0 || index >= function.params.size()) return false;
        String type = function.params.get(index).type;
        return type != null && type.replace('.', '_').contains("ParseError");
    }

    private static String bindingTypeName(String name) {
        String identity = currentEnclosingIdentity.get();
        String context = "";
        if (identity != null) {
            if (identity.startsWith("main@")) context = "main";
            else if (identity.startsWith("fn:")) {
                String target = identity.substring(3);
                int at = target.lastIndexOf('@');
                context = at > 0 ? target.substring(0, at) : target;
            }
        }
        return bindingTypeName(name, context);
    }

    private static org.mosslang.compiler.types.TypeRef bindingTypeRef(String name) {
        Program program = currentProgram.get();
        if (program == null || program.localBindingTypes == null) return null;
        String identity = currentEnclosingIdentity.get();
        String context = "";
        if (identity != null) {
            if (identity.startsWith("main@")) context = "main";
            else if (identity.startsWith("fn:")) {
                String target = identity.substring(3);
                int at = target.lastIndexOf('@');
                context = at > 0 ? target.substring(0, at) : target;
            }
        }
        Map<String, org.mosslang.compiler.types.TypeRef> bindings =
                program.localBindingTypes.get(context);
        org.mosslang.compiler.types.TypeRef type = bindings == null ? null : bindings.get(name);
        if (type == null) {
            String lookupContext = context;
            Function function = program.functions.stream()
                    .filter(candidate -> candidate.name.equals(lookupContext)).findFirst().orElse(null);
            if (function != null) {
                Param parameter = function.params.stream()
                        .filter(candidate -> candidate.name.equals(name)).findFirst().orElse(null);
                if (parameter != null) type = parameter.checkedTypeRef != null
                        ? parameter.checkedTypeRef : parameter.typeRef;
            }
        }
        return type;
    }

    private static String bindingTypeName(String name, String context) {
        Program program = currentProgram.get();
        if (program == null) return "";
        if (program.localBindingTypes == null) return "";
        Map<String, org.mosslang.compiler.types.TypeRef> bindings = program.localBindingTypes.get(context);
        org.mosslang.compiler.types.TypeRef type = bindings != null ? bindings.get(name) : null;
        if (type == null) {
            Function function = program.functions.stream()
                    .filter(candidate -> candidate.name.equals(context)).findFirst().orElse(null);
            if (function != null) {
                Param parameter = function.params.stream()
                        .filter(candidate -> candidate.name.equals(name)).findFirst().orElse(null);
                if (parameter != null) {
                    type = parameter.typeRef != null ? parameter.typeRef
                            : (parameter.type != null ? org.mosslang.compiler.types.TypeParser
                                    .parseCanonical(parameter.type) : null);
                }
            }
        }
        if (type == null) {
            for (Map<String, org.mosslang.compiler.types.TypeRef> candidates : program.localBindingTypes.values()) {
                type = candidates.get(name);
                if (type != null) break;
            }
        }
        return type != null ? type.toCanonicalString() : "";
    }

    private static String currentEnclosingFunctionName() {
        String identity = currentEnclosingIdentity.get();
        if (identity == null) return "";
        if (identity.startsWith("main@")) return "main";
        if (!identity.startsWith("fn:")) return "";
        String target = identity.substring(3);
        int at = target.lastIndexOf('@');
        return at > 0 ? target.substring(0, at) : target;
    }

    private static String statementExpressionForGenerator(Stmt statement) {
        if (statement == null) return "";
        StringBuilder expression = new StringBuilder();
        for (String value : List.of(statement.text == null ? "" : statement.text,
                statement.a == null ? "" : statement.a,
                statement.b == null ? "" : statement.b)) {
            if (!value.isBlank()) expression.append(value).append(' ');
        }
        if (statement.args != null) {
            for (String argument : statement.args) expression.append(argument).append(' ');
        }
        return expression.toString();
    }

    private static Method resolveObjectMethod(String receiver, String methodName,
                                              int argumentCount, String context) {
        Program program = currentProgram.get();
        if (program == null) return null;
        String receiverType = bindingTypeName(receiver, context);
        if (receiverType.isEmpty()) return null;
        for (ObjectType object : program.objects) {
            if (!object.name.equals(receiverType)
                    && !object.name.endsWith("__" + receiverType)) continue;
            return object.methods.stream().filter(method -> method.name.equals(methodName)
                    && method.params.size() == argumentCount).findFirst().orElse(null);
        }
        return null;
    }

    private static boolean expressionCallsRaisingMethod(String expression, String context) {
        if (expression == null || expression.isBlank()) return false;
        java.util.regex.Matcher calls = java.util.regex.Pattern.compile(
                "(?<![A-Za-z0-9_])([A-Za-z_][A-Za-z0-9_]*(?:\\s*\\.\\s*[A-Za-z_][A-Za-z0-9_]*)*)"
                        + "\\s*\\.\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*\\(")
                .matcher(maskStringLiterals(expression));
        while (calls.find()) {
            String receiver = calls.group(1).replaceAll("\\s", "");
            String methodName = calls.group(2);
            int open = expression.indexOf('(', calls.start());
            int close = matchingCloseParen(expression, open);
            int arguments = close <= open + 1 ? 0
                    : ExpressionTyper.parseArguments(expression.substring(open + 1, close)).size();
            if (methodMayRaise(resolveObjectMethod(receiver, methodName, arguments, context))) return true;
        }
        return false;
    }

    private static String maskStringLiterals(String expression) {
        StringBuilder masked = new StringBuilder(expression.length());
        char quote = 0;
        boolean escaped = false;
        for (int index = 0; index < expression.length(); index++) {
            char value = expression.charAt(index);
            if (quote != 0) {
                masked.append(' ');
                if (escaped) escaped = false;
                else if (value == '\\') escaped = true;
                else if (value == quote) quote = 0;
            } else if (value == '"' || value == '\'') {
                quote = value;
                escaped = false;
                masked.append(' ');
            } else {
                masked.append(value);
            }
        }
        return masked.toString();
    }

    private static int matchingCloseParen(String text, int openIndex) {
        if (text == null || openIndex < 0 || openIndex >= text.length()
                || text.charAt(openIndex) != '(') return -1;
        int depth = 0;
        boolean inString = false;
        for (int i = openIndex; i < text.length(); i++) {
            char c = text.charAt(i);
            int backslashes = 0;
            for (int j = i - 1; j >= 0 && text.charAt(j) == '\\'; j--) backslashes++;
            if (c == '"' && (backslashes & 1) == 0) {
                inString = !inString;
                continue;
            }
            if (inString) continue;
            if (c == '(') depth++;
            else if (c == ')' && --depth == 0) return i;
        }
        return -1;
    }

    private static Method standardErrorWriteMethod(String receiver, String methodName, int argCount) {
        if (!currentWriteMethods.get().contains(methodName)) return null;
        Program program = currentProgram.get();
        if (program == null) return null;
        String receiverType = bindingTypeName(receiver);
        for (ObjectType object : program.objects) {
            if (!receiverType.isEmpty() && !object.name.equals(receiverType)
                    && !object.name.endsWith("__" + receiverType)) continue;
            for (Method method : object.methods) {
                if (!method.name.equals(methodName) || method.params.size() != argCount || argCount == 0) continue;
                String type = method.params.get(argCount - 1).type;
                if (type != null && type.replace('.', '_').contains("ParseError")) return method;
            }
        }
        return null;
    }

    private static boolean methodMayRaise(Method method) {
        if (method == null) return false;
        if (!method.sourceFreeRaiseVariants.isEmpty()) return true;
        boolean inTry = false;
        for (Stmt statement : method.body) {
            if (statement.kind == Stmt.Kind.Try) inTry = true;
            if (statement.kind == Stmt.Kind.Raise) return true;
            if (!inTry && statement.text != null && statement.text.trim().startsWith("raise ")) return true;
        }
        return false;
    }

    private static Set<String> stringLocalsUsedByRecovery(Handler handler) {
        Set<String> candidates = new LinkedHashSet<>();
        List<Stmt> body = handler.body;
        int tryIndex = -1;
        int recoverIndex = -1;
        for (int i = 0; i < body.size(); i++) {
            if (body.get(i).kind == Stmt.Kind.Try) tryIndex = i;
            else if (tryIndex >= 0 && body.get(i).kind == Stmt.Kind.Recover) {
                recoverIndex = i;
                break;
            }
        }
        if (tryIndex < 0 || recoverIndex < 0) return candidates;
        for (int i = tryIndex + 1; i < recoverIndex; i++) {
            Stmt statement = body.get(i);
            if (statement.kind != Stmt.Kind.Assign && statement.kind != Stmt.Kind.Let
                    && statement.kind != Stmt.Kind.Var) continue;
            String name = statement.a != null ? statement.a.trim() : "";
            int colon = name.indexOf(':');
            if (colon >= 0) name = name.substring(0, colon).trim();
            String value = statement.b != null ? statement.b.trim() : "";
            boolean stringValue = value.startsWith("\"") || (statement.semanticType != null
                    && (statement.semanticType.equalsIgnoreCase("string")
                    || statement.semanticType.equalsIgnoreCase("MossStr")));
            if (!name.isEmpty() && stringValue) candidates.add(name);
        }
        if (candidates.isEmpty()) return candidates;
        Set<String> captured = new LinkedHashSet<>();
        for (String name : candidates) {
            for (int i = recoverIndex + 1; i < body.size(); i++) {
                Stmt statement = body.get(i);
                boolean writesName = (statement.kind == Stmt.Kind.Assign || statement.kind == Stmt.Kind.Let
                        || statement.kind == Stmt.Kind.Var) && name.equals(statement.a != null ? statement.a.trim() : "");
                boolean used = !writesName
                        && (SemanticChecker.expressionUses(statement.text, name)
                        || SemanticChecker.expressionUses(statement.a, name)
                        || SemanticChecker.expressionUses(statement.b, name));
                if (statement.args != null) {
                    for (String arg : statement.args) used |= SemanticChecker.expressionUses(arg, name);
                }
                if (used) {
                    captured.add(name);
                    break;
                }
            }
            for (FailureArm arm : handler.failureArms) {
                if (captured.contains(name)) break;
                for (Stmt statement : arm.body) {
                    boolean writesName = (statement.kind == Stmt.Kind.Assign || statement.kind == Stmt.Kind.Let
                            || statement.kind == Stmt.Kind.Var) && name.equals(statement.a != null ? statement.a.trim() : "");
                    boolean used = !writesName
                            && (SemanticChecker.expressionUses(statement.text, name)
                            || SemanticChecker.expressionUses(statement.a, name)
                            || SemanticChecker.expressionUses(statement.b, name));
                    if (statement.args != null) {
                        for (String arg : statement.args) used |= SemanticChecker.expressionUses(arg, name);
                    }
                    if (used) {
                        captured.add(name);
                        break;
                    }
                }
            }
        }
        return captured;
    }

    private static Map<String, String> handlerFailureCaptureTypes(
            Handler handler, Set<String> initiallyDeclared, Set<String> stateFields) {
        Map<String, String> availableTypes = new LinkedHashMap<>();
        Set<String> candidates = new LinkedHashSet<>(initiallyDeclared);
        for (Param parameter : handler.params) {
            if (parameter.name != null && !parameter.name.isEmpty()) {
                candidates.add(parameter.name);
                availableTypes.put(parameter.name, parameter.type);
            }
        }
        for (Stmt statement : handler.body) {
            if (statement.kind != Stmt.Kind.Assign && statement.kind != Stmt.Kind.Let
                    && statement.kind != Stmt.Kind.Var) continue;
            String name = statement.a != null ? statement.a.trim() : "";
            if (name.isEmpty()) continue;
            candidates.add(name);
            String type = statement.semanticType;
            if ((type == null || type.isEmpty()) && statement.b != null
                    && statement.b.trim().startsWith("\"")) type = "string";
            if (type != null && !type.isEmpty()) availableTypes.put(name, type);
        }
        Set<String> captured = new LinkedHashSet<>();
        for (FailureArm arm : handler.failureArms) {
            captured.addAll(failureArmLocalUses(arm, candidates));
        }
        Map<String, String> result = new LinkedHashMap<>();
        for (String name : captured) {
            if (stateFields.contains(name)) continue;
            String type = availableTypes.get(name);
            if (type != null && !type.isEmpty()) result.put(name, type);
        }
        return result;
    }

    private static Set<String> failureArmLocalUses(FailureArm arm, Set<String> declared) {
        Set<String> used = new LinkedHashSet<>();
        for (String name : declared) {
            for (Stmt statement : arm.body) {
                boolean writesName = (statement.kind == Stmt.Kind.Assign || statement.kind == Stmt.Kind.Let
                        || statement.kind == Stmt.Kind.Var) && name.equals(statement.a != null ? statement.a.trim() : "");
                if (writesName) continue;
                boolean referenced = SemanticChecker.expressionUses(statement.text, name)
                        || SemanticChecker.expressionUses(statement.a, name)
                        || SemanticChecker.expressionUses(statement.b, name);
                if (statement.args != null) {
                    for (String arg : statement.args) referenced |= SemanticChecker.expressionUses(arg, name);
                }
                if (referenced) {
                    used.add(name);
                    break;
                }
            }
        }
        return used;
    }

    private static String lowerExpr(String expr, Set<String> stateFields, Set<String> objFields,
                                    Map<String, Domain> domains, Map<String, ObjectType> objects) {
        return lowerExpr(expr, stateFields, objFields, domains, objects, false);
    }

    private static String lowerExpr(String expr, Set<String> stateFields, Set<String> objFields,
                                    Map<String, Domain> domains, Map<String, ObjectType> objects,
                                    boolean inTry) {
        if (expr == null) return "";
        expr = expr.trim();
        for (String f : objFields) {
            expr = expr.replaceAll("(?<!\\.)\\b" + f + "\\b", "self." + f);
        }
        return lowerExpr(expr, stateFields, domains, objects);
    }

    private static String lowerSortCall(List<String> arguments, Set<String> stateFields,
                                        Map<String, Domain> domains,
                                        Map<String, ObjectType> objects) {
        if (arguments.isEmpty()) return "()";
        String place = stripSortNamedValue(arguments.get(0));
        org.mosslang.compiler.types.TypeRef vectorTypeRef = bindingTypeRef(place);
        if (vectorTypeRef == null && place.endsWith("]")) {
            int open = org.mosslang.compiler.semantics.ExpressionTyper
                    .findMatchingOpenBracket(place, place.length() - 1);
            if (open > 0) {
                org.mosslang.compiler.types.TypeRef baseType = bindingTypeRef(
                        place.substring(0, open).trim());
                if (baseType != null && !baseType.typeArgs().isEmpty())
                    vectorTypeRef = baseType.typeArgs().get(0);
            }
        }
        String elementType = vectorTypeRef != null && !vectorTypeRef.typeArgs().isEmpty()
                ? vectorTypeRef.typeArgs().get(0).toCanonicalString() : "int";
        String target = lowerSortPlace(place, stateFields, domains, objects);
        if (arguments.size() == 1) {
            return "(" + target + ").sort_by(|__moss_a, __moss_b| "
                    + sortComparison(elementType, "*__moss_a", "*__moss_b") + ")";
        }

        String named = arguments.get(1).trim();
        int colon = named.indexOf(':');
        String key = colon < 0 ? named : named.substring(colon + 1).trim();
        boolean reverse = false;
        if (key.startsWith("Reverse(") && key.endsWith(")")) {
            List<String> reverseArgs = splitTopLevel(key.substring(8, key.length() - 1), ',');
            if (reverseArgs.size() == 1) {
                reverse = true;
                key = reverseArgs.get(0).trim();
            }
        }
        String callA = lowerSortKeyCall(key, "__moss_a", elementType, stateFields, domains, objects);
        String callB = lowerSortKeyCall(key, "__moss_b", elementType, stateFields, domains, objects);
        String keyType = "int";
        Program program = currentProgram.get();
        if (program != null) {
            Function keyFunction = findSortKeyFunction(program, key);
            if (keyFunction != null) {
                if (keyFunction.returnTypeRef != null)
                    keyType = keyFunction.returnTypeRef.toCanonicalString();
                else if (keyFunction.returnType != null && !keyFunction.returnType.isEmpty())
                    keyType = keyFunction.returnType;
            }
        }
        String left = reverse ? "__moss_kb" : "__moss_ka";
        String right = reverse ? "__moss_ka" : "__moss_kb";
        return "(" + target + ").sort_by(|__moss_a, __moss_b| { let __moss_ka = " + callA
                + "; let __moss_kb = " + callB + "; "
                + sortComparison(keyType, left, right) + " })";
    }

    private static String stripSortNamedValue(String value) {
        String trimmed = value == null ? "" : value.trim();
        int colon = trimmed.indexOf(':');
        return colon > 0 ? trimmed.substring(colon + 1).trim() : trimmed;
    }

    private static String lowerSortPlace(String place, Set<String> stateFields,
                                         Map<String, Domain> domains,
                                         Map<String, ObjectType> objects) {
        String value = place.trim();
        if (value.endsWith("]")) {
            int open = org.mosslang.compiler.semantics.ExpressionTyper
                    .findMatchingOpenBracket(value, value.length() - 1);
            if (open > 0) {
                String base = lowerSortPlace(value.substring(0, open), stateFields, domains, objects);
                String index = lowerExpr(value.substring(open + 1, value.length() - 1).trim(),
                        stateFields, domains, objects);
                return base + "[(" + index + ") as usize]";
            }
        }
        return lowerExpr(value, stateFields, domains, objects);
    }

    private static String lowerSortKeyCall(String key, String argument, String elementType,
                                           Set<String> stateFields, Map<String, Domain> domains,
                                           Map<String, ObjectType> objects) {
        Program program = currentProgram.get();
        Function keyFunction = program == null ? null : findSortKeyFunction(program, key);
        if (keyFunction == null) return key + "(" + argument + ")";
        String rustPath = sourceFreeFunctionRustPath(keyFunction, key, currentGenericFunctions.get());
        boolean copy = Set.of("int", "float", "bool", "char")
                .contains(elementType == null ? "" : elementType.toLowerCase(Locale.ROOT));
        org.mosslang.compiler.types.AccessMode mode = keyFunction.parameterAccessModes.isEmpty()
                ? org.mosslang.compiler.types.AccessMode.Read : keyFunction.parameterAccessModes.get(0);
        String actual;
        if (copy) actual = "*" + argument;
        else if (mode == org.mosslang.compiler.types.AccessMode.Read) actual = argument;
        else actual = "(*" + argument + ").clone()";
        if (currentGenericFunctions.get().contains(keyFunction.name))
            return "__moss_fn_" + keyFunction.name + "!(" + actual + ")";
        return rustPath + "(" + actual + ")";
    }

    private static Function findSortKeyFunction(Program program, String key) {
        if (program == null) return null;
        String normalized = key.replace('.', '_');
        String internal = key.replace(".", "__");
        return program.functions.stream().filter(function -> function.name.equals(key)
                || function.name.equals(normalized) || function.name.equals(internal)
                || function.declarationName.equals(key)).findFirst().orElse(null);
    }

    private static String sortComparison(String type, String left, String right) {
        if (type != null && type.toLowerCase(Locale.ROOT).equals("float"))
            return "moss_float_total_cmp(" + left + " as f64, " + right + " as f64)";
        return "(" + left + ").cmp(&(" + right + "))";
    }

    private static boolean hasOuterParens(String s) {
        if (s == null || s.length() < 2 || !s.startsWith("(") || !s.endsWith(")")) return false;
        int depth = 0;
        boolean inQuote = false;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '"' && (i == 0 || s.charAt(i - 1) != '\\')) {
                inQuote = !inQuote;
            } else if (!inQuote) {
                if (c == '(') depth++;
                else if (c == ')') {
                    depth--;
                    if (depth == 0 && i < s.length() - 1) return false;
                }
            }
        }
        return depth == 0;
    }

    private static String replacePipelinePlaceholder(String expression, String replacement) {
        StringBuilder out = new StringBuilder(expression.length() + replacement.length());
        boolean inString = false, inChar = false, escaped = false;
        for (int i = 0; i < expression.length(); i++) {
            char ch = expression.charAt(i);
            if (escaped) {
                out.append(ch);
                escaped = false;
                continue;
            }
            if ((inString || inChar) && ch == '\\') {
                out.append(ch);
                escaped = true;
                continue;
            }
            if (!inChar && ch == '"') inString = !inString;
            else if (!inString && ch == '\'') inChar = !inChar;
            if (!inString && !inChar && ch == '_') {
                boolean leftIdentifier = i > 0 && (Character.isLetterOrDigit(expression.charAt(i - 1)) || expression.charAt(i - 1) == '_');
                boolean rightIdentifier = i + 1 < expression.length() && (Character.isLetterOrDigit(expression.charAt(i + 1)) || expression.charAt(i + 1) == '_');
                if (!leftIdentifier && !rightIdentifier) {
                    out.append(replacement);
                    continue;
                }
            }
            out.append(ch);
        }
        return out.toString();
    }

    private static boolean isSingleStringLiteral(String s) {
        if (s == null || s.length() < 2 || !s.startsWith("\"") || !s.endsWith("\"")) return false;
        for (int i = 1; i < s.length() - 1; i++) {
            char c = s.charAt(i);
            if (c == '"' && s.charAt(i - 1) != '\\') return false;
        }
        return true;
    }

    private static String[] splitOperator(String input, String... operators) {
        int parens = 0, brackets = 0, braces = 0;
        boolean inQuote = false;
        for (int i = input.length() - 1; i >= 0; i--) {
            char c = input.charAt(i);
            if (c == '"' && (i == 0 || input.charAt(i - 1) != '\\')) {
                inQuote = !inQuote;
            }
            if (inQuote) continue;
            if (c == ')') parens++;
            else if (c == '(') parens--;
            else if (c == ']') brackets++;
            else if (c == '[') brackets--;
            else if (c == '}') braces++;
            else if (c == '{') braces--;
            if (parens > 0 || brackets > 0 || braces > 0) continue;

            for (String op : operators) {
                if (i + op.length() <= input.length() && input.regionMatches(i, op, 0, op.length())) {
                    if (op.equals("|")) {
                        if (i + 1 < input.length() && (input.charAt(i + 1) == '>' || input.charAt(i + 1) == '|' || input.charAt(i + 1) == '=')) continue;
                        if (i > 0 && input.charAt(i - 1) == '|') continue;
                    } else if (op.equals("&")) {
                        if (i + 1 < input.length() && (input.charAt(i + 1) == '&' || input.charAt(i + 1) == '=')) continue;
                        if (i > 0 && input.charAt(i - 1) == '&') continue;
                    } else if (op.equals("^")) {
                        if (i + 1 < input.length() && input.charAt(i + 1) == '=') continue;
                    } else if (op.equals("<")) {
                        if (i + 1 < input.length() && (input.charAt(i + 1) == '<' || input.charAt(i + 1) == '=')) continue;
                        if (i > 0 && input.charAt(i - 1) == '<') continue;
                    } else if (op.equals(">")) {
                        if (i + 1 < input.length() && (input.charAt(i + 1) == '>' || input.charAt(i + 1) == '=')) continue;
                        if (i > 0 && (input.charAt(i - 1) == '>' || input.charAt(i - 1) == '|')) continue;
                    } else if (op.equals("-") || op.equals("+")) {
                        int before = i - 1;
                        while (before >= 0 && Character.isWhitespace(input.charAt(before))) before--;
                        if (before < 0) continue;
                        char prev = input.charAt(before);
                        if (prev == '(' || prev == '[' || prev == '{' || prev == ',' || prev == ':' ||
                            prev == '+' || prev == '-' || prev == '*' || prev == '/' || prev == '%' ||
                            prev == '=' || prev == '<' || prev == '>' || prev == '!' || prev == '|' ||
                            prev == '&' || prev == '^' || prev == '~') {
                            continue;
                        }
                    }
                    return new String[]{input.substring(0, i).trim(), input.substring(i + op.length()).trim(), op};
                }
            }
        }
        return null;
    }

    private static String lowerExpr(String expr, Set<String> stateFields, Map<String, Domain> domains,
                                    Map<String, ObjectType> objects) {
        if (expr == null) return "";
        expr = expr.trim();
        while (hasOuterParens(expr)) {
            expr = expr.substring(1, expr.length() - 1).trim();
        }
        if (expr.isEmpty()) return "";

        if (expr.contains("|>") && splitPipeline(expr).size() > 1) {
            List<String> stages = splitPipeline(expr);
            if (stages.size() == 2 && stages.get(1).trim().equals("sum")) {
                return "(" + lowerExpr(stages.get(0).trim(), stateFields, domains, objects) + ").iter().sum::<i64>()";
            }
            String src = lowerExpr(stages.get(0).trim(), stateFields, domains, objects);
            if (stages.size() == 2 && stages.get(1).trim().equals("count")) {
                return "((" + src + ").len() as i64)";
            }
            StringBuilder pipe = new StringBuilder();
            pipe.append("{ let __moss_src = (").append(src).append(").clone(); ");
            String currentVec = "__moss_src";
            for (int si = 1; si < stages.size(); si++) {
                String stage = stages.get(si).trim();
                if (stage.equals("sum")) {
                    pipe.append(currentVec).append(".iter().sum::<i64>() }");
                    return pipe.toString();
                } else if (stage.equals("count")) {
                    pipe.append("(").append(currentVec).append(".len() as i64) }");
                    return pipe.toString();
                } else if (stage.startsWith("map(") && stage.endsWith(")")) {
                    String mapper = stage.substring(4, stage.length() - 1).trim();
                    String nextVec = "__moss_map_" + si;
                    pipe.append("let mut ").append(nextVec).append(" = std::vec::Vec::new(); ");
                    pipe.append("for __item in ").append(currentVec).append(".into_iter() { ");
                    String callExpr = replacePipelinePlaceholder(mapper, "__item")
                            .equals(mapper) ? mapper + "(__item)"
                            : replacePipelinePlaceholder(mapper, "__item");
                    pipe.append(nextVec).append(".push(").append(lowerExpr(callExpr, stateFields, domains, objects)).append("); } ");
                    currentVec = nextVec;
                } else if (stage.startsWith("filter(") && stage.endsWith(")")) {
                    String pred = stage.substring(7, stage.length() - 1).trim();
                    String nextVec = "__moss_filter_" + si;
                    pipe.append("let mut ").append(nextVec).append(" = std::vec::Vec::new(); ");
                    pipe.append("for __item in ").append(currentVec).append(".into_iter() { ");
                    String replaced = replacePipelinePlaceholder(pred, "__item.clone()");
                    String condExpr = replaced.equals(pred) ? pred + "(__item.clone())" : replaced;
                    pipe.append("if ").append(lowerExpr(condExpr, stateFields, domains, objects)).append(" { ").append(nextVec).append(".push(__item); } } ");
                    currentVec = nextVec;
                } else if (stage.startsWith("reduce(") && stage.endsWith(")")) {
                    List<String> rArgs = splitTopLevel(stage.substring(7, stage.length() - 1).trim(), ',');
                    String seedExpr = rArgs.get(0).trim();
                    String redFn = rArgs.get(1).trim();
                    pipe.append("let mut __acc = ").append(lowerExpr(seedExpr, stateFields, domains, objects)).append("; ");
                    pipe.append("for __item in ").append(currentVec).append(".into_iter() { ");
                    String redCall = redFn + "(__acc, __item)";
                    pipe.append("__acc = ").append(lowerExpr(redCall, stateFields, domains, objects)).append("; } __acc }");
                    return pipe.toString();
                }
            }
            pipe.append(currentVec).append(" }");
            return pipe.toString();
        }
        if (isSingleStringLiteral(expr)) {
            return "MossStr::from(" + expr + ")";
        }

        // 1. Boolean operators: " or ", " xor ", " and "
        String[] op = splitOperator(expr, " or ");
        if (op != null) {
            return "(" + lowerExpr(op[0], stateFields, domains, objects) + ") || (" + lowerExpr(op[1], stateFields, domains, objects) + ")";
        }
        op = splitOperator(expr, " xor ");
        if (op != null) {
            return "(" + lowerExpr(op[0], stateFields, domains, objects) + ") ^ (" + lowerExpr(op[1], stateFields, domains, objects) + ")";
        }
        op = splitOperator(expr, " and ");
        if (op != null) {
            return "(" + lowerExpr(op[0], stateFields, domains, objects) + ") && (" + lowerExpr(op[1], stateFields, domains, objects) + ")";
        }
        if (expr.startsWith("not ") || expr.startsWith("not(")) {
            String sub = expr.startsWith("not ") ? expr.substring(4) : expr.substring(3);
            return "!(" + lowerExpr(sub.trim(), stateFields, domains, objects) + ")";
        }

        // 2. Comparisons: "==", "!=", "<=", ">=", "<", ">"
        op = splitOperator(expr, "==", "!=", "<=", ">=", "<", ">");
        if (op != null) {
            return "(" + lowerExpr(op[0], stateFields, domains, objects) + ") " + op[2] + " (" + lowerExpr(op[1], stateFields, domains, objects) + ")";
        }

        // 3. Bitwise OR: "|"
        op = splitOperator(expr, "|");
        if (op != null) {
            return "(" + lowerExpr(op[0], stateFields, domains, objects) + ") | (" + lowerExpr(op[1], stateFields, domains, objects) + ")";
        }

        // 4. Bitwise XOR: "^"
        op = splitOperator(expr, "^");
        if (op != null) {
            return "(" + lowerExpr(op[0], stateFields, domains, objects) + ") ^ (" + lowerExpr(op[1], stateFields, domains, objects) + ")";
        }

        // 5. Bitwise AND: "&"
        op = splitOperator(expr, "&");
        if (op != null) {
            return "(" + lowerExpr(op[0], stateFields, domains, objects) + ") & (" + lowerExpr(op[1], stateFields, domains, objects) + ")";
        }

        // 6. Bitwise Shifts: "<<", ">>"
        op = splitOperator(expr, "<<", ">>");
        if (op != null) {
            String fnName = op[2].equals("<<") ? "__moss_shl" : "__moss_shr";
            return fnName + "(" + lowerExpr(op[0], stateFields, domains, objects) + ", " + lowerExpr(op[1], stateFields, domains, objects) + ")";
        }

        // 7. Additive: "+", "-"
        op = splitOperator(expr, "+", "-");
        if (op != null) {
            String lhs = lowerExpr(op[0], stateFields, domains, objects);
            if (op[2].equals("+") && op[0].trim().matches("^[A-Za-z_][A-Za-z0-9_]*$")) {
                lhs = "(" + lhs + ").clone()";
            }
            String rhs = lowerExpr(op[1], stateFields, domains, objects);
            String mName = op[2].equals("+") ? "__moss_add" : "__moss_sub";
            return "(" + lhs + ")." + mName + "(" + rhs + ")";
        }

        // 8. Multiplicative: "*", "/", "%"
        op = splitOperator(expr, "*", "/", "%");
        if (op != null) {
            String lhs = lowerExpr(op[0], stateFields, domains, objects);
            String rhs = lowerExpr(op[1], stateFields, domains, objects);
            if (op[2].equals("*")) {
                return "(" + lhs + ").__moss_mul(" + rhs + ")";
            }
            return "(" + lhs + ") " + op[2] + " (" + rhs + ")";
        }

        // 9. Unary bitwise NOT: "~"
        if (expr.startsWith("~")) {
            return "!(" + lowerExpr(expr.substring(1).trim(), stateFields, domains, objects) + ")";
        }

        // 10. Numeric literals (including INT64_MIN and float literals)
        if (expr.equals("-9223372036854775808")) {
            return "i64::MIN";
        }
        if (expr.matches("^-?\\d+$")) {
            return expr + "_i64";
        }
        if (expr.matches("^-?(\\d+\\.\\d+|\\d+[eE][+-]?\\d+|\\d+\\.\\d+[eE][+-]?\\d+)$")) {
            return expr + "_f64";
        }

        // 10b. Unary negation "-"
        if (expr.startsWith("-")) {
            return "-(" + lowerExpr(expr.substring(1).trim(), stateFields, domains, objects) + ")";
        }

        String enumExpr = lowerEnumExpr(expr, stateFields, domains, objects);
        if (enumExpr != null) {
            return enumExpr;
        }

        // Message expression e.g. message worker.Run() -> worker.Run_root()
        if (expr.startsWith("message ")) {
            String inner = expr.substring(8).trim();
            int openParen = inner.indexOf('(');
            int dot = inner.indexOf('.');
            if (dot > 0 && openParen > dot && inner.endsWith(")")) {
                String receiver = inner.substring(0, dot).trim();
                String handler = inner.substring(dot + 1, openParen).trim();
                String args = inner.substring(openParen + 1, inner.length() - 1).trim();
                List<String> loweredArgs = new ArrayList<>();
                if (!args.isEmpty()) {
                    for (String part : splitTopLevel(args, ',')) {
                        part = part.trim();
                        int colonIdx = -1;
                        int pDepth = 0;
                        boolean inQ = false;
                        for (int ci = 0; ci < part.length(); ci++) {
                            char ch = part.charAt(ci);
                            if (ch == '"' && (ci == 0 || part.charAt(ci - 1) != '\\')) inQ = !inQ;
                            if (!inQ) {
                                if (ch == '(' || ch == '[' || ch == '{') pDepth++;
                                else if (ch == ')' || ch == ']' || ch == '}') pDepth--;
                                else if (ch == ':' && pDepth == 0) {
                                    colonIdx = ci;
                                    break;
                                }
                            }
                        }
                        if (colonIdx > 0) {
                            part = part.substring(colonIdx + 1).trim();
                        }
                        if (stateFields.contains(part)) {
                            loweredArgs.add("&state." + part);
                        } else {
                            loweredArgs.add(lowerExpr(part, stateFields, domains, objects));
                        }
                    }
                }
                String target = receiver;
                if (!stateFields.isEmpty() || domains.containsKey(receiver)) {
                    target = "state." + receiver;
                }
                return target + "." + handler + "_root(" + String.join(", ", loweredArgs) + ")";
            }
        }

        // Vector literal e.g. [1, 2, 3] or [-2, 1, 3, 4]
        if (expr.startsWith("[") && expr.endsWith("]")) {
            String inner = expr.substring(1, expr.length() - 1).trim();
            if (inner.isEmpty()) {
                return "std::vec::Vec::new()";
            }
            List<String> items = new ArrayList<>();
            for (String part : splitTopLevel(inner, ',')) {
                part = part.trim();
                if (part.startsWith("\"")) {
                    items.add("MossStr::from(" + part + ")");
                } else if (part.matches("^-?\\d+$")) {
                    items.add(part + "_i64");
                } else {
                    String loweredItem = lowerExpr(part, stateFields, domains, objects);
                    if (part.matches("^[A-Za-z_][A-Za-z0-9_]*$") && !part.equals("true") && !part.equals("false")) {
                        loweredItem = loweredItem + ".clone()";
                    }
                    items.add(loweredItem);
                }
            }
            return "vec![" + String.join(", ", items) + "]";
        }

        // Domain construction check e.g. Counter() -> construct_Counter(0)
        int openParen = -1;
        if (expr.endsWith(")")) {
            int depth = 0;
            boolean inStr = false;
            for (int i = expr.length() - 1; i >= 0; i--) {
                char c = expr.charAt(i);
                if (c == '"' && (i == 0 || expr.charAt(i - 1) != '\\')) {
                    inStr = !inStr;
                }
                if (inStr) continue;
                if (c == ')') depth++;
                else if (c == '(') {
                    depth--;
                    if (depth == 0) {
                        openParen = i;
                        break;
                    }
                }
            }
        }
        if (openParen > 0 && expr.endsWith(")")) {
            String callee = expr.substring(0, openParen).trim();
            if (domains.containsKey(callee)) {
                Domain d = domains.get(callee);
                String args = expr.substring(openParen + 1, expr.length() - 1).trim();
                List<String> argList = new ArrayList<>();
                if (!args.isEmpty()) {
                    for (String part : splitTopLevel(args, ',')) {
                        part = part.trim();
                        int colonIdx = -1;
                        int pDepth = 0;
                        boolean inQ = false;
                        for (int ci = 0; ci < part.length(); ci++) {
                            char ch = part.charAt(ci);
                            if (ch == '"' && (ci == 0 || part.charAt(ci - 1) != '\\')) inQ = !inQ;
                            if (!inQ) {
                                if (ch == '(' || ch == '[' || ch == '{') pDepth++;
                                else if (ch == ')' || ch == ']' || ch == '}') pDepth--;
                                else if (ch == ':' && pDepth == 0) {
                                    colonIdx = ci;
                                    break;
                                }
                            }
                        }
                        if (colonIdx > 0) {
                            part = part.substring(colonIdx + 1).trim();
                        }
                        argList.add(cloneIfIdent(part, lowerExpr(part, stateFields, domains, objects)));
                    }
                }
                int expectedCount = d.routes.size() + d.state.size();
                if (argList.size() < expectedCount) {
                    int stateStart = argList.size() - d.routes.size();
                    if (stateStart < 0) stateStart = 0;
                    for (int i = stateStart; i < d.state.size(); i++) {
                        Field f = d.state.get(i);
                        String val = f.init != null && !f.init.isEmpty() ? lowerExpr(f.init, stateFields, domains, objects) : "0";
                        argList.add(val);
                    }
                }
                String constructor = d.sourceFree
                        ? "moss_" + d.definingModule + "::construct_" + d.declarationName
                        : "construct_" + callee;
                return constructor + "(" + String.join(", ", argList) + ")";
            }

            if (callee.equals("Vector") || callee.startsWith("Vector[")) {
                if (callee.startsWith("Vector[") && callee.endsWith("]")) {
                    String innerT = rustType(callee.substring(7, callee.length() - 1).trim());
                    return "std::vec::Vec::<" + innerT + ">::new()";
                }
                return "std::vec::Vec::new()";
            }
            if (callee.equals("Map") || callee.startsWith("Map[")) {
                return "std::collections::BTreeMap::new()";
            }
            if (callee.equals("Queue") || callee.startsWith("Queue[")) {
                return "std::collections::VecDeque::new()";
            }

            // External or imported domain/object constructor e.g. service__Worker() or provider__Carrier()
            String simpleName = callee.contains("__") ? callee.substring(callee.lastIndexOf("__") + 2) : callee;
            if (simpleName.equals("Some") || simpleName.equals("None") || simpleName.equals("Ok") || simpleName.equals("Err")) {
                String args = expr.substring(openParen + 1, expr.length() - 1).trim();
                List<String> argList = new ArrayList<>();
                if (!args.isEmpty()) {
                    for (String part : splitTopLevel(args, ',')) {
                        part = part.trim();
                        int colonIdx = -1;
                        int pDepth = 0;
                        boolean inQ = false;
                        for (int ci = 0; ci < part.length(); ci++) {
                            char ch = part.charAt(ci);
                            if (ch == '"' && (ci == 0 || part.charAt(ci - 1) != '\\')) inQ = !inQ;
                            if (!inQ) {
                                if (ch == '(' || ch == '[' || ch == '{') pDepth++;
                                else if (ch == ')' || ch == ']' || ch == '}') pDepth--;
                                else if (ch == ':' && pDepth == 0) {
                                    colonIdx = ci;
                                    break;
                                }
                            }
                        }
                        if (colonIdx > 0) {
                            part = part.substring(colonIdx + 1).trim();
                        }
                        argList.add(lowerExpr(part, stateFields, domains, objects));
                    }
                }
                return simpleName + "(" + String.join(", ", argList) + ")";
            }
            String argsInside = expr.substring(openParen + 1, expr.length() - 1).trim();
            boolean hasNamedArgs = false;
            int topDepth = 0;
            boolean topInStr = false;
            for (int ci = 0; ci < argsInside.length(); ci++) {
                char ch = argsInside.charAt(ci);
                if (ch == '"' && (ci == 0 || argsInside.charAt(ci - 1) != '\\')) topInStr = !topInStr;
                if (!topInStr) {
                    if (ch == '(' || ch == '[' || ch == '{') topDepth++;
                    else if (ch == ')' || ch == ']' || ch == '}') topDepth--;
                    else if ((ch == ':' || ch == '=') && topDepth == 0) {
                        hasNamedArgs = true;
                        break;
                    }
                }
            }
            boolean qualifiedExternalType = callee.contains(".") || callee.contains("__");
            if (Character.isUpperCase(simpleName.charAt(0)) && (hasNamedArgs || qualifiedExternalType
                    || objects.containsKey(callee) || objects.containsKey(simpleName))) {
                List<String> fieldAssignments = new ArrayList<>();
                if (!argsInside.isEmpty()) {
                    for (String part : splitTopLevel(argsInside, ',')) {
                        part = part.trim();
                        if (part.contains("=")) {
                            String[] kv = part.split("=", 2);
                            fieldAssignments.add(kv[0].trim() + ": " + lowerExpr(kv[1].trim(), stateFields, domains, objects));
                        } else if (part.contains(":")) {
                            String[] kv = part.split(":", 2);
                            fieldAssignments.add(kv[0].trim() + ": " + lowerExpr(kv[1].trim(), stateFields, domains, objects));
                        }
                    }
                }
                return sourceFreeRustName(callee, currentProgram.get()) + " { "
                        + String.join(", ", fieldAssignments) + " }";
            }
            if (callee.equals("Executor")) {
                return "__MossExecutor";
            }
            if (!objects.containsKey(callee) && !isKnownFunction(callee, simpleName)
                    && !simpleName.isEmpty() && Character.isUpperCase(simpleName.charAt(0))
                    && (!callee.contains(".") || callee.contains("__"))) {
                String args = expr.substring(openParen + 1, expr.length() - 1).trim();
                List<String> argList = new ArrayList<>();
                if (!args.isEmpty()) {
                    for (String part : splitTopLevel(args, ',')) {
                        part = part.trim();
                        int colonIdx = -1;
                        int pDepth = 0;
                        boolean inQ = false;
                        for (int ci = 0; ci < part.length(); ci++) {
                            char ch = part.charAt(ci);
                            if (ch == '"' && (ci == 0 || part.charAt(ci - 1) != '\\')) inQ = !inQ;
                            if (!inQ) {
                                if (ch == '(' || ch == '[' || ch == '{') pDepth++;
                                else if (ch == ')' || ch == ']' || ch == '}') pDepth--;
                                else if (ch == ':' && pDepth == 0) {
                                    colonIdx = ci;
                                    break;
                                }
                            }
                        }
                        if (colonIdx > 0) {
                            part = part.substring(colonIdx + 1).trim();
                        }
                        argList.add(cloneIfIdent(part, lowerExpr(part, stateFields, domains, objects)));
                    }
                }
                String cName = callee.startsWith("construct_") ? callee : ("construct_" + callee);
                return cName + "(" + String.join(", ", argList) + ")";
            }

            // Mutable standard-error arguments need a bridge when the method is
            // supplied by another crate, so evaluation and ownership stay ordered.
            if (callee.contains(".") && !callee.contains("__")) {
                int dot = callee.lastIndexOf('.');
                String recv = callee.substring(0, dot).trim();
                String methodName = callee.substring(dot + 1).trim();
                String args = expr.substring(openParen + 1, expr.length() - 1).trim();
                List<String> rawArgs = args.isEmpty() ? Collections.emptyList() : splitTopLevel(args, ',');
                Method writeErrorMethod = standardErrorWriteMethod(recv, methodName, rawArgs.size());
                if (writeErrorMethod != null && !rawArgs.isEmpty()) {
                    String writeArg = rawArgs.get(rawArgs.size() - 1).trim();
                    String owner = writeErrorMethod.owner;
                    String providerModule = owner.contains("__") ? owner.substring(0, owner.indexOf("__")) : "";
                    StringBuilder bridge = new StringBuilder();
                    bridge.append("{ ");
                    List<String> callArgs = new ArrayList<>();
                    for (int ai = 0; ai < rawArgs.size() - 1; ai++) {
                        String tmp = "__moss_arg_" + ai;
                        bridge.append("let ").append(tmp).append(" = ")
                              .append(lowerExpr(rawArgs.get(ai).trim(), stateFields, domains, objects)).append("; ");
                        callArgs.add(tmp);
                    }
                    if (providerModule.isEmpty()) {
                        bridge.append("let mut __moss_provider_val = ").append(writeArg).append(".clone(); ");
                    } else {
                        bridge.append("let mut __moss_provider_val = moss_").append(providerModule)
                              .append("::std__ParseError::__moss_from_any(&").append(writeArg).append("); ");
                    }
                    callArgs.add("&mut __moss_provider_val");
                    String callStr = recv + "." + methodName + "(" + String.join(", ", callArgs) + ")";
                    if (methodMayRaise(writeErrorMethod)) {
                        bridge.append("let __moss_res = ").append(callStr).append("; ");
                        bridge.append(writeArg).append(" = ");
                        if (!providerModule.isEmpty()) bridge.append("std__ParseError::__moss_from_any(&__moss_provider_val)");
                        else bridge.append("__moss_provider_val");
                        bridge.append("; ");
                        if (currentInTry.get()) {
                            bridge.append("__moss_res.map_err(|e| __moss_raise(format!(\"{:?}\", e)))? }");
                        } else {
                            bridge.append("__moss_res.unwrap_or_default() }");
                        }
                    } else {
                        bridge.append("let __moss_res = ").append(callStr).append("; ");
                        bridge.append(writeArg).append(" = ");
                        if (!providerModule.isEmpty()) bridge.append("std__ParseError::__moss_from_any(&__moss_provider_val)");
                        else bridge.append("__moss_provider_val");
                        bridge.append("; ");
                        bridge.append("__moss_res }");
                    }
                    return bridge.toString();
                }
                if (methodName.equals("fail") || methodName.endsWith("__fail") || methodName.equals("poke")) {
                    List<String> callArgs = new ArrayList<>();
                    for (String ra : rawArgs) {
                        callArgs.add(lowerExpr(ra.trim(), stateFields, domains, objects));
                    }
                    Program callProgram = currentProgram.get();
                    Function qualifiedSourceFreeFunction = callProgram == null ? null
                            : callProgram.functions.stream()
                                    .filter(function -> function.name.equals(recv + "." + methodName)
                                            && function.sourceFreeAbiVersion > 0
                                            && !currentGenericFunctions.get().contains(function.name))
                                    .findFirst().orElse(null);
                    if (qualifiedSourceFreeFunction != null) {
                        String rustPath = sourceFreeFunctionRustPath(qualifiedSourceFreeFunction,
                                qualifiedSourceFreeFunction.name, currentGenericFunctions.get());
                        String qualifiedCall = rustPath + "(" + String.join(", ", callArgs) + ")";
                        if (currentInTry.get()) {
                            return qualifiedCall
                                    + ".map_err(|e| __moss_raise(format!(\"{:?}\", e)))?";
                        }
                        return qualifiedCall + ".unwrap_or_default()";
                    }
                    String callStr = recv + "." + methodName + "(" + String.join(", ", callArgs) + ")";
                    if (currentInTry.get()) {
                        return callStr + ".map_err(|e| __moss_raise(format!(\"{:?}\", e)))?";
                    }
                    return callStr + ".unwrap_or_default()";
                }
                if (callee.equals("FileIO.open")) {
                    String pathArg = rawArgs.isEmpty() ? "MossStr::from(\"\")" : lowerExpr(rawArgs.get(0).trim(), stateFields, domains, objects);
                    if (rawArgs.size() >= 1 && rawArgs.get(0).trim().matches("^[A-Za-z_][A-Za-z0-9_]*$")) {
                        pathArg = pathArg + ".clone()";
                    }
                    String callStr = "__MossFile::open(" + pathArg + ")";
                    if (currentInTry.get()) {
                        return callStr + "?";
                    }
                    return callStr + ".unwrap_or_default()";
                }
                String loweredRecv = lowerExpr(recv, stateFields, domains, objects);
                if (methodName.equals("codepoint") && rawArgs.isEmpty()
                        && ("char".equals(bindingTypeName(recv))
                        || recv.matches("'(?:[^'\\\\]|\\\\.)*'"))) {
                    return "((" + loweredRecv + ") as u32 as i64)";
                }
                if (methodName.equals("push") && rawArgs.size() == 1) {
                    String itemExpr = lowerExpr(rawArgs.get(0).trim(), stateFields, domains, objects);
                    if (rawArgs.get(0).trim().matches("^[A-Za-z_][A-Za-z0-9_]*$")) {
                        itemExpr = itemExpr + ".clone()";
                    }
                    return loweredRecv + ".__moss_push(" + itemExpr + ")";
                }
                if (methodName.equals("pop") && rawArgs.isEmpty()) {
                    return loweredRecv + ".__moss_pop()";
                }
                if (methodName.equals("keys") && rawArgs.isEmpty()) {
                    return loweredRecv + ".__moss_keys()";
                }
                if (methodName.equals("values") && rawArgs.isEmpty()) {
                    return loweredRecv + ".__moss_values()";
                }
                if (methodName.equals("get") && rawArgs.size() == 2) {
                    String keyExpr = lowerExpr(rawArgs.get(0).trim(), stateFields, domains, objects);
                    String fbExpr = lowerExpr(rawArgs.get(1).trim(), stateFields, domains, objects);
                    return "((*(" + loweredRecv + ").get(&(" + keyExpr + ")).unwrap_or(&(" + fbExpr + "))).clone())";
                }
                if (methodName.equals("delete") && rawArgs.size() == 3) {
                    String keyExpr = lowerExpr(rawArgs.get(0).trim(), stateFields, domains, objects);
                    String fbExpr = lowerExpr(rawArgs.get(1).trim(), stateFields, domains, objects);
                    String foundTarget = lowerExpr(rawArgs.get(2).trim(), stateFields, domains, objects);
                    return "{ let __k = (" + keyExpr + ").clone(); let __fb = (" + fbExpr + ").clone(); if let Some(__v) = (" + loweredRecv + ").remove(&__k) { " + foundTarget + " = true; __v } else { " + foundTarget + " = false; __fb } }";
                }
                List<String> callArgs = new ArrayList<>();
                for (int ai = 0; ai < rawArgs.size(); ai++) {
                    String ra = rawArgs.get(ai).trim();
                    String loweredRa = lowerExpr(ra, stateFields, domains, objects);
                    if (ai == 0 && currentWriteMethods.get().contains(methodName)) {
                        loweredRa = "&mut " + loweredRa;
                    } else if (ra.matches("^[A-Za-z_][A-Za-z0-9_]*$") && !ra.equals("true") && !ra.equals("false")) {
                        loweredRa = loweredRa + ".clone()";
                    }
                    callArgs.add(loweredRa);
                }
                String methodCall = loweredRecv + "." + methodName
                        + "(" + String.join(", ", callArgs) + ")";
                Method resolvedMethod = resolveObjectMethod(recv, methodName, rawArgs.size(),
                        currentEnclosingFunctionName());
                Program callProgram = currentProgram.get();
                Function qualifiedSourceFreeFunction = callProgram == null ? null
                        : callProgram.functions.stream()
                                .filter(function -> function.name.equals(recv + "." + methodName)
                                        && function.sourceFreeAbiVersion > 0
                                        && !currentGenericFunctions.get().contains(function.name))
                                .findFirst().orElse(null);
                if (resolvedMethod == null && qualifiedSourceFreeFunction != null) {
                    String rustPath = sourceFreeFunctionRustPath(qualifiedSourceFreeFunction,
                            qualifiedSourceFreeFunction.name, currentGenericFunctions.get());
                    String qualifiedCall = rustPath + "(" + String.join(", ", callArgs) + ")";
                    if (!qualifiedSourceFreeFunction.sourceFreeRaiseVariants.isEmpty()) {
                        if (currentInTry.get()) {
                            return qualifiedCall
                                    + ".map_err(|e| __moss_raise(format!(\"{:?}\", e)))?";
                        }
                        return qualifiedCall + ".unwrap_or_default()";
                    }
                    return qualifiedCall;
                }
                if (methodMayRaise(resolvedMethod)) {
                    if (currentInTry.get()) {
                        if (resolvedMethod.sourceFree) {
                            return methodCall + ".map_err(|e| __moss_raise(format!(\"{:?}\", e)))?";
                        }
                        return methodCall + "?";
                    }
                    return methodCall + ".unwrap_or_default()";
                }
                return methodCall;
            }

            // Generic macro call e.g. twice(x) or erase(m, "a", 99, found)
            if (currentGenericFunctions.get().contains(callee)) {
                String args = expr.substring(openParen + 1, expr.length() - 1).trim();
                List<String> macroArgs = new ArrayList<>();
                if (!args.isEmpty()) {
                    for (String part : splitTopLevel(args, ',')) {
                        part = part.trim();
                        if (stateFields.contains(part)) {
                            macroArgs.add("(*state." + part + ")");
                        } else {
                            macroArgs.add(lowerExpr(part, stateFields, domains, objects));
                        }
                    }
                }
                return "__moss_fn_" + callee + "!(" + String.join(", ", macroArgs) + ")";
            }

            Program current = currentProgram.get();
            if ("sort".equals(callee) && (current == null
                    || current.functions.stream().noneMatch(function -> function.name.equals("sort")))) {
                List<String> sortArgs = splitTopLevel(
                        expr.substring(openParen + 1, expr.length() - 1).trim(), ',');
                return lowerSortCall(sortArgs, stateFields, domains, objects);
            }

            if ("replace".equals(callee)) {
                String args = expr.substring(openParen + 1, expr.length() - 1).trim();
                List<String> rArgs = splitTopLevel(args, ',');
                if (rArgs.size() == 2) {
                    String placeStr = rArgs.get(0).trim();
                    String repStr = rArgs.get(1).trim();
                    String repLowered = lowerExpr(repStr, stateFields, domains, objects);
                    String placeLowered;
                    if (placeStr.endsWith("]") && placeStr.contains("[")) {
                        int ob = ExpressionTyper.findMatchingOpenBracket(placeStr, placeStr.length() - 1);
                        String base = placeStr.substring(0, ob).trim();
                        String idx = placeStr.substring(ob + 1, placeStr.length() - 1).trim();
                        placeLowered = base + "[" + lowerExpr(idx, stateFields, domains, objects) + " as usize]";
                    } else if (stateFields.contains(placeStr)) {
                        placeLowered = "(*state." + placeStr + ")";
                    } else {
                        placeLowered = placeStr;
                    }
                    return "{ let __moss_replacement = " + repLowered + "; std::mem::replace(&mut " + placeLowered + ", __moss_replacement) }";
                }
            }

            // Normal function call e.g. inspect(record) or add(a, b) or FlipFn(...)
            if (Character.isLowerCase(callee.charAt(0)) || callee.contains("__")
                    || isKnownFunction(callee, simpleName)) {
                String args = expr.substring(openParen + 1, expr.length() - 1).trim();
                List<String> argList = new ArrayList<>();
                Set<Integer> writeArgs = currentWriteParamIndices.get().getOrDefault(callee,
                        currentWriteParamIndices.get().getOrDefault(simpleName, Collections.emptySet()));
                if (!args.isEmpty()) {
                    for (String part : splitTopLevel(args, ',')) {
                        int argIndex = argList.size();
                        part = part.trim();
                        int colonIdx = -1;
                        int pDepth = 0;
                        boolean inQ = false;
                        for (int ci = 0; ci < part.length(); ci++) {
                            char ch = part.charAt(ci);
                            if (ch == '"' && (ci == 0 || part.charAt(ci - 1) != '\\')) inQ = !inQ;
                            if (!inQ) {
                                if (ch == '(' || ch == '[' || ch == '{') pDepth++;
                                else if (ch == ')' || ch == ']' || ch == '}') pDepth--;
                                else if (ch == ':' && pDepth == 0) {
                                    colonIdx = ci;
                                    break;
                                }
                            }
                        }
                        if (colonIdx > 0) {
                            part = part.substring(colonIdx + 1).trim();
                        }
                        boolean isObjStateField = false;
                        if (stateFields.contains(part)) {
                            for (Domain dom : domains.values()) {
                                for (Field fld : dom.state) {
                                    if (fld.name.equals(part) && fld.type != null) {
                                        String rt = rustType(fld.type);
                                        if (objects.containsKey(fld.type) || objects.containsKey(rt) ||
                                            (!rt.equals("i64") && !rt.equals("f64") && !rt.equals("bool") && !rt.equals("char") && !rt.equals("MossStr") && !rt.equals("()"))) {
                                            isObjStateField = true;
                                        }
                                    }
                                }
                            }
                        }
                        if (isObjStateField) {
                            argList.add("state." + part);
                        } else {
                            String loweredPart = lowerExpr(part, stateFields, domains, objects);
                            if (writeArgs.contains(argIndex)) {
                                loweredPart = "&mut " + loweredPart;
                            } else if (loweredPart.contains(" { ") && !loweredPart.startsWith("&")) {
                                Program prog = currentProgram.get();
                                Function calleeFn = prog != null ? prog.functions.stream().filter(f -> f.name.equals(callee) || f.name.equals(simpleName)).findFirst().orElse(null) : null;
                                boolean calleeTakesValue = calleeFn != null && calleeFn.exported;
                                if (!calleeTakesValue) {
                                    loweredPart = "&" + loweredPart;
                                }
                            } else {
                                boolean isNominalVar = false;
                                Program prog = currentProgram.get();
                                if (prog != null && prog.localBindingTypes != null) {
                                    for (Map<String, org.mosslang.compiler.types.TypeRef> bindings : prog.localBindingTypes.values()) {
                                        org.mosslang.compiler.types.TypeRef ref = bindings.get(part);
                                        if (ref != null && ref.kind() == org.mosslang.compiler.types.TypeKind.NominalInstance) {
                                            isNominalVar = true;
                                            break;
                                        }
                                    }
                                }
                                if (!isNominalVar && (part.equals("a") || part.equals("b") || part.equals("token")) && (simpleName.equals("read") || simpleName.endsWith("__read"))) {
                                    isNominalVar = true;
                                }
                                if (isNominalVar) {
                                    loweredPart = "&" + loweredPart;
                                } else if (part.matches("^[A-Za-z_][A-Za-z0-9_]*$") && !part.equals("true") && !part.equals("false")) {
                                    loweredPart = loweredPart + ".clone()";
                                }
                            }
                            argList.add(loweredPart);
                        }
                    }
                }
                if (callee.equals("sqrt") && argList.size() == 1) {
                    return "((" + argList.get(0) + ") as f64).sqrt()";
                }
                    Function calleeFunction = currentProgram.get() == null ? null
                            : currentProgram.get().functions.stream()
                                    .filter(function -> function.name.equals(callee) || function.name.equals(simpleName))
                                    .findFirst().orElse(null);
                    int finalArgIndex = argList.size() - 1;
                    if (!argList.isEmpty() && writeArgs.contains(finalArgIndex)
                            && isStandardErrorWriteParam(calleeFunction, finalArgIndex)) {
                        List<String> rawArgs = splitTopLevel(args, ',');
                        String writeArg = rawArgs.get(rawArgs.size() - 1).trim();
                        StringBuilder bridge = new StringBuilder();
                        bridge.append("{ ");
                        List<String> callArgs = new ArrayList<>();
                        for (int ai = 0; ai < rawArgs.size() - 1; ai++) {
                            String tmp = "__moss_fn_arg_" + ai;
                            bridge.append("let ").append(tmp).append(" = ")
                                  .append(lowerExpr(rawArgs.get(ai).trim(), stateFields, domains, objects)).append("; ");
                            callArgs.add(tmp);
                        }
                        if (callee.contains("__")) {
                            String providerMod = callee.substring(0, callee.indexOf("__"));
                            bridge.append("let mut __moss_provider_val = moss_").append(providerMod)
                                  .append("::std__ParseError::__moss_from_any(&").append(writeArg).append("); ");
                        } else {
                            bridge.append("let mut __moss_provider_val = ").append(writeArg).append(".clone(); ");
                        }
                        callArgs.add("&mut __moss_provider_val");
                        bridge.append("let __moss_res = ").append(callee).append("(").append(String.join(", ", callArgs)).append("); ");
                        bridge.append(writeArg).append(" = std__ParseError::__moss_from_any(&__moss_provider_val); ");
                        bridge.append("__moss_res }");
                        return bridge.toString();
                    }
                    if (callee.equals("assert")) {
                    return "assert!(" + String.join(", ", argList) + ")";
                }
                if (callee.equals("assertEqual") || callee.equals("assert_eq")) {
                    return "assert_eq!(" + String.join(", ", argList) + ")";
                }
                boolean calleeRaises = currentRaisingFunctions.get().contains(callee)
                        || simpleName.equals("fail");
                    String rustCallee = sourceFreeFunctionRustPath(calleeFunction, callee,
                            currentGenericFunctions.get());
                    String callStr = rustCallee + "(" + String.join(", ", argList) + ")";
                if (calleeRaises) {
                    if (currentInTry.get()) {
                        // Same-crate Moss callables use the shared __MossRaised
                        // carrier. Preserve its typed on_fail captures while
                        // propagating instead of flattening the error to text.
                        return callStr + "?";
                    } else {
                        return callStr + ".unwrap_or_default()";
                    }
                }
                return callStr;
            }
        }

        // Indexing e.g. values[0]
        if (expr.endsWith("]") && !expr.startsWith("[")) {
            int closeBr = expr.length() - 1;
            int depth = 0;
            int openBr = -1;
            for (int i = closeBr; i >= 0; i--) {
                char ch = expr.charAt(i);
                if (ch == ']') depth++;
                else if (ch == '[') {
                    depth--;
                    if (depth == 0) {
                        openBr = i;
                        break;
                    }
                }
            }
            if (openBr > 0) {
                String rawBase = expr.substring(0, openBr).trim();
                String base = lowerExpr(rawBase, stateFields, domains, objects);
                String idx = lowerExpr(expr.substring(openBr + 1, closeBr).trim(), stateFields, domains, objects);
                boolean isMap = false;
                Program prog = currentProgram.get();
                if (prog != null && prog.localBindingTypes != null) {
                    for (Map<String, org.mosslang.compiler.types.TypeRef> bindings : prog.localBindingTypes.values()) {
                        org.mosslang.compiler.types.TypeRef ref = bindings.get(rawBase);
                        if (ref != null && ref.kind() == org.mosslang.compiler.types.TypeKind.Builtin && "map".equals(ref.name())) {
                            isMap = true;
                            break;
                        }
                    }
                }
                if (isMap) {
                    return "((" + base + ")[&(" + idx + ")].clone())";
                }
                return base + "[(" + idx + ") as usize].clone()";
            }
        }

        // Write parameter access e.g. value -> (*value)
        if (currentWriteParams.get().contains(expr)) {
            return "(*" + expr + ")";
        }

        // State field access inside handler e.g. value -> (*state.value)
        if (stateFields.contains(expr)) {
            return "(*state." + expr + ")";
        }

        for (String f : stateFields) {
            expr = expr.replaceAll("\\b" + f + "\\b", "(*state." + f + ")");
        }

        return expr;
    }
}
