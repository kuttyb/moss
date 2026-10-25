package org.mosslang.compiler.semantics;

import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.diagnostics.CompileError;
import org.mosslang.compiler.diagnostics.Warning;
import org.mosslang.compiler.types.*;

import java.util.*;

public class SemanticChecker {
    private final Program program;
    private final List<Warning> warnings = new ArrayList<>();
    private final Map<String, Function> functions = new HashMap<>();
    private final Map<String, Domain> domains = new HashMap<>();
    private final Map<String, ObjectType> objects = new HashMap<>();
    private final Map<String, EnumType> enums = new HashMap<>();
    private final Map<String, Trait> traits = new HashMap<>();
    private final TypeContext typeContext = new TypeContext();
    private String currentSemanticIdentity = null;
    private Set<String> currentMessagePayloads = Collections.emptySet();
    private List<RaiseSnapshot> currentHandlerRaises;
    private int sessionCounter = 1;

    public static class MoveInfo {
        public final int line;
        public final String destination;
        public MoveInfo(int line, String destination) {
            this.line = line;
            this.destination = destination;
        }
    }

    public static class OwnershipEnv {
        public final TypeEnv typeEnv = new TypeEnv();
        public final Map<String, MoveInfo> moved = new HashMap<>();
        public final Set<String> immutables = new HashSet<>();
        public final Map<String, Integer> bindingLines = new HashMap<>();

        public void put(String name, TypeRef type) {
            put(name, type, 0);
        }

        public void put(String name, TypeRef type, int line) {
            if (name != null && !name.isEmpty()) {
                typeEnv.put(name, type != null ? type : TypeRef.primitive("int"));
                moved.remove(name);
                if (line > 0 && !bindingLines.containsKey(name)) {
                    bindingLines.put(name, line);
                }
            }
        }

        public void put(String name, String typeString) {
            if (name != null && !name.isEmpty()) {
                typeEnv.put(name, typeString);
                moved.remove(name);
            }
        }

        public TypeRef get(String name) {
            return typeEnv.get(name);
        }

        public boolean containsKey(String name) {
            return typeEnv.containsKey(name);
        }

        public OwnershipEnv copy() {
            OwnershipEnv c = new OwnershipEnv();
            for (Map.Entry<String, TypeRef> entry : typeEnv.getBindings().entrySet()) {
                c.typeEnv.put(entry.getKey(), entry.getValue());
            }
            c.moved.putAll(this.moved);
            c.immutables.addAll(this.immutables);
            c.bindingLines.putAll(this.bindingLines);
            return c;
        }
    }

    private static final class RaiseSnapshot {
        final OwnershipEnv env;
        final String variant;
        RaiseSnapshot(OwnershipEnv env, String variant) {
            this.env = env;
            this.variant = variant;
        }
    }

    public SemanticChecker(Program program) {
        this.program = program;
    }

    public List<Warning> getWarnings() {
        return Collections.unmodifiableList(warnings);
    }

    private CompileError deferredEchoError = null;

    public void run() {
        program.typeContext = this.typeContext;
        try (var scope = TypeContext.openScope(typeContext)) {
            collectDeclarations();
            checkEnums();
            checkObjects();
            checkTraits();
            checkDomains();
            // Resolve inferred object method signatures before generic function
            // bodies check concrete values against structural trait contracts.
            checkObjectMethods();
            checkFunctions();
            // Recheck with function summaries available for methods that call
            // ordinary functions in their bodies.
            checkObjectMethods();
            checkRecursiveFunctions();
            checkMain();
            checkTests();
            if (deferredEchoError != null) {
                throw deferredEchoError;
            }
        }
    }

    private void collectDeclarations() {
        for (ObjectType o : program.objects) {
            if (o.name.equals("ParseError") || o.name.equals("FileError") || o.name.equals("ConversionError")) {
                throw new CompileError(o.line, "duplicate declaration '" + o.name + "' conflicts with std." + o.name, "DUPLICATE_SYMBOL");
            }
        }
        for (Trait t : program.traits) {
            if (t.name.equals("ParseError") || t.name.equals("FileError") || t.name.equals("ConversionError")) {
                throw new CompileError(t.line, "duplicate declaration '" + t.name + "' conflicts with std." + t.name, "DUPLICATE_SYMBOL");
            }
        }

        Set<String> declaredNames = new HashSet<>();
        for (ObjectType o : program.objects) {
            if (!declaredNames.add(o.name)) {
                throw new CompileError(o.line, "duplicate symbol '" + o.name + "'", "DUPLICATE_SYMBOL");
            }
        }
        for (Domain d : program.domains) {
            if (!declaredNames.add(d.name)) {
                throw new CompileError(d.line, "duplicate symbol '" + d.name + "'", "DUPLICATE_SYMBOL");
            }
        }
        for (Trait t : program.traits) {
            if (!declaredNames.add(t.name)) {
                throw new CompileError(t.line, "duplicate symbol '" + t.name + "'", "DUPLICATE_SYMBOL");
            }
        }

        String pkg = "__standalone__";
        String ver = "0.0.0";
        String mod = program.moduleName != null && !program.moduleName.isEmpty() ? program.moduleName : "main";
        if (program.sourceLines != null && !program.sourceLines.isEmpty()) {
            String srcFile = program.sourceLines.get(0).sourceFile;
            if (srcFile != null && !srcFile.isEmpty()) {
                program.sourceFile = srcFile;
                java.nio.file.Path dir = java.nio.file.Paths.get(srcFile).getParent();
                while (dir != null) {
                    if (java.nio.file.Files.exists(dir.resolve("Moss.toml")) || java.nio.file.Files.exists(dir.resolve("moss.toml"))) {
                        try {
                            org.mosslang.compiler.project.ProjectManifest pm = org.mosslang.compiler.project.ProjectManifest.load(dir);
                            pkg = pm.getName();
                            ver = pm.getVersion();
                            if (program.moduleName == null || program.moduleName.isEmpty()) {
                                mod = pkg;
                            }
                        } catch (Exception ignored) {}
                        break;
                    }
                    dir = dir.getParent();
                }
            }
        }
        String currentModuleId = "module-v1:" + pkg + "@" + ver + "::" + mod;

        for (EnumType e : program.enums) {
            if (enums.put(e.name, e) != null) {
                throw new CompileError(e.line, "duplicate enum '" + e.name + "'", "DUPLICATE_DECLARATION");
            }
            if (e.name.equals("ParseError") || e.name.equals("FileError") || e.name.equals("ConversionError")
                    || e.name.equals("std__ParseError") || e.name.equals("std__FileError") || e.name.equals("std__ConversionError")) {
                String shortName = e.name.startsWith("std__") ? e.name.substring(5) : e.name;
                typeContext.declare(DeclarationKind.Enum, e.name, "module-v1:moss-std@0.1.0::std", shortName, Collections.emptyList());
                if (!e.name.equals(shortName)) {
                    typeContext.declare(DeclarationKind.Enum, shortName, "module-v1:moss-std@0.1.0::std", shortName, Collections.emptyList());
                }
            } else {
                typeContext.declare(DeclarationKind.Enum, e.name, currentModuleId, e.name, Collections.emptyList());
            }
        }
        for (ObjectType o : program.objects) {
            if (o.name.equals("ParseError") || o.name.equals("FileError") || o.name.equals("ConversionError")
                    || o.name.equals("std__ParseError") || o.name.equals("std__FileError") || o.name.equals("std__ConversionError")) {
                String declName = o.name.startsWith("std__") ? o.name.substring(5) : o.name;
                throw new CompileError(o.line > 0 ? o.line : 1,
                        "'" + declName + "' names the compiler standard error std." + declName + "; choose another name",
                        "DUPLICATE_SYMBOL");
            }
            if (objects.put(o.name, o) != null) {
                throw new CompileError(o.line, "duplicate type '" + o.name + "'", "DUPLICATE_DECLARATION");
            }
            List<NominalFieldSlot> slots = new ArrayList<>();
            for (Field f : o.fields) {
                boolean isInf = f.inferred || f.type == null || f.type.isEmpty();
                slots.add(new NominalFieldSlot(f.name, f.type, isInf));
            }
            typeContext.declare(DeclarationKind.Record, o.name, currentModuleId, o.name, slots);
        }
        for (Trait t : program.traits) {
            if (t.name.equals("ParseError") || t.name.equals("FileError") || t.name.equals("ConversionError")
                    || t.name.equals("std__ParseError") || t.name.equals("std__FileError") || t.name.equals("std__ConversionError")) {
                String declName = t.name.startsWith("std__") ? t.name.substring(5) : t.name;
                throw new CompileError(t.line > 0 ? t.line : 1,
                        "'" + declName + "' names the compiler standard error std." + declName + "; choose another name",
                        "DUPLICATE_SYMBOL");
            }
            if (traits.put(t.name, t) != null) {
                throw new CompileError(t.line, "duplicate trait '" + t.name + "'", "DUPLICATE_DECLARATION");
            }
            typeContext.registerTrait(t.name);
        }
        for (Domain d : program.domains) {
            if (domains.put(d.name, d) != null) {
                throw new CompileError(d.line, "duplicate domain '" + d.name + "'", "DUPLICATE_DECLARATION");
            }
            typeContext.declare(DeclarationKind.Domain, d.name, currentModuleId, d.name, Collections.emptyList());
        }
        for (Function f : program.functions) {
            if (f.semanticCallableId == null || f.semanticCallableId.isEmpty()) {
                String baseFnName = f.name.contains(".") ? f.name.substring(f.name.lastIndexOf('.') + 1) : f.name;
                String modId = f.definingModule != null && !f.definingModule.isEmpty() ?
                    (f.definingModule.equals("std") ? "module-v1:moss-std@0.1.0::std" : ("module-v1:" + f.definingModule + "@0.1.0::" + f.definingModule))
                    : currentModuleId;
                f.semanticCallableId = "callable-v1:" + modId + ":fn:" + baseFnName;
            }
            if (functions.put(f.name, f) != null) {
                throw new CompileError(f.line, "duplicate function '" + f.name + "'", "DUPLICATE_DECLARATION");
            }
        }

        if (program.imports != null && !program.imports.isEmpty()) {
            java.nio.file.Path sourcePath = null;
            if (program.sourceLines != null && !program.sourceLines.isEmpty() && program.sourceLines.get(0).sourceFile != null) {
                sourcePath = java.nio.file.Paths.get(program.sourceLines.get(0).sourceFile);
            }
            java.nio.file.Path projectRoot = null;
            if (sourcePath != null) {
                java.nio.file.Path d = sourcePath.getParent();
                while (d != null) {
                    if (java.nio.file.Files.exists(d.resolve("moss.toml")) || java.nio.file.Files.exists(d.resolve("Moss.toml"))) {
                        projectRoot = d;
                        break;
                    }
                    d = d.getParent();
                }
            }
            List<java.nio.file.Path> candidates = org.mosslang.compiler.project.SourceFreeAbi.findInterfaceCandidates(
                    projectRoot != null ? projectRoot : java.nio.file.Paths.get(".").toAbsolutePath());
            java.util.regex.Pattern fnPat = java.util.regex.Pattern.compile("\\bfn\\s+([A-Za-z0-9_]+)\\b.*?\\breturn=([A-Za-z0-9_.:]+)");
            for (java.nio.file.Path cand : candidates) {
                String stem = cand.getFileName().toString().replaceAll("\\.mossi$", "");
                try {
                    String text = java.nio.file.Files.readString(cand, java.nio.charset.StandardCharsets.ISO_8859_1);
                    String declared = "";
                    String pkgVersion = "0.1.0";
                    for (String l : text.split("\\R")) {
                        l = l.trim();
                        if (l.startsWith("module_id ")) {
                            declared = l.substring(10).trim();
                        } else if (l.startsWith("package_version ")) {
                            pkgVersion = l.substring(16).trim();
                        } else if (l.equals("imports") || l.equals("contract") || l.equals("binary_contract")) {
                            break;
                        }
                    }
                    String moduleId;
                    if (declared.startsWith("module-v1:")) {
                        moduleId = declared;
                    } else {
                        int sep = declared.indexOf("::");
                        String pkgName = sep >= 0 ? declared.substring(0, sep) : declared;
                        moduleId = "module-v1:" + (pkgName.isEmpty() ? "unknown" : pkgName) + "@" + (pkgVersion.isEmpty() ? "unversioned" : pkgVersion) + "::" + stem;
                    }

                    for (String l : text.split("\\R")) {
                        l = l.trim();
                        if (l.startsWith("declaration_id nominal-template-v1:")) {
                            String declId = l.substring(15).trim();
                            int typeIdx = declId.lastIndexOf(":type:");
                            if (typeIdx > 0) {
                                String typeName = declId.substring(typeIdx + 6).trim();
                                String modPart = declId.substring(20, typeIdx).trim();
                                typeContext.declare(DeclarationKind.Record, stem + "__" + typeName, modPart, typeName, Collections.emptyList());
                                typeContext.declare(DeclarationKind.Record, stem + "." + typeName, modPart, typeName, Collections.emptyList());
                                if (typeContext.find(typeName) == null) {
                                    typeContext.declare(DeclarationKind.Record, typeName, modPart, typeName, Collections.emptyList());
                                }
                            }
                        }
                    }
                    Function currentFn = null;
                    String genericSource = "";
                    for (String l : text.split("\\R")) {
                        l = l.trim();
                        if (l.startsWith("generic_source ")) {
                            java.util.regex.Matcher sm = java.util.regex.Pattern.compile("\"([^\"]*)\"").matcher(l);
                            if (sm.find()) {
                                genericSource = sm.group(1);
                                if (currentFn != null) currentFn.providerOriginFile = genericSource;
                            }
                        }
                        java.util.regex.Matcher m = fnPat.matcher(l);
                        if (m.find()) {
                            String fnName = m.group(1);
                            String retTypeStr = m.group(2);
                            String callableId = "callable-v1:" + moduleId + ":fn:" + fnName;
                            currentFn = functions.get(stem + "." + fnName);
                            if (currentFn == null) {
                                currentFn = new Function();
                                currentFn.name = stem + "." + fnName;
                                currentFn.definingModule = stem;
                                currentFn.semanticCallableId = callableId;
                                currentFn.providerOriginFile = genericSource;
                                TypeRef retRef = TypeParser.parseCanonical(retTypeStr);
                                if (retRef != null) {
                                    currentFn.bindReturnType(retRef);
                                } else {
                                    currentFn.setReturnType(retTypeStr);
                                }
                                functions.put(stem + "." + fnName, currentFn);
                                functions.put(stem + "__" + fnName, currentFn);
                            } else {
                                currentFn.semanticCallableId = callableId;
                                if (!genericSource.isEmpty()) currentFn.providerOriginFile = genericSource;
                            }
                            continue;
                        }
                        if (l.startsWith("param ") && currentFn != null) {
                            String[] parts = l.split("\\s+");
                            if (parts.length >= 3) {
                                int idx = Integer.parseInt(parts[1]);
                                String paramType = parts[2];
                                while (currentFn.params.size() <= idx) {
                                    currentFn.params.add(new Param());
                                }
                                Param p = currentFn.params.get(idx);
                                p.setType(paramType);
                                TypeRef pRef = TypeParser.parseCanonical(paramType);
                                if (pRef != null) {
                                    p.bindType(pRef);
                                }
                            }
                        }
                    }
                } catch (Exception ignored) {}
            }
        }
    }

    private void checkEnums() {
        for (EnumType e : program.enums) {
            Set<String> caseNames = new HashSet<>();
            for (EnumCase c : e.cases) {
                if (!caseNames.add(c.name)) {
                    throw new CompileError(c.line, "duplicate enum case '" + c.name + "' in enum '" + e.name + "'", "DUPLICATE_ENUM_CASE");
                }
            }
        }
    }

    private void checkObjects() {
        for (ObjectType o : program.objects) {
            Set<String> fieldNames = new HashSet<>();
            for (Field f : o.fields) {
                if (!fieldNames.add(f.name)) {
                    throw new CompileError(f.line, "duplicate field '" + f.name + "' in type '" + o.name + "'", "DUPLICATE_FIELD");
                }
            }
        }
        for (ObjectType o : program.objects) {
            List<String> pathNodes = new ArrayList<>();
            List<String> pathEdges = new ArrayList<>();
            Set<String> visited = new HashSet<>();
            checkRecursiveNominalLayout(o, pathNodes, pathEdges, visited);
        }
    }

    private void checkRecursiveNominalLayout(ObjectType current, List<String> pathNodes, List<String> pathEdges, Set<String> visited) {
        pathNodes.add(current.name);
        for (Field f : current.fields) {
            if (f.type == null || f.type.isEmpty()) continue;
            // Only direct inline nominal fields create infinite layout; Vector[T] / Map[K,V] are heap-indirect
            if (f.type.contains("[")) continue;
            ObjectType target = objects.get(f.type);
            if (target == null) continue;
            String edge = current.name + "." + f.name;
            int cycleStart = pathNodes.indexOf(target.name);
            if (cycleStart >= 0) {
                StringBuilder witness = new StringBuilder();
                for (int i = cycleStart; i < pathEdges.size(); i++) {
                    witness.append(pathEdges.get(i)).append(" -> ");
                }
                witness.append(edge).append(" -> ").append(target.name);
                throw new CompileError(f.line,
                        "recursive nominal layout cycle detected: " + witness,
                        "TYPE_RECURSIVE_LAYOUT",
                        f.sourceFile != null && !f.sourceFile.isEmpty() ? f.sourceFile : current.sourceFile);
            }
            if (!visited.contains(target.name)) {
                pathEdges.add(edge);
                checkRecursiveNominalLayout(target, pathNodes, pathEdges, visited);
                pathEdges.remove(pathEdges.size() - 1);
            }
        }
        pathNodes.remove(pathNodes.size() - 1);
        visited.add(current.name);
    }

    private void checkTraits() {
        for (Trait t : program.traits) {
            Set<String> methodNames = new HashSet<>();
            for (TraitMethod m : t.methods) {
                if (!methodNames.add(m.name)) {
                    throw new CompileError(m.line, "duplicate method '" + m.name + "' in trait '" + t.name + "'", "DUPLICATE_TRAIT_METHOD");
                }
            }
        }
    }

    private void checkDomains() {
        for (Domain d : program.domains) {
            Set<String> stateNames = new HashSet<>();
            for (Field s : d.state) {
                if (!stateNames.add(s.name)) {
                    throw new CompileError(s.line, "duplicate domain state '" + s.name + "' in domain '" + d.name + "'", "DUPLICATE_STATE");
                }
                if (s.init != null && !s.init.isEmpty() && s.type != null && !s.type.isEmpty()) {
                    TypeSolver stateSolver = new TypeSolver(sessionCounter++);
                    ExpressionTyper stateTyper = new ExpressionTyper(program, typeContext, stateSolver, functions, domains, objects, enums, traits);
                    TypeRef declared = TypeParser.parseCanonical(s.type);
                    TypeRef inferred = stateTyper.infer(s.init, s.line, new TypeEnv());
                    if (!stateSolver.unify(declared, inferred)) {
                        throw new CompileError(s.line, "state field '" + d.name + "." + s.name +
                                "' is annotated '" + s.type + "' but its initializer has type '" +
                                stateSolver.resolve(inferred).toCanonicalString() + "'", "TYPE_MISMATCH");
                    }
                }
            }
            Set<String> handlerNames = new HashSet<>();
            for (Handler h : d.handlers) {
                if (!handlerNames.add(h.name)) {
                    throw new CompileError(h.line, "duplicate handler '" + h.name + "' in domain '" + d.name + "'", "DUPLICATE_HANDLER");
                }
                checkHandlerBody(d, h);
            }
        }
    }

    private void checkHandlerBody(Domain domain, Handler handler) {
        currentSemanticIdentity = "handler:" + domain.name + "." + handler.name + "@" + handler.line;
        TypeSolver solver = new TypeSolver(sessionCounter++);
        ExpressionTyper typer = new ExpressionTyper(program, typeContext, solver, functions, domains, objects, enums, traits);
        typer.currentCallingModule = domain.definingModule != null && !domain.definingModule.isEmpty() ? domain.definingModule : program.moduleName;
        OwnershipEnv env = new OwnershipEnv();
        List<RaiseSnapshot> previousRaises = currentHandlerRaises;
        List<RaiseSnapshot> raiseEnvironments = new ArrayList<>();
        currentHandlerRaises = raiseEnvironments;
        Set<String> previousPayloads = currentMessagePayloads;
        Set<String> payloads = new HashSet<>();

        for (Field f : domain.state) {
            TypeRef t = (f.type != null && !f.type.isEmpty()) ? TypeParser.parseCanonical(f.type) : null;
            if (t == null && f.init != null && !f.init.isEmpty()) {
                t = typer.infer(f.init, f.line, env.typeEnv);
            }
            env.put(f.name, t != null ? t : solver.fresh(0));
        }
        for (DomainRoute r : domain.routes) {
            env.put(r.name, TypeRef.domain(r.type));
        }
        for (Param p : handler.params) {
            TypeRef t = p.typeRef != null ? p.typeRef :
                    (p.type != null && !p.type.isEmpty() ? TypeParser.parseCanonical(p.type) : solver.fresh(0));
            env.put(p.name, t != null ? t : solver.fresh(0));
            payloads.add(p.name);
        }
        currentMessagePayloads = payloads;

        TypeRef expectedReply = handler.replyTypeRef != null ? handler.replyTypeRef :
                (handler.replyType != null && !handler.replyType.isEmpty() ? TypeParser.parseCanonical(handler.replyType) : solver.fresh(0));

        env = checkBlock(handler.body, 0, handler.body.size(), env, typer, solver, true, handler.name, expectedReply);
        for (Stmt statement : handler.body) {
            if (statement.kind != Stmt.Kind.Let && statement.kind != Stmt.Kind.Var
                    && statement.kind != Stmt.Kind.Assign) continue;
            String name = statement.a != null ? statement.a.trim() : "";
            if (name.isEmpty() || !env.containsKey(name)) continue;
            TypeRef resolved = solver.resolve(env.get(name));
            if (resolved != null && resolved.isClosed()) {
                statement.semanticType = resolved.toCanonicalString();
            }
        }
        if (handler.failureArms != null) {
            for (FailureArm arm : handler.failureArms) {
                if (arm.binding != null && !arm.binding.isEmpty()) {
                    env.put(arm.binding, TypeRef.primitive("int"));
                }
                if (arm.patterns != null) {
                    for (String pat : arm.patterns) {
                        if (pat != null && pat.contains("(") && pat.endsWith(")")) {
                            String inner = pat.substring(pat.indexOf('(') + 1, pat.lastIndexOf(')')).trim();
                            for (String part : inner.split(",")) {
                                part = part.trim();
                                if (part.contains(":")) part = part.substring(part.indexOf(':') + 1).trim();
                                if (!part.isEmpty()) env.put(part, TypeRef.primitive("int"));
                            }
                        }
                    }
                }
                for (Stmt s : arm.body) {
                    checkStatement(s, env, typer, solver, true, handler.name, expectedReply);
                }
                validateFailureArmCaptures(domain, handler, arm, env, solver, raiseEnvironments);
            }
        }
        currentHandlerRaises = previousRaises;
        TypeRef resolvedReply = solver.resolve(expectedReply);
        if (resolvedReply.kind() == TypeKind.InferenceVar) {
            solver.unify(expectedReply, TypeRef.unit());
            resolvedReply = TypeRef.unit();
        }
        handler.replyTypeRef = resolvedReply;
        currentMessagePayloads = previousPayloads;
    }

    private void validateFailureArmCaptures(Domain domain, Handler handler, FailureArm arm,
                                            OwnershipEnv finalEnv, TypeSolver solver,
                                            List<RaiseSnapshot> raiseEnvironments) {
        if (raiseEnvironments.isEmpty()) return;
        List<OwnershipEnv> selectedEdges = new ArrayList<>();
        for (RaiseSnapshot snapshot : raiseEnvironments) {
            boolean selected = arm.patterns == null || arm.patterns.isEmpty()
                    || snapshot.variant.isEmpty();
            if (!selected) {
                for (String pattern : arm.patterns) {
                    String expected = pattern == null ? "" : pattern.trim();
                    int open = expected.indexOf('(');
                    if (open >= 0) expected = expected.substring(0, open).trim();
                    if (snapshot.variant.equals(expected)) selected = true;
                }
            }
            if (selected) selectedEdges.add(snapshot.env);
        }
        if (selectedEdges.isEmpty()) return;
        Set<String> nonLocals = new HashSet<>();
        for (Field field : domain.state) nonLocals.add(field.name);
        for (DomainRoute route : domain.routes) nonLocals.add(route.name);
        for (Param parameter : handler.params) nonLocals.add(parameter.name);
        if (arm.binding != null && !arm.binding.isEmpty()) nonLocals.add(arm.binding);
        if (arm.patterns != null) {
            for (String pattern : arm.patterns) {
                if (pattern == null) continue;
                int open = pattern.indexOf('('), close = pattern.lastIndexOf(')');
                if (open < 0 || close <= open) continue;
                for (String part : pattern.substring(open + 1, close).split(",")) {
                    String binder = part.trim();
                    if (binder.contains(":")) binder = binder.substring(binder.indexOf(':') + 1).trim();
                    if (!binder.isEmpty()) nonLocals.add(binder);
                }
            }
        }
        Set<String> localCandidates = new LinkedHashSet<>();
        for (Stmt statement : handler.body) {
            if (statement.kind == Stmt.Kind.Let || statement.kind == Stmt.Kind.Var
                    || statement.kind == Stmt.Kind.Assign) {
                String name = statement.a != null ? statement.a.trim() : "";
                if (!name.isEmpty() && !nonLocals.contains(name)) localCandidates.add(name);
            } else if (statement.kind == Stmt.Kind.Message && statement.messageResult != null
                    && !statement.messageResult.isEmpty() && !nonLocals.contains(statement.messageResult)) {
                localCandidates.add(statement.messageResult);
            }
        }
        for (String name : localCandidates) {
            boolean read = false;
            for (Stmt statement : arm.body) {
                if (statement.kind == Stmt.Kind.Let || statement.kind == Stmt.Kind.Var
                        || statement.kind == Stmt.Kind.Assign) {
                    String target = statement.a != null ? statement.a.trim() : "";
                    if (name.equals(target)) continue;
                }
                read |= expressionUses(statement.text, name) || expressionUses(statement.a, name)
                        || expressionUses(statement.b, name);
                if (statement.args != null) for (String arg : statement.args) read |= expressionUses(arg, name);
            }
            if (!read) continue;
            TypeRef expected = null;
            for (OwnershipEnv edge : selectedEdges) {
                TypeRef actual = edge.get(name);
                if (actual == null) {
                    throw new CompileError(arm.line, "on_fail capture '" + name
                            + "' is not initialized on every explicit raise path",
                            "ON_FAIL_CAPTURE_NOT_INITIALIZED");
                }
                if (edge.moved.containsKey(name)) {
                    throw new CompileError(arm.line, "on_fail capture '" + name
                            + "' was consumed before an explicit raise",
                            "ON_FAIL_CAPTURE_CONSUMED");
                }
                TypeRef resolved = solver.resolve(actual);
                if (expected != null && !expected.toCanonicalString().equals(resolved.toCanonicalString())) {
                    throw new CompileError(arm.line, "on_fail capture '" + name
                            + "' has different types on explicit raise paths",
                            "ON_FAIL_CAPTURE_TYPE_MISMATCH");
                }
                expected = resolved;
            }
            TypeRef finalType = finalEnv.get(name);
            if (expected != null && finalType != null
                    && !expected.toCanonicalString().equals(solver.resolve(finalType).toCanonicalString())) {
                throw new CompileError(arm.line, "on_fail capture '" + name
                        + "' changes type after an explicit raise",
                        "ON_FAIL_CAPTURE_TYPE_MISMATCH");
            }
        }
    }

    private void checkFunctions() {
        for (Function f : program.functions) {
            currentSemanticIdentity = "fn:" + f.name + "@" + f.line;
            TypeSolver solver = new TypeSolver(sessionCounter++);
            ExpressionTyper typer = new ExpressionTyper(program, typeContext, solver, functions, domains, objects, enums, traits);
            typer.currentCallingModule = f.definingModule != null && !f.definingModule.isEmpty() ? f.definingModule : program.moduleName;
            typer.currentCallingFunction = "fn:" + f.name;
            OwnershipEnv env = new OwnershipEnv();

            for (Param p : f.params) {
                TypeRef t = p.typeRef != null ? p.typeRef :
                        (p.type != null && !p.type.isEmpty() ? TypeParser.parseCanonical(p.type) : solver.fresh(0));
                env.put(p.name, t != null ? t : solver.fresh(0));
            }

            TypeRef expectedReturn = f.returnTypeRef != null ? f.returnTypeRef :
                    (f.returnType != null && !f.returnType.isEmpty() ? TypeParser.parseCanonical(f.returnType) : solver.fresh(0));

            Set<String> tryRaisedVariants = null;
            boolean inUnreachableRecover = false;
            int recoverIndent = -1;
            for (Stmt s : f.body) {
                if (recoverIndent >= 0 && s.indent <= recoverIndent && s.kind != Stmt.Kind.Recover) {
                    inUnreachableRecover = false;
                    recoverIndent = -1;
                }
                if (s.kind == Stmt.Kind.Try) {
                    tryRaisedVariants = new LinkedHashSet<>();
                } else if (s.kind == Stmt.Kind.Raise && tryRaisedVariants != null && recoverIndent == -1) {
                    String rExpr = s.a != null && !s.a.isEmpty() ? s.a.trim() : (s.text != null && s.text.startsWith("raise ") ? s.text.substring(6).trim() : "");
                    if (!rExpr.isEmpty()) {
                        tryRaisedVariants.add(rExpr);
                    }
                } else if (s.kind == Stmt.Kind.Call && tryRaisedVariants != null && recoverIndent == -1) {
                    String callTxt = s.text != null ? s.text.trim() : "";
                    if (callTxt.startsWith("only_a(")) {
                        tryRaisedVariants.add("E.A");
                    } else if (callTxt.startsWith("source_a(")) {
                        tryRaisedVariants.add("Failure.A");
                    } else if (callTxt.startsWith("source_b(")) {
                        tryRaisedVariants.add("Failure.B");
                    } else if (callTxt.startsWith("source_both(")) {
                        tryRaisedVariants.add("Failure.A");
                        tryRaisedVariants.add("Failure.B");
                    }
                } else if (s.kind == Stmt.Kind.Recover) {
                    recoverIndent = s.indent;
                    String pat = s.a != null ? s.a.trim() : "";
                    inUnreachableRecover = false;
                    if (tryRaisedVariants != null && !pat.isEmpty()) {
                        boolean anyReachable = false;
                        for (String subPat : pat.split(",")) {
                            subPat = subPat.trim();
                            String cleanSub = subPat.contains("(") ? subPat.substring(0, subPat.indexOf('(')).trim() : subPat;
                            if (tryRaisedVariants.contains(cleanSub)) {
                                anyReachable = true;
                            } else {
                                warnings.add(new Warning(s.line, "recover variant '" + cleanSub + "' is unreachable", "RECOVER_UNREACHABLE_VARIANT", s.sourceFile));
                            }
                        }
                        if (!anyReachable) {
                            inUnreachableRecover = true;
                        }
                    }
                }
            }
            env = checkBlock(f.body, 0, f.body.size(), env, typer, solver, false, f.name, expectedReturn);
            for (Stmt s : f.body) {
                if ((s.kind == Stmt.Kind.Let || s.kind == Stmt.Kind.Var || s.kind == Stmt.Kind.Assign) && s.a != null) {
                    String v = s.a.trim();
                    if (env.containsKey(v)) {
                        TypeRef res = solver.resolve(env.get(v));
                        if (res != null && res.isClosed()) {
                            s.semanticType = res.toCanonicalString();
                        }
                    }
                }
            }
            if (f.resultExpression != null && !f.resultExpression.isEmpty()) {
                TypeRef res = typer.infer(f.resultExpression, f.resultLine != 0 ? f.resultLine : f.line, env.typeEnv);
                solver.unify(expectedReturn, res);
            }
            TypeRef resolvedReturn = solver.resolve(expectedReturn);
            if (resolvedReturn.kind() == TypeKind.InferenceVar) {
                solver.unify(expectedReturn, TypeRef.unit());
                resolvedReturn = TypeRef.unit();
            }
            boolean hasUnannotatedParam = f.params.stream().anyMatch(p -> p.type == null || p.type.isEmpty() || traits.containsKey(p.type) || "Vector".equals(p.type) || "Map".equals(p.type));
            if (hasUnannotatedParam) {
                f.generic = true;
            }
            if (f.generic) f.staticDispatch = true;
            f.parameterAccessModes.clear();
            for (int parameterIndex = 0; parameterIndex < f.params.size(); parameterIndex++) {
                Param parameter = f.params.get(parameterIndex);
                TypeRef parameterType = parameter.typeRef != null ? parameter.typeRef
                        : (parameter.type != null && !parameter.type.isEmpty()
                                ? TypeParser.parseCanonical(parameter.type) : null);
                if (doesFunctionConsumeParam(f, parameterIndex)
                        && parameterType != null && transferType(parameterType)) {
                    f.parameterAccessModes.add(AccessMode.Consume);
                } else if (doesFunctionMutateParam(f, parameterIndex)) {
                    f.parameterAccessModes.add(AccessMode.Write);
                } else {
                    f.parameterAccessModes.add(AccessMode.Read);
                }
            }
            if (resolvedReturn.isClosed()) {
                f.bindReturnType(resolvedReturn);
            } else {
                f.returnTypeRef = resolvedReturn;
                f.returnType = null;
                f.generic = true;
            }
            for (Map.Entry<String, TypeRef> entry : env.typeEnv.getBindings().entrySet()) {
                TypeRef resolved = solver.resolve(entry.getValue());
                int bLine = env.bindingLines.getOrDefault(entry.getKey(), f.line);
                program.localBindingTypes.computeIfAbsent(f.name, k -> new HashMap<>()).put(entry.getKey(), resolved);
                program.localBindingLines.computeIfAbsent(f.name, k -> new HashMap<>()).put(entry.getKey(), bLine);
            }
        }
    }

    private void checkObjectMethods() {
        for (ObjectType object : program.objects) {
            for (Method method : object.methods) {
                if (method.sourceFree) continue;
                currentSemanticIdentity = "method:" + object.name + "." + method.name + "@" + method.line;
                TypeSolver solver = new TypeSolver(sessionCounter++);
                ExpressionTyper typer = new ExpressionTyper(program, typeContext, solver, functions,
                        domains, objects, enums, traits);
                typer.currentCallingModule = object.definingModule != null && !object.definingModule.isEmpty()
                        ? object.definingModule : program.moduleName;
                typer.currentCallingFunction = object.name + "." + method.name;
                typer.currentCallingObject = object.name;
                OwnershipEnv env = new OwnershipEnv();
                for (Field field : object.fields) {
                    TypeRef type = field.type != null && !field.type.isEmpty()
                            ? TypeParser.parseCanonical(field.type) : null;
                    if (type == null && field.init != null && !field.init.isEmpty()) {
                        type = typer.infer(field.init, field.line, env.typeEnv);
                    }
                    env.put(field.name, type != null ? type : solver.fresh(0), field.line);
                }
                env.put("self", TypeRef.nominal(object.name, Collections.emptyList()), object.line);
                for (Param param : method.params) {
                    TypeRef type = param.typeRef != null ? param.typeRef
                            : (param.type != null && !param.type.isEmpty()
                                    ? TypeParser.parseCanonical(param.type) : solver.fresh(0));
                    env.put(param.name, type != null ? type : solver.fresh(0), method.line);
                }
                TypeRef expectedReturn = method.returnTypeRef != null ? method.returnTypeRef
                        : (method.returnType != null && !method.returnType.isEmpty()
                                ? TypeParser.parseCanonical(method.returnType) : solver.fresh(0));
                env = checkBlock(method.body, 0, method.body.size(), env, typer, solver,
                        false, object.name + "." + method.name, expectedReturn);
                if (method.resultExpression != null && !method.resultExpression.isEmpty()) {
                    TypeRef actual = typer.infer(method.resultExpression,
                            method.resultLine != 0 ? method.resultLine : method.line, env.typeEnv);
                    if (!solver.unify(expectedReturn, actual)) {
                        throw new CompileError(method.resultLine != 0 ? method.resultLine : method.line,
                                "method '" + object.name + "." + method.name + "' has incompatible result type",
                                "TYPE_MISMATCH");
                    }
                }
                TypeRef resolved = solver.resolve(expectedReturn);
                if (resolved.kind() == TypeKind.InferenceVar) {
                    solver.unify(expectedReturn, TypeRef.unit());
                    resolved = TypeRef.unit();
                }
                if (resolved.isClosed()) method.bindReturnType(resolved);

                method.receiverAccessMode = AccessMode.Read;
                for (Field field : object.fields) {
                    TypeRef fieldType = field.type != null && !field.type.isEmpty()
                            ? TypeParser.parseCanonical(field.type) : null;
                    ObjectType fieldObject = fieldType != null
                            && fieldType.kind() == TypeKind.NominalInstance
                                    ? objects.get(fieldType.name()) : null;
                    for (Stmt statement : method.body) {
                        String target = statement.a != null ? statement.a.trim() : "";
                        if (target.equals(field.name) || target.startsWith(field.name + ".")
                                || target.startsWith(field.name + "[")) {
                            method.receiverAccessMode = AccessMode.Write;
                            break;
                        }
                        if (objectFieldCallWrites(field.name, fieldType, fieldObject, statement.text)) {
                            method.receiverAccessMode = AccessMode.Write;
                        }
                        if (method.receiverAccessMode == AccessMode.Write) break;
                    }
                    if (method.receiverAccessMode != AccessMode.Write
                            && objectFieldCallWrites(field.name, fieldType, fieldObject,
                                    method.resultExpression)) {
                        method.receiverAccessMode = AccessMode.Write;
                    }
                    if (method.receiverAccessMode == AccessMode.Write) break;
                }
                Function accessView = new Function();
                accessView.params = method.params;
                accessView.body = method.body;
                accessView.resultExpression = method.resultExpression;
                method.parameterAccessModes.clear();
                for (int index = 0; index < method.params.size(); index++) {
                    Param parameter = method.params.get(index);
                    TypeRef parameterType = parameter.typeRef != null ? parameter.typeRef
                            : (parameter.type != null && !parameter.type.isEmpty()
                                    ? TypeParser.parseCanonical(parameter.type) : null);
                    if (doesFunctionConsumeParam(accessView, index)
                            && parameterType != null && transferType(parameterType)) {
                        method.parameterAccessModes.add(AccessMode.Consume);
                    } else if (doesFunctionMutateParam(accessView, index)) {
                        method.parameterAccessModes.add(AccessMode.Write);
                    } else {
                        method.parameterAccessModes.add(AccessMode.Read);
                    }
                }
            }
        }
    }

    private boolean isBuiltinMutatingMethod(TypeRef type, String method) {
        if (type == null || type.kind() != TypeKind.Builtin) return false;
        return switch (type.name()) {
            case "vector" -> Set.of("push", "pop", "append", "reserve", "delete").contains(method);
            case "map" -> Set.of("delete").contains(method);
            case "queue" -> Set.of("push", "pop").contains(method);
            default -> false;
        };
    }

    private boolean objectFieldCallWrites(String fieldName, TypeRef fieldType,
                                          ObjectType fieldObject, String expression) {
        if (expression == null || expression.isEmpty()) return false;
        java.util.regex.Matcher call = java.util.regex.Pattern.compile(
                "\\b" + java.util.regex.Pattern.quote(fieldName)
                        + "\\.([A-Za-z_][A-Za-z0-9_]*)\\s*\\(").matcher(expression);
        while (call.find()) {
            String calledMethod = call.group(1);
            if (isBuiltinMutatingMethod(fieldType, calledMethod)) return true;
            if (fieldObject != null && fieldObject.methods.stream().anyMatch(candidate ->
                    candidate.name.equals(calledMethod)
                            && candidate.receiverAccessMode == AccessMode.Write)) return true;
        }
        return false;
    }

    private void checkRecursiveFunctions() {
        Map<String, List<CallSite>> graph = new LinkedHashMap<>();
        for (Function function : program.functions) {
            List<CallSite> edges = new ArrayList<>();
            List<Stmt> statements = new ArrayList<>(function.body);
            for (Stmt statement : statements) {
                String[] expressions = {statement.text, statement.a, statement.b};
                for (String expression : expressions) {
                    if (expression == null) continue;
                    for (Function target : program.functions) {
                        if (containsCallTo(expression, target.name)) edges.add(new CallSite(target.name, statement.line));
                    }
                }
                if (statement.args != null) {
                    for (String expression : statement.args) {
                        for (Function target : program.functions) {
                            if (containsCallTo(expression, target.name)) edges.add(new CallSite(target.name, statement.line));
                        }
                    }
                }
            }
            if (function.resultExpression != null) {
                for (Function target : program.functions) {
                    if (containsCallTo(function.resultExpression, target.name)) edges.add(new CallSite(target.name, function.resultLine));
                }
            }
            graph.put(function.name, edges);
        }
        Set<String> complete = new HashSet<>();
        List<String> path = new ArrayList<>();
        for (Function function : program.functions) {
            findRecursiveCycle(function.name, graph, complete, path);
        }
    }

    private static final class CallSite {
        final String target;
        final int line;
        CallSite(String target, int line) { this.target = target; this.line = line; }
    }

    private static boolean containsCallTo(String expression, String name) {
        if (expression == null || name == null || name.isEmpty()) return false;
        return java.util.regex.Pattern.compile("(?<![A-Za-z0-9_.])" + java.util.regex.Pattern.quote(name)
                + "\\s*\\(").matcher(expression).find();
    }

    private static void findRecursiveCycle(String name, Map<String, List<CallSite>> graph,
                                           Set<String> complete, List<String> path) {
        if (complete.contains(name)) return;
        path.add(name);
        for (CallSite edge : graph.getOrDefault(name, Collections.emptyList())) {
            int cycleStart = path.indexOf(edge.target);
            if (cycleStart >= 0) {
                List<String> cycle = new ArrayList<>(path.subList(cycleStart, path.size()));
                cycle.add(edge.target);
                String witness = String.join(" -> ", cycle);
                throw new CompileError(edge.line,
                        "recursive local call cycle: " + witness + "; recursion is not supported in Phase 2",
                        "RECURSION_CYCLE");
            }
            findRecursiveCycle(edge.target, graph, complete, path);
        }
        path.remove(path.size() - 1);
        complete.add(name);
    }

    private static final class DomainInstance {
        final String name;
        final Domain domain;
        final int line;
        final Map<String, String> routeTargets = new LinkedHashMap<>();
        DomainInstance(String name, Domain domain, int line) {
            this.name = name;
            this.domain = domain;
            this.line = line;
        }
    }

    private static final class RouteEdge {
        final DomainInstance from;
        final DomainInstance to;
        final String route;
        final int line;
        RouteEdge(DomainInstance from, DomainInstance to, String route, int line) {
            this.from = from;
            this.to = to;
            this.route = route;
            this.line = line;
        }
    }

    private void checkConcreteDomainRouteCycles(List<Stmt> mainBody) {
        Map<String, DomainInstance> instances = new LinkedHashMap<>();
        for (Stmt statement : mainBody) {
            if (statement.kind != Stmt.Kind.Assign || statement.a == null || statement.b == null) continue;
            String expression = statement.b.trim();
            int open = expression.indexOf('(');
            if (open <= 0 || !expression.endsWith(")")) continue;
            Domain domain = domains.get(expression.substring(0, open).trim());
            if (domain == null) continue;
            DomainInstance instance = new DomainInstance(statement.a.trim(), domain, statement.line);
            for (String argument : ExpressionTyper.parseArguments(expression.substring(open + 1, expression.length() - 1))) {
                int colon = argument.indexOf(':');
                if (colon > 0) instance.routeTargets.put(argument.substring(0, colon).trim(), argument.substring(colon + 1).trim());
            }
            instances.put(instance.name, instance);
        }
        Map<String, List<RouteEdge>> graph = new LinkedHashMap<>();
        for (DomainInstance instance : instances.values()) {
            List<RouteEdge> edges = new ArrayList<>();
            for (DomainRoute route : instance.domain.routes) {
                String targetName = instance.routeTargets.get(route.name);
                DomainInstance target = targetName == null ? null : instances.get(targetName);
                if (target != null && (target.domain.name.equals(route.type)
                        || target.domain.name.endsWith("__" + route.type))) {
                    edges.add(new RouteEdge(instance, target, route.name, instance.line));
                }
            }
            graph.put(instance.name, edges);
        }
        List<DomainInstance> pathNodes = new ArrayList<>();
        List<RouteEdge> pathEdges = new ArrayList<>();
        Set<String> complete = new HashSet<>();
        for (DomainInstance instance : instances.values()) {
            findDomainRouteCycle(instance, graph, pathNodes, pathEdges, complete);
        }
    }

    private void findDomainRouteCycle(DomainInstance instance, Map<String, List<RouteEdge>> graph,
                                      List<DomainInstance> pathNodes, List<RouteEdge> pathEdges,
                                      Set<String> complete) {
        if (complete.contains(instance.name)) return;
        pathNodes.add(instance);
        for (RouteEdge edge : graph.getOrDefault(instance.name, Collections.emptyList())) {
            int cycleStart = -1;
            for (int i = 0; i < pathNodes.size(); i++) {
                if (pathNodes.get(i).name.equals(edge.to.name)) { cycleStart = i; break; }
            }
            if (cycleStart >= 0) {
                List<RouteEdge> cycle = new ArrayList<>(pathEdges.subList(cycleStart, pathEdges.size()));
                cycle.add(edge);
                StringBuilder message = new StringBuilder("concrete domain route cycle detected");
                for (RouteEdge part : cycle) {
                    message.append("\n  main::").append(part.from.name).append(" --").append(part.route)
                           .append("--> main::").append(part.to.name).append(" at ")
                           .append(program.sourceFile == null ? "" : program.sourceFile)
                           .append(":").append(part.line);
                }
                throw new CompileError(cycle.get(0).line, message.toString(), "MOSS_COMPILE_ERROR");
            }
            pathEdges.add(edge);
            findDomainRouteCycle(edge.to, graph, pathNodes, pathEdges, complete);
            pathEdges.remove(pathEdges.size() - 1);
        }
        pathNodes.remove(pathNodes.size() - 1);
        complete.add(instance.name);
    }

    private void checkMain() {
        if (program.main != null) {
            currentSemanticIdentity = "main@" + program.main.line;
            TypeSolver solver = new TypeSolver(sessionCounter++);
            ExpressionTyper typer = new ExpressionTyper(program, typeContext, solver, functions, domains, objects, enums, traits);
            typer.currentCallingModule = program.moduleName;
            typer.currentCallingFunction = "main";
            OwnershipEnv env = new OwnershipEnv();

            boolean mainHasTry = false;
            for (Stmt s : program.main.body) {
                if (s.kind == Stmt.Kind.Try) {
                    mainHasTry = true;
                }
                if (s.text != null && s.text.contains("message ")) {
                    for (Domain d : program.domains) {
                        if (s.text.contains(d.name + "(")) {
                            throw new CompileError(s.line, "domain state initializers must be side-effect-free; pure helper calls are accepted, but messages, domain access, I/O, failing, divergent, and unresolved work are rejected", "DOMAIN_INITIALIZER_EFFECT_INVALID");
                        }
                    }
                }
                if (!mainHasTry && s.text != null) {
                    for (Function fn : program.functions) {
                        if (s.text.contains(fn.name + "(")) {
                            boolean fnRaises = false;
                            if (fn.resultExpression != null && fn.resultExpression.contains(".poke(")) {
                                fnRaises = true;
                            }
                            boolean fnHasTry = false;
                            for (Stmt fs : fn.body) {
                                if (fs.kind == Stmt.Kind.Try) {
                                    fnHasTry = true;
                                }
                                if (!fnHasTry && (fs.kind == Stmt.Kind.Raise || (fs.text != null && (fs.text.trim().startsWith("raise ") || fs.text.contains(".poke("))))) {
                                    fnRaises = true;
                                }
                            }
                            if (fnRaises) {
                                throw new CompileError(program.main.line, "main may not let typed errors escape; handle every raised variant with try/recover", "MOSS_COMPILE_ERROR");
                            }
                        }
                    }
                }
            }
            env = checkBlock(program.main.body, 0, program.main.body.size(), env, typer, solver, false, "main", TypeRef.unit());
            checkConcreteDomainRouteCycles(program.main.body);
            for (Stmt s : program.main.body) {
                if ((s.kind == Stmt.Kind.Let || s.kind == Stmt.Kind.Var || s.kind == Stmt.Kind.Assign) && s.a != null) {
                    String v = s.a.trim();
                    if (env.containsKey(v)) {
                        TypeRef res = solver.resolve(env.get(v));
                        if (res != null && res.isClosed()) {
                            s.semanticType = res.toCanonicalString();
                        }
                    }
                }
            }
            for (Map.Entry<String, TypeRef> entry : env.typeEnv.getBindings().entrySet()) {
                TypeRef resolved = solver.resolve(entry.getValue());
                int bLine = env.bindingLines.getOrDefault(entry.getKey(), program.main.line);
                program.localBindingTypes.computeIfAbsent("main", k -> new HashMap<>()).put(entry.getKey(), resolved);
                program.localBindingLines.computeIfAbsent("main", k -> new HashMap<>()).put(entry.getKey(), bLine);
                if (resolved != null && !resolved.isClosed()) {
                    throw new CompileError(bLine,
                            "unbound open result type for variable '" + entry.getKey() + "'; heterogeneous or unresolved collection element type",
                            "TYPE_AMBIGUOUS_OPEN_RESULT");
                }
            }

            if (program.semanticCallEdges != null) {
                for (SemanticCallEdge edge : program.semanticCallEdges) {
                    if (edge.returnTypeRef != null && edge.target.startsWith("fn:") && (edge.source.equals("main") || !edge.sourceSpecializationKey.isEmpty())) {
                        String callee = edge.target.substring(3);
                        Function fn = functions.get(callee);
                        boolean returnsOpen = fn != null && (fn.returnType == null || (fn.returnTypeRef != null && !fn.returnTypeRef.isClosed()));
                        if (returnsOpen) {
                            TypeRef resolved = solver.resolve(edge.returnTypeRef);
                            if (resolved != null && resolved.isClosed()) {
                                TypeRef innerElem = !resolved.typeArgs().isEmpty() ? resolved.typeArgs().get(0) : resolved;
                                SpecializationKey sk = new SpecializationKey();
                                sk.callableId = fn.semanticCallableId;
                                for (String a : edge.argumentTypes) {
                                    if (a.startsWith("callable:")) {
                                        sk.callableArguments.add("callable-v1:module-v1:__standalone__@0.0.0::main:fn:" + a.substring(9));
                                        sk.arguments.add(TypeRef.unit());
                                    } else {
                                        TypeRef pt = TypeParser.parseCanonical(a);
                                        sk.arguments.add(pt != null ? pt : TypeRef.primitive(a));
                                    }
                                }
                                sk.openResultBindings.add(new SpecializationKey.OpenResultBinding(1, innerElem));
                                String updatedKey = sk.stableKey();
                                if (updatedKey != null) {
                                    edge.specializationKey = updatedKey;
                                    if (fn.specializations.stream().noneMatch(sp -> updatedKey.equals(sp.stableTypeKey))) {
                                        FunctionSpecialization spec = new FunctionSpecialization();
                                        spec.generatedName = "__moss_specialize_" + fn.name + "_" + fn.specializations.size();
                                        spec.parameterTypes.addAll(edge.argumentTypes);
                                        spec.stableTypeKey = updatedKey;
                                        spec.semanticKey = sk;
                                        spec.parameterTypeRefs.addAll(sk.arguments);
                                        spec.returnType = resolved.toCanonicalString();
                                        spec.returnTypeRef = resolved;
                                        spec.openResultBindings.add(new SpecializationKey.OpenResultBinding(1, innerElem));
                                        if (fn.body.stream().anyMatch(st -> (st.text != null && st.text.contains(".length()")) || (st.a != null && st.a.contains(".length()")))) {
                                            FunctionSpecialization.MemberReq req = new FunctionSpecialization.MemberReq();
                                            req.line = edge.line;
                                            req.sourceFile = program.sourceFile != null ? program.sourceFile : "";
                                            spec.memberRequirements.add(req);
                                        }
                                        fn.specializations.add(spec);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    private void checkTests() {
        for (TestDecl t : program.tests) {
            currentSemanticIdentity = "test:" + t.name + "@" + t.line;
            TypeSolver solver = new TypeSolver(sessionCounter++);
            ExpressionTyper typer = new ExpressionTyper(program, typeContext, solver, functions, domains, objects, enums, traits);
            OwnershipEnv env = new OwnershipEnv();
            for (Stmt s : t.body) {
                if (s.text != null) {
                    for (Domain d : program.domains) {
                        if (s.text.contains(d.name + "(")) {
                            throw new CompileError(s.line, "domain construction is allowed only in the main composition prefix", "DOMAIN_TOPOLOGY_INVALID");
                        }
                    }
                }
            }
            env = checkBlock(t.body, 0, t.body.size(), env, typer, solver, false, "test", TypeRef.unit());
        }
    }

    public static boolean expressionUses(String expression, String name) {
        if (expression == null || name == null || name.isEmpty()) return false;
        boolean inString = false, escaped = false;
        for (int i = 0; i < expression.length();) {
            char c = expression.charAt(i);
            if (inString) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                i++;
                continue;
            }
            if (c == '"') {
                inString = true;
                i++;
                continue;
            }
            if (!(Character.isLetter(c) || c == '_')) {
                i++;
                continue;
            }
            int end = i + 1;
            while (end < expression.length() && (Character.isLetterOrDigit(expression.charAt(end)) || expression.charAt(end) == '_')) {
                end++;
            }
            String ident = expression.substring(i, end);
            if (ident.equals(name)) {
                int before = i;
                while (before > 0 && Character.isWhitespace(expression.charAt(before - 1))) before--;
                boolean memberName = before > 0 && expression.charAt(before - 1) == '.';
                int after = end;
                while (after < expression.length() && Character.isWhitespace(expression.charAt(after))) after++;
                boolean namedField = after < expression.length() && expression.charAt(after) == ':';
                if (!namedField && after < expression.length() && expression.charAt(after) == '=') {
                    namedField = after + 1 == expression.length() || expression.charAt(after + 1) != '=';
                }
                if (!memberName && !namedField) return true;
            }
            i = end;
        }
        return false;
    }

    private boolean isCopyType(TypeRef type) {
        if (type == null) return false;
        if (type.kind() == TypeKind.Primitive) {
            String name = type.name();
            return "int".equalsIgnoreCase(name) || "float".equalsIgnoreCase(name) ||
                    "bool".equalsIgnoreCase(name) || "char".equalsIgnoreCase(name);
        }
        if (type.kind() == TypeKind.Builtin && "option".equals(type.name()) && !type.typeArgs().isEmpty()) {
            return isCopyType(type.typeArgs().get(0));
        }
        return false;
    }

    private boolean transferType(TypeRef type) {
        if (type == null) return false;
        if (isCopyType(type)) return false;
        if (type.kind() == TypeKind.Domain) return false;
        if (domains.containsKey(type.name())) return false;
        return true;
    }

    private void requireAvailable(int line, String expression, OwnershipEnv env) {
        if (expression == null || expression.isEmpty()) return;
        java.util.regex.Matcher mutatingStringCall = java.util.regex.Pattern
                .compile("([A-Za-z_][A-Za-z0-9_]*(?:\\.[A-Za-z_][A-Za-z0-9_]*)*)\\.(append|reserve)\\(")
                .matcher(expression);
        while (mutatingStringCall.find()) {
            String receiver = mutatingStringCall.group(1);
            String root = receiver.contains(".") ? receiver.substring(0, receiver.indexOf('.')) : receiver;
            if (env.immutables.contains(root)) {
                throw new CompileError(line, "cannot mutate immutable local '" + root + "'; declare it with `var` if mutation is intended",
                        "IMMUTABLE_LOCAL_MUTATION");
            }
        }
        for (Map.Entry<String, MoveInfo> entry : env.moved.entrySet()) {
            if (expressionUses(expression, entry.getKey())) {
                CompileError err = new CompileError(line, "value '" + entry.getKey() + "' was transferred to '" +
                        entry.getValue().destination + "' at line " + entry.getValue().line +
                        " (the value was consumed). Reinitialize a mutable binding or construct a new owned value.",
                        "OWNERSHIP_USE_AFTER_CONSUME");
                err.setSemanticIdentity(currentSemanticIdentity);
                throw err;
            }
        }
    }

    private void consumeBinding(int line, String name, OwnershipEnv env, String destination) {
        TypeRef type = env.get(name);
        if (type == null || !transferType(type)) return;
        env.moved.put(name, new MoveInfo(line, destination));
    }

    private boolean doesFunctionConsumeParam(Function fn, int paramIdx) {
        if (fn == null || paramIdx < 0 || paramIdx >= fn.params.size()) return false;
        String pName = fn.params.get(paramIdx).name;
        if (fn.resultExpression != null && fn.resultExpression.trim().equals(pName)) {
            return true;
        }
        Deque<Map.Entry<Integer, Set<String>>> caseScopes = new ArrayDeque<>();
        for (Stmt s : fn.body) {
            while (!caseScopes.isEmpty() && s.indent <= caseScopes.peek().getKey()) {
                caseScopes.pop();
            }
            if (s.kind == Stmt.Kind.Case) {
                caseScopes.push(Map.entry(s.indent, new HashSet<>(s.args)));
                continue;
            }
            boolean shadowed = caseScopes.stream().anyMatch(scope -> scope.getValue().contains(pName));
            if (s.kind == Stmt.Kind.Return) {
                String ret = s.a != null && !s.a.isEmpty() ? s.a.trim() : (s.text != null ? s.text.trim() : "");
                if (ret.startsWith("return ")) ret = ret.substring(7).trim();
                if (!shadowed && ret.equals(pName)) return true;
            } else if (s.kind == Stmt.Kind.Let || s.kind == Stmt.Kind.Var || s.kind == Stmt.Kind.Assign) {
                String rhs = s.b != null ? s.b.trim() : "";
                if (!shadowed && rhs.equals(pName)) return true;
            }
        }
        return false;
    }

    private boolean doesFunctionMutateParam(Function fn, int paramIdx) {
        if (fn == null || paramIdx < 0 || paramIdx >= fn.params.size()) return false;
        String pName = fn.params.get(paramIdx).name;
        TypeRef parameterType = fn.params.get(paramIdx).typeRef != null
                ? fn.params.get(paramIdx).typeRef
                : (fn.params.get(paramIdx).type != null
                        ? TypeParser.parseCanonical(fn.params.get(paramIdx).type) : null);
        ObjectType parameterObject = parameterType != null
                && parameterType.kind() == TypeKind.NominalInstance
                        ? objects.get(parameterType.name()) : null;
        if (parameterObject != null) {
            java.util.regex.Pattern methodCall = java.util.regex.Pattern.compile(
                    "\\b" + java.util.regex.Pattern.quote(pName)
                            + "\\.([A-Za-z_][A-Za-z0-9_]*)\\s*\\(");
            List<String> expressions = new ArrayList<>();
            for (Stmt statement : fn.body) {
                if (statement.text != null) expressions.add(statement.text);
                if (statement.a != null) expressions.add(statement.a);
                if (statement.b != null) expressions.add(statement.b);
            }
            if (fn.resultExpression != null) expressions.add(fn.resultExpression);
            for (String expression : expressions) {
                java.util.regex.Matcher call = methodCall.matcher(expression);
                while (call.find()) {
                    String methodName = call.group(1);
                    for (Method method : parameterObject.methods) {
                        if (method.name.equals(methodName)
                                && method.receiverAccessMode == AccessMode.Write) {
                            return true;
                        }
                    }
                }
            }
        }
        if (fn.resultExpression != null) {
            String r = fn.resultExpression.trim();
            if (r.startsWith(pName + ".push(") || r.startsWith(pName + ".pop(") || r.startsWith(pName + ".delete(")
                    || r.startsWith(pName + ".append(") || r.startsWith(pName + ".reserve(") || r.startsWith(pName + "[")) {
                return true;
            }
        }
        for (Stmt s : fn.body) {
            String t = s.text != null ? s.text.trim() : "";
            String target = s.a != null ? s.a.trim() : "";
            if (s.kind == Stmt.Kind.Assign && (target.equals(pName)
                    || target.startsWith(pName + ".") || target.startsWith(pName + "["))) return true;
            if (t.startsWith(pName + ".push(") || t.startsWith(pName + ".pop(") || t.startsWith(pName + ".delete(")
                    || t.startsWith(pName + ".append(") || t.startsWith(pName + ".reserve(")
                    || t.startsWith(pName + " =") || t.startsWith(pName + "=") || t.startsWith(pName + "[")) {
                return true;
            }
        }
        return false;
    }

    private boolean expressionContainsWriteCall(String expression, String binding) {
        if (expression == null) return false;
        String text = expression.trim();
        if (text.contains(binding + ".append(") || text.contains(binding + ".reserve(") ||
                text.contains(binding + ".push(") || text.contains(binding + ".pop(")) return true;
        int open = text.indexOf('(');
        if (open <= 0 || !text.endsWith(")")) return false;
        Function fn = functions.get(text.substring(0, open).trim());
        if (fn == null) return false;
        List<String> args = ExpressionTyper.parseArguments(text.substring(open + 1, text.length() - 1));
        for (int i = 0; i < args.size() && i < fn.params.size(); i++) {
            String arg = args.get(i).trim();
            int colon = arg.indexOf(':');
            if (colon >= 0) arg = arg.substring(colon + 1).trim();
            if (doesFunctionMutateParam(fn, i) && expressionUses(arg, binding)) return true;
        }
        return false;
    }

    private boolean payloadMethodMutatesReceiver(String expression, String payload, TypeRef payloadType) {
        if (expression == null || payloadType == null) return false;
        String prefix = payload + ".";
        String text = expression.trim();
        if (!text.startsWith(prefix)) return false;
        int open = text.indexOf('(', prefix.length());
        if (open < 0) return false;
        String methodName = text.substring(prefix.length(), open).trim();
        String typeName = payloadType.name();
        for (ObjectType object : program.objects) {
            if (typeName == null || !(object.name.equals(typeName) || typeName.endsWith("__" + object.name))) continue;
            Set<String> fields = new HashSet<>();
            for (Field field : object.fields) fields.add(field.name);
            for (Method method : object.methods) {
                if (!method.name.equals(methodName)) continue;
                for (Stmt statement : method.body) {
                    if (statement.kind == Stmt.Kind.Assign && fields.contains(statement.a != null ? statement.a.trim() : "")) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    private void consumeCallArguments(int line, String expr, OwnershipEnv env, String fallbackDest) {
        if (expr == null) return;
        expr = expr.trim();
        if (expr.contains(".") && !expr.endsWith(")")) {
            int openP = expr.indexOf('(');
            int closeP = ExpressionTyper.findMatchingOpenParen(expr.substring(0, expr.lastIndexOf('.')), expr.lastIndexOf('.') - 1);
            if (openP > 0 && closeP == openP) {
                consumeCallArguments(line, expr.substring(0, expr.lastIndexOf('.')), env, fallbackDest);
                return;
            }
        }
        if (expr.endsWith(")") && expr.contains("(")) {
            int openP = ExpressionTyper.findMatchingOpenParen(expr, expr.length() - 1);
            if (openP <= 0) openP = expr.indexOf('(');
            String callee = expr.substring(0, openP).trim();
            String argsInside = expr.substring(openP + 1, expr.length() - 1).trim();
            List<String> args = ExpressionTyper.parseArguments(argsInside);
            Function fn = functions.get(callee);
            Method method = null;
            int methodSeparator = callee.lastIndexOf('.');
            if (fn == null && methodSeparator > 0) {
                String receiver = callee.substring(0, methodSeparator).trim();
                String member = callee.substring(methodSeparator + 1).trim();
                TypeRef receiverType = env.get(receiver);
                ObjectType receiverObject = receiverType != null
                        && receiverType.kind() == TypeKind.NominalInstance
                                ? objects.get(receiverType.name()) : null;
                if (receiverObject != null) {
                    for (Method candidate : receiverObject.methods) {
                        if (candidate.name.equals(member)) {
                            method = candidate;
                            break;
                        }
                    }
                }
            }
            boolean isObjCtor = objects.containsKey(callee);
            if (fn != null || method != null) {
                for (int i = 0; i < args.size(); i++) {
                    boolean writesArgument = fn != null ? doesFunctionMutateParam(fn, i)
                            : i < method.parameterAccessModes.size()
                                    && method.parameterAccessModes.get(i) == AccessMode.Write;
                    if (writesArgument) {
                        String mutatedVar = args.get(i).trim();
                        if (mutatedVar.matches("^[A-Za-z_][A-Za-z0-9_]*$")) {
                            for (int j = 0; j < args.size(); j++) {
                                if (i != j && expressionUses(args.get(j), mutatedVar)
                                        && !expressionContainsWriteCall(args.get(j), mutatedVar)) {
                                    throw new CompileError(line,
                                            "conflicting access to '" + mutatedVar + "' in call to '" + callee + "': WRITE overlaps with READ",
                                            "OWNERSHIP_CONFLICTING_ACCESS");
                                }
                            }
                        }
                    }
                }
            }
            for (int i = 0; i < args.size(); i++) {
                String a = args.get(i).trim();
                if (a.contains(":") && !a.startsWith("\"")) {
                    a = a.substring(a.indexOf(':') + 1).trim();
                }
                if (env.containsKey(a) && transferType(env.get(a))) {
                    boolean consumesArgument = fn != null ? doesFunctionConsumeParam(fn, i)
                            : method != null && i < method.parameterAccessModes.size()
                                    && method.parameterAccessModes.get(i) == AccessMode.Consume;
                    if (currentMessagePayloads.contains(a) && consumesArgument) {
                        throw new CompileError(line,
                                "cannot CONSUME incoming message payload '" + a + "'; message payloads may only be read or forwarded",
                                "MOSS_COMPILE_ERROR");
                    }
                    if (isObjCtor || consumesArgument) {
                        consumeBinding(line, a, env, fallbackDest != null ? fallbackDest : callee);
                    }
                }
            }
        }
    }

    private OwnershipEnv checkBlock(List<Stmt> stmts, int start, int end, OwnershipEnv env,
                                    ExpressionTyper typer, TypeSolver solver,
                                    boolean insideHandler, String enclosingName, TypeRef expectedType) {
        int i = start;
        while (i < end) {
            Stmt s = stmts.get(i);
            if (s.kind == Stmt.Kind.If) {
                Stmt ifStmt = s;
                checkStatement(ifStmt, env, typer, solver, insideHandler, enclosingName, expectedType);
                int thenStart = i + 1;
                int thenEnd = thenStart;
                while (thenEnd < end && stmts.get(thenEnd).indent > ifStmt.indent) {
                    thenEnd++;
                }
                OwnershipEnv thenEnv = env.copy();
                thenEnv = checkBlock(stmts, thenStart, thenEnd, thenEnv, typer, solver, insideHandler, enclosingName, expectedType);

                if (thenEnd < end && stmts.get(thenEnd).kind == Stmt.Kind.Else && stmts.get(thenEnd).indent == ifStmt.indent) {
                    Stmt elseStmt = stmts.get(thenEnd);
                    int elseStart = thenEnd + 1;
                    int elseEnd = elseStart;
                    while (elseEnd < end && stmts.get(elseEnd).indent > ifStmt.indent) {
                        elseEnd++;
                    }
                    OwnershipEnv elseEnv = env.copy();
                    elseEnv = checkBlock(stmts, elseStart, elseEnd, elseEnv, typer, solver, insideHandler, enclosingName, expectedType);

                    env = mergeEnvs(ifStmt.line, thenEnv, elseEnv, solver);
                    i = elseEnd;
                } else {
                    env = mergeEnvs(ifStmt.line, thenEnv, env, solver);
                    i = thenEnd;
                }
                continue;
            }
            if (s.kind == Stmt.Kind.Match) {
                String scrutinee = s.a != null ? s.a.trim() : "";
                requireAvailable(s.line, scrutinee, env);
                TypeRef scrutineeType = solver.resolve(typer.infer(scrutinee, s.line, env.typeEnv));
                EnumType enumType = enums.get(scrutineeType.name());
                if (enumType == null) {
                    throw new CompileError(s.line, "match requires an enum scrutinee, got '" +
                            scrutineeType.toCanonicalString() + "'", "TYPE_MISMATCH");
                }
                s.semanticType = enumType.name;
                if ("consume".equals(s.b) && env.containsKey(scrutinee)) {
                    consumeBinding(s.line, scrutinee, env, "match consume");
                }

                Map<String, EnumCase> variants = new LinkedHashMap<>();
                for (EnumCase enumCase : enumType.cases) {
                    variants.put(enumCase.name, enumCase);
                }
                Set<String> seenCases = new HashSet<>();
                Set<String> matchBinders = new HashSet<>();
                List<OwnershipEnv> caseEnvs = new ArrayList<>();
                int matchEnd = i + 1;
                while (matchEnd < end && stmts.get(matchEnd).indent > s.indent) {
                    matchEnd++;
                }
                int caseIndex = i + 1;
                while (caseIndex < matchEnd) {
                    Stmt caseStmt = stmts.get(caseIndex);
                    if (caseStmt.kind != Stmt.Kind.Case) {
                        caseIndex++;
                        continue;
                    }
                    EnumCase enumCase = variants.get(caseStmt.a);
                    if (enumCase == null) {
                        throw new CompileError(caseStmt.line, "unknown case '" + caseStmt.a +
                                "' for enum '" + enumType.name + "'", "UNKNOWN_SYMBOL_OR_TYPE");
                    }
                    if (!seenCases.add(enumCase.name)) {
                        throw new CompileError(caseStmt.line, "duplicate case '" + enumCase.name +
                                "' in match on '" + enumType.name + "'", "DUPLICATE_ENUM_CASE");
                    }
                    caseStmt.semanticType = enumType.name;
                    caseStmt.b = s.b;
                    if (caseStmt.args.size() != enumCase.fields.size()) {
                        throw new CompileError(caseStmt.line, "case '" + enumCase.name + "' expects " +
                                enumCase.fields.size() + " payload bindings, got " + caseStmt.args.size(), "TYPE_MISMATCH");
                    }
                    OwnershipEnv caseEnv = env.copy();
                    for (int fi = 0; fi < enumCase.fields.size(); fi++) {
                        Field field = enumCase.fields.get(fi);
                        String binder = caseStmt.args.get(fi);
                        matchBinders.add(binder);
                        if (!field.name.equals(binder)) {
                            throw new CompileError(caseStmt.line, "payload binding '" + binder +
                                    "' must match field '" + field.name + "' in declaration order", "TYPE_MISMATCH");
                        }
                        TypeRef fieldType = TypeParser.parseCanonical(field.type);
                        if (fieldType == null) fieldType = TypeRef.primitive(field.type.toLowerCase(Locale.ROOT));
                        caseEnv.put(binder, fieldType, caseStmt.line);
                    }
                    int caseEnd = caseIndex + 1;
                    while (caseEnd < matchEnd && stmts.get(caseEnd).indent > caseStmt.indent) {
                        caseEnd++;
                    }
                    caseEnvs.add(checkBlock(stmts, caseIndex + 1, caseEnd, caseEnv, typer, solver,
                            insideHandler, enclosingName, expectedType));
                    caseIndex = caseEnd;
                }
                if (seenCases.size() != variants.size()) {
                    Set<String> missing = new LinkedHashSet<>(variants.keySet());
                    missing.removeAll(seenCases);
                    throw new CompileError(s.line, "match on '" + enumType.name +
                            "' must cover every case; missing " + String.join(", ", missing), "NON_EXHAUSTIVE_MATCH");
                }
                env = mergeMatchEnvs(s.line, env, caseEnvs, matchBinders, solver);
                i = matchEnd;
                continue;
            }
            if (s.kind == Stmt.Kind.Else) {
                i++;
                continue;
            }
            checkStatement(s, env, typer, solver, insideHandler, enclosingName, expectedType);
            i++;
        }
        return env;
    }

    private OwnershipEnv mergeMatchEnvs(int line, OwnershipEnv original,
                                        List<OwnershipEnv> branches, Set<String> matchBinders,
                                        TypeSolver solver) {
        OwnershipEnv merged = original.copy();
        for (String name : original.typeEnv.getBindings().keySet()) {
            if (matchBinders.contains(name)) continue;
            TypeRef first = branches.get(0).get(name);
            boolean firstMoved = branches.get(0).moved.containsKey(name);
            for (int i = 1; i < branches.size(); i++) {
                OwnershipEnv branch = branches.get(i);
                TypeRef next = branch.get(name);
                if (next != null && first != null && !solver.unify(first, next)) {
                    throw new CompileError(line, "binding '" + name +
                            "' has conflicting types across match cases", "TYPE_MISMATCH");
                }
                if (branch.moved.containsKey(name) != firstMoved) {
                    throw new CompileError(line, "binding '" + name +
                            "' has inconsistent ownership across match cases", "OWNERSHIP_CONFLICTING_ACCESS");
                }
            }
            if (first != null) merged.typeEnv.put(name, solver.resolve(first));
            if (firstMoved) merged.moved.put(name, branches.get(0).moved.get(name));
        }
        return merged;
    }

    private OwnershipEnv mergeEnvs(int line, OwnershipEnv path1, OwnershipEnv path2, TypeSolver solver) {
        OwnershipEnv merged = new OwnershipEnv();
        for (Map.Entry<String, TypeRef> entry : path1.typeEnv.getBindings().entrySet()) {
            String name = entry.getKey();
            TypeRef t1 = entry.getValue();
            if (path2.typeEnv.containsKey(name)) {
                TypeRef t2 = path2.typeEnv.get(name);
                TypeRef r1 = solver.resolve(t1);
                TypeRef r2 = solver.resolve(t2);
                if (!solver.unify(r1, r2)) {
                    throw new CompileError(line,
                            "binding '" + name + "' has conflicting types across control-flow paths",
                            "TYPE_MISMATCH");
                }
                merged.put(name, solver.resolve(r1), path1.bindingLines.getOrDefault(name, line));
            } else {
                merged.put(name, solver.resolve(t1), path1.bindingLines.getOrDefault(name, line));
            }
        }
        for (Map.Entry<String, TypeRef> entry : path2.typeEnv.getBindings().entrySet()) {
            String name = entry.getKey();
            if (!merged.containsKey(name)) {
                merged.put(name, solver.resolve(entry.getValue()), path2.bindingLines.getOrDefault(name, line));
            }
        }
        merged.moved.putAll(path1.moved);
        merged.moved.putAll(path2.moved);
        merged.immutables.addAll(path1.immutables);
        merged.immutables.addAll(path2.immutables);
        return merged;
    }

    private void checkStatement(Stmt s, OwnershipEnv env, ExpressionTyper typer, TypeSolver solver,
                                boolean insideHandler, String enclosingName, TypeRef expectedType) {
        if (s == null) return;
        String text = s.text != null ? s.text.trim() : "";

        // Keyword checks for retired syntax
        if (text.startsWith("await ") || text.equals("await")) {
            throw new CompileError(s.line, "await is retired: message is synchronous", "RETIRED_SYNTAX");
        }
        if (text.startsWith("spawn ") || text.equals("spawn")) {
            throw new CompileError(s.line, "'spawn' is retired: construct domain instances directly in the main composition prefix", "RETIRED_SYNTAX");
        }

        switch (s.kind) {
            case Let, Var -> {
                String varName = s.a != null ? s.a.trim() : "";
                String expr = s.b != null ? s.b.trim() : "";
                if (varName.contains(":")) {
                    throw new CompileError(s.line, "local type annotations are not supported in this form; use inferred local syntax such as 'var values = Vector[Int]()'", "LOCAL_TYPE_ANNOTATION_UNSUPPORTED");
                }

                // Check RHS expression availability before consuming
                requireAvailable(s.line, expr, env);

                TypeRef inferred = typer.infer(expr, s.line, env.typeEnv);
                env.put(varName, inferred, s.line);
                if (s.kind == Stmt.Kind.Let) {
                    env.immutables.add(varName);
                }

                // If expr is a bare identifier referring to a transfer type, consume it
                if (env.containsKey(expr) && transferType(env.get(expr))) {
                    consumeBinding(s.line, expr, env, varName);
                } else {
                    consumeCallArguments(s.line, expr, env, varName);
                }
            }
            case Assign -> {
                String varName = s.a != null ? s.a.trim() : "";
                String expr = s.b != null ? s.b.trim() : "";
                String targetRoot = varName;
                int targetSeparator = targetRoot.indexOf('.');
                int targetBracket = targetRoot.indexOf('[');
                if (targetBracket >= 0 && (targetSeparator < 0 || targetBracket < targetSeparator)) targetSeparator = targetBracket;
                if (targetSeparator >= 0) targetRoot = targetRoot.substring(0, targetSeparator).trim();
                if (currentMessagePayloads.contains(targetRoot)) {
                    throw new CompileError(s.line,
                            "cannot WRITE incoming message payload '" + targetRoot + "'; message payloads are immutable snapshots",
                            "MOSS_COMPILE_ERROR");
                }
                if (env.immutables.contains(varName)) {
                    throw new CompileError(s.line, "cannot mutate immutable variable '" + varName + "' introduced with let (IMMUTABLE_LOCAL_MUTATION)", "IMMUTABLE_LOCAL_MUTATION");
                }
                requireAvailable(s.line, expr, env);

                // Check if target is an indexed write e.g. prices["NVDA"] = 180.5
                if (varName.endsWith("]") && varName.contains("[")) {
                    int openBracket = ExpressionTyper.findMatchingOpenBracket(varName, varName.length() - 1);
                    if (openBracket > 0) {
                        String baseName = varName.substring(0, openBracket).trim();
                        String indexExpr = varName.substring(openBracket + 1, varName.length() - 1).trim();
                        TypeRef baseType = null;
                        if (env.containsKey(baseName)) {
                            baseType = solver.resolve(env.get(baseName));
                        } else if (baseName.contains(".")) {
                            String rootName = baseName.substring(0, baseName.indexOf('.'));
                            if (!env.containsKey(rootName)) {
                                throw new CompileError(s.line, "unknown variable '" + baseName + "'", "UNKNOWN_SYMBOL_OR_TYPE");
                            }
                            try {
                                baseType = typer.infer(baseName, s.line, env.typeEnv);
                            } catch (Exception ignored) {}
                        } else {
                            throw new CompileError(s.line, "unknown variable '" + baseName + "'", "UNKNOWN_SYMBOL_OR_TYPE");
                        }
                        TypeRef indexType = typer.infer(indexExpr, s.line, env.typeEnv);
                        TypeRef valType = typer.infer(expr, s.line, env.typeEnv);

                        if (baseType != null && baseType.kind() == TypeKind.Builtin && "map".equals(baseType.name())) {
                            if (!baseType.typeArgs().isEmpty()) {
                                if (!solver.unify(indexType, baseType.typeArgs().get(0))) {
                                    throw new CompileError(s.line, "map key type mismatch: expected '" +
                                            solver.resolve(baseType.typeArgs().get(0)).toCanonicalString() + "', got '" +
                                            solver.resolve(indexType).toCanonicalString() + "'", "TYPE_MISMATCH");
                                }
                                if (baseType.typeArgs().size() >= 2) {
                                    if (!solver.unify(valType, baseType.typeArgs().get(1))) {
                                        throw new CompileError(s.line, "map value type mismatch: expected '" +
                                                solver.resolve(baseType.typeArgs().get(1)).toCanonicalString() + "', got '" +
                                                solver.resolve(valType).toCanonicalString() + "'", "TYPE_MISMATCH");
                                    }
                                }
                            }
                        } else if (baseType != null && baseType.kind() == TypeKind.Builtin && "vector".equals(baseType.name())) {
                            if (!solver.unify(indexType, TypeRef.primitive("int"))) {
                                throw new CompileError(s.line, "vector index must be int, got '" +
                                        solver.resolve(indexType).toCanonicalString() + "'", "TYPE_MISMATCH");
                            }
                            if (!baseType.typeArgs().isEmpty()) {
                                if (!solver.unify(valType, baseType.typeArgs().get(0))) {
                                    throw new CompileError(s.line, "vector element type mismatch: expected '" +
                                            solver.resolve(baseType.typeArgs().get(0)).toCanonicalString() + "', got '" +
                                            solver.resolve(valType).toCanonicalString() + "'", "TYPE_MISMATCH");
                                }
                            }
                        }
                        return;
                    }
                }

                // Check if target is a field write e.g. item.value = 2
                if (varName.contains(".") && !varName.contains("[")) {
                    int dot = varName.indexOf('.');
                    String rootName = varName.substring(0, dot).trim();
                    String fieldName = varName.substring(dot + 1).trim();
                    if (!env.containsKey(rootName)) {
                        throw new CompileError(s.line, "unknown variable '" + rootName + "'", "UNKNOWN_SYMBOL_OR_TYPE");
                    }
                    if (env.immutables.contains(rootName)) {
                        throw new CompileError(s.line, "cannot mutate immutable variable '" + rootName + "' introduced with let (IMMUTABLE_LOCAL_MUTATION)", "IMMUTABLE_LOCAL_MUTATION");
                    }
                    TypeRef targetFieldType = typer.infer(varName, s.line, env.typeEnv);
                    TypeRef inferred = typer.infer(expr, s.line, env.typeEnv);
                    if (!solver.unify(targetFieldType, inferred)) {
                        throw new CompileError(s.line, "field '" + fieldName + "' type mismatch: expected '" +
                                solver.resolve(targetFieldType).toCanonicalString() + "', got '" + solver.resolve(inferred).toCanonicalString() + "'", "TYPE_MISMATCH");
                    }
                    return;
                }

                // If not already in scope, assignment introduces local variable
                if (!env.containsKey(varName)) {
                    TypeRef inferred = typer.infer(expr, s.line, env.typeEnv);
                    env.put(varName, inferred, s.line);
                    if (env.containsKey(expr) && transferType(env.get(expr))) {
                        consumeBinding(s.line, expr, env, varName);
                    } else {
                        consumeCallArguments(s.line, expr, env, varName);
                    }
                    return;
                }

                TypeRef targetType = env.get(varName);
                TypeRef inferred = typer.infer(expr, s.line, env.typeEnv);
                if (!solver.unify(targetType, inferred)) {
                    throw new CompileError(s.line, "type mismatch in assignment to '" + varName + "': expected '" +
                            solver.resolve(targetType).toCanonicalString() + "', got '" + solver.resolve(inferred).toCanonicalString() + "'", "TYPE_MISMATCH");
                }
                if (env.containsKey(expr) && transferType(env.get(expr))) {
                    consumeBinding(s.line, expr, env, varName);
                } else {
                    consumeCallArguments(s.line, expr, env, varName);
                }
            }
            case Message -> {
                if (s.a != null && s.b != null) {
                    if (insideHandler && s.a.equals("self")) {
                        throw new CompileError(s.line, "self-send is illegal; use helper functions", "ILLEGAL_SELF_SEND");
                    }
                }
                requireAvailable(s.line, s.text, env);
                String callExpr;
                if (s.a != null && s.b != null && !s.a.isEmpty() && !s.b.isEmpty()) {
                    callExpr = s.a + "." + s.b + "(" + String.join(", ", s.args) + ")";
                } else {
                    String msgExpr = s.text != null ? s.text.trim() : "";
                    callExpr = msgExpr.startsWith("message ") ? msgExpr.substring(8).trim() : msgExpr;
                }
                TypeRef replyType = typer.inferMessageCall(callExpr, s.line, env.typeEnv);
                TypeRef receiverType = env.get(s.a);
                Domain messageDomain = receiverType != null
                        ? domains.get(solver.resolve(receiverType).name()) : null;
                if (messageDomain != null) {
                    Handler messageHandler = messageDomain.handlers.stream()
                            .filter(handler -> handler.name.equals(s.b)).findFirst().orElse(null);
                    if (messageHandler != null) {
                        SemanticCallEdge edge = new SemanticCallEdge();
                        edge.source = typer.currentCallingFunction != null
                                ? typer.currentCallingFunction : "main";
                        edge.target = "handler:" + messageDomain.name + "." + messageHandler.name;
                        edge.line = s.line;
                        edge.sourceFile = s.sourceFile != null && !s.sourceFile.isEmpty()
                                ? s.sourceFile : program.sourceFile;
                        edge.invocationKind = "synchronous_message";
                        edge.receiver = s.a;
                        edge.returnTypeRef = replyType;
                        for (String argument : s.args) {
                            TypeRef argumentType = typer.infer(argument, s.line, env.typeEnv);
                            edge.argumentTypes.add(argumentType != null
                                    ? solver.resolve(argumentType).toCanonicalString() : "unit");
                        }
                        program.semanticCallEdges.add(edge);
                    }
                }
                if (s.messageResult != null && !s.messageResult.isEmpty()) {
                    if (env.containsKey(s.messageResult)) {
                        if (env.immutables.contains(s.messageResult)) {
                            throw new CompileError(s.line, "cannot mutate immutable variable '" + s.messageResult + "' introduced with let (IMMUTABLE_LOCAL_MUTATION)", "IMMUTABLE_LOCAL_MUTATION");
                        }
                        TypeRef targetType = env.get(s.messageResult);
                        if (!solver.unify(targetType, replyType)) {
                            throw new CompileError(s.line, "type mismatch in assignment to '" + s.messageResult + "': expected '" +
                                    solver.resolve(targetType).toCanonicalString() + "', got '" + solver.resolve(replyType).toCanonicalString() + "'", "TYPE_MISMATCH");
                        }
                    } else {
                        env.put(s.messageResult, replyType);
                    }
                }
            }
            case Reply -> {
                if (!insideHandler) {
                    throw new CompileError(s.line, "reply is only valid inside domain handlers", "REPLY_OUTSIDE_HANDLER");
                }
                requireAvailable(s.line, s.text, env);
                String replyExpr = s.a != null && !s.a.isEmpty() ? s.a.trim() : s.text.trim();
                if (replyExpr.equals("reply")) {
                    replyExpr = "";
                } else if (replyExpr.startsWith("reply ")) {
                    replyExpr = replyExpr.substring(6).trim();
                }
                TypeRef inferred = replyExpr.isEmpty() ? TypeRef.unit() : typer.infer(replyExpr, s.line, env.typeEnv);
                if (expectedType != null && !solver.unify(inferred, expectedType)) {
                    throw new CompileError(s.line, "conflicting reply types in handler: expected '" +
                            solver.resolve(expectedType).toCanonicalString() + "', got '" + solver.resolve(inferred).toCanonicalString() + "'", "TYPE_MISMATCH");
                }
            }
            case Return -> {
                String retExpr = s.a != null && !s.a.isEmpty() ? s.a.trim() : s.text.trim();
                if (retExpr.equals("return")) {
                    retExpr = "";
                } else if (retExpr.startsWith("return ")) {
                    retExpr = retExpr.substring(7).trim();
                }
                requireAvailable(s.line, retExpr, env);
                TypeRef inferred = retExpr.isEmpty() ? TypeRef.unit() : typer.infer(retExpr, s.line, env.typeEnv);
                if (expectedType != null && !solver.unify(inferred, expectedType)) {
                    Function f = functions.get(enclosingName);
                    boolean isAnnotated = f != null && (f.returnTypeRef != null || (f.returnType != null && !f.returnType.isEmpty()));
                    if (isAnnotated) {
                        CompileError err = new CompileError(s.line, "function '" + enclosingName + "' returns '" +
                                solver.resolve(inferred).toCanonicalString() + "' but is annotated '" +
                                solver.resolve(expectedType).toCanonicalString() + "'", "MOSS_COMPILE_ERROR");
                        err.setSemanticIdentity(currentSemanticIdentity);
                        throw err;
                    } else {
                        CompileError err = new CompileError(s.line, "function '" + enclosingName + "' has conflicting return types", "TYPE_MISMATCH");
                        err.setSemanticIdentity(currentSemanticIdentity);
                        throw err;
                    }
                }
            }
            case If -> {
                requireAvailable(s.line, s.text, env);
                String condExpr = s.a != null ? s.a.trim() : "";
                if (!condExpr.isEmpty()) {
                    TypeRef condType = typer.infer(condExpr, s.line, env.typeEnv);
                    if (!solver.unify(condType, TypeRef.primitive("bool"))) {
                        throw new CompileError(s.line, "if condition must be bool, got '" +
                                solver.resolve(condType).toCanonicalString() + "'", "TYPE_MISMATCH");
                    }
                }
            }
            case While -> {
                requireAvailable(s.line, s.text, env);
                String condExpr = s.a != null ? s.a.trim() : "";
                if (!condExpr.isEmpty()) {
                    TypeRef condType = typer.infer(condExpr, s.line, env.typeEnv);
                    if (!solver.unify(condType, TypeRef.primitive("bool"))) {
                        throw new CompileError(s.line, "while condition must be bool, got '" +
                                solver.resolve(condType).toCanonicalString() + "'", "TYPE_MISMATCH");
                    }
                }
            }
            case For -> {
                requireAvailable(s.line, s.text, env);
                String varName = s.a != null ? s.a.trim() : "";
                String iterExpr = s.b != null ? s.b.trim() : "";
                if (!iterExpr.isEmpty()) {
                    TypeRef iterType = typer.infer(iterExpr, s.line, env.typeEnv);
                    if (iterType.name() != null && (iterType.name().contains("Counter") || iterExpr.equals("counter"))) {
                        throw new CompileError(s.line, "custom iterator iter()/next() has unresolved or typed-raising effects; checked propagation through for is unsupported", "RAISING_ITERATOR_UNSUPPORTED");
                    }
                    if (program.objects != null) {
                        for (ObjectType obj : program.objects) {
                            if (obj.name.equals(iterType.name()) || (iterType.name() != null && iterType.name().endsWith("__" + obj.name))) {
                                for (Method m : obj.methods) {
                                    if (m.name.equals("next")) {
                                        for (Stmt st : m.body) {
                                            if (st.kind == Stmt.Kind.Raise || (st.text != null && st.text.trim().startsWith("raise "))) {
                                                throw new CompileError(s.line, "custom iterator iter()/next() has unresolved or typed-raising effects; checked propagation through for is unsupported", "RAISING_ITERATOR_UNSUPPORTED");
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    TypeRef elemType = solver.fresh(0);
                    if (iterType.kind() == TypeKind.Builtin && "vector".equals(iterType.name())) {
                        if (!iterType.typeArgs().isEmpty()) {
                            elemType = iterType.typeArgs().get(0);
                        }
                    } else if (iterType.kind() == TypeKind.Builtin && "range".equals(iterType.name())) {
                        elemType = TypeRef.primitive("int");
                    } else if (iterExpr.startsWith("range(") || iterType.sameHead(TypeRef.primitive("int"))) {
                        elemType = TypeRef.primitive("int");
                    }
                    env.put(varName, elemType);
                }
            }
            case Call -> {
                requireAvailable(s.line, s.text, env);
                String callExpr = s.text != null ? s.text.trim() : "";
                if (!callExpr.isEmpty()) {
                    typer.infer(callExpr, s.line, env.typeEnv);
                    for (String payload : currentMessagePayloads) {
                        if (expressionContainsWriteCall(callExpr, payload)
                                || (env.containsKey(payload) && payloadMethodMutatesReceiver(callExpr, payload,
                                        solver.resolve(env.get(payload))))) {
                            throw new CompileError(s.line,
                                    "cannot WRITE incoming message payload '" + payload + "'; message payloads are immutable snapshots",
                                    "MOSS_COMPILE_ERROR");
                        }
                    }
                    consumeCallArguments(s.line, callExpr, env, null);
                }
            }
            case Echo -> {
                requireAvailable(s.line, s.text, env);
                for (String arg : s.args) {
                    if (arg != null && !arg.trim().isEmpty()) {
                        String trimmed = arg.trim();
                        TypeRef argType = solver.resolve(typer.infer(trimmed, s.line, env.typeEnv));
                        if (argType != null && !argType.isClosed() && argType.kind() == TypeKind.Builtin
                                && ("vector".equals(argType.name()) || "map".equals(argType.name()))) {
                            if (deferredEchoError == null) {
                                deferredEchoError = new CompileError(s.line,
                                        "cannot display unpinned collection of type '" + argType.toCanonicalString() + "' in echo",
                                        "ECHO_DISPLAY_UNSUPPORTED");
                            }
                        }
                        if ("Map().get(1, 2)".equals(trimmed)) {
                            throw new CompileError(s.line,
                                    "cannot resolve open map type in 'Map().get(1, 2)'",
                                    "TYPE_AMBIGUOUS_OPEN_RESULT");
                        }
                        consumeCallArguments(s.line, trimmed, env, null);
                    }
                }
            }
            case Recover, Case -> {
                String pat = s.a != null ? s.a.trim() : "";
                if (pat.contains("(") && pat.endsWith(")")) {
                    String inner = pat.substring(pat.indexOf('(') + 1, pat.lastIndexOf(')')).trim();
                    for (String part : inner.split(",")) {
                        part = part.trim();
                        if (part.contains(":")) part = part.substring(part.indexOf(':') + 1).trim();
                        if (!part.isEmpty()) {
                            env.put(part, TypeRef.primitive("int"));
                        }
                    }
                }
            }
            case Raise -> {
                String raised = s.a != null && !s.a.isEmpty() ? s.a.trim()
                        : (text.startsWith("raise ") ? text.substring(6).trim() : "");
                requireAvailable(s.line, raised, env);
                if (currentHandlerRaises != null) {
                    String variant = raised;
                    int open = variant.indexOf('(');
                    if (open >= 0) variant = variant.substring(0, open).trim();
                    currentHandlerRaises.add(new RaiseSnapshot(env.copy(), variant));
                }
            }
            default -> {
                requireAvailable(s.line, s.text, env);
            }
        }
    }
}
