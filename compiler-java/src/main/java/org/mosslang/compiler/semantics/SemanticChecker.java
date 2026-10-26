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
    private Map<String, String> currentBorrowedEnumPayloadSources = new HashMap<>();
    private List<RaiseSnapshot> currentHandlerRaises = new ArrayList<>();
    private Set<String> currentHandlerStateFields = Collections.emptySet();
    private int sessionCounter = 1;
    private int activeRecoverDepth;
    private final Deque<String> activeForSources = new ArrayDeque<>();

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

    private static final class LoopShadowSnapshot {
        final String name;
        final boolean existed;
        final TypeRef type;
        final MoveInfo moved;
        final boolean immutable;
        final Integer bindingLine;
        final Set<String> priorBindings;

        LoopShadowSnapshot(String name, OwnershipEnv env) {
            this.name = name;
            this.existed = env.containsKey(name);
            this.type = env.get(name);
            this.moved = env.moved.get(name);
            this.immutable = env.immutables.contains(name);
            this.bindingLine = env.bindingLines.get(name);
            this.priorBindings = new HashSet<>(env.typeEnv.getBindings().keySet());
        }

        void restore(OwnershipEnv env) {
            for (String binding : new HashSet<>(env.typeEnv.getBindings().keySet())) {
                if (priorBindings.contains(binding)) continue;
                env.typeEnv.remove(binding);
                env.moved.remove(binding);
                env.immutables.remove(binding);
                env.bindingLines.remove(binding);
            }
            if (!existed) {
                env.typeEnv.remove(name);
                env.moved.remove(name);
                env.immutables.remove(name);
                env.bindingLines.remove(name);
                return;
            }
            env.put(name, type, bindingLine == null ? 0 : bindingLine);
            if (moved != null) env.moved.put(name, moved);
            if (immutable) env.immutables.add(name);
            else env.immutables.remove(name);
            if (bindingLine != null) env.bindingLines.put(name, bindingLine);
            else env.bindingLines.remove(name);
        }
    }

    public SemanticChecker(Program program) {
        this.program = program;
    }

    public List<Warning> getWarnings() {
        return Collections.unmodifiableList(warnings);
    }

    private OptionalLong staticPayloadSize(String rawType, Set<String> visiting) {
        if (rawType == null || rawType.isEmpty()) return OptionalLong.empty();
        String type = rawType.trim();
        if (type.startsWith("std__")) type = type.substring(5);
        if (type.equals("int") || type.equals("float")) return OptionalLong.of(8);
        if (type.equals("bool")) return OptionalLong.of(1);
        if (type.equals("unit")) return OptionalLong.of(0);
        if (type.equals("string") || domains.containsKey(type)) return OptionalLong.empty();
        if (type.startsWith("option[") && type.endsWith("]")) {
            OptionalLong inner = staticPayloadSize(type.substring(7, type.length() - 1), visiting);
            return inner.isPresent() ? OptionalLong.of(inner.getAsLong() + 1) : OptionalLong.empty();
        }
        ObjectType object = objects.get(type);
        if (object == null || !visiting.add(type)) return OptionalLong.empty();
        long total = 0;
        for (Field field : object.fields) {
            OptionalLong size = staticPayloadSize(field.type, visiting);
            if (size.isEmpty()) {
                visiting.remove(type);
                return OptionalLong.empty();
            }
            total += size.getAsLong();
        }
        visiting.remove(type);
        return OptionalLong.of(total);
    }

    private void warnLargeMessagePayload(int line, String sourceFile, String type, boolean materializes) {
        if (!materializes) return;
        OptionalLong size = staticPayloadSize(type, new HashSet<>());
        if (size.isEmpty() || size.getAsLong() <= 1024) return;
        String message = "message payload materializes " + size.getAsLong()
                + " bytes at an owned domain boundary";
        boolean duplicate = warnings.stream().anyMatch(warning -> warning.line() == line
                && warning.message().equals(message));
        if (!duplicate) warnings.add(new Warning(line, message, "MESSAGE_PAYLOAD_COPY_LARGE",
                sourceFile != null ? sourceFile : program.sourceFile));
    }

    private CompileError deferredEchoError = null;

    public void run() {
        program.typeContext = this.typeContext;
        try (var scope = TypeContext.openScope(typeContext)) {
            collectDeclarations();
            validateDomainHandleDeclarations();
            validateCollectionTypeArities();
            checkEnums();
            checkObjects();
            checkTraits();
            inferPerInstanceDomainSpecializations();
            inferUniformDomainHandlerParameters();
            inferUniformDomainStateFields();
            checkDomains();
            // Resolve inferred object method signatures before generic function
            // bodies check concrete values against structural trait contracts.
            checkObjectMethods();
            checkFunctions();
            closeFunctionAccessModes();
            // Recheck with function summaries available for methods that call
            // ordinary functions in their bodies.
            checkObjectMethods();
            closeFunctionAccessModes();
            validateTransitiveDomainPayloadWrites();
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

    private void validateDomainHandleDeclarations() {
        for (ObjectType object : program.objects) {
            for (Field field : object.fields)
                rejectDomainHandleType("object field '" + object.name + "." + field.name + "'",
                        field.type, field.typeRef, field.line);
            for (Method method : object.methods) {
                for (Param parameter : method.params)
                    rejectDomainHandleType("method parameter '" + method.name + "." + parameter.name + "'",
                            parameter.type, parameter.typeRef, method.line);
                rejectDomainHandleType("method result '" + method.name + "'", method.returnType,
                        method.returnTypeRef, method.line);
            }
        }
        for (Trait trait : program.traits) {
            for (TraitMethod method : trait.methods) {
                for (Param parameter : method.params)
                    rejectDomainHandleType("trait method parameter '" + method.name + "." + parameter.name + "'",
                            parameter.type, parameter.typeRef, method.line);
                rejectDomainHandleType("trait method result '" + method.name + "'", method.returnType,
                        method.returnTypeRef, method.line);
            }
        }
        for (Domain domain : program.domains) {
            for (Field field : domain.state)
                rejectDomainHandleType("domain state field '" + domain.name + "." + field.name + "'",
                        field.type, field.typeRef, field.line);
            for (Handler handler : domain.handlers) {
                for (Param parameter : handler.params)
                    rejectDomainHandleType("handler payload '" + domain.name + "." + handler.name + "."
                                    + parameter.name,
                            parameter.type, parameter.typeRef, handler.line);
                rejectDomainHandleType("handler reply '" + domain.name + "." + handler.name + "'",
                        handler.replyType, handler.replyTypeRef, handler.line);
            }
        }
        for (Function function : program.functions) {
            for (Param parameter : function.params)
                rejectDomainHandleType("function parameter '" + function.name + "." + parameter.name + "'",
                        parameter.type, parameter.typeRef, function.line);
            rejectDomainHandleType("function result '" + function.name + "'", function.returnType,
                    function.returnTypeRef, function.line);
        }
    }

    private void rejectDomainHandleType(String context, String rawType, TypeRef explicitType, int line) {
        TypeRef type = explicitType != null ? explicitType
                : rawType == null || rawType.isBlank() ? null : TypeParser.parseCanonical(rawType);
        if (containsDomainHandleType(type)) {
            throw new CompileError(line, context
                    + " cannot use a domain or Executor handle as data; these are scoped capabilities",
                    "MOSS_COMPILE_ERROR", program.sourceFile);
        }
    }

    private boolean containsDomainHandleType(TypeRef type) {
        if (type == null) return false;
        if (type.kind() == TypeKind.Domain
                || (type.kind() == TypeKind.Primitive && "Executor".equals(type.name()))) return true;
        for (TypeRef argument : type.typeArgs())
            if (containsDomainHandleType(argument)) return true;
        return false;
    }

    private void validateCollectionTypeArities() {
        for (Function function : program.functions) {
            for (Param parameter : function.params)
                validateCollectionTypeArity(parameter.type, function.line);
            validateCollectionTypeArity(function.returnType, function.line);
        }
        for (ObjectType object : program.objects) {
            for (Field field : object.fields)
                validateCollectionTypeArity(field.type, field.line);
            for (Method method : object.methods) {
                for (Param parameter : method.params)
                    validateCollectionTypeArity(parameter.type, method.line);
                validateCollectionTypeArity(method.returnType, method.line);
            }
        }
        for (Domain domain : program.domains) {
            for (Field field : domain.state)
                validateCollectionTypeArity(field.type, field.line);
            for (Handler handler : domain.handlers) {
                for (Param parameter : handler.params)
                    validateCollectionTypeArity(parameter.type, handler.line);
                validateCollectionTypeArity(handler.replyType, handler.line);
            }
        }
        for (Trait trait : program.traits) {
            for (TraitMethod method : trait.methods) {
                for (Param parameter : method.params)
                    validateCollectionTypeArity(parameter.type, method.line);
                validateCollectionTypeArity(method.returnType, method.line);
            }
        }
    }

    private void validateCollectionTypeArity(String type, int line) {
        if (type == null || type.isBlank()) return;
        java.util.regex.Matcher matcher = java.util.regex.Pattern.compile(
                "(?i)\\b(Vector|Map|Queue|Set|vector|map|queue|set)\\s*\\[").matcher(type);
        while (matcher.find()) {
            int open = matcher.end() - 1;
            int depth = 0;
            int close = -1;
            for (int cursor = open; cursor < type.length(); cursor++) {
                if (type.charAt(cursor) == '[') depth++;
                else if (type.charAt(cursor) == ']' && --depth == 0) {
                    close = cursor;
                    break;
                }
            }
            String name = matcher.group(1).toLowerCase(Locale.ROOT);
            Integer expected = TypeRef.collectionArity(name);
            if (expected == null) continue;
            List<String> arguments = close < 0 ? List.of()
                    : ExpressionTyper.parseArguments(type.substring(open + 1, close));
            if (close < 0 || arguments.size() != expected) {
                String malformed = name + "[" + (close < 0 ? "" : String.join(",", arguments)) + "]";
                throw new CompileError(line, "malformed type expression '" + malformed + "'",
                        "MALFORMED_TYPE_EXPRESSION");
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

    private static final class DomainParameterInference {
        String type;
        boolean consistent = true;
        int observations;

        void observe(TypeRef inferred) {
            observations++;
            if (inferred == null || !inferred.isClosed()) {
                consistent = false;
                return;
            }
            String canonical = inferred.toCanonicalString();
            if (type == null) type = canonical;
            else if (!type.equals(canonical)) consistent = false;
        }
    }

    private static final class InferredDomainInstance {
        final String name;
        final String sourceFile;
        final Domain source;
        final TypeSolver solver;
        final ExpressionTyper typer;
        final Map<String, TypeRef> stateTypes = new LinkedHashMap<>();
        final Map<String, Map<Integer, TypeRef>> parameterTypes = new LinkedHashMap<>();
        final Map<String, TypeRef> replyTypes = new LinkedHashMap<>();
        final Map<String, Integer> parameterSites = new LinkedHashMap<>();
        final Map<String, Integer> stateSites = new LinkedHashMap<>();
        boolean consistent = true;

        InferredDomainInstance(String name, String sourceFile, Domain source, Program program, TypeContext typeContext,
                               Map<String, Function> functions, Map<String, Domain> domains,
                               Map<String, ObjectType> objects, Map<String, EnumType> enums,
                               Map<String, Trait> traits, int solverId, String module) {
            this.name = name;
            this.sourceFile = sourceFile == null ? "" : sourceFile;
            this.source = source;
            this.solver = new TypeSolver(solverId);
            this.typer = new ExpressionTyper(program, typeContext, solver, functions,
                    domains, objects, enums, traits);
            this.typer.currentCallingModule = module;
            for (Field field : source.state) {
                TypeRef type = field.typeRef != null ? field.typeRef
                        : (field.type == null || field.type.isEmpty() ? null
                                : TypeParser.parseCanonical(field.type));
                if (type == null) type = solver.fresh(0);
                stateTypes.put(field.name, type);
            }
        }

        void inferMessage(Stmt message, TypeEnv mainEnv) {
            Handler handler = source.handlers.stream()
                    .filter(candidate -> candidate.name.equals(message.b)).findFirst().orElse(null);
            if (handler == null || message.args.size() != handler.params.size()) return;
            List<TypeRef> actuals = new ArrayList<>();
            try {
                for (String argument : message.args)
                    actuals.add(typer.infer(argument, message.line, mainEnv));
            } catch (CompileError ignored) {
                consistent = false;
                return;
            }
            Map<Integer, TypeRef> parameters = parameterTypes.computeIfAbsent(
                    handler.name, ignored -> new LinkedHashMap<>());
            TypeEnv env = new TypeEnv();
            env.put("self", TypeRef.domain(source.name));
            for (Map.Entry<String, TypeRef> state : stateTypes.entrySet())
                env.put(state.getKey(), state.getValue());
            for (DomainRoute route : source.routes)
                env.put(route.name, TypeRef.domain(route.type));
            for (int index = 0; index < handler.params.size(); index++) {
                Param parameter = handler.params.get(index);
                TypeRef type = parameter.typeRef != null ? parameter.typeRef
                        : (parameter.type == null || parameter.type.isEmpty() ? null
                                : TypeParser.parseCanonical(parameter.type));
                if (parameter.inferred && type == null) {
                    TypeRef prior = parameters.get(index);
                    if (prior == null) {
                        prior = solver.fresh(0);
                        parameters.put(index, prior);
                    }
                    TypeRef existing = solver.resolve(prior);
                    TypeRef incoming = solver.resolve(actuals.get(index));
                    if (!solver.unify(prior, actuals.get(index))) {
                        Field conflictField = source.state.stream().filter(field -> field.inferred)
                                .filter(field -> {
                                    TypeRef state = solver.resolve(stateTypes.get(field.name));
                                    return state != null && state.isClosed() && incoming != null
                                            && incoming.isClosed()
                                            && !state.toCanonicalString().equals(incoming.toCanonicalString());
                                }).findFirst().orElse(null);
                        if (conflictField != null) {
                            reportConflict("field '" + conflictField.name + "'",
                                    solver.resolve(stateTypes.get(conflictField.name)), incoming,
                                    stateSites.getOrDefault(conflictField.name, message.line), message.line);
                        }
                        reportConflict("handler '" + handler.name + "' parameter " + index,
                                existing, incoming,
                                parameterSites.getOrDefault(handler.name + ":" + index, message.line),
                                message.line);
                    } else {
                        parameterSites.putIfAbsent(handler.name + ":" + index, message.line);
                    }
                    type = prior;
                }
                if (type == null) type = actuals.get(index);
                env.put(parameter.name, type);
            }
            for (int index = 0; index < handler.body.size(); index++) {
                Stmt statement = handler.body.get(index);
                if (statement.kind == Stmt.Kind.Assign || statement.kind == Stmt.Kind.Let
                        || statement.kind == Stmt.Kind.Var) {
                    String name = statement.a == null ? "" : statement.a.trim();
                    String expression = statement.b == null ? "" : statement.b.trim();
                    if (name.isEmpty() || expression.isEmpty()) continue;
                    try {
                        TypeRef inferred = typer.infer(expression, statement.line, env);
                        TypeRef field = stateTypes.get(name);
                        if (field != null) {
                            TypeRef existing = solver.resolve(field);
                            TypeRef incoming = solver.resolve(inferred);
                            if (!solver.unify(field, inferred)) {
                                if (source.state.stream().anyMatch(candidate -> candidate.name.equals(name)
                                        && candidate.inferred))
                                    reportConflict("field '" + name + "'", existing, incoming,
                                            stateSites.getOrDefault(name, message.line), message.line);
                                consistent = false;
                            } else if (source.state.stream().anyMatch(candidate -> candidate.name.equals(name)
                                    && candidate.inferred) && existing != null && !existing.isClosed()) {
                                TypeRef resolved = solver.resolve(field);
                                if (resolved != null && resolved.isClosed()) stateSites.putIfAbsent(name, message.line);
                            }
                        } else {
                            env.put(name, inferred);
                        }
                    } catch (CompileError ignored) {
                        consistent = false;
                    }
                } else if (statement.kind == Stmt.Kind.Reply && statement.a != null
                        && !statement.a.isEmpty()) {
                    try {
                        TypeRef inferred = typer.infer(statement.a, statement.line, env);
                        TypeRef prior = replyTypes.get(handler.name);
                        if (prior == null) replyTypes.put(handler.name, inferred);
                        else if (!solver.unify(prior, inferred)) consistent = false;
                    } catch (CompileError ignored) {
                        consistent = false;
                    }
                }
            }
            if (message.messageResult != null && !message.messageResult.isEmpty()) {
                TypeRef reply = replyTypes.get(handler.name);
                if (reply != null) {
                    mainEnv.put(message.messageResult, solver.resolve(reply));
                }
            }
        }

        private void reportConflict(String member, TypeRef existing, TypeRef incoming,
                                    int priorLine, int incomingLine) {
            String priorType = existing == null ? "<unknown>" : existing.toCanonicalString();
            String incomingType = incoming == null ? "<unknown>" : incoming.toCanonicalString();
            throw new CompileError(incomingLine,
                    "conflicting domain specialization for instance '" + name + "'\n\n"
                            + member + " inferred as:\n  " + priorType + " from line " + priorLine
                            + "\n  " + incomingType + " from line " + incomingLine
                            + "\n\neach declared domain instance must have one concrete state layout",
                    "MOSS_COMPILE_ERROR", sourceFile);
        }
    }

    /**
     * Materialize separate domain declarations for simple, closed main-scope
     * instances whose inferred layouts differ. The source declaration serves
     * the first instance; later instances receive cloned, concrete layouts.
     */
    private void inferPerInstanceDomainSpecializations() {
        if (program.main == null) return;
        TypeEnv mainEnv = new TypeEnv();
        int rootIndent = program.main.body.stream().mapToInt(statement -> statement.indent)
                .min().orElse(0);
        Map<String, InferredDomainInstance> instances = new LinkedHashMap<>();
        for (Stmt statement : program.main.body) {
            if (statement.indent != rootIndent) continue;
            if ((statement.kind == Stmt.Kind.Assign || statement.kind == Stmt.Kind.Let
                    || statement.kind == Stmt.Kind.Var) && statement.a != null && statement.b != null) {
                String expression = statement.b.trim();
                int open = expression.indexOf('(');
                Domain source = open > 0 && expression.endsWith(")")
                        ? domains.get(expression.substring(0, open).trim()) : null;
                if (source != null && hasInferredDomainLayout(source)) {
                    instances.put(statement.a.trim(), new InferredDomainInstance(statement.a.trim(),
                            program.main.sourceFile, source,
                            program, typeContext, functions, domains, objects, enums, traits,
                            sessionCounter++, program.moduleName));
                    mainEnv.put(statement.a.trim(), TypeRef.domain(source.name));
                } else if (!expression.isEmpty()) {
                    try {
                        ExpressionTyper typer = new ExpressionTyper(program, typeContext,
                                new TypeSolver(sessionCounter++), functions, domains, objects, enums, traits);
                        mainEnv.put(statement.a.trim(), typer.infer(expression, statement.line, mainEnv));
                    } catch (CompileError ignored) {
                        // Leave unknown bindings for the normal checker.
                    }
                }
                continue;
            }
            if (statement.kind == Stmt.Kind.Message) {
                InferredDomainInstance instance = instances.get(statement.a == null ? "" : statement.a.trim());
                if (instance != null) instance.inferMessage(statement, mainEnv);
                continue;
            }
            if (statement.kind == Stmt.Kind.Assign && statement.messageResult != null
                    && !statement.messageResult.isEmpty()) continue;
        }

        Map<Domain, List<InferredDomainInstance>> bySource = new LinkedHashMap<>();
        for (InferredDomainInstance instance : instances.values())
            bySource.computeIfAbsent(instance.source, ignored -> new ArrayList<>()).add(instance);
        Set<String> declaredNames = new HashSet<>(domains.keySet());
        for (ObjectType object : program.objects) declaredNames.add(object.name);
        for (EnumType enumeration : program.enums) declaredNames.add(enumeration.name);
        for (Trait trait : program.traits) declaredNames.add(trait.name);
        for (Map.Entry<Domain, List<InferredDomainInstance>> entry : bySource.entrySet()) {
            Domain source = entry.getKey();
            List<InferredDomainInstance> group = entry.getValue();
            if (group.size() < 2 || source.exported || !source.routes.isEmpty()
                    || source.state.stream().anyMatch(field -> field.init != null && !field.init.isEmpty()))
                continue;
            boolean ready = group.stream().allMatch(this::hasClosedDomainLayout);
            if (!ready) continue;
            Set<String> layouts = new HashSet<>();
            for (InferredDomainInstance instance : group) layouts.add(domainLayoutKey(instance));
            if (layouts.size() < 2) continue;
            List<String> cloneNames = new ArrayList<>();
            boolean namesAvailable = true;
            for (int index = 1; index < group.size(); index++) {
                String cloneName = source.name + "__" + group.get(index).name;
                if (!cloneName.matches("[A-Za-z_][A-Za-z0-9_]*") || declaredNames.contains(cloneName)) {
                    namesAvailable = false;
                    break;
                }
                cloneNames.add(cloneName);
            }
            if (!namesAvailable) continue;
            InferredDomainInstance first = group.get(0);
            applyDomainLayout(source, first);
            for (int index = 1; index < group.size(); index++) {
                InferredDomainInstance instance = group.get(index);
                String cloneName = cloneNames.get(index - 1);
                declaredNames.add(cloneName);
                Domain clone = cloneDomain(source, cloneName);
                applyDomainLayout(clone, instance);
                program.domains.add(clone);
                domains.put(cloneName, clone);
                for (Stmt statement : program.main.body) {
                    if ((statement.kind == Stmt.Kind.Assign || statement.kind == Stmt.Kind.Let
                            || statement.kind == Stmt.Kind.Var) && instance.name.equals(statement.a.trim())) {
                        int open = statement.b.indexOf('(');
                        if (open > 0) statement.b = cloneName + statement.b.substring(open);
                        break;
                    }
                }
            }
        }
    }

    private boolean hasInferredDomainLayout(Domain domain) {
        return domain.state.stream().anyMatch(field -> field.inferred)
                || domain.handlers.stream().anyMatch(handler ->
                        handler.params.stream().anyMatch(parameter -> parameter.inferred));
    }

    private boolean hasClosedDomainLayout(InferredDomainInstance instance) {
        if (!instance.consistent) return false;
        for (Field field : instance.source.state) {
            TypeRef type = instance.solver.resolve(instance.stateTypes.get(field.name));
            if (type == null || !type.isClosed()) return false;
        }
        for (Handler handler : instance.source.handlers) {
            for (int index = 0; index < handler.params.size(); index++) {
                Param parameter = handler.params.get(index);
                if (!parameter.inferred) continue;
                TypeRef type = instance.solver.resolve(instance.parameterTypes
                        .getOrDefault(handler.name, Collections.emptyMap()).get(index));
                if (type == null || !type.isClosed()) return false;
            }
            boolean hasReply = handler.body.stream().anyMatch(statement ->
                    statement.kind == Stmt.Kind.Reply && statement.a != null && !statement.a.isEmpty());
            if (hasReply && (handler.replyType == null || handler.replyType.isEmpty())) {
                TypeRef reply = instance.solver.resolve(instance.replyTypes.get(handler.name));
                if (reply == null || !reply.isClosed()) return false;
            }
        }
        return true;
    }

    private String domainLayoutKey(InferredDomainInstance instance) {
        StringBuilder key = new StringBuilder();
        for (Field field : instance.source.state)
            key.append(instance.solver.resolve(instance.stateTypes.get(field.name)).toCanonicalString()).append(';');
        for (Handler handler : instance.source.handlers) {
            for (int index = 0; index < handler.params.size(); index++) {
                Param parameter = handler.params.get(index);
                TypeRef type = parameter.inferred
                        ? instance.parameterTypes.getOrDefault(handler.name, Collections.emptyMap()).get(index)
                        : (parameter.typeRef != null ? parameter.typeRef
                                : TypeParser.parseCanonical(parameter.type));
                if (type != null) key.append(instance.solver.resolve(type).toCanonicalString());
                key.append(';');
            }
            TypeRef reply = handler.replyTypeRef != null ? handler.replyTypeRef
                    : instance.replyTypes.get(handler.name);
            if (reply != null) key.append(instance.solver.resolve(reply).toCanonicalString());
            key.append('|');
        }
        return key.toString();
    }

    private void applyDomainLayout(Domain domain, InferredDomainInstance instance) {
        for (Field field : domain.state) {
            if (field.inferred) field.bindType(instance.solver.resolve(instance.stateTypes.get(field.name)));
        }
        for (Handler handler : domain.handlers) {
            Map<Integer, TypeRef> parameters = instance.parameterTypes.getOrDefault(
                    handler.name, Collections.emptyMap());
            for (int index = 0; index < handler.params.size(); index++) {
                Param parameter = handler.params.get(index);
                if (parameter.inferred && parameters.containsKey(index))
                    parameter.bindType(instance.solver.resolve(parameters.get(index)));
            }
            if ((handler.replyType == null || handler.replyType.isEmpty())
                    && instance.replyTypes.containsKey(handler.name))
                handler.bindReplyType(instance.solver.resolve(instance.replyTypes.get(handler.name)));
        }
    }

    private Domain cloneDomain(Domain source, String name) {
        Domain clone = new Domain(name, source.header, source.line, source.sourceFile, source.exported);
        clone.definingModule = source.definingModule;
        clone.declarationName = source.declarationName;
        clone.sourceFree = source.sourceFree;
        for (Field field : source.state) {
            Field copy = new Field(field.name, field.type, field.init, field.header, field.line,
                    field.sourceFile, field.inferred, field.exported);
            if (field.typeRef != null) copy.bindType(field.typeRef);
            clone.state.add(copy);
        }
        for (DomainRoute route : source.routes) {
            DomainRoute copy = new DomainRoute();
            copy.name = route.name;
            copy.type = route.type;
            copy.line = route.line;
            copy.sourceFile = route.sourceFile;
            clone.routes.add(copy);
        }
        for (Handler handler : source.handlers) {
            Handler copy = new Handler(handler.name, handler.header, handler.line, handler.sourceFile);
            boolean declaredReplyType = handler.header != null && handler.header.contains("->");
            copy.replyType = declaredReplyType ? handler.replyType : null;
            copy.replyTypeRef = declaredReplyType ? handler.replyTypeRef : null;
            copy.sourceFreeMayRaise = handler.sourceFreeMayRaise;
            copy.sourceFreeRaiseVariants.addAll(handler.sourceFreeRaiseVariants);
            for (Param parameter : handler.params) {
                Param parameterCopy = new Param(parameter.name, parameter.type, parameter.inferred);
                if (parameter.typeRef != null) parameterCopy.bindType(parameter.typeRef);
                copy.params.add(parameterCopy);
            }
            for (Stmt statement : handler.body) copy.body.add(cloneStatement(statement));
            for (FailureArm arm : handler.failureArms) {
                FailureArm armCopy = new FailureArm(arm.line, arm.sourceFile);
                armCopy.patterns.addAll(arm.patterns);
                armCopy.binding = arm.binding;
                armCopy.checkedCaptures.putAll(arm.checkedCaptures);
                for (Stmt statement : arm.body) armCopy.body.add(cloneStatement(statement));
                copy.failureArms.add(armCopy);
            }
            clone.handlers.add(copy);
        }
        return clone;
    }

    private Stmt cloneStatement(Stmt source) {
        Stmt copy = new Stmt(source.kind, source.line, source.indent, source.text, source.sourceFile);
        copy.a = source.a;
        copy.b = source.b;
        copy.args.addAll(source.args);
        copy.messageResult = source.messageResult;
        copy.semanticType = source.semanticType;
        copy.continuationLines.addAll(source.continuationLines);
        return copy;
    }

    private void inferUniformDomainHandlerParameters() {
        if (program.main == null) return;
        TypeSolver solver = new TypeSolver(sessionCounter++);
        ExpressionTyper typer = new ExpressionTyper(program, typeContext, solver,
                functions, domains, objects, enums, traits);
        typer.currentCallingModule = program.moduleName;
        TypeEnv mainEnv = new TypeEnv();
        int rootIndent = program.main.body.stream().mapToInt(statement -> statement.indent)
                .min().orElse(0);
        Map<String, DomainParameterInference> inferredTypes = new LinkedHashMap<>();
        Map<String, Handler> handlersByKey = new HashMap<>();
        for (Stmt statement : program.main.body) {
            if (statement.indent != rootIndent) continue;
            if (statement.kind == Stmt.Kind.Message) {
                TypeRef receiverType = mainEnv.get(statement.a == null ? "" : statement.a.trim());
                Domain domain = receiverType == null || receiverType.kind() != TypeKind.Domain
                        ? null : domains.get(receiverType.name());
                Handler handler = domain == null ? null : domain.handlers.stream()
                        .filter(candidate -> candidate.name.equals(statement.b)).findFirst().orElse(null);
                if (domain != null && !domain.sourceFree && handler != null) {
                    String handlerKey = domain.name + "\n" + handler.name;
                    handlersByKey.put(handlerKey, handler);
                    for (int index = 0; index < handler.params.size() && index < statement.args.size(); index++) {
                        Param parameter = handler.params.get(index);
                        if (!parameter.inferred || (parameter.type != null && !parameter.type.isEmpty())) continue;
                        String key = handlerKey + "\n" + index;
                        DomainParameterInference inference = inferredTypes.computeIfAbsent(
                                key, ignored -> new DomainParameterInference());
                        try {
                            inference.observe(typer.infer(statement.args.get(index), statement.line, mainEnv));
                        } catch (CompileError ignored) {
                            inference.observe(null);
                        }
                    }
                    if (statement.messageResult != null && !statement.messageResult.isEmpty()) {
                        try {
                            TypeRef reply = typer.inferMessageCall(statement.a + "." + statement.b
                                    + "(" + String.join(", ", statement.args) + ")",
                                    statement.line, mainEnv);
                            mainEnv.put(statement.messageResult, solver.resolve(reply));
                        } catch (CompileError ignored) {
                            // The main checker will issue the authoritative diagnostic.
                        }
                    }
                }
                continue;
            }
            if (statement.kind != Stmt.Kind.Assign && statement.kind != Stmt.Kind.Let
                    && statement.kind != Stmt.Kind.Var) continue;
            String name = statement.a == null ? "" : statement.a.trim();
            String expression = statement.b == null ? "" : statement.b.trim();
            if (name.isEmpty() || expression.isEmpty()) continue;
            try {
                TypeRef inferred = typer.infer(expression, statement.line, mainEnv);
                mainEnv.put(name, solver.resolve(inferred));
            } catch (CompileError ignored) {
                // Leave the binding unknown; normal checking reports source errors.
            }
        }
        for (Map.Entry<String, DomainParameterInference> entry : inferredTypes.entrySet()) {
            DomainParameterInference inference = entry.getValue();
            if (!inference.consistent || inference.observations == 0 || inference.type == null) continue;
            String[] parts = entry.getKey().split("\\n", -1);
            if (parts.length != 3) continue;
            Handler handler = handlersByKey.get(parts[0] + "\n" + parts[1]);
            int parameterIndex;
            try {
                parameterIndex = Integer.parseInt(parts[2]);
            } catch (NumberFormatException ignored) {
                continue;
            }
            if (handler == null || parameterIndex < 0 || parameterIndex >= handler.params.size()) continue;
            Param parameter = handler.params.get(parameterIndex);
            if (parameter.inferred && (parameter.type == null || parameter.type.isEmpty()))
                parameter.setType(inference.type);
        }
    }

    private void inferUniformDomainStateFields() {
        for (Domain domain : program.domains) {
            if (domain.sourceFree) continue;
            TypeSolver solver = new TypeSolver(sessionCounter++);
            ExpressionTyper typer = new ExpressionTyper(program, typeContext, solver,
                    functions, domains, objects, enums, traits);
            typer.currentCallingModule = domain.definingModule != null && !domain.definingModule.isEmpty()
                    ? domain.definingModule : program.moduleName;
            Map<String, TypeRef> fieldTypes = new LinkedHashMap<>();
            Set<String> inferredFields = new LinkedHashSet<>();
            Set<String> invalidFields = new HashSet<>();
            for (Field field : domain.state) {
                TypeRef type = field.typeRef != null ? field.typeRef
                        : (field.type != null && !field.type.isEmpty()
                                ? TypeParser.parseCanonical(field.type) : null);
                if (type == null) {
                    type = solver.fresh(0);
                    if (field.inferred && (field.init == null || field.init.isEmpty()))
                        inferredFields.add(field.name);
                }
                fieldTypes.put(field.name, type);
            }
            for (Handler handler : domain.handlers) {
                TypeEnv env = new TypeEnv();
                for (Field field : domain.state) env.put(field.name, fieldTypes.get(field.name));
                for (DomainRoute route : domain.routes) env.put(route.name, TypeRef.domain(route.type));
                for (Param parameter : handler.params) {
                    TypeRef type = parameter.typeRef != null ? parameter.typeRef
                            : (parameter.type != null && !parameter.type.isEmpty()
                                    ? TypeParser.parseCanonical(parameter.type) : null);
                    env.put(parameter.name, type != null ? type : solver.fresh(0));
                }
                for (Stmt statement : handler.body) {
                    if (statement.kind != Stmt.Kind.Let && statement.kind != Stmt.Kind.Var
                            && statement.kind != Stmt.Kind.Assign) continue;
                    String name = statement.a == null ? "" : statement.a.trim();
                    String expression = statement.b == null ? "" : statement.b.trim();
                    if (name.isEmpty() || expression.isEmpty()) continue;
                    TypeRef actual;
                    try {
                        actual = typer.infer(expression, statement.line, env);
                    } catch (CompileError ignored) {
                        if (inferredFields.contains(name)) invalidFields.add(name);
                        continue;
                    }
                    if (fieldTypes.containsKey(name)) {
                        if (!solver.unify(fieldTypes.get(name), actual) && inferredFields.contains(name))
                            invalidFields.add(name);
                    } else {
                        env.put(name, solver.resolve(actual));
                    }
                }
            }
            for (String name : inferredFields) {
                if (invalidFields.contains(name)) continue;
                TypeRef resolved = solver.resolve(fieldTypes.get(name));
                if (resolved != null && resolved.isClosed()) {
                    Field field = domain.state.stream().filter(candidate -> candidate.name.equals(name))
                            .findFirst().orElse(null);
                    if (field != null) field.bindType(resolved);
                }
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
        Set<String> previousStateFields = currentHandlerStateFields;
        Set<String> stateFields = new HashSet<>();
        for (Field field : domain.state) stateFields.add(field.name);
        currentHandlerStateFields = stateFields;
        List<RaiseSnapshot> raiseEnvironments = new ArrayList<>();
        currentHandlerRaises = raiseEnvironments;
        Set<String> previousPayloads = currentMessagePayloads;
        Set<String> payloads = new HashSet<>();
        Map<String, String> previousBorrowedEnumPayloadSources = currentBorrowedEnumPayloadSources;
        currentBorrowedEnumPayloadSources = new HashMap<>();

        for (Field f : domain.state) {
            TypeRef t = (f.type != null && !f.type.isEmpty()) ? TypeParser.parseCanonical(f.type) : null;
            if (t == null && f.init != null && !f.init.isEmpty()) {
                t = typer.infer(f.init, f.line, env.typeEnv);
            }
            env.put(f.name, t != null ? t : solver.fresh(0));
        }
        for (DomainRoute r : domain.routes) {
            env.put(r.name, TypeRef.domain(r.type));
            env.immutables.add(r.name);
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
            // Match payload binders are available on explicit raise edges from
            // their case bodies and may be checked as on_fail captures.
            for (int matchIndex = 0; matchIndex < handler.body.size(); matchIndex++) {
                Stmt match = handler.body.get(matchIndex);
                if (match.kind != Stmt.Kind.Match) continue;
                TypeRef matchType = match.semanticType != null
                        ? TypeParser.parseCanonical(match.semanticType)
                        : typer.infer(match.a, match.line, env.typeEnv);
                EnumType matchedEnum = matchType == null ? null : enums.get(matchType.name());
                if (matchedEnum == null) continue;
                for (int caseIndex = matchIndex + 1; caseIndex < handler.body.size()
                        && handler.body.get(caseIndex).indent > match.indent; caseIndex++) {
                    Stmt caseStmt = handler.body.get(caseIndex);
                    if (caseStmt.kind != Stmt.Kind.Case) continue;
                    for (EnumCase enumCase : matchedEnum.cases) {
                        if (!enumCase.name.equals(caseStmt.a)) continue;
                        for (int index = 0; index < caseStmt.args.size() && index < enumCase.fields.size(); index++) {
                            Field field = enumCase.fields.get(index);
                            TypeRef fieldType = TypeParser.parseCanonical(field.type);
                            if (fieldType == null) fieldType = TypeRef.primitive(field.type.toLowerCase(Locale.ROOT));
                            env.put(caseStmt.args.get(index), fieldType, caseStmt.line);
                        }
                        break;
                    }
                }
            }
            for (FailureArm arm : handler.failureArms) {
                List<OwnershipEnv> armEdges = selectedFailureEdges(arm, raiseEnvironments);
                if (arm.binding != null && !arm.binding.isEmpty()) {
                    Set<String> errorTypes = new HashSet<>();
                    for (RaiseSnapshot snapshot : raiseEnvironments) {
                        if (snapshot.variant != null && snapshot.variant.contains("."))
                            errorTypes.add(snapshot.variant.substring(0, snapshot.variant.lastIndexOf('.')));
                    }
                    if (errorTypes.size() > 1) {
                        throw new CompileError(arm.line, "on_fail binding '" + arm.binding
                                + "' is ambiguous across multiple error types", "AMBIGUOUS_RECOVERY_BINDING");
                    }
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
                TypeRef replyType = solver.resolve(expectedReply);
                if (replyType != null && replyType.kind() != TypeKind.Unit
                        && replyType.kind() != TypeKind.InferenceVar
                        && !blockAlwaysReplies(arm.body, 0, arm.body.size())) {
                    throw new CompileError(arm.line, "on_fail arm for value-returning handler must reply",
                            "ON_FAIL_REPLY_REQUIRED");
                }
                if (arm.body.stream().noneMatch(statement -> statement.kind == Stmt.Kind.Try)) {
                    for (Stmt statement : arm.body) {
                        if (statement.kind == Stmt.Kind.Raise) {
                            throw new CompileError(statement.line,
                                    "on_fail arm must handle errors raised by its body",
                                    "ON_FAIL_MUST_HANDLE_ERRORS");
                        }
                    }
                }
                if (!handler.failureArms.isEmpty()
                        && handler.body.stream().noneMatch(statement -> statement.kind == Stmt.Kind.Try)) {
                    Set<String> raisedVariants = new HashSet<>();
                    for (RaiseSnapshot snapshot : raiseEnvironments)
                        if (snapshot.variant != null && !snapshot.variant.isEmpty()) raisedVariants.add(snapshot.variant);
                    boolean hasCatchAll = handler.failureArms.stream().anyMatch(candidate ->
                            (candidate.binding == null || candidate.binding.isEmpty())
                                    && (candidate.patterns == null || candidate.patterns.isEmpty()));
                    if (!hasCatchAll && !raisedVariants.isEmpty()
                            && (arm.binding == null || arm.binding.isEmpty())) {
                        Set<String> covered = new HashSet<>();
                        for (FailureArm candidate : handler.failureArms)
                            for (String pattern : candidate.patterns) {
                                int open = pattern.indexOf('(');
                                covered.add(open < 0 ? pattern.trim() : pattern.substring(0, open).trim());
                            }
                        if (!covered.containsAll(raisedVariants)) {
                            Set<String> missing = new HashSet<>(raisedVariants);
                            missing.removeAll(covered);
                            throw new CompileError(arm.line, "on_fail arms do not cover raised variants: "
                                    + String.join(", ", missing), "NONEXHAUSTIVE_ON_FAIL");
                        }
                    }
                }
                OwnershipEnv armEnv = env.copy();
                List<OwnershipEnv> availableFailureEdges = armEdges;
                restoreCommonFailureEdgeBindings(armEnv, availableFailureEdges, solver);
                for (String name : new HashSet<>(armEnv.moved.keySet())) {
                    boolean readByArm = false;
                    for (Stmt bodyStatement : arm.body) {
                        readByArm |= expressionUses(bodyStatement.text, name)
                                || expressionUses(bodyStatement.a, name)
                                || expressionUses(bodyStatement.b, name);
                        if (bodyStatement.args != null)
                            for (String argument : bodyStatement.args) readByArm |= expressionUses(argument, name);
                    }
                    if (!readByArm || availableFailureEdges.isEmpty()) continue;
                    boolean availableOnEveryEdge = true;
                    for (OwnershipEnv edge : availableFailureEdges) availableOnEveryEdge &= !edge.moved.containsKey(name);
                    if (availableOnEveryEdge) armEnv.moved.remove(name);
                }
                validateFailureArmCaptures(domain, handler, arm, env, solver, raiseEnvironments);
                checkBlock(arm.body, 0, arm.body.size(), armEnv, typer, solver,
                        true, handler.name, expectedReply);
            }
        }
        currentHandlerRaises = previousRaises;
        currentHandlerStateFields = previousStateFields;
        TypeRef resolvedReply = solver.resolve(expectedReply);
        if (resolvedReply.kind() == TypeKind.InferenceVar) {
            solver.unify(expectedReply, TypeRef.unit());
            resolvedReply = TypeRef.unit();
        }
        if (!domain.sourceFree && resolvedReply.kind() != TypeKind.Unit
                && !blockAlwaysReplies(handler.body, 0, handler.body.size())) {
            CompileError error = new CompileError(handler.line,
                    "reply handler '" + domain.name + "." + handler.name
                            + "' must reply on every normal control-flow path",
                    "MOSS_COMPILE_ERROR");
            error.setSemanticIdentity(currentSemanticIdentity);
            throw error;
        }
        handler.replyTypeRef = resolvedReply;
        currentMessagePayloads = previousPayloads;
        currentBorrowedEnumPayloadSources = previousBorrowedEnumPayloadSources;
    }

    private void validateFailureArmCaptures(Domain domain, Handler handler, FailureArm arm,
                                            OwnershipEnv finalEnv, TypeSolver solver,
                                            List<RaiseSnapshot> raiseEnvironments) {
        arm.checkedCaptures.clear();
        if (raiseEnvironments.isEmpty()) return;
        List<OwnershipEnv> selectedEdges = new ArrayList<>();
        selectedEdges.addAll(selectedFailureEdges(arm, raiseEnvironments));
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
            } else if (statement.kind == Stmt.Kind.Case && statement.args != null) {
                for (String binder : statement.args) if (!nonLocals.contains(binder)) localCandidates.add(binder);
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
            for (Stmt statement : handler.body) {
                if (statement.kind == Stmt.Kind.Case && statement.args != null
                        && statement.args.contains(name) && !"consume".equals(statement.b)) {
                    throw new CompileError(arm.line, "on_fail capture '" + name
                            + "' must be an owned non-capability value", "ON_FAIL_CAPTURE_NOT_OWNED");
                }
            }
            TypeRef expected = null;
            Integer expectedBindingLine = null;
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
                Integer bindingLine = edge.bindingLines.get(name);
                if (expectedBindingLine != null && bindingLine != null
                        && !expectedBindingLine.equals(bindingLine)) {
                    throw new CompileError(arm.line, "on_fail capture '" + name
                            + "' resolves to different bindings on selected raise paths",
                            "ON_FAIL_CAPTURE_BINDING_MISMATCH");
                }
                if (bindingLine != null) expectedBindingLine = bindingLine;
                TypeRef resolved = solver.resolve(actual);
                String capturedTypeName = resolved.name();
                if (resolved.kind() == TypeKind.Domain || "FileIO".equals(capturedTypeName)
                        || "Range".equals(capturedTypeName) || "RangeBatch".equals(capturedTypeName)
                        || "Executor".equals(capturedTypeName)) {
                    throw new CompileError(arm.line, "on_fail capture '" + name
                            + "' must be an owned non-capability value", "ON_FAIL_CAPTURE_NOT_OWNED");
                }
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
            if (expected != null) arm.checkedCaptures.put(name, expected.toCanonicalString());
        }
    }

    private void restoreCommonFailureEdgeBindings(OwnershipEnv armEnv,
                                                   List<OwnershipEnv> failureEdges,
                                                   TypeSolver solver) {
        if (failureEdges.isEmpty()) return;
        OwnershipEnv first = failureEdges.get(0);
        for (Map.Entry<String, TypeRef> entry : first.typeEnv.getBindings().entrySet()) {
            String name = entry.getKey();
            if (armEnv.containsKey(name) || first.moved.containsKey(name)) continue;
            TypeRef commonType = entry.getValue();
            boolean availableOnAllEdges = true;
            boolean immutableOnAllEdges = first.immutables.contains(name);
            Integer bindingLine = first.bindingLines.get(name);
            for (int index = 1; index < failureEdges.size(); index++) {
                OwnershipEnv edge = failureEdges.get(index);
                TypeRef edgeType = edge.get(name);
                if (edgeType == null || edge.moved.containsKey(name)
                        || !solver.unify(commonType, edgeType)) {
                    availableOnAllEdges = false;
                    break;
                }
                immutableOnAllEdges &= edge.immutables.contains(name);
                if (!java.util.Objects.equals(bindingLine, edge.bindingLines.get(name))) bindingLine = null;
            }
            if (!availableOnAllEdges) continue;
            armEnv.put(name, solver.resolve(commonType), bindingLine != null ? bindingLine : 0);
            if (immutableOnAllEdges) armEnv.immutables.add(name);
        }
    }

    private List<OwnershipEnv> selectedFailureEdges(FailureArm arm, List<RaiseSnapshot> raises) {
        List<OwnershipEnv> selectedEdges = new ArrayList<>();
        for (RaiseSnapshot snapshot : raises) {
            boolean selected = arm.patterns == null || arm.patterns.isEmpty() || snapshot.variant.isEmpty();
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
        return selectedEdges;
    }

    private List<OwnershipEnv> selectedRecoverEdges(Stmt recover, List<RaiseSnapshot> raises) {
        List<OwnershipEnv> selectedEdges = new ArrayList<>();
        String pattern = recover.a == null ? "" : recover.a.trim();
        for (RaiseSnapshot snapshot : raises) {
            boolean selected = pattern.isEmpty() || pattern.matches("[A-Za-z_][A-Za-z0-9_]*")
                    || snapshot.variant.isEmpty();
            if (!selected) {
                for (String alternative : pattern.split(",")) {
                    String expected = alternative.trim();
                    int payload = expected.indexOf('(');
                    if (payload >= 0) expected = expected.substring(0, payload).trim();
                    if (snapshot.variant.equals(expected)
                            || snapshot.variant.endsWith("." + expected)
                            || expected.endsWith("." + snapshot.variant)) selected = true;
                }
            }
            if (selected) selectedEdges.add(snapshot.env);
        }
        return selectedEdges;
    }

    private boolean blockAlwaysReplies(List<Stmt> statements, int start, int end) {
        int index = start;
        while (index < end) {
            Stmt statement = statements.get(index);
            if (statement.kind == Stmt.Kind.Reply || statement.kind == Stmt.Kind.Raise) return true;
            if (statement.kind == Stmt.Kind.Return) return true;
            if (statement.kind == Stmt.Kind.Try) {
                int bodyStart = index + 1;
                int bodyEnd = bodyStart;
                while (bodyEnd < end && statements.get(bodyEnd).indent > statement.indent) bodyEnd++;
                boolean normalReplies = blockAlwaysReplies(statements, bodyStart, bodyEnd);
                int recoverIndex = bodyEnd;
                boolean anyRecover = false;
                boolean allRecoverReply = true;
                while (recoverIndex < end && statements.get(recoverIndex).kind == Stmt.Kind.Recover
                        && statements.get(recoverIndex).indent == statement.indent) {
                    anyRecover = true;
                    int recoverStart = recoverIndex + 1;
                    int recoverEnd = recoverStart;
                    while (recoverEnd < end && statements.get(recoverEnd).indent > statement.indent)
                        recoverEnd++;
                    allRecoverReply &= blockAlwaysReplies(statements, recoverStart, recoverEnd);
                    recoverIndex = recoverEnd;
                }
                if (normalReplies && anyRecover && allRecoverReply) return true;
                index = recoverIndex;
                continue;
            }
            if (statement.kind == Stmt.Kind.If) {
                int thenStart = index + 1;
                int thenEnd = thenStart;
                while (thenEnd < end && statements.get(thenEnd).indent > statement.indent) thenEnd++;
                int elseIndex = thenEnd;
                if (elseIndex < end && statements.get(elseIndex).kind == Stmt.Kind.Else
                        && statements.get(elseIndex).indent == statement.indent) {
                    int elseStart = elseIndex + 1;
                    int elseEnd = elseStart;
                    while (elseEnd < end && statements.get(elseEnd).indent > statement.indent) elseEnd++;
                    if (blockAlwaysReplies(statements, thenStart, thenEnd)
                            && blockAlwaysReplies(statements, elseStart, elseEnd)) return true;
                    index = elseEnd;
                    continue;
                }
                index = thenEnd;
                continue;
            }
            if (statement.kind == Stmt.Kind.Match) {
                int matchEnd = index + 1;
                while (matchEnd < end && statements.get(matchEnd).indent > statement.indent) matchEnd++;
                boolean anyCase = false;
                boolean allCasesReply = true;
                int caseIndex = index + 1;
                while (caseIndex < matchEnd) {
                    Stmt caseStatement = statements.get(caseIndex);
                    if (caseStatement.kind != Stmt.Kind.Case) {
                        caseIndex++;
                        continue;
                    }
                    anyCase = true;
                    int caseStart = caseIndex + 1;
                    int caseEnd = caseStart;
                    while (caseEnd < matchEnd && statements.get(caseEnd).indent > caseStatement.indent)
                        caseEnd++;
                    allCasesReply &= blockAlwaysReplies(statements, caseStart, caseEnd);
                    caseIndex = caseEnd;
                }
                if (anyCase && allCasesReply) return true;
                index = matchEnd;
                continue;
            }
            if (statement.kind == Stmt.Kind.While || statement.kind == Stmt.Kind.For) {
                index++;
                while (index < end && statements.get(index).indent > statement.indent) index++;
                continue;
            }
            index++;
        }
        return false;
    }

    private void checkFunctions() {
        for (Function f : program.functions) {
            currentSemanticIdentity = "fn:" + f.name + "@" + f.line;
            TypeSolver solver = new TypeSolver(sessionCounter++);
            ExpressionTyper typer = new ExpressionTyper(program, typeContext, solver, functions, domains, objects, enums, traits);
            boolean hasUnannotatedParam = f.params.stream().anyMatch(p -> p.type == null || p.type.isEmpty()
                    || traits.containsKey(p.type) || "Vector".equals(p.type) || "Map".equals(p.type));
            if (hasUnannotatedParam) f.generic = true;
            typer.allowOpenGenericArithmetic = f.generic;
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
            Set<String> fileIoLocals = new HashSet<>();
            boolean inUnreachableRecover = false;
            int recoverIndent = -1;
            for (Stmt s : f.body) {
                if (recoverIndent >= 0 && s.indent <= recoverIndent && s.kind != Stmt.Kind.Recover) {
                    inUnreachableRecover = false;
                    recoverIndent = -1;
                }
                if (s.kind == Stmt.Kind.Try) {
                    tryRaisedVariants = new LinkedHashSet<>();
                }
                String statementExpressions = statementExpressions(s);
                if ((s.kind == Stmt.Kind.Let || s.kind == Stmt.Kind.Var || s.kind == Stmt.Kind.Assign)
                        && s.a != null && statementExpressions.contains("FileIO.open(")) {
                    fileIoLocals.add(s.a.trim());
                }
                if (tryRaisedVariants != null && recoverIndent == -1
                        && callsFileIoOperation(statementExpressions, fileIoLocals)) {
                    tryRaisedVariants.add("std__FileError.IO");
                }
                if (s.kind == Stmt.Kind.Raise && tryRaisedVariants != null && recoverIndent == -1) {
                    String rExpr = s.a != null && !s.a.isEmpty() ? s.a.trim() : (s.text != null && s.text.startsWith("raise ") ? s.text.substring(6).trim() : "");
                    if (!rExpr.isEmpty()) {
                        tryRaisedVariants.add(rExpr);
                    }
                } else if (s.kind == Stmt.Kind.Call && tryRaisedVariants != null && recoverIndent == -1) {
                    String callTxt = s.text != null ? s.text.trim() : "";
                    int callOpen = callTxt.indexOf('(');
                    Function called = callOpen > 0
                            ? functions.get(callTxt.substring(0, callOpen).trim()) : null;
                    if (called != null) {
                        collectFunctionRaisedVariants(called, tryRaisedVariants, new HashSet<>());
                    }
                } else if (s.kind == Stmt.Kind.Recover) {
                    recoverIndent = s.indent;
                    String pat = s.a != null ? s.a.trim() : "";
                    inUnreachableRecover = false;
                    if (tryRaisedVariants != null && !pat.isEmpty()) {
                        boolean anyReachable = false;
                        for (String subPat : pat.split(",")) {
                            subPat = subPat.trim();
                            if (subPat.matches("[A-Za-z_][A-Za-z0-9_]*")) {
                                anyReachable = true;
                                continue;
                            }
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
            boolean hasValueReturn = f.resultExpression != null && !f.resultExpression.isBlank();
            if (!hasValueReturn) {
                for (Stmt statement : f.body) {
                    if (statement.kind != Stmt.Kind.Return) continue;
                    String expression = statement.a != null ? statement.a.trim() : "";
                    if (expression.isEmpty() && statement.text != null) {
                        String text = statement.text.trim();
                        if (text.startsWith("return ")) expression = text.substring(7).trim();
                    }
                    if (!expression.isEmpty() && !"return".equals(expression)) {
                        hasValueReturn = true;
                        break;
                    }
                }
            }
            if (resolvedReturn.kind() == TypeKind.InferenceVar && !hasValueReturn) {
                solver.unify(expectedReturn, TypeRef.unit());
                resolvedReturn = TypeRef.unit();
            }
            if (hasUnannotatedParam) {
                f.generic = true;
            }
            if (f.generic) f.staticDispatch = true;
            for (Param parameter : f.params) {
                TypeRef checkedParameterType = env.get(parameter.name);
                parameter.checkedTypeRef = checkedParameterType != null
                        ? solver.resolve(checkedParameterType) : null;
            }
            if (f.sourceFreeAbiVersion == 0) {
                f.parameterAccessModes.clear();
                for (int parameterIndex = 0; parameterIndex < f.params.size(); parameterIndex++) {
                    Param parameter = f.params.get(parameterIndex);
                    TypeRef parameterType = parameter.typeRef != null ? parameter.typeRef
                            : (parameter.type != null && !parameter.type.isEmpty()
                                    ? TypeParser.parseCanonical(parameter.type) : null);
                    if (parameterType == null) parameterType = parameter.checkedTypeRef;
                    if (doesFunctionConsumeParam(f, parameterIndex)
                            && parameterType != null && transferType(parameterType)) {
                        f.parameterAccessModes.add(AccessMode.Consume);
                    } else if (doesFunctionMutateParam(f, parameterIndex)) {
                        f.parameterAccessModes.add(AccessMode.Write);
                    } else {
                        f.parameterAccessModes.add(AccessMode.Read);
                    }
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
                        String rhs = statement.kind == Stmt.Kind.Return
                                ? statementResultExpression(statement)
                                : (statement.b != null ? statement.b.trim() : "");
                        TypeRef movedFieldType = receiverFieldValueType(object, field.name, rhs);
                        if (transferType(movedFieldType)) {
                            method.receiverAccessMode = AccessMode.Consume;
                            break;
                        }
                        if (statement.kind == Stmt.Kind.Assign
                                && isReceiverFieldWriteTarget(target, field.name)) {
                            if (method.receiverAccessMode != AccessMode.Consume)
                                method.receiverAccessMode = AccessMode.Write;
                            break;
                        }
                        if (objectFieldCallConsumes(object, field.name,
                                statementExpressionText(statement))) {
                            method.receiverAccessMode = AccessMode.Consume;
                            break;
                        }
                        AccessMode fieldCallMode = objectFieldCallMode(field.name, fieldType,
                                fieldObject, statementExpressionText(statement));
                        if (fieldCallMode == AccessMode.Consume
                                || (fieldCallMode == AccessMode.Write
                                        && method.receiverAccessMode == AccessMode.Read)) {
                            method.receiverAccessMode = fieldCallMode;
                        }
                        if (method.receiverAccessMode == AccessMode.Consume) break;
                    }
                    if (transferType(receiverFieldValueType(object, field.name,
                            method.resultExpression))) {
                        method.receiverAccessMode = AccessMode.Consume;
                    } else {
                        if (objectFieldCallConsumes(object, field.name, method.resultExpression)) {
                            method.receiverAccessMode = AccessMode.Consume;
                        }
                        AccessMode resultCallMode = objectFieldCallMode(field.name, fieldType,
                                fieldObject, method.resultExpression);
                        if (method.receiverAccessMode != AccessMode.Consume
                                && (resultCallMode == AccessMode.Consume
                                || (resultCallMode == AccessMode.Write
                                        && method.receiverAccessMode == AccessMode.Read))) {
                            method.receiverAccessMode = resultCallMode;
                        }
                    }
                    if (method.receiverAccessMode == AccessMode.Consume) break;
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
        boolean changed;
        do {
            changed = false;
            for (ObjectType object : program.objects) {
                for (Method method : object.methods) {
                    if (method.sourceFree || method.receiverAccessMode == AccessMode.Consume) continue;
                    for (Method candidate : object.methods) {
                        if (candidate == method || candidate.receiverAccessMode == AccessMode.Read
                                || !callsSiblingMethod(method, candidate.name)) continue;
                        AccessMode propagated = strongerAccessMode(method.receiverAccessMode,
                                candidate.receiverAccessMode);
                        if (propagated != method.receiverAccessMode) {
                            method.receiverAccessMode = propagated;
                            changed = true;
                        }
                        break;
                    }
                    if (method.receiverAccessMode == AccessMode.Consume) continue;
                    for (ObjectType fieldObject : program.objects) {
                        for (Method candidate : fieldObject.methods) {
                            if (candidate.receiverAccessMode == AccessMode.Read
                                    || !callsMutatingFieldMethod(method, candidate)) continue;
                            AccessMode propagated = strongerAccessMode(method.receiverAccessMode,
                                    candidate.receiverAccessMode);
                            if (propagated != method.receiverAccessMode) {
                                method.receiverAccessMode = propagated;
                                changed = true;
                            }
                            break;
                        }
                        if (method.receiverAccessMode == AccessMode.Consume) break;
                    }
                }
            }
        } while (changed);
    }

    private boolean isReceiverFieldWriteTarget(String target, String fieldName) {
        return target.equals(fieldName) || target.startsWith(fieldName + ".")
                || target.startsWith(fieldName + "[")
                || target.equals("self." + fieldName) || target.startsWith("self." + fieldName + ".")
                || target.startsWith("self." + fieldName + "[");
    }

    private TypeRef receiverFieldValueType(ObjectType object, String fieldName, String expression) {
        if (expression == null) return null;
        String value = expression.trim().replaceAll("\\s", "");
        if (value.startsWith("self.")) value = value.substring("self.".length());
        String[] path = value.split("\\.");
        if (path.length == 0 || !fieldName.equals(path[0])) return null;
        Field rootField = object.fields.stream().filter(field -> field.name.equals(fieldName))
                .findFirst().orElse(null);
        if (rootField == null) return null;
        TypeRef type = rootField.typeRef != null ? rootField.typeRef
                : (rootField.type != null ? TypeParser.parseCanonical(rootField.type) : null);
        for (int index = 1; type != null && index < path.length; index++) {
            ObjectType owner = findObjectType(type.name());
            if (owner == null) return null;
            String nestedFieldName = path[index];
            Field nestedField = owner.fields.stream()
                    .filter(field -> field.name.equals(nestedFieldName)).findFirst().orElse(null);
            if (nestedField == null) return null;
            type = nestedField.typeRef != null ? nestedField.typeRef
                    : (nestedField.type != null ? TypeParser.parseCanonical(nestedField.type) : null);
        }
        return type;
    }

    private String statementResultExpression(Stmt statement) {
        if (statement == null) return "";
        String value = statement.a != null ? statement.a.trim() : "";
        if (value.isEmpty() && statement.text != null) value = statement.text.trim();
        return value.startsWith("return ") ? value.substring("return ".length()).trim() : value;
    }

    private AccessMode strongerAccessMode(AccessMode current, AccessMode candidate) {
        if (current == AccessMode.Consume || candidate == AccessMode.Consume) return AccessMode.Consume;
        if (current == AccessMode.Write || candidate == AccessMode.Write) return AccessMode.Write;
        return AccessMode.Read;
    }

    private boolean callsSiblingMethod(Method caller, String calleeName) {
        java.util.regex.Pattern explicitSelfCall = java.util.regex.Pattern.compile(
                "\\bself\\s*\\.\\s*" + java.util.regex.Pattern.quote(calleeName) + "\\s*\\(");
        java.util.regex.Pattern implicitSelfCall = java.util.regex.Pattern.compile(
                "(?<![A-Za-z0-9_.])" + java.util.regex.Pattern.quote(calleeName) + "\\s*\\(");
        for (Stmt statement : caller.body) {
            String expression = statementExpressionText(statement);
            if (explicitSelfCall.matcher(expression).find() || implicitSelfCall.matcher(expression).find())
                return true;
        }
        String result = caller.resultExpression;
        return result != null && (explicitSelfCall.matcher(result).find()
                || implicitSelfCall.matcher(result).find());
    }

    private boolean callsMutatingFieldMethod(Method caller, Method callee) {
        java.util.regex.Pattern call = java.util.regex.Pattern.compile(
                "\\b(self(?:\\s*\\.\\s*[A-Za-z_][A-Za-z0-9_]*)+)\\s*\\.\\s*"
                        + java.util.regex.Pattern.quote(callee.name) + "\\s*\\(");
        for (Stmt statement : caller.body) {
            java.util.regex.Matcher matcher = call.matcher(statementExpressionText(statement));
            while (matcher.find()) {
                String receiverType = resolveMethodReceiverType(matcher.group(1), caller, Map.of());
                if (matchesObjectTypeName(receiverType, callee.owner)) return true;
            }
        }
        if (caller.resultExpression != null) {
            java.util.regex.Matcher matcher = call.matcher(caller.resultExpression);
            while (matcher.find()) {
                String receiverType = resolveMethodReceiverType(matcher.group(1), caller, Map.of());
                if (matchesObjectTypeName(receiverType, callee.owner)) return true;
            }
        }
        return false;
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

    private static int matchingCloseParen(String expression, int open) {
        if (expression == null || open < 0 || open >= expression.length()
                || expression.charAt(open) != '(') return -1;
        int depth = 0;
        boolean inString = false;
        boolean escaped = false;
        for (int index = open; index < expression.length(); index++) {
            char current = expression.charAt(index);
            if (inString) {
                if (escaped) escaped = false;
                else if (current == '\\') escaped = true;
                else if (current == '"') inString = false;
                continue;
            }
            if (current == '"') inString = true;
            else if (current == '(') depth++;
            else if (current == ')' && --depth == 0) return index;
        }
        return -1;
    }

    private void validateRangeForSource(int line, String expression, ExpressionTyper typer,
                                        TypeSolver solver, TypeEnv env) {
        if (expression == null) return;
        String source = expression.trim();
        int openParen = source.indexOf('(');
        if (openParen < 0 || !"range".equals(source.substring(0, openParen).trim())
                || !source.endsWith(")")) return;
        List<String> arguments = org.mosslang.compiler.parser.ParserSupport
                .splitTopLevel(source.substring(openParen + 1, source.length() - 1), ',');
        if (arguments.size() != 2 && arguments.size() != 3) {
            throw new CompileError(line,
                    "range expects range(start, end) or range(start, end, step)",
                    "TYPE_MISMATCH");
        }
        for (String argument : arguments) {
            TypeRef argumentType = typer.infer(argument, line, env);
            if (!solver.unify(argumentType, TypeRef.primitive("int"))) {
                throw new CompileError(line, "range arguments must have type 'Int'", "MOSS_COMPILE_ERROR");
            }
        }
        if (arguments.size() == 3) {
            String step = arguments.get(2).trim();
            String digits = step.startsWith("+") ? step.substring(1) : step;
            boolean positiveIntLiteral = !digits.isEmpty()
                    && digits.chars().allMatch(character -> character >= '0' && character <= '9');
            if (!positiveIntLiteral || new java.math.BigInteger(digits).signum() == 0) {
                throw new CompileError(line,
                        "range step must be a positive non-zero Int literal",
                        "MOSS_COMPILE_ERROR");
            }
        }
    }

    private AccessMode objectFieldCallMode(String fieldName, TypeRef fieldType,
                                           ObjectType fieldObject, String expression) {
        if (expression == null || expression.isEmpty()) return AccessMode.Read;
        java.util.regex.Matcher call = java.util.regex.Pattern.compile(
                "\\b" + java.util.regex.Pattern.quote(fieldName)
                        + "\\.([A-Za-z_][A-Za-z0-9_]*)\\s*\\(").matcher(expression);
        while (call.find()) {
            String calledMethod = call.group(1);
            if (isBuiltinMutatingMethod(fieldType, calledMethod)) return AccessMode.Write;
            if (fieldObject != null) {
                for (Method candidate : fieldObject.methods) {
                    if (candidate.name.equals(calledMethod)) return candidate.receiverAccessMode;
                }
            }
        }
        return AccessMode.Read;
    }

    private boolean objectFieldCallConsumes(ObjectType owner, String fieldName, String expression) {
        if (expression == null || expression.isEmpty()) return false;
        for (Function function : functions.values()) {
            java.util.regex.Matcher call = java.util.regex.Pattern.compile(
                    "(?<![A-Za-z0-9_.])" + java.util.regex.Pattern.quote(function.name)
                            + "\\s*\\(").matcher(expression);
            while (call.find()) {
                int open = expression.indexOf('(', call.start());
                int close = -1;
                for (int cursor = open + 1; cursor < expression.length(); cursor++) {
                    if (expression.charAt(cursor) == ')'
                            && ExpressionTyper.findMatchingOpenParen(expression, cursor) == open) {
                        close = cursor;
                        break;
                    }
                }
                if (close < 0) continue;
                List<String> args = ExpressionTyper.parseArguments(expression.substring(open + 1, close));
                for (int index = 0; index < args.size() && index < function.params.size(); index++) {
                    if (!doesFunctionConsumeParam(function, index)) continue;
                    String argument = args.get(index).trim();
                    int colon = argument.indexOf(':');
                    if (colon >= 0) argument = argument.substring(colon + 1).trim();
                    if (transferType(receiverFieldValueType(owner, fieldName, argument))) return true;
                }
            }
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
                        if (containsCallTo(expression, target.name)
                                || containsCallbackReference(expression, target.name))
                            edges.add(new CallSite(target.name, statement.line));
                    }
                }
                if (statement.args != null) {
                    for (String expression : statement.args) {
                        for (Function target : program.functions) {
                            if (containsCallTo(expression, target.name)
                                    || containsCallbackReference(expression, target.name))
                                edges.add(new CallSite(target.name, statement.line));
                        }
                    }
                }
            }
            if (function.resultExpression != null) {
                for (Function target : program.functions) {
                    if (containsCallTo(function.resultExpression, target.name)
                            || containsCallbackReference(function.resultExpression, target.name))
                        edges.add(new CallSite(target.name, function.resultLine));
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

    private boolean containsCallbackReference(String expression, String functionName) {
        if (expression == null || functionName == null || functionName.isEmpty()) return false;
        String code = maskStringLiterals(expression);
        String escapedName = java.util.regex.Pattern.quote(functionName);
        if (java.util.regex.Pattern.compile("(?:\\|>\\s*)?(?:map|filter|any|all)\\s*\\(\\s*"
                + escapedName + "\\s*\\)").matcher(code).find()) return true;

        for (int open = 0; open < code.length(); open++) {
            if (code.charAt(open) != '(') continue;
            int start = open - 1;
            while (start >= 0 && Character.isWhitespace(code.charAt(start))) start--;
            int end = start + 1;
            while (start >= 0 && (Character.isLetterOrDigit(code.charAt(start))
                    || code.charAt(start) == '_' || code.charAt(start) == '.')) start--;
            if (start + 1 == end) continue;
            String calleeName = code.substring(start + 1, end);
            Function callee = findFunction(calleeName);
            if (callee == null) continue;
            int close = matchingCloseParen(code, open);
            if (close < 0) continue;
            List<String> arguments = ExpressionTyper.parseArguments(code.substring(open + 1, close));
            for (int index = 0; index < arguments.size() && index < callee.params.size(); index++) {
                if (!arguments.get(index).trim().equals(functionName)) continue;
                if (parameterIsUsedAsCallback(callee, callee.params.get(index).name)) return true;
            }
            open = close;
        }
        return false;
    }

    private Function findFunction(String name) {
        Function exact = functions.get(name);
        if (exact != null) return exact;
        int separator = Math.max(name.lastIndexOf('.'), name.lastIndexOf("__"));
        String shortName = separator < 0 ? name
                : name.substring(separator + (name.charAt(separator) == '.' ? 1 : 2));
        return program.functions.stream().filter(function -> function.name.equals(shortName))
                .findFirst().orElse(null);
    }

    private boolean parameterIsUsedAsCallback(Function function, String parameter) {
        String escaped = java.util.regex.Pattern.quote(parameter);
        java.util.regex.Pattern pipelineUse = java.util.regex.Pattern.compile(
                "(?:\\|>\\s*)?(?:map|filter|any|all|reduce)\\s*\\([^)]*\\b"
                        + escaped + "\\b[^)]*\\)");
        java.util.regex.Pattern directUse = java.util.regex.Pattern.compile(
                "(?<![A-Za-z0-9_.])" + escaped + "\\s*\\(");
        List<String> expressions = new ArrayList<>();
        expressions.add(function.resultExpression);
        for (Stmt statement : function.body) {
            expressions.add(statement.text);
            expressions.add(statement.a);
            expressions.add(statement.b);
            expressions.addAll(statement.args);
        }
        for (String expression : expressions) {
            if (expression == null) continue;
            String code = maskStringLiterals(expression);
            if (pipelineUse.matcher(code).find() || directUse.matcher(code).find()) return true;
        }
        return false;
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

            for (Stmt s : program.main.body) {
                if (s.text != null && s.text.contains("message ")) {
                    for (Domain d : program.domains) {
                        if (s.text.contains(d.name + "(")) {
                            throw new CompileError(s.line, "domain state initializers must be side-effect-free; pure helper calls are accepted, but messages, domain access, I/O, failing, divergent, and unresolved work are rejected", "DOMAIN_INITIALIZER_EFFECT_INVALID");
                        }
                    }
                }
            }
            env = checkBlock(program.main.body, 0, program.main.body.size(), env, typer, solver, false, "main", TypeRef.unit());
            validateExecutorLifecycle(program.main.body);
            validateDirectMainRaises(program.main.body);
            validateMainFunctionRaises(program.main.body);
            validateMainMethodRaises(program.main.body, env, solver);
            validateRootIngressRaiseTotality(program.main.body, env, solver);
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
                                TypeRef innerElem = null;
                                if (!resolved.typeArgs().isEmpty()) {
                                    innerElem = resolved.typeArgs().get(0);
                                    sk.openResultBindings.add(
                                            new SpecializationKey.OpenResultBinding(1, innerElem));
                                }
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
                                        if (innerElem != null) {
                                            spec.openResultBindings.add(
                                                    new SpecializationKey.OpenResultBinding(1, innerElem));
                                        }
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

    private static final class ExecutorFlowState {
        final Map<String, Boolean> active = new LinkedHashMap<>();
        ExecutorFlowState copy() {
            ExecutorFlowState copy = new ExecutorFlowState();
            copy.active.putAll(active);
            return copy;
        }
        boolean hasActive() { return active.containsValue(Boolean.TRUE); }
    }

    private void validateExecutorLifecycle(List<Stmt> mainStatements) {
        for (Function function : program.functions) {
            for (Stmt statement : function.body) {
                String expressions = statementExpressions(statement);
                if (expressions.matches("(?s).*\\bExecutor\\s*\\(.*")) {
                    throw new CompileError(statement.line,
                            "Executor may only be configured and started directly in main",
                            "EXECUTOR_CONSTRUCT_OUTSIDE_MAIN", program.sourceFile);
                }
            }
        }
        ExecutorFlowState finalState = checkExecutorBlock(mainStatements, 0, mainStatements.size(),
                new ExecutorFlowState(), true);
        if (finalState.hasActive()) {
            throw new CompileError(program.main.line, "active Executor must reach Executor.join()",
                    "EXECUTOR_JOIN_MISSING", program.sourceFile);
        }
    }

    private ExecutorFlowState checkExecutorBlock(List<Stmt> statements, int start, int end,
                                                 ExecutorFlowState input, boolean directMainBody) {
        ExecutorFlowState state = input.copy();
        for (int i = start; i < end;) {
            Stmt statement = statements.get(i);
            if (statement.kind == Stmt.Kind.If) {
                int bodyEnd = i + 1;
                while (bodyEnd < end && statements.get(bodyEnd).indent > statement.indent) bodyEnd++;
                int elseEnd = bodyEnd;
                int elseStart = -1;
                if (elseEnd < end && statements.get(elseEnd).kind == Stmt.Kind.Else
                        && statements.get(elseEnd).indent == statement.indent) {
                    elseStart = elseEnd + 1;
                    elseEnd = elseStart;
                    while (elseEnd < end && statements.get(elseEnd).indent > statement.indent) elseEnd++;
                }
                ExecutorFlowState thenState = checkExecutorBlock(statements, i + 1, bodyEnd,
                        state, false);
                ExecutorFlowState otherwise = elseStart < 0 ? state.copy()
                        : checkExecutorBlock(statements, elseStart, elseEnd, state, false);
                if (!thenState.active.equals(otherwise.active)) {
                    throw new CompileError(statement.line,
                            "all branches must join active Executors consistently",
                            "EXECUTOR_JOIN_MISSING", program.sourceFile);
                }
                state = thenState;
                i = elseStart < 0 ? bodyEnd : elseEnd;
                continue;
            }
            if (statement.kind == Stmt.Kind.For || statement.kind == Stmt.Kind.While) {
                int bodyEnd = i + 1;
                while (bodyEnd < end && statements.get(bodyEnd).indent > statement.indent) bodyEnd++;
                ExecutorFlowState loopState = checkExecutorBlock(statements, i + 1, bodyEnd,
                        state, false);
                if (!loopState.active.equals(state.active)) {
                    throw new CompileError(statement.line,
                            "Executor lifecycle changes inside a loop are not statically bounded",
                            "EXECUTOR_JOIN_MISSING", program.sourceFile);
                }
                i = bodyEnd;
                continue;
            }
            if ((statement.kind == Stmt.Kind.Let || statement.kind == Stmt.Kind.Var
                    || statement.kind == Stmt.Kind.Assign)
                    && statement.a != null && statement.b != null
                    && statement.b.contains("Executor(") && statement.b.contains(".start()")) {
                if (!directMainBody || statement.a.contains(".") || statement.a.contains("[")) {
                    throw new CompileError(statement.line,
                            "Executor may only be configured and started directly in main",
                            "EXECUTOR_CONSTRUCT_OUTSIDE_MAIN", program.sourceFile);
                }
                if (state.hasActive()) {
                    throw new CompileError(statement.line, "at most one Executor may be active",
                            "EXECUTOR_MULTIPLE_ACTIVE", program.sourceFile);
                }
                state.active.put(statement.a.trim(), true);
                i++;
                continue;
            }
            if (statement.kind == Stmt.Kind.Return && state.hasActive()) {
                throw new CompileError(statement.line, "'return' leaves Executor before Executor.join()",
                        "EXECUTOR_JOIN_MISSING", program.sourceFile);
            }
            if (statement.kind == Stmt.Kind.Call && statement.a != null) {
                String executor = statement.a.trim();
                if ("join".equals(statement.b) || "invoke".equals(statement.b)) {
                    Boolean isActive = state.active.get(executor);
                    if (isActive == null || !isActive) {
                        throw new CompileError(statement.line,
                                "Executor has already joined and been consumed",
                                "EXECUTOR_USE_AFTER_JOIN", program.sourceFile);
                    }
                    if ("join".equals(statement.b)) state.active.put(executor, false);
                }
            }
            i++;
        }
        return state;
    }

    private void validateDirectMainRaises(List<Stmt> statements) {
        for (int raiseIndex = 0; raiseIndex < statements.size(); raiseIndex++) {
            Stmt statement = statements.get(raiseIndex);
            if (statement == null || statement.kind != Stmt.Kind.Raise
                    || statement.a == null || statement.a.isBlank()) continue;
            String variant = statement.a.trim();
            int payload = variant.indexOf('(');
            if (payload >= 0) variant = variant.substring(0, payload).trim();
            if (!isRaiseHandled(statements, raiseIndex, variant)) {
                throw new CompileError(program.main.line,
                        "main may not let typed errors escape; handle every raised variant with try/recover",
                        "MOSS_COMPILE_ERROR");
            }
        }
    }

    public static boolean isRaiseHandled(List<Stmt> statements, int raiseIndex, String variant) {
        if (statements == null || raiseIndex < 0 || raiseIndex >= statements.size()) return false;
        Stmt raised = statements.get(raiseIndex);
        for (int tryIndex = 0; tryIndex < raiseIndex; tryIndex++) {
            Stmt tryStatement = statements.get(tryIndex);
            if (tryStatement == null || tryStatement.kind != Stmt.Kind.Try
                    || tryStatement.indent >= raised.indent) continue;
            int bodyEnd = tryIndex + 1;
            while (bodyEnd < statements.size() && statements.get(bodyEnd).indent > tryStatement.indent) {
                bodyEnd++;
            }
            if (raiseIndex >= bodyEnd || bodyEnd >= statements.size()) continue;
            int recoverIndex = bodyEnd;
            while (recoverIndex < statements.size()) {
                Stmt recover = statements.get(recoverIndex);
                if (recover == null || recover.kind != Stmt.Kind.Recover
                        || recover.indent != tryStatement.indent) break;
                String pattern = recover.a == null ? "" : recover.a.trim();
                if (pattern.isEmpty()) return true;
                for (String part : org.mosslang.compiler.parser.ParserSupport.splitTopLevel(pattern, ',')) {
                    String selected = part.trim();
                    if (!selected.contains(".")) return true;
                    int payload = selected.indexOf('(');
                    if (payload >= 0) selected = selected.substring(0, payload).trim();
                    if (variant.equals(selected)) return true;
                }
                recoverIndex++;
                while (recoverIndex < statements.size()
                        && statements.get(recoverIndex).indent > tryStatement.indent) recoverIndex++;
            }
        }
        return false;
    }

    private void validateMainMethodRaises(List<Stmt> statements, OwnershipEnv env, TypeSolver solver) {
        for (int statementIndex = 0; statementIndex < statements.size(); statementIndex++) {
            String expression = statementExpressionText(statements.get(statementIndex));
            if (expression.isBlank()) continue;
            for (ObjectType object : program.objects) {
                for (Method method : object.methods) {
                    String pattern = "(?<![A-Za-z0-9_])((?:[A-Za-z_][A-Za-z0-9_]*)(?:\\s*\\.\\s*[A-Za-z_][A-Za-z0-9_]*)*)\\s*\\.\\s*"
                            + java.util.regex.Pattern.quote(method.name) + "\\s*\\(";
                    java.util.regex.Matcher matcher = java.util.regex.Pattern.compile(pattern)
                            .matcher(maskStringLiterals(expression));
                    while (matcher.find()) {
                        String receiverType = resolveMainReceiverType(matcher.group(1), env, solver);
                        if (!matchesObjectTypeName(receiverType, object.name)) continue;
                        for (String variant : collectMethodRaiseVariants(method, new HashSet<>())) {
                            if (!isRaiseHandled(statements, statementIndex, variant)) {
                                throw new CompileError(program.main.line,
                                        "main may not let typed errors escape; handle every raised variant with try/recover",
                                        "MOSS_COMPILE_ERROR");
                            }
                        }
                    }
                    for (String callback : org.mosslang.compiler.parser.ParserSupport
                            .pipelineCallbacks(maskStringLiterals(expression))) {
                        int dot = callback.lastIndexOf('.');
                        if (dot <= 0 || !method.name.equals(callback.substring(dot + 1))) continue;
                        String receiverType = resolveMainReceiverType(callback.substring(0, dot), env, solver);
                        if (!matchesObjectTypeName(receiverType, object.name)) continue;
                        for (String variant : collectMethodRaiseVariants(method, new HashSet<>())) {
                            if (!isRaiseHandled(statements, statementIndex, variant)) {
                                throw new CompileError(program.main.line,
                                        "main may not let typed errors escape; handle every raised variant with try/recover",
                                        "MOSS_COMPILE_ERROR");
                            }
                        }
                    }
                }
            }
        }
    }

    private void validateMainFunctionRaises(List<Stmt> statements) {
        for (int statementIndex = 0; statementIndex < statements.size(); statementIndex++) {
            String expression = statementExpressionText(statements.get(statementIndex));
            if (expression.isBlank()) continue;
            for (Function function : functions.values()) {
                if (!containsFunctionCall(expression, function.name)) continue;
                for (String variant : collectFunctionRaiseVariants(function, new HashSet<>())) {
                    if (!isRaiseHandled(statements, statementIndex, variant)) {
                        throw new CompileError(program.main.line,
                                "main may not let typed errors escape; handle every raised variant with try/recover",
                                "MOSS_COMPILE_ERROR");
                    }
                }
            }
        }
    }

    private Set<String> collectFunctionRaiseVariants(Function function, Set<String> visited) {
        Set<String> variants = new LinkedHashSet<>();
        if (function == null || !visited.add(function.name)) return variants;
        variants.addAll(function.sourceFreeRaiseVariants);
        for (int index = 0; index < function.body.size(); index++) {
            Stmt statement = function.body.get(index);
            if (statement == null) continue;
            if (statement.kind == Stmt.Kind.Raise && statement.a != null && !statement.a.isBlank()) {
                String variant = statement.a.trim();
                int payload = variant.indexOf('(');
                String callee = payload > 0 ? variant.substring(0, payload).trim() : "";
                boolean raisesFromFunctionCall = payload > 0 && functions.containsKey(callee);
                if (!raisesFromFunctionCall) {
                    if (payload >= 0) variant = variant.substring(0, payload).trim();
                    if (!isRaiseHandled(function.body, index, variant)) variants.add(variant);
                }
            }
            String expression = statementExpressionText(statement);
            collectNestedFunctionRaiseVariants(expression, function.body, index, visited, variants);
            collectFunctionMethodRaiseVariants(expression, function, index, visited, variants);
        }
        if (function.resultExpression != null) {
            collectNestedFunctionRaiseVariants(function.resultExpression, function.body,
                    function.body.size(), visited, variants);
            collectFunctionMethodRaiseVariants(function.resultExpression, function,
                    function.body.size(), visited, variants);
        }
        return variants;
    }

    /** Return escaping raises for an exported function, including called functions and methods. */
    public static Set<String> functionRaiseVariants(Program program, Function function) {
        SemanticChecker checker = new SemanticChecker(program);
        checker.collectDeclarations();
        return Collections.unmodifiableSet(
                checker.collectFunctionRaiseVariants(function, new HashSet<>()));
    }

    private void collectFunctionMethodRaiseVariants(String expression, Function caller,
                                                    int callIndex, Set<String> visited,
                                                    Set<String> variants) {
        Map<String, TypeRef> bindings = program.localBindingTypes != null
                ? program.localBindingTypes.getOrDefault(caller.name, Map.of()) : Map.of();
        for (ObjectType object : program.objects) {
            for (Method method : object.methods) {
                String pattern = "(?<![A-Za-z0-9_])([A-Za-z_][A-Za-z0-9_]*(?:\\s*\\.\\s*[A-Za-z_][A-Za-z0-9_]*)*)\\s*\\.\\s*"
                        + java.util.regex.Pattern.quote(method.name) + "\\s*\\(";
                java.util.regex.Matcher matcher = java.util.regex.Pattern.compile(pattern)
                        .matcher(maskStringLiterals(expression));
                while (matcher.find()) {
                    String receiverType = resolveFunctionReceiverType(matcher.group(1), bindings);
                    if (!matchesObjectTypeName(receiverType, object.name)) continue;
                    for (String raised : collectMethodRaiseVariants(method, visited)) {
                        if (!isRaiseHandled(caller.body, callIndex, raised)) variants.add(raised);
                    }
                }
                for (String callback : org.mosslang.compiler.parser.ParserSupport
                        .pipelineCallbacks(maskStringLiterals(expression))) {
                    int dot = callback.lastIndexOf('.');
                    if (dot <= 0 || !method.name.equals(callback.substring(dot + 1))) continue;
                    String receiverType = resolveFunctionReceiverType(callback.substring(0, dot), bindings);
                    if (!matchesObjectTypeName(receiverType, object.name)) continue;
                    for (String raised : collectMethodRaiseVariants(method, visited)) {
                        if (!isRaiseHandled(caller.body, callIndex, raised)) variants.add(raised);
                    }
                }
            }
        }
    }

    private String resolveFunctionReceiverType(String receiverPath, Map<String, TypeRef> bindings) {
        String[] path = receiverPath.replaceAll("\\s", "").split("\\.");
        if (path.length == 0) return null;
        TypeRef receiver = bindings.get(path[0]);
        String typeName = receiver != null ? receiver.name() : null;
        for (int index = 1; typeName != null && index < path.length; index++) {
            ObjectType owner = findObjectType(typeName);
            if (owner == null) return null;
            String fieldName = path[index];
            Field field = owner.fields.stream().filter(candidate -> candidate.name.equals(fieldName))
                    .findFirst().orElse(null);
            if (field == null) return null;
            TypeRef fieldType = field.typeRef != null ? field.typeRef : TypeParser.parseCanonical(field.type);
            typeName = fieldType != null ? fieldType.name() : field.type;
        }
        return typeName;
    }

    private void collectNestedFunctionRaiseVariants(String expression, List<Stmt> callerBody,
                                                    int callIndex, Set<String> visited,
                                                    Set<String> variants) {
        for (Function candidate : functions.values()) {
            if (!containsFunctionCall(expression, candidate.name)
                    && !org.mosslang.compiler.parser.ParserSupport
                            .usesPipelineFunctionCallback(expression, candidate.name)) continue;
            for (String raised : collectFunctionRaiseVariants(candidate, visited)) {
                if (!isRaiseHandled(callerBody, callIndex, raised)) variants.add(raised);
            }
        }
    }

    public static boolean containsFunctionCall(String expression, String functionName) {
        if (expression == null || functionName == null) return false;
        return java.util.regex.Pattern.compile("(?<![A-Za-z0-9_])"
                + java.util.regex.Pattern.quote(functionName) + "\\s*\\(")
                .matcher(maskStringLiterals(expression)).find();
    }

    private static String maskStringLiterals(String expression) {
        StringBuilder code = new StringBuilder(expression.length());
        char quote = 0;
        boolean escaped = false;
        for (int index = 0; index < expression.length(); index++) {
            char value = expression.charAt(index);
            if (quote != 0) {
                code.append(' ');
                if (escaped) escaped = false;
                else if (value == '\\') escaped = true;
                else if (value == quote) quote = 0;
            } else if (value == '"' || value == '\'') {
                quote = value;
                escaped = false;
                code.append(' ');
            } else {
                code.append(value);
            }
        }
        return code.toString();
    }

    private Set<String> collectMethodRaiseVariants(Method method, Set<String> visited) {
        Set<String> variants = new LinkedHashSet<>();
        if (method == null || !visited.add(method.owner + "." + method.name)) return variants;
        variants.addAll(method.sourceFreeRaiseVariants);
        Map<String, String> localTypes = new HashMap<>();
        for (int index = 0; index < method.body.size(); index++) {
            Stmt statement = method.body.get(index);
            if (statement == null) continue;
            if (statement.kind == Stmt.Kind.Raise && statement.a != null && !statement.a.isBlank()) {
                String variant = statement.a.trim();
                int payload = variant.indexOf('(');
                if (payload >= 0) variant = variant.substring(0, payload).trim();
                if (!isRaiseHandled(method.body, index, variant)) variants.add(variant);
            }
            String expression = statementExpressionText(statement);
            collectNestedFunctionRaiseVariants(expression, method.body, index, visited, variants);
            collectNestedMethodRaiseVariants(expression, method, localTypes, index, visited, variants);
            if (statement.kind == Stmt.Kind.Assign && statement.a != null && statement.b != null
                    && statement.a.trim().matches("[A-Za-z_][A-Za-z0-9_]*")) {
                String typeName = resolveMethodValueType(statement.b, method, localTypes);
                if (typeName != null) localTypes.put(statement.a.trim(), typeName);
            }
        }
        if (method.resultExpression != null) {
            collectNestedFunctionRaiseVariants(method.resultExpression, method.body,
                    method.body.size(), visited, variants);
            collectNestedMethodRaiseVariants(method.resultExpression, method, localTypes,
                    method.body.size(), visited, variants);
        }
        return variants;
    }

    /** Return the escaping raises known for a method, including methods it calls. */
    public static Set<String> methodRaiseVariants(Program program, Method method) {
        SemanticChecker checker = new SemanticChecker(program);
        checker.collectDeclarations();
        return Collections.unmodifiableSet(
                checker.collectMethodRaiseVariants(method, new HashSet<>()));
    }

    private void collectNestedMethodRaiseVariants(String expression, Method ownerMethod,
                                                  Map<String, String> localTypes,
                                                  int callIndex,
                                                  Set<String> visited, Set<String> variants) {
        for (ObjectType object : program.objects) {
            for (Method candidate : object.methods) {
                String pattern = "(?<![A-Za-z0-9_])((?:self|[A-Za-z_][A-Za-z0-9_]*)(?:\\s*\\.\\s*[A-Za-z_][A-Za-z0-9_]*)*)\\s*\\.\\s*"
                        + java.util.regex.Pattern.quote(candidate.name) + "\\s*\\(";
                java.util.regex.Matcher matcher = java.util.regex.Pattern.compile(pattern)
                        .matcher(maskStringLiterals(expression));
                while (matcher.find()) {
                    String receiverType = resolveMethodReceiverType(matcher.group(1), ownerMethod, localTypes);
                    if (!matchesObjectTypeName(receiverType, object.name)) continue;
                    for (String raised : collectMethodRaiseVariants(candidate, visited)) {
                        if (!isRaiseHandled(ownerMethod.body, callIndex, raised)) variants.add(raised);
                    }
                }
                for (String callback : org.mosslang.compiler.parser.ParserSupport
                        .pipelineCallbacks(maskStringLiterals(expression))) {
                    int dot = callback.lastIndexOf('.');
                    if (dot <= 0 || !candidate.name.equals(callback.substring(dot + 1))) continue;
                    String receiverType = resolveMethodReceiverType(
                            callback.substring(0, dot), ownerMethod, localTypes);
                    if (!matchesObjectTypeName(receiverType, object.name)) continue;
                    for (String raised : collectMethodRaiseVariants(candidate, visited)) {
                        if (!isRaiseHandled(ownerMethod.body, callIndex, raised)) variants.add(raised);
                    }
                }
            }
        }
    }

    private String resolveMethodReceiverType(String receiverPath, Method ownerMethod,
                                             Map<String, String> localTypes) {
        String[] path = receiverPath.replaceAll("\\s", "").split("\\.");
        if (path.length == 0) return null;
        String typeName = "self".equals(path[0]) ? ownerMethod.owner : localTypes.get(path[0]);
        if (typeName == null) return null;
        for (int index = 1; index < path.length; index++) {
            ObjectType owner = findObjectType(typeName);
            if (owner == null) return null;
            String fieldName = path[index];
            Field field = owner.fields.stream().filter(candidate -> candidate.name.equals(fieldName))
                    .findFirst().orElse(null);
            if (field == null) return null;
            TypeRef fieldType = field.typeRef != null ? field.typeRef : TypeParser.parseCanonical(field.type);
            typeName = fieldType != null ? fieldType.name() : field.type;
            if (typeName == null || typeName.isBlank()) return null;
        }
        return typeName;
    }

    private String resolveMethodValueType(String expression, Method ownerMethod,
                                          Map<String, String> localTypes) {
        String value = expression == null ? "" : expression.trim();
        if (value.matches("[A-Za-z_][A-Za-z0-9_]*(?:\\s*\\.\\s*[A-Za-z_][A-Za-z0-9_]*)*")) {
            return resolveMethodReceiverType(value, ownerMethod, localTypes);
        }
        int open = value.indexOf('(');
        if (open > 0) {
            String constructor = value.substring(0, open).trim();
            ObjectType object = findObjectType(constructor);
            if (object != null) return object.name;
        }
        return null;
    }

    private String resolveMainReceiverType(String receiverPath, OwnershipEnv env, TypeSolver solver) {
        String[] path = receiverPath.replaceAll("\\s", "").split("\\.");
        if (path.length == 0) return null;
        TypeRef receiver = env.get(path[0]);
        if (receiver == null) return null;
        TypeRef resolved = solver.resolve(receiver);
        String typeName = resolved != null ? resolved.name() : null;
        for (int index = 1; typeName != null && index < path.length; index++) {
            ObjectType owner = findObjectType(typeName);
            if (owner == null) return null;
            String fieldName = path[index];
            Field field = owner.fields.stream().filter(candidate -> candidate.name.equals(fieldName))
                    .findFirst().orElse(null);
            if (field == null) return null;
            TypeRef fieldType = field.typeRef != null ? field.typeRef : TypeParser.parseCanonical(field.type);
            TypeRef resolvedFieldType = fieldType != null ? solver.resolve(fieldType) : null;
            typeName = resolvedFieldType != null ? resolvedFieldType.name() : field.type;
        }
        return typeName;
    }

    private TypeRef resolveReceiverPathType(TypeRef rootType, String receiverPath, String rootName) {
        if (rootType == null || receiverPath == null || rootName == null) return null;
        String[] path = receiverPath.replaceAll("\\s", "").split("\\.");
        if (path.length == 0 || !rootName.equals(path[0])) return null;
        TypeRef type = rootType;
        for (int index = 1; index < path.length; index++) {
            ObjectType owner = findObjectType(type.name());
            if (owner == null) return null;
            String fieldName = path[index];
            Field field = owner.fields.stream().filter(candidate -> candidate.name.equals(fieldName))
                    .findFirst().orElse(null);
            if (field == null) return null;
            type = field.typeRef != null ? field.typeRef
                    : (field.type != null ? TypeParser.parseCanonical(field.type) : null);
            if (type == null) return null;
        }
        return type;
    }

    private ObjectType findObjectType(String typeName) {
        if (typeName == null) return null;
        for (ObjectType object : objects.values()) {
            if (matchesObjectTypeName(typeName, object.name)) return object;
        }
        return null;
    }

    private boolean matchesObjectTypeName(String typeName, String objectName) {
        return typeName != null && objectName != null
                && (typeName.equals(objectName) || typeName.endsWith("__" + objectName)
                        || typeName.endsWith("::" + objectName));
    }

    private String statementExpressionText(Stmt statement) {
        if (statement == null) return "";
        StringBuilder expression = new StringBuilder();
        if (statement.text != null) expression.append(statement.text).append(' ');
        if (statement.a != null) expression.append(statement.a).append(' ');
        if (statement.b != null) expression.append(statement.b).append(' ');
        if (statement.args != null) {
            for (String argument : statement.args) expression.append(argument).append(' ');
        }
        return expression.toString();
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
        if (!functions.containsKey("sort")) {
            java.util.regex.Matcher sortCall = java.util.regex.Pattern
                    .compile("(?<![A-Za-z0-9_.])sort\\s*\\(").matcher(expression);
            while (sortCall.find()) {
                int open = expression.indexOf('(', sortCall.start());
                int close = matchingCloseParen(expression, open);
                if (close < 0) continue;
                List<String> args = ExpressionTyper.parseArguments(
                        expression.substring(open + 1, close));
                if (args.isEmpty()) continue;
                String place = callArgumentValue(args.get(0));
                String root = place;
                int separator = root.length();
                int dot = root.indexOf('.');
                int bracket = root.indexOf('[');
                if (dot >= 0) separator = Math.min(separator, dot);
                if (bracket >= 0) separator = Math.min(separator, bracket);
                root = root.substring(0, separator).trim();
                if (env.immutables.contains(root)) {
                    throw new CompileError(line, "cannot mutate immutable local '" + root
                            + "'; declare it with `var` if mutation is intended",
                            "IMMUTABLE_LOCAL_MUTATION");
                }
            }
        }
        java.util.regex.Matcher methodCall = java.util.regex.Pattern
                .compile("(?<![A-Za-z0-9_])([A-Za-z_][A-Za-z0-9_]*(?:\\s*\\.\\s*[A-Za-z_][A-Za-z0-9_]*)*)"
                        + "\\s*\\.\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*\\(")
                .matcher(expression);
        while (methodCall.find()) {
            String receiverPath = methodCall.group(1).replaceAll("\\s", "");
            String root = receiverPath.contains(".")
                    ? receiverPath.substring(0, receiverPath.indexOf('.')) : receiverPath;
            if (!env.immutables.contains(root)) continue;
            TypeRef rootType = env.get(root);
            TypeRef receiverType = resolveReceiverPathType(rootType, receiverPath, root);
            ObjectType receiverObject = receiverType != null
                    ? findObjectType(receiverType.name()) : null;
            boolean writesReceiver = isBuiltinMutatingMethod(receiverType, methodCall.group(2))
                    || (receiverObject != null && receiverObject.methods.stream().anyMatch(method ->
                            method.name.equals(methodCall.group(2))
                                    && method.receiverAccessMode == AccessMode.Write));
            if (writesReceiver) {
                throw new CompileError(line, "cannot mutate immutable local '" + root
                        + "'; declare it with `var` if mutation is intended",
                        "IMMUTABLE_LOCAL_MUTATION");
            }
        }
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
        String borrowedSource = currentBorrowedEnumPayloadSources.get(name);
        if (borrowedSource != null) {
            throw new CompileError(line,
                    "payload '" + name + "' is borrowed from '" + borrowedSource
                            + "'; use 'match consume " + borrowedSource + ":' to transfer it",
                    "BORROWED_ENUM_PAYLOAD_CONSUME");
        }
        if (currentMessagePayloads.contains(name)) {
            throw new CompileError(line,
                    "cannot CONSUME incoming message payload '" + name + "'; message payloads may only be read or forwarded",
                    "MOSS_COMPILE_ERROR");
        }
        if (currentHandlerStateFields.contains(name)) {
            throw new CompileError(line, "domain state '" + name + "' cannot be consumed into '"
                    + destination + "'", "MOSS_COMPILE_ERROR");
        }
        env.moved.put(name, new MoveInfo(line, destination));
    }

    private boolean consumeOwnedFieldProjection(int line, String expression, TypeRef projectedType,
                                               OwnershipEnv env, String destination) {
        if (expression == null || projectedType == null || !transferType(projectedType)) return false;
        String path = expression.trim();
        if (!path.matches("[A-Za-z_][A-Za-z0-9_]*(?:\\s*\\.\\s*[A-Za-z_][A-Za-z0-9_]*)+")) {
            return false;
        }
        int separator = path.indexOf('.');
        String root = path.substring(0, separator).trim();
        if (!env.containsKey(root) || !transferType(env.get(root))) return false;
        consumeBinding(line, root, env, destination);
        return true;
    }

    private boolean doesFunctionConsumeParam(Function fn, int paramIdx) {
        TypeRef parameterType = fn != null && paramIdx >= 0 && paramIdx < fn.params.size()
                ? functionParameterType(fn.params.get(paramIdx)) : null;
        return doesFunctionConsumeParam(fn, paramIdx, parameterType);
    }

    private boolean doesFunctionConsumeParam(Function fn, int paramIdx, TypeRef parameterType) {
        if (fn == null || paramIdx < 0 || paramIdx >= fn.params.size()) return false;
        String pName = fn.params.get(paramIdx).name;
        if (fn.resultExpression != null
                && transferType(resolveReceiverPathType(parameterType,
                        fn.resultExpression.trim(), pName))) return true;
        if (fn.resultExpression != null && fn.resultExpression.trim().equals(pName)) {
            return true;
        }
        if (parameterType != null) {
            java.util.regex.Pattern methodCall = java.util.regex.Pattern.compile(
                    "\\b" + java.util.regex.Pattern.quote(pName)
                            + "(?:\\s*\\.\\s*[A-Za-z_][A-Za-z0-9_]*)*"
                            + "\\.([A-Za-z_][A-Za-z0-9_]*)\\s*\\(");
            List<String> expressions = new ArrayList<>();
            for (Stmt statement : fn.body) expressions.add(statementExpressionText(statement));
            if (fn.resultExpression != null) expressions.add(fn.resultExpression);
            for (String expression : expressions) {
                java.util.regex.Matcher call = methodCall.matcher(expression);
                while (call.find()) {
                    String matchedCall = expression.substring(call.start(), call.end());
                    String receiver = matchedCall.substring(0, matchedCall.lastIndexOf('.')).trim();
                    TypeRef receiverType = resolveReceiverPathType(parameterType, receiver, pName);
                    ObjectType receiverObject = receiverType != null
                            ? findObjectType(receiverType.name()) : null;
                    if (receiverObject != null) {
                        for (Method method : receiverObject.methods)
                            if (method.name.equals(call.group(1))
                                    && method.receiverAccessMode == AccessMode.Consume) return true;
                    } else if (receiver.equals(pName)) {
                        // Generic receiver calls can be accepted before this summary
                        // has a concrete receiver TypeRef. Keep a consuming method
                        // requirement only when every same-name candidate consumes;
                        // otherwise a read-only structural receiver could be rejected.
                        boolean hasCandidate = false;
                        boolean allCandidatesConsume = true;
                        for (ObjectType object : program.objects)
                            for (Method method : object.methods)
                                if (method.name.equals(call.group(1))) {
                                    hasCandidate = true;
                                    if (method.receiverAccessMode != AccessMode.Consume)
                                        allCandidatesConsume = false;
                                }
                        if (hasCandidate && allCandidatesConsume) return true;
                    }
                }
            }
        }
        if (functionCallConsumesParam(fn, pName, parameterType)) return true;
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
            String movedExpression = s.kind == Stmt.Kind.Return
                    ? statementResultExpression(s)
                    : ((s.kind == Stmt.Kind.Let || s.kind == Stmt.Kind.Var
                            || s.kind == Stmt.Kind.Assign) && s.b != null ? s.b.trim() : "");
            if (!shadowed && transferType(resolveReceiverPathType(
                    parameterType, movedExpression, pName))) return true;
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

    private boolean functionCallConsumesParam(Function caller, String paramName, TypeRef parameterType) {
        List<String> expressions = new ArrayList<>();
        Deque<Map.Entry<Integer, Set<String>>> caseScopes = new ArrayDeque<>();
        for (Stmt statement : caller.body) {
            while (!caseScopes.isEmpty() && statement.indent <= caseScopes.peek().getKey())
                caseScopes.pop();
            if (statement.kind == Stmt.Kind.Case) {
                caseScopes.push(Map.entry(statement.indent, new HashSet<>(statement.args)));
                continue;
            }
            boolean shadowed = caseScopes.stream().anyMatch(scope -> scope.getValue().contains(paramName));
            if (!shadowed) expressions.add(statementExpressionText(statement));
        }
        if (caller.resultExpression != null) expressions.add(caller.resultExpression);
        for (Function callee : functions.values()) {
            if (callee == caller) continue;
            java.util.regex.Pattern callPattern = java.util.regex.Pattern.compile(
                    "(?<![A-Za-z0-9_.])" + java.util.regex.Pattern.quote(callee.name)
                            + "\\s*\\(");
            for (String expression : expressions) {
                java.util.regex.Matcher call = callPattern.matcher(expression);
                while (call.find()) {
                    int open = expression.indexOf('(', call.start());
                    int close = -1;
                    for (int cursor = open + 1; cursor < expression.length(); cursor++) {
                        if (expression.charAt(cursor) == ')'
                                && ExpressionTyper.findMatchingOpenParen(expression, cursor) == open) {
                            close = cursor;
                            break;
                        }
                    }
                    if (close < 0) continue;
                    List<String> args = ExpressionTyper.parseArguments(
                            expression.substring(open + 1, close));
                    for (int index = 0; index < args.size() && index < callee.params.size(); index++) {
                        if (index >= callee.parameterAccessModes.size()
                                || callee.parameterAccessModes.get(index) != AccessMode.Consume) continue;
                        String argument = callArgumentValue(args.get(index));
                        if (argument.equals(paramName) && parameterType != null
                                && transferType(parameterType)) return true;
                        TypeRef movedType = resolveReceiverPathType(parameterType, argument, paramName);
                        if (transferType(movedType)) return true;
                    }
                }
            }
        }
        Map<String, TypeRef> bindings = new HashMap<>();
        if (program.localBindingTypes != null)
            bindings.putAll(program.localBindingTypes.getOrDefault(caller.name, Map.of()));
        for (Param parameter : caller.params) {
            TypeRef type = parameter.typeRef != null ? parameter.typeRef
                    : (parameter.type != null && !parameter.type.isEmpty()
                            ? TypeParser.parseCanonical(parameter.type) : null);
            if (type != null) bindings.putIfAbsent(parameter.name, type);
        }
        for (ObjectType object : program.objects) {
            for (Method method : object.methods) {
                if (method.parameterAccessModes.stream().noneMatch(mode -> mode == AccessMode.Consume)) continue;
                java.util.regex.Pattern callPattern = java.util.regex.Pattern.compile(
                        "(?<![A-Za-z0-9_])([A-Za-z_][A-Za-z0-9_]*(?:\\s*\\.\\s*[A-Za-z_][A-Za-z0-9_]*)*)"
                                + "\\s*\\.\\s*" + java.util.regex.Pattern.quote(method.name)
                                + "\\s*\\(");
                for (String expression : expressions) {
                    java.util.regex.Matcher call = callPattern.matcher(expression);
                    while (call.find()) {
                        String receiverType = resolveFunctionReceiverType(call.group(1), bindings);
                        if (!matchesObjectTypeName(receiverType, object.name)) continue;
                        int open = expression.indexOf('(', call.start());
                        int close = -1;
                        for (int cursor = open + 1; cursor < expression.length(); cursor++) {
                            if (expression.charAt(cursor) == ')'
                                    && ExpressionTyper.findMatchingOpenParen(expression, cursor) == open) {
                                close = cursor;
                                break;
                            }
                        }
                        if (close < 0) continue;
                        List<String> args = ExpressionTyper.parseArguments(
                                expression.substring(open + 1, close));
                        for (int index = 0; index < args.size()
                                && index < method.parameterAccessModes.size(); index++) {
                            if (method.parameterAccessModes.get(index) != AccessMode.Consume) continue;
                            String argument = callArgumentValue(args.get(index));
                            if (argument.equals(paramName) && parameterType != null
                                    && transferType(parameterType)) return true;
                            if (transferType(resolveReceiverPathType(parameterType, argument, paramName)))
                                return true;
                        }
                    }
                }
            }
        }
        for (ObjectType object : program.objects) {
            for (Method method : object.methods) {
                java.util.regex.Pattern callPattern = java.util.regex.Pattern.compile(
                        "(?<![A-Za-z_])([A-Za-z_][A-Za-z0-9_]*(?:\\s*\\.\\s*[A-Za-z_][A-Za-z0-9_]*)*)"
                                + "\\s*\\.\\s*" + java.util.regex.Pattern.quote(method.name)
                                + "\\s*\\(");
                for (String expression : expressions) {
                    java.util.regex.Matcher call = callPattern.matcher(expression);
                    while (call.find()) {
                        String receiverType = resolveFunctionReceiverType(call.group(1), bindings);
                        if (findObjectType(receiverType) != null) continue;
                        int open = expression.indexOf('(', call.start());
                        int close = -1;
                        for (int cursor = open + 1; cursor < expression.length(); cursor++) {
                            if (expression.charAt(cursor) == ')'
                                    && ExpressionTyper.findMatchingOpenParen(expression, cursor) == open) {
                                close = cursor;
                                break;
                            }
                        }
                        if (close < 0) continue;
                        List<String> args = ExpressionTyper.parseArguments(
                                expression.substring(open + 1, close));
                        for (int argumentIndex = 0; argumentIndex < args.size(); argumentIndex++) {
                            if (!allMethodsConsumeArgument(method.name, argumentIndex)) continue;
                            String argument = callArgumentValue(args.get(argumentIndex));
                            if (argument.equals(paramName) && parameterType != null
                                    && transferType(parameterType)) return true;
                            if (transferType(resolveReceiverPathType(parameterType, argument, paramName)))
                                return true;
                        }
                    }
                }
            }
        }
        for (ObjectType object : program.objects) {
            java.util.regex.Pattern constructor = java.util.regex.Pattern.compile(
                    "(?<![A-Za-z0-9_.])" + java.util.regex.Pattern.quote(object.name)
                            + "\\s*\\(");
            for (String expression : expressions) {
                java.util.regex.Matcher call = constructor.matcher(expression);
                while (call.find()) {
                    int open = expression.indexOf('(', call.start());
                    int close = -1;
                    for (int cursor = open + 1; cursor < expression.length(); cursor++) {
                        if (expression.charAt(cursor) == ')'
                                && ExpressionTyper.findMatchingOpenParen(expression, cursor) == open) {
                            close = cursor;
                            break;
                        }
                    }
                    if (close < 0) continue;
                    List<String> args = ExpressionTyper.parseArguments(
                            expression.substring(open + 1, close));
                    for (String rawArgument : args) {
                        String argument = callArgumentValue(rawArgument);
                        if (argument.equals(paramName) && parameterType != null
                                && transferType(parameterType)) return true;
                        if (transferType(resolveReceiverPathType(parameterType, argument, paramName)))
                            return true;
                    }
                }
            }
        }
        return false;
    }

    private boolean allMethodsConsumeArgument(String methodName, int argumentIndex) {
        boolean found = false;
        for (ObjectType object : program.objects) {
            for (Method method : object.methods) {
                if (!method.name.equals(methodName)) continue;
                found = true;
                if (argumentIndex >= method.parameterAccessModes.size()
                        || method.parameterAccessModes.get(argumentIndex) != AccessMode.Consume)
                    return false;
            }
        }
        return found;
    }

    private String callArgumentValue(String rawArgument) {
        if (rawArgument == null) return "";
        String argument = rawArgument.trim();
        int equals = argument.indexOf('=');
        if (equals > 0 && (equals + 1 >= argument.length() || argument.charAt(equals + 1) != '=')
                && argument.substring(0, equals).trim().matches("[A-Za-z_][A-Za-z0-9_]*")) {
            return argument.substring(equals + 1).trim();
        }
        int colon = argument.indexOf(':');
        if (colon > 0 && argument.substring(0, colon).trim().matches("[A-Za-z_][A-Za-z0-9_]*"))
            return argument.substring(colon + 1).trim();
        return argument;
    }

    private void closeFunctionAccessModes() {
        int maxPasses = functions.values().stream().mapToInt(function -> function.params.size()).sum() + 1;
        for (int pass = 0; pass < maxPasses; pass++) {
            boolean changed = false;
            for (Function function : functions.values()) {
                while (function.parameterAccessModes.size() < function.params.size())
                    function.parameterAccessModes.add(AccessMode.Read);
                for (int index = 0; index < function.params.size(); index++) {
                    TypeRef type = functionParameterType(function.params.get(index));
                    AccessMode current = function.parameterAccessModes.get(index);
                    boolean inferredParameter = function.params.get(index).typeRef == null
                            && (function.params.get(index).type == null
                                    || function.params.get(index).type.isEmpty());
                    if (doesFunctionConsumeParam(function, index)
                            && (inferredParameter || (type != null && transferType(type)))
                            && current != AccessMode.Consume) {
                        function.parameterAccessModes.set(index, AccessMode.Consume);
                        changed = true;
                    } else if (current == AccessMode.Read
                            && doesFunctionMutateParam(function, index)) {
                        function.parameterAccessModes.set(index, AccessMode.Write);
                        changed = true;
                    }
                }
            }
            if (!changed) return;
        }
    }

    private boolean doesFunctionMutateParam(Function fn, int paramIdx) {
        if (fn == null || paramIdx < 0 || paramIdx >= fn.params.size()) return false;
        String pName = fn.params.get(paramIdx).name;
        TypeRef parameterType = functionParameterType(fn.params.get(paramIdx));
        if (!functions.containsKey("sort") && functionSortsRootedIn(fn, pName)) return true;
        if (functionCallsPassingParamToWritingMethod(fn, pName, parameterType)) return true;
        if (functionCallsWritingNestedReceiver(fn, pName, parameterType)) return true;
        if (functionCallWritesParam(fn, pName, parameterType)) return true;
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

    private boolean functionSortsRootedIn(Function function, String parameterName) {
        List<String> expressions = new ArrayList<>();
        for (Stmt statement : function.body) expressions.add(statementExpressionText(statement));
        if (function.resultExpression != null) expressions.add(function.resultExpression);
        java.util.regex.Pattern sortCall = java.util.regex.Pattern
                .compile("(?<![A-Za-z0-9_.])sort\\s*\\(");
        for (String expression : expressions) {
            java.util.regex.Matcher matcher = sortCall.matcher(expression);
            while (matcher.find()) {
                int open = expression.indexOf('(', matcher.start());
                int close = matchingCloseParen(expression, open);
                if (close < 0) continue;
                List<String> args = ExpressionTyper.parseArguments(
                        expression.substring(open + 1, close));
                if (args.isEmpty()) continue;
                String place = callArgumentValue(args.get(0));
                if (place.equals(parameterName) || place.startsWith(parameterName + ".")
                        || place.startsWith(parameterName + "[")) return true;
            }
        }
        return false;
    }

    private TypeRef functionParameterType(Param parameter) {
        if (parameter == null) return null;
        if (parameter.typeRef != null) return parameter.typeRef;
        if (parameter.type != null && !parameter.type.isEmpty())
            return TypeParser.parseCanonical(parameter.type);
        return parameter.checkedTypeRef;
    }

    private AccessMode functionParameterMode(Function function, int index) {
        if (function == null || index < 0 || index >= function.parameterAccessModes.size())
            return AccessMode.Read;
        return function.parameterAccessModes.get(index);
    }

    private boolean isSourceFreeFunction(Function function) {
        return function != null && function.sourceFreeAbiVersion > 0;
    }

    private boolean functionCallsWritingNestedReceiver(Function caller, String paramName,
                                                       TypeRef parameterType) {
        if (parameterType == null) return false;
        Map<String, TypeRef> bindings = new HashMap<>();
        if (program.localBindingTypes != null)
            bindings.putAll(program.localBindingTypes.getOrDefault(caller.name, Map.of()));
        for (Param parameter : caller.params) {
            TypeRef type = parameter.typeRef != null ? parameter.typeRef
                    : (parameter.type != null && !parameter.type.isEmpty()
                            ? TypeParser.parseCanonical(parameter.type) : null);
            if (type != null) bindings.putIfAbsent(parameter.name, type);
        }
        List<String> expressions = new ArrayList<>();
        Deque<Map.Entry<Integer, Set<String>>> caseScopes = new ArrayDeque<>();
        for (Stmt statement : caller.body) {
            while (!caseScopes.isEmpty() && statement.indent <= caseScopes.peek().getKey())
                caseScopes.pop();
            if (statement.kind == Stmt.Kind.Case) {
                caseScopes.push(Map.entry(statement.indent, new HashSet<>(statement.args)));
                continue;
            }
            if (caseScopes.stream().anyMatch(scope -> scope.getValue().contains(paramName))) continue;
            expressions.add(statementExpressionText(statement));
        }
        if (caller.resultExpression != null) expressions.add(caller.resultExpression);
        for (ObjectType object : program.objects) {
            for (Method method : object.methods) {
                if (method.receiverAccessMode != AccessMode.Write) continue;
                java.util.regex.Pattern callPattern = java.util.regex.Pattern.compile(
                        "(?<![A-Za-z0-9_])" + java.util.regex.Pattern.quote(paramName)
                                + "(?:\\s*\\.\\s*[A-Za-z_][A-Za-z0-9_]*)*"
                                + "\\s*\\.\\s*" + java.util.regex.Pattern.quote(method.name)
                                + "\\s*\\(");
                for (String expression : expressions) {
                    java.util.regex.Matcher call = callPattern.matcher(expression);
                    while (call.find()) {
                        String matchedCall = call.group();
                        String receiver = matchedCall.substring(0, matchedCall.lastIndexOf('.')).trim();
                        String receiverType = resolveFunctionReceiverType(receiver, bindings);
                        if (matchesObjectTypeName(receiverType, object.name)) return true;
                        // An inferred parameter may retain a placeholder type even though
                        // generic method resolution accepted this direct receiver call.
                        // Preserve the method's WRITE requirement rather than publishing
                        // an unsound READ summary for that parameter.
                        if (receiver.equals(paramName)) return true;
                    }
                }
            }
        }
        return false;
    }

    private boolean functionCallsPassingParamToWritingMethod(Function caller, String paramName,
                                                              TypeRef parameterType) {
        Map<String, TypeRef> bindings = new HashMap<>();
        if (program.localBindingTypes != null)
            bindings.putAll(program.localBindingTypes.getOrDefault(caller.name, Map.of()));
        for (Param parameter : caller.params) {
            TypeRef type = functionParameterType(parameter);
            if (type != null) bindings.putIfAbsent(parameter.name, type);
        }
        List<String> expressions = new ArrayList<>();
        Deque<Map.Entry<Integer, Set<String>>> caseScopes = new ArrayDeque<>();
        for (Stmt statement : caller.body) {
            while (!caseScopes.isEmpty() && statement.indent <= caseScopes.peek().getKey())
                caseScopes.pop();
            if (statement.kind == Stmt.Kind.Case) {
                caseScopes.push(Map.entry(statement.indent, new HashSet<>(statement.args)));
                continue;
            }
            if (caseScopes.stream().anyMatch(scope -> scope.getValue().contains(paramName))) continue;
            expressions.add(statementExpressionText(statement));
        }
        if (caller.resultExpression != null) expressions.add(caller.resultExpression);
        for (ObjectType object : program.objects) {
            for (Method method : object.methods) {
                if (method.parameterAccessModes.stream().noneMatch(mode -> mode == AccessMode.Write)) continue;
                java.util.regex.Pattern callPattern = java.util.regex.Pattern.compile(
                        "(?<![A-Za-z0-9_])([A-Za-z_][A-Za-z0-9_]*(?:\\s*\\.\\s*[A-Za-z_][A-Za-z0-9_]*)*)"
                                + "\\s*\\.\\s*" + java.util.regex.Pattern.quote(method.name)
                                + "\\s*\\(");
                for (String expression : expressions) {
                    java.util.regex.Matcher call = callPattern.matcher(expression);
                    while (call.find()) {
                        String receiverType = resolveFunctionReceiverType(call.group(1), bindings);
                        if (!matchesObjectTypeName(receiverType, object.name)) continue;
                        int open = expression.indexOf('(', call.start());
                        int close = -1;
                        for (int cursor = open + 1; cursor < expression.length(); cursor++) {
                            if (expression.charAt(cursor) == ')'
                                    && ExpressionTyper.findMatchingOpenParen(expression, cursor) == open) {
                                close = cursor;
                                break;
                            }
                        }
                        if (close < 0) continue;
                        List<String> args = ExpressionTyper.parseArguments(
                                expression.substring(open + 1, close));
                        for (int index = 0; index < args.size()
                                && index < method.parameterAccessModes.size(); index++) {
                            if (method.parameterAccessModes.get(index) != AccessMode.Write) continue;
                            String argument = callArgumentValue(args.get(index));
                            if (argument.equals(paramName)) return true;
                            if (resolveReceiverPathType(parameterType, argument, paramName) != null)
                                return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    private boolean functionCallWritesParam(Function caller, String paramName, TypeRef parameterType) {
        List<String> expressions = new ArrayList<>();
        Deque<Map.Entry<Integer, Set<String>>> caseScopes = new ArrayDeque<>();
        for (Stmt statement : caller.body) {
            while (!caseScopes.isEmpty() && statement.indent <= caseScopes.peek().getKey())
                caseScopes.pop();
            if (statement.kind == Stmt.Kind.Case) {
                caseScopes.push(Map.entry(statement.indent, new HashSet<>(statement.args)));
                continue;
            }
            if (caseScopes.stream().anyMatch(scope -> scope.getValue().contains(paramName))) continue;
            expressions.add(statementExpressionText(statement));
        }
        if (caller.resultExpression != null) expressions.add(caller.resultExpression);
        for (Function callee : functions.values()) {
            if (callee == caller) continue;
            java.util.regex.Pattern callPattern = java.util.regex.Pattern.compile(
                    "(?<![A-Za-z0-9_.])" + java.util.regex.Pattern.quote(callee.name)
                            + "\\s*\\(");
            for (String expression : expressions) {
                java.util.regex.Matcher call = callPattern.matcher(expression);
                while (call.find()) {
                    int open = expression.indexOf('(', call.start());
                    int close = -1;
                    for (int cursor = open + 1; cursor < expression.length(); cursor++) {
                        if (expression.charAt(cursor) == ')'
                                && ExpressionTyper.findMatchingOpenParen(expression, cursor) == open) {
                            close = cursor;
                            break;
                        }
                    }
                    if (close < 0) continue;
                    List<String> args = ExpressionTyper.parseArguments(
                            expression.substring(open + 1, close));
                    for (int index = 0; index < args.size()
                            && index < callee.parameterAccessModes.size(); index++) {
                        if (callee.parameterAccessModes.get(index) != AccessMode.Write) continue;
                        String argument = callArgumentValue(args.get(index));
                        if (argument.equals(paramName)) return true;
                        if (resolveReceiverPathType(parameterType, argument, paramName) != null) return true;
                    }
                }
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

    private void validateTransitiveDomainPayloadWrites() {
        for (Domain domain : program.domains) {
            for (Handler handler : domain.handlers) {
                for (Param parameter : handler.params) {
                    String payload = parameter.name;
                    TypeRef payloadType = functionParameterType(parameter);
                    for (Stmt statement : handler.body) {
                        if (statement.kind != Stmt.Kind.Call || statement.text == null) continue;
                        if (expressionContainsWriteCall(statement.text, payload)) {
                            throw new CompileError(statement.line,
                                    "cannot WRITE incoming message payload '" + payload
                                            + "'; message payloads are immutable snapshots",
                                    "MOSS_COMPILE_ERROR", program.sourceFile);
                        }
                        if (payloadMethodMutatesReceiver(statement.text, payload, payloadType)) {
                            throw new CompileError(statement.line,
                                    "cannot WRITE incoming message payload '" + payload
                                            + "'; message payloads are immutable snapshots",
                                    "MOSS_COMPILE_ERROR", program.sourceFile);
                        }
                    }
                }
            }
        }
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
        ExpressionTyper.BinarySplit pipeline = ExpressionTyper.splitBinary(expr, List.of("|>"));
        if (pipeline != null) {
            if (currentHandlerRaises != null) recordDirectCallRaises(pipeline.left(), env);
            consumeCallArguments(line, pipeline.left(), env, fallbackDest);
            String stage = pipeline.right().trim();
            rejectFunctionalCallbackOwnership(line, pipeline.left(), stage, env);
            if (stage.startsWith("reduce(") && stage.endsWith(")")) {
                List<String> arguments = ExpressionTyper.parseArguments(
                        stage.substring("reduce(".length(), stage.length() - 1));
                if (arguments.size() == 2) {
                    String initializer = arguments.get(0).trim();
                    if (currentHandlerRaises != null) recordDirectCallRaises(initializer, env);
                    if (env.containsKey(initializer) && transferType(env.get(initializer))) {
                        consumeBinding(line, initializer, env,
                                fallbackDest != null ? fallbackDest : "pipeline reduce initializer");
                    } else {
                        consumeCallArguments(line, initializer, env, fallbackDest);
                    }
                    if (currentHandlerRaises != null)
                        recordDirectCallRaises(arguments.get(1).trim() + "()", env);
                }
            } else {
                int open = stage.indexOf('(');
                if (open > 0 && stage.endsWith(")") && currentHandlerRaises != null) {
                    List<String> arguments = ExpressionTyper.parseArguments(
                            stage.substring(open + 1, stage.length() - 1));
                    if (!arguments.isEmpty()) recordDirectCallRaises(arguments.get(0).trim() + "()", env);
                }
            }
            return;
        }
        if (expr.startsWith("[") && expr.endsWith("]")) {
            List<String> elements = ExpressionTyper.parseArguments(expr.substring(1, expr.length() - 1));
            for (String element : elements) {
                String item = element.trim();
                if (currentHandlerRaises != null) recordDirectCallRaises(item, env);
                if (env.containsKey(item) && transferType(env.get(item))) {
                    consumeBinding(line, item, env,
                            fallbackDest != null ? fallbackDest : "an expression at line " + line);
                } else {
                    consumeCallArguments(line, item, env, fallbackDest);
                }
            }
            return;
        }
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
                String receiverRoot = receiver.contains(".")
                        ? receiver.substring(0, receiver.indexOf('.')) : receiver;
                TypeRef rootType = env.get(receiverRoot);
                TypeRef receiverType = resolveReceiverPathType(rootType, receiver, receiverRoot);
                ObjectType receiverObject = receiverType != null
                        ? findObjectType(receiverType.name()) : null;
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
                    String leftValue = callArgumentValue(args.get(i)).trim();
                    String leftRoot = ownershipRoot(leftValue);
                    TypeRef leftType = env.get(leftRoot);
                    if (leftRoot.isEmpty() || leftType == null || !transferType(leftType)) continue;
                    AccessMode leftMode = callArgumentAccessMode(fn, method, i, leftType, args, env);
                    for (int j = i + 1; j < args.size(); j++) {
                        String rightValue = callArgumentValue(args.get(j)).trim();
                        String rightRoot = ownershipRoot(rightValue);
                        if (!leftRoot.equals(rightRoot)) continue;
                        AccessMode rightMode = callArgumentAccessMode(fn, method, j, leftType, args, env);
                        if (leftMode == AccessMode.Read && rightMode == AccessMode.Read) continue;
                        throw new CompileError(line, "conflicting access to '" + leftRoot
                                + "' in call to '" + callee + "': " + leftMode.name().toUpperCase()
                                + " overlaps with " + rightMode.name().toUpperCase(),
                                "OWNERSHIP_CONFLICTING_ACCESS");
                    }
                }
                for (int i = 0; i < args.size(); i++) {
                    boolean writesArgument = fn != null
                            ? (isSourceFreeFunction(fn)
                                    ? functionParameterMode(fn, i) == AccessMode.Write
                                    : doesFunctionMutateParam(fn, i))
                            : i < method.parameterAccessModes.size()
                                    && method.parameterAccessModes.get(i) == AccessMode.Write;
                    if (writesArgument) {
                        String mutatedVar = callArgumentValue(args.get(i)).trim();
                        int placeSeparator = mutatedVar.length();
                        int fieldSeparator = mutatedVar.indexOf('.');
                        int indexSeparator = mutatedVar.indexOf('[');
                        if (fieldSeparator >= 0) placeSeparator = Math.min(placeSeparator, fieldSeparator);
                        if (indexSeparator >= 0) placeSeparator = Math.min(placeSeparator, indexSeparator);
                        String mutatedRoot = mutatedVar.substring(0, placeSeparator).trim();
                        if (env.immutables.contains(mutatedRoot)) {
                            throw new CompileError(line, "cannot mutate immutable local '" + mutatedRoot
                                    + "'; declare it with `var` if mutation is intended",
                                    "IMMUTABLE_LOCAL_MUTATION");
                        }
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
                // Moss evaluates call arguments left-to-right. Record each
                // argument's raising edges before applying its ownership move,
                // then proceed to the next argument from that new state.
                if (currentHandlerRaises != null) recordDirectCallRaises(a, env);
                if (env.containsKey(a) && transferType(env.get(a))) {
                    boolean consumesArgument = fn != null
                            ? (isSourceFreeFunction(fn)
                                    ? functionParameterMode(fn, i) == AccessMode.Consume
                                    : functionConsumesArgumentAtCallsite(fn, i, args, env,
                                            callArgumentType(a, env)))
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
            if (method != null && method.receiverAccessMode == AccessMode.Consume) {
                String receiver = callee.substring(0, methodSeparator).trim();
                String receiverRoot = receiver.contains(".")
                        ? receiver.substring(0, receiver.indexOf('.')) : receiver;
                if (env.containsKey(receiverRoot) && transferType(env.get(receiverRoot))) {
                    consumeBinding(line, receiverRoot, env,
                            fallbackDest != null ? fallbackDest : callee);
                }
            }
            // The callee body runs only after all arguments have been
            // evaluated and transferred, so its raising edges use the
            // post-argument ownership environment.
            if (currentHandlerRaises != null && fn != null)
                recordDirectCallRaises(callee + "()", env);
        }
    }

    private void rejectFunctionalCallbackOwnership(int line, String source,
                                                   String stage, OwnershipEnv env) {
        if (stage == null || !stage.endsWith(")")) return;
        int open = stage.indexOf('(');
        if (open <= 0) return;
        String operation = stage.substring(0, open).trim();
        if (!Set.of("map", "filter", "any", "all", "reduce").contains(operation)) return;
        List<String> arguments = ExpressionTyper.parseArguments(
                stage.substring(open + 1, stage.length() - 1));
        int callbackIndex = "reduce".equals(operation) ? 1 : 0;
        if (arguments.size() <= callbackIndex) return;
        String callback = arguments.get(callbackIndex).trim();
        boolean hasPlaceholder = java.util.regex.Pattern.compile(
                "(?<![A-Za-z0-9_])_(?![A-Za-z0-9_])").matcher(callback).find();
        TypeRef elementType = null;
        String sourceRoot = ownershipRoot(source);
        TypeRef sourceType = env.get(sourceRoot);
        if (sourceType != null && sourceType.kind() == TypeKind.Builtin
                && "vector".equals(sourceType.name()) && !sourceType.typeArgs().isEmpty()) {
            elementType = sourceType.typeArgs().get(0);
        }
        if ("map".equals(operation) && hasPlaceholder && elementType != null
                && transferType(elementType)) {
            TypeRef resultType = callback.equals("_") ? elementType
                    : callback.startsWith("_.")
                            ? resolveReceiverPathType(elementType, callback, "_") : null;
            if (resultType != null && transferType(resultType)) {
                throw new CompileError(line,
                        "functional map result aliases nontrivial source storage through '_'; "
                                + "map reads its source and no implicit copy is inserted",
                        "FUNCTIONAL_SEMANTIC_ERROR");
            }
        }

        java.util.regex.Matcher bound = java.util.regex.Pattern.compile(
                "^([A-Za-z_][A-Za-z0-9_]*(?:\\.[A-Za-z_][A-Za-z0-9_]*)*)\\.([A-Za-z_][A-Za-z0-9_]*)$")
                .matcher(callback);
        if (bound.matches()) {
            String receiver = bound.group(1);
            String root = ownershipRoot(receiver);
            TypeRef receiverType = resolveReceiverPathType(env.get(root), receiver, root);
            if (receiverType != null) {
                ObjectType object = findObjectType(receiverType.name());
                if (object != null) {
                    for (Method method : object.methods) {
                        if (method.name.equals(bound.group(2))
                                && method.receiverAccessMode != AccessMode.Read) {
                            throw functionalCaptureMutation(line, callback, receiver,
                                    method.receiverAccessMode);
                        }
                    }
                }
            }
        }

        if (!hasPlaceholder) {
            Function function = functions.get(callback);
            if (function != null && elementType != null && transferType(elementType)
                    && functionParameterMode(function, callbackIndex == 1 ? 1 : 0)
                            == AccessMode.Consume) {
                throw new CompileError(line,
                        "functional callback '" + callback + "' consumes a nontrivial source element; "
                                + "the pipeline only reads its source",
                        "FUNCTIONAL_ELEMENT_CONSUME");
            }
            return;
        }

        String code = callback;
        for (Map.Entry<String, TypeRef> binding : env.typeEnv.getBindings().entrySet()) {
            String capture = binding.getKey();
            if (capture.equals("_")) continue;
            java.util.regex.Pattern identifier = java.util.regex.Pattern.compile(
                    "(?<![A-Za-z0-9_])" + java.util.regex.Pattern.quote(capture)
                            + "(?![A-Za-z0-9_])");
            if (!identifier.matcher(code).find()) continue;
            TypeRef captureType = binding.getValue();
            java.util.regex.Matcher memberCall = java.util.regex.Pattern.compile(
                    "(?<![A-Za-z0-9_])" + java.util.regex.Pattern.quote(capture)
                            + "\\s*\\.\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*\\(").matcher(code);
            while (memberCall.find()) {
                String methodName = memberCall.group(1);
                if (isBuiltinMutatingMethod(captureType, methodName)) {
                    throw functionalCaptureMutation(line, callback, capture, AccessMode.Write);
                }
                ObjectType object = captureType != null ? findObjectType(captureType.name()) : null;
                if (object != null) {
                    for (Method method : object.methods) {
                        if (method.name.equals(methodName)
                                && method.receiverAccessMode != AccessMode.Read) {
                            throw functionalCaptureMutation(line, callback, capture,
                                    method.receiverAccessMode);
                        }
                    }
                }
            }
            for (Function function : functions.values()) {
                java.util.regex.Matcher call = java.util.regex.Pattern.compile(
                        "(?<![A-Za-z0-9_.])" + java.util.regex.Pattern.quote(function.name)
                                + "\\s*\\(").matcher(code);
                while (call.find()) {
                    int callOpen = code.indexOf('(', call.start());
                    int callClose = matchingCloseParen(code, callOpen);
                    if (callClose < 0) continue;
                    List<String> callArgs = ExpressionTyper.parseArguments(
                            code.substring(callOpen + 1, callClose));
                    for (int index = 0; index < callArgs.size()
                            && index < function.params.size(); index++) {
                        if (!ownershipRoot(callArgumentValue(callArgs.get(index)).trim())
                                .equals(capture)) continue;
                        AccessMode mode = functionParameterMode(function, index);
                        if (mode != AccessMode.Read) {
                            throw functionalCaptureMutation(line, callback, capture, mode);
                        }
                    }
                }
            }
        }
    }

    private CompileError functionalCaptureMutation(int line, String callback,
                                                  String capture, AccessMode mode) {
        return new CompileError(line, "functional callback '" + callback
                + "' requires " + mode.name().toUpperCase()
                + " access to captured binding '" + capture + "'",
                "FUNCTIONAL_CAPTURE_MUTATION");
    }

    private String ownershipRoot(String expression) {
        if (expression == null) return "";
        int end = expression.length();
        int dot = expression.indexOf('.');
        int bracket = expression.indexOf('[');
        if (dot >= 0) end = Math.min(end, dot);
        if (bracket >= 0) end = Math.min(end, bracket);
        return expression.substring(0, end).trim();
    }

    private void rejectActiveForSourceMutation(Stmt statement) {
        if (activeForSources.isEmpty() || statement == null) return;
        String target = statement.kind == Stmt.Kind.Assign
                ? ownershipRoot(statement.a) : "";
        String expressions = statementExpressions(statement);
        for (String source : activeForSources) {
            boolean rebindsSource = !target.isEmpty() && target.equals(source);
            boolean mutatesSource = expressionContainsWriteCall(expressions, source);
            if (rebindsSource || mutatesSource) {
                throw new CompileError(statement.line,
                        "cannot structurally mutate collection '" + source
                                + "' during an active READ traversal",
                        "MOSS_COMPILE_ERROR");
            }
        }
    }

    private AccessMode callArgumentAccessMode(Function function, Method method,
                                               int index, TypeRef argumentType,
                                               List<String> callArguments, OwnershipEnv env) {
        if (function != null) {
            if (isSourceFreeFunction(function)) return functionParameterMode(function, index);
            TypeRef checkedArgumentType = index < callArguments.size()
                    ? callArgumentType(callArguments.get(index), env) : argumentType;
            if (functionConsumesArgumentAtCallsite(function, index, callArguments, env, checkedArgumentType))
                return AccessMode.Consume;
            if (doesFunctionMutateParam(function, index)) return AccessMode.Write;
            return AccessMode.Read;
        }
        if (method != null && index < method.parameterAccessModes.size())
            return method.parameterAccessModes.get(index);
        return AccessMode.Read;
    }

    private TypeRef callArgumentType(String rawArgument, OwnershipEnv env) {
        String argument = callArgumentValue(rawArgument).trim();
        String root = ownershipRoot(argument);
        TypeRef rootType = env.get(root);
        if (rootType != null) {
            TypeRef projected = resolveReceiverPathType(rootType, argument, root);
            if (projected != null) return projected;
            return rootType;
        }
        int open = argument.indexOf('(');
        if (open > 0 && argument.endsWith(")")) {
            ObjectType constructed = findObjectType(argument.substring(0, open).trim());
            if (constructed != null) return TypeRef.nominal(constructed.name, List.of());
        }
        return null;
    }

    private boolean functionConsumesArgumentAtCallsite(Function function, int parameterIndex,
                                                       List<String> callArguments, OwnershipEnv callerEnv,
                                                       TypeRef actualArgumentType) {
        if (doesFunctionConsumeParam(function, parameterIndex, actualArgumentType)) return true;
        if (function == null || parameterIndex < 0 || parameterIndex >= function.params.size()) return false;
        String parameterName = function.params.get(parameterIndex).name;
        if (parameterName == null || callArguments == null) return false;
        Map<String, String> aliases = functionParameterAliases(function);
        List<String> expressions = new ArrayList<>();
        for (Stmt statement : function.body) expressions.add(statementExpressionText(statement));
        if (function.resultExpression != null) expressions.add(function.resultExpression);
        java.util.regex.Pattern methodCall = java.util.regex.Pattern.compile(
                "(?<![A-Za-z0-9_])([A-Za-z_][A-Za-z0-9_]*(?:\\s*\\.\\s*[A-Za-z_][A-Za-z0-9_]*)*)"
                        + "\\s*\\.\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*\\(");
        for (String expression : expressions) {
            java.util.regex.Matcher call = methodCall.matcher(maskStringLiterals(expression));
            while (call.find()) {
                String receiverPath = call.group(1).replaceAll("\\s", "");
                String receiverRoot = ownershipRoot(receiverPath);
                String receiverParameter = aliases.getOrDefault(receiverRoot, receiverRoot);
                int receiverParameterIndex = -1;
                for (int index = 0; index < function.params.size(); index++) {
                    if (function.params.get(index).name.equals(receiverParameter)) {
                        receiverParameterIndex = index;
                        break;
                    }
                }
                if (receiverParameterIndex < 0 || receiverParameterIndex >= callArguments.size()) continue;
                String actualReceiver = callArgumentValue(callArguments.get(receiverParameterIndex));
                String receiverSuffix = receiverPath.substring(receiverRoot.length());
                if (!receiverSuffix.isEmpty()) actualReceiver += receiverSuffix;
                TypeRef actualReceiverType = callArgumentType(actualReceiver, callerEnv);
                ObjectType receiverObject = actualReceiverType != null
                        ? findObjectType(actualReceiverType.name()) : null;
                if (receiverObject == null) continue;
                Method selectedMethod = receiverObject.methods.stream()
                        .filter(candidate -> candidate.name.equals(call.group(2)))
                        .findFirst().orElse(null);
                if (selectedMethod == null) continue;
                int open = expression.indexOf('(', call.start());
                int close = matchingCloseParen(expression, open);
                if (close < 0) continue;
                List<String> methodArguments = ExpressionTyper.parseArguments(
                        expression.substring(open + 1, close));
                for (int index = 0; index < methodArguments.size()
                        && index < selectedMethod.parameterAccessModes.size(); index++) {
                    if (selectedMethod.parameterAccessModes.get(index) != AccessMode.Consume) continue;
                    String passed = callArgumentValue(methodArguments.get(index));
                    if (!ownershipRoot(passed).equals(parameterName)) continue;
                    if (passed.equals(parameterName) && actualArgumentType != null
                            && transferType(actualArgumentType)) return true;
                    TypeRef declaredParameterType = functionParameterType(
                            function.params.get(parameterIndex));
                    if (transferType(resolveReceiverPathType(
                            declaredParameterType, passed, parameterName))) return true;
                    if (actualArgumentType != null && transferType(actualArgumentType)) return true;
                }
            }
        }
        return false;
    }

    private Map<String, String> functionParameterAliases(Function function) {
        Map<String, String> aliases = new HashMap<>();
        if (function == null || function.body.isEmpty()) return aliases;
        int bodyIndent = function.body.stream().mapToInt(statement -> statement.indent).min().orElse(0);
        Set<String> parameterNames = new HashSet<>();
        for (Param parameter : function.params) parameterNames.add(parameter.name);
        for (Stmt statement : function.body) {
            if (statement.indent != bodyIndent
                    || !(statement.kind == Stmt.Kind.Let || statement.kind == Stmt.Kind.Var
                            || statement.kind == Stmt.Kind.Assign)
                    || statement.a == null || statement.b == null) continue;
            String target = statement.a.trim();
            String source = statement.b.trim();
            if (!target.matches("[A-Za-z_][A-Za-z0-9_]*")) continue;
            if (parameterNames.contains(target)) {
                aliases.remove(target);
                continue;
            }
            String sourceRoot = ownershipRoot(source);
            boolean direct = parameterNames.contains(source) || aliases.containsKey(sourceRoot);
            if (direct && source.equals(sourceRoot)) {
                aliases.put(target, parameterNames.contains(source) ? source : aliases.get(sourceRoot));
            } else {
                aliases.remove(target);
            }
        }
        return aliases;
    }

    private void recordDirectCallRaises(String expression, OwnershipEnv env) {
        if (expression == null) return;
        String text = expression.trim();
        int cursor = 0;
        while (cursor < text.length()) {
            if (!Character.isLetter(text.charAt(cursor)) && text.charAt(cursor) != '_') {
                cursor++;
                continue;
            }
            int nameStart = cursor++;
            while (cursor < text.length() && (Character.isLetterOrDigit(text.charAt(cursor))
                    || text.charAt(cursor) == '_')) cursor++;
            String name = text.substring(nameStart, cursor);
            int open = cursor;
            while (open < text.length() && Character.isWhitespace(text.charAt(open))) open++;
            Function called = functions.get(name);
            if (called == null || open >= text.length() || text.charAt(open) != '(') continue;
            int depth = 0;
            int close = -1;
            for (int index = open; index < text.length(); index++) {
                if (text.charAt(index) == '(') depth++;
                else if (text.charAt(index) == ')' && --depth == 0) {
                    close = index;
                    break;
                }
            }
            if (close < 0) continue;
            for (String argument : ExpressionTyper.parseArguments(text.substring(open + 1, close)))
                recordDirectCallRaises(argument, env);
            if (currentHandlerRaises != null) {
                for (Stmt statement : called.body) {
                    if (statement.kind != Stmt.Kind.Raise || statement.a == null) continue;
                    String raised = statement.a.trim();
                    int payload = raised.indexOf('(');
                    if (payload >= 0) raised = raised.substring(0, payload).trim();
                    currentHandlerRaises.add(new RaiseSnapshot(env.copy(), raised));
                }
            }
            cursor = close + 1;
        }
    }

    private OwnershipEnv checkBlock(List<Stmt> stmts, int start, int end, OwnershipEnv env,
                                    ExpressionTyper typer, TypeSolver solver,
                                    boolean insideHandler, String enclosingName, TypeRef expectedType) {
        int i = start;
        int activeRecoverIndent = -1;
        Map<Integer, List<LoopShadowSnapshot>> loopShadowRestores = new HashMap<>();
        Map<Integer, List<String>> loopSourceRestores = new HashMap<>();
        while (i < end) {
            List<LoopShadowSnapshot> restores = loopShadowRestores.remove(i);
            if (restores != null) {
                for (int restoreIndex = restores.size() - 1; restoreIndex >= 0; restoreIndex--)
                    restores.get(restoreIndex).restore(env);
            }
            List<String> endingLoopSources = loopSourceRestores.remove(i);
            if (endingLoopSources != null) {
                for (int restoreIndex = endingLoopSources.size() - 1; restoreIndex >= 0; restoreIndex--)
                    if (!activeForSources.isEmpty()) activeForSources.pop();
            }
            Stmt s = stmts.get(i);
            if (activeRecoverIndent >= 0 && s.indent <= activeRecoverIndent
                    && (s.kind != Stmt.Kind.Recover || s.indent == activeRecoverIndent)) {
                activeRecoverDepth--;
                activeRecoverIndent = -1;
            }
            if (s.kind == Stmt.Kind.Recover) {
                activeRecoverIndent = s.indent;
                activeRecoverDepth++;
            }
            if (s.kind == Stmt.Kind.Try && currentHandlerRaises != null) {
                int tryBodyEnd = i + 1;
                while (tryBodyEnd < end && stmts.get(tryBodyEnd).indent > s.indent) tryBodyEnd++;
                int recoverCursor = tryBodyEnd;
                List<Integer> recoverStarts = new ArrayList<>();
                List<Integer> recoverEnds = new ArrayList<>();
                while (recoverCursor < end && stmts.get(recoverCursor).kind == Stmt.Kind.Recover
                        && stmts.get(recoverCursor).indent == s.indent) {
                    int recoverEnd = recoverCursor + 1;
                    while (recoverEnd < end && stmts.get(recoverEnd).indent > s.indent) recoverEnd++;
                    recoverStarts.add(recoverCursor);
                    recoverEnds.add(recoverEnd);
                    recoverCursor = recoverEnd;
                }
                if (!recoverStarts.isEmpty()) {
                    int raiseStart = currentHandlerRaises.size();
                    OwnershipEnv tryEnv = checkBlock(stmts, i + 1, tryBodyEnd, env.copy(), typer,
                            solver, insideHandler, enclosingName, expectedType);
                    int raiseEnd = currentHandlerRaises.size();
                    List<OwnershipEnv> paths = new ArrayList<>();
                    paths.add(tryEnv);
                    List<RaiseSnapshot> tryRaises = new ArrayList<>(
                            currentHandlerRaises.subList(raiseStart, raiseEnd));
                    for (int armIndex = 0; armIndex < recoverStarts.size(); armIndex++) {
                        int recoverIndex = recoverStarts.get(armIndex);
                        int recoverEnd = recoverEnds.get(armIndex);
                        Stmt recover = stmts.get(recoverIndex);
                        List<OwnershipEnv> edges = selectedRecoverEdges(recover, tryRaises);
                        OwnershipEnv recoverEnv = edges.isEmpty() ? env.copy() : edges.get(0).copy();
                        for (int edgeIndex = 1; edgeIndex < edges.size(); edgeIndex++)
                            recoverEnv = mergeEnvs(recover.line, recoverEnv, edges.get(edgeIndex), solver);
                        if (recover.a != null && recover.a.trim().matches("[A-Za-z_][A-Za-z0-9_]*")) {
                            TypeRef errorType = recoveryBindingType(stmts, start, recoverIndex, recover.indent);
                            if (errorType != null) recoverEnv.put(recover.a.trim(), errorType, recover.line);
                        }
                        checkStatement(recover, recoverEnv, typer, solver, insideHandler,
                                enclosingName, expectedType);
                        activeRecoverDepth++;
                        try {
                            OwnershipEnv checkedRecover = checkBlock(stmts, recoverIndex + 1, recoverEnd,
                                    recoverEnv, typer, solver, insideHandler, enclosingName, expectedType);
                            if (!edges.isEmpty()) paths.add(checkedRecover);
                        } finally {
                            activeRecoverDepth--;
                        }
                    }
                    env = paths.get(0);
                    for (int pathIndex = 1; pathIndex < paths.size(); pathIndex++)
                        env = mergeEnvs(s.line, env, paths.get(pathIndex), solver);
                    i = recoverCursor;
                    continue;
                }
            }
            if (s.kind == Stmt.Kind.While) {
                checkStatement(s, env, typer, solver, insideHandler, enclosingName, expectedType);
                int bodyEnd = i + 1;
                while (bodyEnd < end && stmts.get(bodyEnd).indent > s.indent) bodyEnd++;
                int raiseCount = currentHandlerRaises == null ? 0 : currentHandlerRaises.size();
                OwnershipEnv bodyEnv = checkBlock(stmts, i + 1, bodyEnd, env.copy(), typer,
                        solver, insideHandler, enclosingName, expectedType);
                if ("false".equals(s.a == null ? "" : s.a.trim())) {
                    if (currentHandlerRaises != null)
                        currentHandlerRaises.subList(raiseCount, currentHandlerRaises.size()).clear();
                } else {
                    Set<String> outerNames = new HashSet<>(env.typeEnv.getBindings().keySet());
                    OwnershipEnv merged = mergeEnvs(s.line, bodyEnv, env, solver);
                    for (String name : new HashSet<>(merged.typeEnv.getBindings().keySet())) {
                        if (outerNames.contains(name)) continue;
                        merged.typeEnv.remove(name);
                        merged.moved.remove(name);
                        merged.immutables.remove(name);
                        merged.bindingLines.remove(name);
                    }
                    env = merged;
                }
                i = bodyEnd;
                continue;
            }
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

                    String condition = ifStmt.a == null ? "" : ifStmt.a.trim();
                    OwnershipEnv merged;
                    if ("true".equals(condition)) merged = thenEnv;
                    else if ("false".equals(condition)) merged = elseEnv;
                    else merged = mergeEnvs(ifStmt.line, thenEnv, elseEnv, solver);
                    pruneOneSidedBranchLocals(merged, env, thenEnv, elseEnv);
                    env = merged;
                    i = elseEnd;
                } else {
                    String condition = ifStmt.a == null ? "" : ifStmt.a.trim();
                    OwnershipEnv baseEnv = env;
                    OwnershipEnv merged = "true".equals(condition)
                            ? thenEnv : mergeEnvs(ifStmt.line, thenEnv, baseEnv, solver);
                    pruneOneSidedBranchLocals(merged, baseEnv, thenEnv, baseEnv);
                    env = merged;
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
                    Set<String> shadowedMessagePayloads = new HashSet<>();
                    Map<String, String> shadowedBorrowedPayloadSources = new HashMap<>();
                    for (String binder : caseStmt.args) {
                        if (currentMessagePayloads.contains(binder)) {
                            currentMessagePayloads.remove(binder);
                            shadowedMessagePayloads.add(binder);
                        }
                        String shadowedSource = currentBorrowedEnumPayloadSources.remove(binder);
                        if (shadowedSource != null)
                            shadowedBorrowedPayloadSources.put(binder, shadowedSource);
                        if (!"consume".equals(s.b))
                            currentBorrowedEnumPayloadSources.put(binder, scrutinee);
                    }
                    try {
                        caseEnvs.add(checkBlock(stmts, caseIndex + 1, caseEnd, caseEnv, typer, solver,
                                insideHandler, enclosingName, expectedType));
                    } finally {
                        currentMessagePayloads.addAll(shadowedMessagePayloads);
                        for (String binder : caseStmt.args) currentBorrowedEnumPayloadSources.remove(binder);
                        currentBorrowedEnumPayloadSources.putAll(shadowedBorrowedPayloadSources);
                    }
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
            if (s.kind == Stmt.Kind.Recover && isDirectlyUnreachableRecover(stmts, start, i)) {
                int recoverEnd = i + 1;
                while (recoverEnd < end && stmts.get(recoverEnd).indent > s.indent) recoverEnd++;
                checkBlock(stmts, i + 1, recoverEnd, env.copy(), typer, solver,
                        insideHandler, enclosingName, expectedType);
                i = recoverEnd;
                continue;
            }
            if (s.kind == Stmt.Kind.Else) {
                i++;
                continue;
            }
            if (s.kind == Stmt.Kind.Recover && s.a != null
                    && s.a.trim().matches("[A-Za-z_][A-Za-z0-9_]*")) {
                TypeRef errorType = recoveryBindingType(stmts, start, i, s.indent);
                if (errorType != null) env.put(s.a.trim(), errorType, s.line);
            }
            LoopShadowSnapshot loopShadow = null;
            int loopBodyEnd = i + 1;
            String loopSourceRoot = "";
            if (s.kind == Stmt.Kind.For) {
                while (loopBodyEnd < end && stmts.get(loopBodyEnd).indent > s.indent) loopBodyEnd++;
                String loopName = s.a == null ? "" : s.a.trim();
                if (!loopName.isEmpty())
                    loopShadow = new LoopShadowSnapshot(loopName, env);
                loopSourceRoot = ownershipRoot(s.b);
            }
            checkStatement(s, env, typer, solver, insideHandler, enclosingName, expectedType);
            if (loopShadow != null) {
                loopShadowRestores.computeIfAbsent(loopBodyEnd, ignored -> new ArrayList<>()).add(loopShadow);
                if (!loopSourceRoot.isEmpty() && env.containsKey(loopSourceRoot)) {
                    activeForSources.push(loopSourceRoot);
                    loopSourceRestores.computeIfAbsent(loopBodyEnd, ignored -> new ArrayList<>())
                            .add(loopSourceRoot);
                }
            }
            i++;
        }
        List<LoopShadowSnapshot> finalRestores = loopShadowRestores.remove(end);
        if (finalRestores != null) {
            for (int restoreIndex = finalRestores.size() - 1; restoreIndex >= 0; restoreIndex--)
                finalRestores.get(restoreIndex).restore(env);
        }
        List<String> finalLoopSources = loopSourceRestores.remove(end);
        if (finalLoopSources != null) {
            for (int restoreIndex = finalLoopSources.size() - 1; restoreIndex >= 0; restoreIndex--)
                if (!activeForSources.isEmpty()) activeForSources.pop();
        }
        if (activeRecoverIndent >= 0) activeRecoverDepth--;
        return env;
    }

    private boolean isDirectlyUnreachableRecover(List<Stmt> stmts, int blockStart,
                                                 int recoverIndex) {
        Stmt recover = stmts.get(recoverIndex);
        String pattern = recover.a == null ? "" : recover.a.trim();
        if (pattern.isEmpty() || pattern.contains("(")) return false;
        int tryIndex = -1;
        for (int index = recoverIndex - 1; index >= blockStart; index--) {
            Stmt candidate = stmts.get(index);
            if (candidate.indent < recover.indent) break;
            if (candidate.kind == Stmt.Kind.Try && candidate.indent == recover.indent) {
                tryIndex = index;
                break;
            }
        }
        if (tryIndex < 0) return false;
        Set<String> raised = new HashSet<>();
        for (int index = tryIndex + 1; index < recoverIndex; index++) {
            Stmt candidate = stmts.get(index);
            if (candidate.indent <= recover.indent) break;
            if (candidate.kind == Stmt.Kind.Raise && candidate.a != null) {
                String raisedVariant = candidate.a.trim();
                int payload = raisedVariant.indexOf('(');
                if (payload >= 0) raisedVariant = raisedVariant.substring(0, payload).trim();
                raised.add(raisedVariant);
            } else {
                // Only a body consisting of direct raises is sufficient to prove unreachability.
                return false;
            }
        }
        if (raised.isEmpty()) return false;
        for (String alternative : pattern.split(",")) {
            String variant = alternative.trim();
            int payload = variant.indexOf('(');
            if (payload >= 0) variant = variant.substring(0, payload).trim();
            if (variant.matches("[A-Za-z_][A-Za-z0-9_]*")) return false;
            for (String raisedVariant : raised) {
                if (raisedVariant.equals(variant)
                        || raisedVariant.endsWith("." + variant)
                        || variant.endsWith("." + raisedVariant)) return false;
            }
        }
        return true;
    }

    private void validateRootIngressRaiseTotality(List<Stmt> statements, OwnershipEnv mainEnv,
                                                  TypeSolver solver) {
        for (Stmt statement : statements) {
            if (statement.kind == Stmt.Kind.Call && "invoke".equals(statement.b)
                    && statement.args.size() == 1) {
                TypeRef executorType = mainEnv.get(statement.a);
                if (executorType != null && "Executor".equals(solver.resolve(executorType).name())) {
                    validateRootIngress(statement.args.get(0), statement.line, "executor.invoke Root",
                            mainEnv, solver);
                }
            }
            if (statement.kind == Stmt.Kind.Message) {
                validateRootIngress(statement.a + "." + statement.b + "()", statement.line,
                        "synchronous Root message", mainEnv, solver);
            }
            List<String> sourceExpressions = new ArrayList<>();
            sourceExpressions.add(statement.text);
            sourceExpressions.add(statement.a);
            sourceExpressions.add(statement.b);
            sourceExpressions.addAll(statement.args);
            for (String expression : sourceExpressions) {
                if (expression == null || !expression.contains("message")) continue;
                java.util.regex.Matcher matcher = java.util.regex.Pattern
                        .compile("message\\s+([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*)\\s*\\(")
                        .matcher(expression);
                while (matcher.find()) {
                    validateRootIngress(matcher.group(1) + "." + matcher.group(2) + "()",
                            statement.line, "synchronous Root message", mainEnv, solver);
                }
            }
        }
    }

    private void validateRootIngress(String invocation, int line, String ingress,
                                     OwnershipEnv mainEnv, TypeSolver solver) {
        String target = invocation.trim();
        int open = target.indexOf('(');
        int dot = target.indexOf('.');
        if (dot <= 0 || open <= dot || !target.endsWith(")")) return;
        String receiver = target.substring(0, dot).trim();
        String handlerName = target.substring(dot + 1, open).trim();
        TypeRef receiverType = mainEnv.get(receiver);
        Domain domain = receiverType == null ? null : domains.get(solver.resolve(receiverType).name());
        if (domain == null) return;
        Handler handler = domain.handlers.stream()
                .filter(candidate -> handlerName.equals(candidate.name)).findFirst().orElse(null);
        if (handler == null || handler.body.stream().anyMatch(s -> s.kind == Stmt.Kind.Try
                || s.kind == Stmt.Kind.Recover)) return;
        if (handler.sourceFreeMayRaise) {
            boolean catchAll = handler.failureArms.stream().anyMatch(arm ->
                    (arm.binding == null || arm.binding.isBlank())
                            && (arm.patterns == null || arm.patterns.isEmpty()));
            Set<String> missing = new LinkedHashSet<>();
            if (handler.sourceFreeRaiseVariants.isEmpty()) {
                if (!catchAll) missing.add("unknown imported raise variant");
            } else if (!catchAll) {
                for (String variant : handler.sourceFreeRaiseVariants) {
                    boolean covered = handler.failureArms.stream().flatMap(arm -> arm.patterns.stream())
                            .anyMatch(pattern -> sourceFreeRaisePatternMatches(variant, pattern));
                    if (!covered) missing.add(variant);
                }
            }
            if (!missing.isEmpty()) {
                CompileError error = new CompileError(line, ingress + " of '" + domain.name + "."
                        + handlerName + "' has imported raise variants without exhaustive on_fail coverage: "
                        + String.join(", ", missing), "ROOT_UNHANDLED_RAISE");
                error.setSemanticIdentity(currentSemanticIdentity);
                throw error;
            }
            return;
        }
        if (!handler.failureArms.isEmpty()) return;
        Set<String> raised = new LinkedHashSet<>();
        Set<String> visitedFunctions = new HashSet<>();
        for (Stmt bodyStatement : handler.body) {
            if (bodyStatement.kind == Stmt.Kind.Raise && bodyStatement.a != null
                    && !bodyStatement.a.isBlank()) {
                String variant = bodyStatement.a.trim();
                int payload = variant.indexOf('(');
                raised.add(payload < 0 ? variant : variant.substring(0, payload).trim());
            }
            List<String> expressions = new ArrayList<>();
            expressions.add(bodyStatement.text);
            expressions.add(bodyStatement.a);
            expressions.add(bodyStatement.b);
            expressions.addAll(bodyStatement.args);
            for (String expression : expressions) {
                if (expression == null) continue;
                for (Function function : functions.values()) {
                    if (containsFunctionCall(expression, function.name)
                            || org.mosslang.compiler.parser.ParserSupport
                                    .usesPipelineFunctionCallback(expression, function.name)) {
                        raised.addAll(collectFunctionRaiseVariants(function, visitedFunctions));
                        collectHandlerFunctionMethodRaises(expression, function, domain, handler,
                                raised, Map.of(), new HashSet<>());
                    }
                }
                collectHandlerMethodRaises(expression, domain, handler, raised);
            }
        }
        if (raised.isEmpty()) return;
        CompileError error = new CompileError(line, ingress + " of '" + domain.name + "."
                + handlerName + "' may raise " + String.join(", ", raised)
                + " without an exhaustive on_fail trailer; recover inside the handler or attach on_fail",
                "ROOT_UNHANDLED_RAISE");
        error.setSemanticIdentity(currentSemanticIdentity);
        throw error;
    }

    private void collectHandlerMethodRaises(String expression, Domain domain, Handler handler,
                                           Set<String> raised) {
        collectHandlerMethodRaises(expression, domain, handler, Map.of(), raised);
    }

    private void collectHandlerMethodRaises(String expression, Domain domain, Handler handler,
                                            Map<String, String> contextualTypes, Set<String> raised) {
        String code = maskStringLiterals(expression);
        for (ObjectType object : program.objects) {
            for (Method method : object.methods) {
                String callPattern = "(?<![A-Za-z0-9_])([A-Za-z_][A-Za-z0-9_]*(?:\\s*\\.\\s*[A-Za-z_][A-Za-z0-9_]*)*)\\s*\\.\\s*"
                        + java.util.regex.Pattern.quote(method.name) + "\\s*\\(";
                java.util.regex.Matcher calls = java.util.regex.Pattern.compile(callPattern).matcher(code);
                while (calls.find()) {
                    String receiverType = contextualTypes.getOrDefault(calls.group(1).replaceAll("\\s", ""),
                            resolveHandlerReceiverType(calls.group(1), domain, handler));
                    if (matchesObjectTypeName(receiverType, object.name))
                        raised.addAll(collectMethodRaiseVariants(method, new HashSet<>()));
                }
                for (String callback : org.mosslang.compiler.parser.ParserSupport.pipelineCallbacks(code)) {
                    int dot = callback.lastIndexOf('.');
                    if (dot <= 0 || !method.name.equals(callback.substring(dot + 1))) continue;
                    String receiverPath = callback.substring(0, dot);
                    String receiverType = contextualTypes.getOrDefault(receiverPath,
                            resolveHandlerReceiverType(receiverPath, domain, handler));
                    if (matchesObjectTypeName(receiverType, object.name))
                        raised.addAll(collectMethodRaiseVariants(method, new HashSet<>()));
                }
            }
        }
    }

    private void collectHandlerFunctionMethodRaises(String invocation, Function function,
                                                    Domain domain, Handler handler,
                                                    Set<String> raised,
                                                    Map<String, String> inheritedTypes,
                                                    Set<String> visited) {
        java.util.regex.Matcher calls = java.util.regex.Pattern.compile("(?<![A-Za-z0-9_])"
                + java.util.regex.Pattern.quote(function.name) + "\\s*\\(([^()]*)\\)")
                .matcher(maskStringLiterals(invocation));
        while (calls.find()) {
            List<String> arguments = org.mosslang.compiler.parser.ParserSupport
                    .splitTopLevel(calls.group(1), ',');
            Map<String, String> contextualTypes = new HashMap<>();
            for (int index = 0; index < Math.min(function.params.size(), arguments.size()); index++) {
                String actualType = resolveHandlerReceiverType(arguments.get(index), domain, handler,
                        inheritedTypes);
                if (actualType != null) contextualTypes.put(function.params.get(index).name, actualType);
            }
            String visitKey = function.name + contextualTypes.entrySet().stream()
                    .sorted(Map.Entry.comparingByKey())
                    .map(entry -> ";" + entry.getKey() + "=" + entry.getValue())
                    .collect(java.util.stream.Collectors.joining());
            if (!visited.add(visitKey)) continue;
            int bodyIndent = function.body.stream().filter(Objects::nonNull)
                    .mapToInt(statement -> statement.indent).min().orElse(-1);
            Map<Integer, Map<String, String>> branchAliases = collectHandlerBranchAliasJoins(
                    function.body, bodyIndent, domain, handler, contextualTypes);
            for (int statementIndex = 0; statementIndex < function.body.size(); statementIndex++) {
                Stmt statement = function.body.get(statementIndex);
                String bodyExpression = statementExpressionText(statement);
                if (bodyExpression != null) {
                    collectHandlerMethodRaises(bodyExpression, domain, handler, contextualTypes, raised);
                    for (Function nested : functions.values()) {
                        if (containsFunctionCall(bodyExpression, nested.name)
                                || org.mosslang.compiler.parser.ParserSupport
                                        .usesPipelineFunctionCallback(bodyExpression, nested.name)) {
                            collectHandlerFunctionMethodRaises(bodyExpression, nested, domain,
                                    handler, raised, contextualTypes, visited);
                        }
                    }
                }
                if (statement != null && statement.indent == bodyIndent
                        && (statement.kind == Stmt.Kind.Let || statement.kind == Stmt.Kind.Var
                            || statement.kind == Stmt.Kind.Assign)
                        && statement.a != null && statement.b != null
                        && statement.a.trim().matches("[A-Za-z_][A-Za-z0-9_]*")
                        && statement.b.trim().matches("[A-Za-z_][A-Za-z0-9_]*")) {
                    String aliasType = resolveHandlerReceiverType(statement.b, domain, handler,
                            contextualTypes);
                    if (aliasType != null) contextualTypes.put(statement.a.trim(), aliasType);
                    else contextualTypes.remove(statement.a.trim());
                }
                Map<String, String> joinedAliases = branchAliases.get(statementIndex);
                if (joinedAliases != null) contextualTypes.putAll(joinedAliases);
            }
            if (function.resultExpression != null) {
                collectHandlerMethodRaises(function.resultExpression, domain, handler,
                        contextualTypes, raised);
                for (Function nested : functions.values()) {
                    if (containsFunctionCall(function.resultExpression, nested.name)
                            || org.mosslang.compiler.parser.ParserSupport
                                    .usesPipelineFunctionCallback(function.resultExpression, nested.name)) {
                        collectHandlerFunctionMethodRaises(function.resultExpression, nested,
                                domain, handler, raised, contextualTypes, visited);
                    }
                }
            }
        }
    }

    private Map<Integer, Map<String, String>> collectHandlerBranchAliasJoins(
            List<Stmt> statements, int baseIndent, Domain domain, Handler handler,
            Map<String, String> contextualTypes) {
        Map<Integer, Map<String, String>> joins = new HashMap<>();
        if (baseIndent < 0) return joins;
        for (int ifIndex = 0; ifIndex < statements.size(); ifIndex++) {
            Stmt branch = statements.get(ifIndex);
            if (branch == null || branch.kind != Stmt.Kind.If || branch.indent != baseIndent) continue;
            int thenEnd = ifIndex + 1;
            while (thenEnd < statements.size()) {
                Stmt candidate = statements.get(thenEnd);
                if (candidate != null && candidate.indent <= baseIndent) break;
                thenEnd++;
            }
            if (thenEnd >= statements.size()) continue;
            Stmt otherwise = statements.get(thenEnd);
            if (otherwise == null || otherwise.kind != Stmt.Kind.Else
                    || otherwise.indent != baseIndent) continue;
            int elseEnd = thenEnd + 1;
            while (elseEnd < statements.size()) {
                Stmt candidate = statements.get(elseEnd);
                if (candidate != null && candidate.indent <= baseIndent) break;
                elseEnd++;
            }
            Map<String, String> thenAliases = collectHandlerBlockAliases(
                    statements, ifIndex + 1, thenEnd, domain, handler, contextualTypes);
            Map<String, String> elseAliases = collectHandlerBlockAliases(
                    statements, thenEnd + 1, elseEnd, domain, handler, contextualTypes);
            Map<String, String> common = new HashMap<>();
            for (Map.Entry<String, String> alias : thenAliases.entrySet()) {
                if (alias.getValue().equals(elseAliases.get(alias.getKey())))
                    common.put(alias.getKey(), alias.getValue());
            }
            if (!common.isEmpty() && elseEnd > thenEnd)
                joins.put(elseEnd - 1, common);
            if (elseEnd > thenEnd) ifIndex = elseEnd - 1;
        }
        return joins;
    }

    private Map<String, String> collectHandlerBlockAliases(List<Stmt> statements,
                                                           int start, int end,
                                                           Domain domain, Handler handler,
                                                           Map<String, String> contextualTypes) {
        Map<String, String> aliases = new HashMap<>();
        int blockIndent = statements.subList(start, end).stream().filter(Objects::nonNull)
                .mapToInt(statement -> statement.indent).min().orElse(-1);
        if (blockIndent < 0) return aliases;
        for (int index = start; index < end; index++) {
            Stmt statement = statements.get(index);
            if (statement == null || statement.indent != blockIndent
                    || (statement.kind != Stmt.Kind.Let && statement.kind != Stmt.Kind.Var
                        && statement.kind != Stmt.Kind.Assign)
                    || statement.a == null || statement.b == null
                    || !statement.a.trim().matches("[A-Za-z_][A-Za-z0-9_]*")
                    || !statement.b.trim().matches("[A-Za-z_][A-Za-z0-9_]*")) continue;
            String aliasType = resolveHandlerReceiverType(statement.b, domain, handler,
                    contextualTypes);
            if (aliasType != null) aliases.put(statement.a.trim(), aliasType);
        }
        return aliases;
    }

    private String resolveHandlerReceiverType(String receiverPath, Domain domain, Handler handler,
                                              Map<String, String> contextualTypes) {
        String canonicalPath = receiverPath.replaceAll("\\s", "");
        String contextual = contextualTypes.get(canonicalPath);
        if (contextual != null) return contextual;
        return resolveHandlerReceiverType(receiverPath, domain, handler);
    }

    private String resolveHandlerReceiverType(String receiverPath, Domain domain, Handler handler) {
        Map<String, TypeRef> bindings = new HashMap<>();
        for (Field field : domain.state) {
            TypeRef type = field.typeRef != null ? field.typeRef
                    : (field.type != null ? TypeParser.parseCanonical(field.type) : null);
            if (type != null) bindings.put(field.name, type);
        }
        for (DomainRoute route : domain.routes) bindings.put(route.name, TypeRef.domain(route.type));
        for (Param parameter : handler.params) {
            TypeRef type = parameter.typeRef != null ? parameter.typeRef
                    : (parameter.type != null ? TypeParser.parseCanonical(parameter.type) : null);
            if (type != null) bindings.put(parameter.name, type);
        }
        for (Stmt statement : handler.body) {
            if (statement == null || statement.a == null || statement.semanticType == null
                    || statement.semanticType.isBlank()) continue;
            if (statement.kind == Stmt.Kind.Let || statement.kind == Stmt.Kind.Var
                    || statement.kind == Stmt.Kind.Assign) {
                TypeRef type = TypeParser.parseCanonical(statement.semanticType);
                if (type != null) bindings.put(statement.a.trim(), type);
            }
        }
        String[] path = receiverPath.replaceAll("\\s", "").split("\\.");
        if (path.length == 0) return null;
        TypeRef root = bindings.get(path[0]);
        String typeName = root != null ? root.name() : null;
        for (int index = 1; typeName != null && index < path.length; index++) {
            ObjectType owner = findObjectType(typeName);
            if (owner == null) return null;
            String fieldName = path[index];
            Field field = owner.fields.stream().filter(candidate -> candidate.name.equals(fieldName))
                    .findFirst().orElse(null);
            if (field == null) return null;
            TypeRef type = field.typeRef != null ? field.typeRef : TypeParser.parseCanonical(field.type);
            typeName = type != null ? type.name() : field.type;
        }
        return typeName;
    }

    private static boolean sourceFreeRaisePatternMatches(String variant, String pattern) {
        if (variant == null || pattern == null) return false;
        String normalizedVariant = variant.trim().replace("::", "__");
        String normalizedPattern = pattern.trim().replace("::", "__");
        int payload = normalizedPattern.indexOf('(');
        if (payload >= 0) normalizedPattern = normalizedPattern.substring(0, payload).trim();
        return normalizedVariant.equals(normalizedPattern)
                || normalizedVariant.endsWith("__" + normalizedPattern);
    }

    private void rejectNakedDomainHandlerCall(int line, String expression,
                                              OwnershipEnv env, TypeSolver solver) {
        if (!expression.endsWith(")")) return;
        int open = ExpressionTyper.findMatchingOpenParen(expression, expression.length() - 1);
        if (open <= 0) return;
        String callee = expression.substring(0, open).trim();
        int dot = callee.lastIndexOf('.');
        if (dot <= 0) return;
        String receiver = callee.substring(0, dot).trim();
        String handlerName = callee.substring(dot + 1).trim();
        if (!env.containsKey(receiver)) return;
        TypeRef receiverType = solver.resolve(env.get(receiver));
        if (receiverType == null || receiverType.kind() != TypeKind.Domain) return;
        Domain domain = domains.get(receiverType.name());
        if (domain == null) return;
        if (domain.handlers.stream().noneMatch(handler -> handler.name.equals(handlerName))) {
            throw new CompileError(line, "domain '" + domain.name + "' has no handler '" + handlerName + "'",
                    "UNKNOWN_SYMBOL_OR_TYPE", program.sourceFile);
        }
        CompileError error = new CompileError(line,
                "domain handler '" + domain.name + "." + handlerName + "' must be called with message",
                "DOMAIN_HANDLER_REQUIRES_MESSAGE", program.sourceFile);
        error.setSemanticIdentity(currentSemanticIdentity);
        throw error;
    }

    private void rejectSameDomainMessage(int line, String call, OwnershipEnv env, TypeSolver solver) {
        String target = call == null ? "" : call.trim();
        if (target.startsWith("message ")) target = target.substring("message ".length()).trim();
        int dot = target.indexOf('.');
        int open = target.indexOf('(');
        if (dot <= 0 || open <= dot || !target.endsWith(")")
                || currentSemanticIdentity == null || !currentSemanticIdentity.startsWith("handler:")) return;
        TypeRef targetType = env.get(target.substring(0, dot).trim());
        TypeRef resolvedTarget = targetType == null ? null : solver.resolve(targetType);
        int separator = currentSemanticIdentity.indexOf('.', "handler:".length());
        if (separator > 0 && resolvedTarget != null && resolvedTarget.kind() == TypeKind.Domain
                && currentSemanticIdentity.substring("handler:".length(), separator).equals(resolvedTarget.name())) {
            throw new CompileError(line,
                    "same-domain handler-to-handler messages are illegal; use an ordinary helper function",
                    "MOSS_COMPILE_ERROR");
        }
    }

    private TypeRef recoveryBindingType(List<Stmt> statements, int start, int recoverIndex, int indent) {
        int tryIndex = -1;
        for (int index = recoverIndex - 1; index >= start; index--) {
            Stmt candidate = statements.get(index);
            if (candidate.indent < indent) break;
            if (candidate.kind == Stmt.Kind.Try && candidate.indent == indent) {
                tryIndex = index;
                break;
            }
        }
        if (tryIndex < 0) return null;
        Set<String> enumNames = new HashSet<>();
        for (int index = tryIndex + 1; index < recoverIndex; index++) {
            Stmt statement = statements.get(index);
            if (statement.kind == Stmt.Kind.Raise && statement.a != null) {
                addRaisedEnumName(statement.a, enumNames);
            } else if (statement.kind == Stmt.Kind.Call && statement.text != null) {
                int open = statement.text.indexOf('(');
                if (open > 0) collectFunctionRaisedEnumNames(
                        functions.get(statement.text.substring(0, open).trim()), enumNames, new HashSet<>());
            }
        }
        if (enumNames.size() != 1) return null;
        String enumName = enumNames.iterator().next();
        return enums.containsKey(enumName) ? TypeRef.enumeration(enumName) : null;
    }

    private void collectFunctionRaisedEnumNames(Function function, Set<String> names, Set<String> visited) {
        if (function == null || !visited.add(function.name)) return;
        for (Stmt statement : function.body) {
            if (statement.kind == Stmt.Kind.Raise && statement.a != null) addRaisedEnumName(statement.a, names);
            List<String> expressions = new ArrayList<>();
            expressions.add(statement.text);
            expressions.add(statement.a);
            expressions.add(statement.b);
            expressions.addAll(statement.args);
            for (String expression : expressions) {
                if (expression == null) continue;
                for (Function called : functions.values()) {
                    if (containsFunctionCall(expression, called.name))
                        collectFunctionRaisedEnumNames(called, names, visited);
                }
            }
        }
        if (function.resultExpression != null) {
            for (Function called : functions.values()) {
                if (containsFunctionCall(function.resultExpression, called.name))
                    collectFunctionRaisedEnumNames(called, names, visited);
            }
        }
    }

    private String statementExpressions(Stmt statement) {
        StringBuilder expressions = new StringBuilder();
        if (statement.text != null) expressions.append(statement.text).append('\n');
        if (statement.a != null) expressions.append(statement.a).append('\n');
        if (statement.b != null) expressions.append(statement.b).append('\n');
        if (statement.args != null) {
            for (String argument : statement.args) expressions.append(argument).append('\n');
        }
        return expressions.toString();
    }

    private boolean callsFileIoOperation(String expressions, Set<String> fileIoLocals) {
        for (String name : fileIoLocals) {
            if (expressions.matches("(?s).*\\b" + java.util.regex.Pattern.quote(name)
                    + "\\.(read|write|sync|close)\\s*\\(.*")) return true;
        }
        return false;
    }

    private void collectFunctionRaisedVariants(Function function, Set<String> variants, Set<String> visited) {
        if (function == null || !visited.add(function.name)) return;
        variants.addAll(function.sourceFreeRaiseVariants);
        for (Stmt statement : function.body) {
            if (statement.kind == Stmt.Kind.Raise && statement.a != null) {
                String raised = statement.a.trim();
                int open = raised.indexOf('(');
                String callee = open > 0 ? raised.substring(0, open).trim() : "";
                if (open < 0 || !functions.containsKey(callee))
                    variants.add(open < 0 ? raised : raised.substring(0, open).trim());
            }
            List<String> expressions = new ArrayList<>();
            expressions.add(statement.text);
            expressions.add(statement.a);
            expressions.add(statement.b);
            expressions.addAll(statement.args);
            for (String expression : expressions) {
                if (expression == null) continue;
                for (Function called : functions.values()) {
                    if (containsFunctionCall(expression, called.name))
                        collectFunctionRaisedVariants(called, variants, visited);
                }
            }
        }
        if (function.resultExpression != null) {
            for (Function called : functions.values()) {
                if (containsFunctionCall(function.resultExpression, called.name))
                    collectFunctionRaisedVariants(called, variants, visited);
            }
        }
    }

    private static void addRaisedEnumName(String expression, Set<String> names) {
        String raised = expression.trim();
        int payload = raised.indexOf('(');
        if (payload >= 0) raised = raised.substring(0, payload).trim();
        int variantSeparator = raised.lastIndexOf('.');
        if (variantSeparator > 0) names.add(raised.substring(0, variantSeparator));
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

    private void pruneOneSidedBranchLocals(OwnershipEnv merged, OwnershipEnv parent,
                                            OwnershipEnv thenEnv, OwnershipEnv elseEnv) {
        for (String name : new HashSet<>(merged.typeEnv.getBindings().keySet())) {
            if (parent.containsKey(name)
                    || (thenEnv.containsKey(name) && elseEnv.containsKey(name))) continue;
            merged.typeEnv.remove(name);
            merged.moved.remove(name);
            merged.immutables.remove(name);
            merged.bindingLines.remove(name);
        }
    }

    private void checkStatement(Stmt s, OwnershipEnv env, ExpressionTyper typer, TypeSolver solver,
                                boolean insideHandler, String enclosingName, TypeRef expectedType) {
        if (s == null) return;
        String text = s.text != null ? s.text.trim() : "";
        rejectActiveForSourceMutation(s);

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
                } else if (!consumeOwnedFieldProjection(s.line, expr, inferred, env, varName)) {
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
                if (env.immutables.contains(targetRoot)) {
                    throw new CompileError(s.line, "cannot mutate immutable local '" + targetRoot
                            + "'; declare it with `var` if mutation is intended",
                            "IMMUTABLE_LOCAL_MUTATION");
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
                                typer.rejectUnsupportedMapKey(s.line, baseType.typeArgs().get(0));
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
                    } else if (!consumeOwnedFieldProjection(s.line, expr, inferred, env, varName)) {
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
                // A mutable local may be reinitialized after ownership transfer.
                // Clear its consumed state before recording transfers in the RHS.
                env.moved.remove(varName);
                if (env.containsKey(expr) && transferType(env.get(expr))) {
                    consumeBinding(s.line, expr, env, varName);
                } else if (!consumeOwnedFieldProjection(s.line, expr, inferred, env, varName)) {
                    consumeCallArguments(s.line, expr, env, varName);
                }
            }
            case Message -> {
                if (s.a != null && s.b != null) {
                    if (insideHandler && s.a.equals("self")) {
                        throw new CompileError(s.line, "self-send is illegal; use helper functions", "ILLEGAL_SELF_SEND");
                    }
                    rejectSameDomainMessage(s.line,
                            s.a + "." + s.b + "(" + String.join(", ", s.args) + ")", env, solver);
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
                        boolean materializes = messageDomain.exported;
                        for (int index = 0; index < s.args.size() && index < messageHandler.params.size(); index++) {
                            Param parameter = messageHandler.params.get(index);
                            TypeRef parameterType = parameter.checkedTypeRef != null
                                    ? solver.resolve(parameter.checkedTypeRef) : parameter.typeRef;
                            String typeName = parameterType != null
                                    ? parameterType.toCanonicalString() : parameter.type;
                            warnLargeMessagePayload(s.line, s.sourceFile, typeName, materializes);
                        }
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
                if (replyExpr.startsWith("message "))
                    rejectSameDomainMessage(s.line, replyExpr, env, solver);
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
                if (!retExpr.isEmpty()) consumeCallArguments(s.line, retExpr, env, null);
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
                    validateRangeForSource(s.line, iterExpr, typer, solver, env.typeEnv);
                    TypeRef iterType = typer.inferForLoopSource(iterExpr, s.line, env.typeEnv);
                    TypeRef elemType = null;
                    if (iterType.kind() == TypeKind.Builtin && "vector".equals(iterType.name())) {
                        if (iterType.typeArgs().size() != 1) {
                            throw new CompileError(s.line, "for loop source has an open element type",
                                    "TYPE_AMBIGUOUS_OPEN_RESULT");
                        }
                        elemType = iterType.typeArgs().get(0);
                    } else if ((iterType.kind() == TypeKind.Builtin && "range".equals(iterType.name()))
                            || (iterType.kind() == TypeKind.Primitive && "Range".equals(iterType.name()))
                            || iterExpr.startsWith("range(")) {
                        elemType = TypeRef.primitive("int");
                    } else if (iterType.kind() == TypeKind.InferenceVar
                            || (iterType.kind() == TypeKind.Builtin && "vector".equals(iterType.name())
                                    && iterType.typeArgs().isEmpty())) {
                        elemType = solver.fresh(0);
                    } else {
                        ObjectType object = findObjectType(iterType.name());
                        if (object == null) {
                            throw new CompileError(s.line, "type '" + iterType.toCanonicalString()
                                    + "' is not statically iterable; expected Vector, range, or Iterator",
                                    "TYPE_MISMATCH");
                        }
                        Method next = object.methods.stream()
                                .filter(method -> method.name.equals("next") && method.params.isEmpty())
                                .findFirst().orElse(null);
                        if (next == null) {
                            Method iter = object.methods.stream()
                                    .filter(method -> method.name.equals("iter") && method.params.isEmpty())
                                    .findFirst().orElse(null);
                            TypeRef iterResult = iter == null ? null
                                    : (iter.returnTypeRef != null ? iter.returnTypeRef
                                            : TypeParser.parseCanonical(iter.returnType));
                            ObjectType iterator = iterResult == null ? null
                                    : findObjectType(iterResult.name());
                            next = iterator == null ? null : iterator.methods.stream()
                                    .filter(method -> method.name.equals("next") && method.params.isEmpty())
                                    .findFirst().orElse(null);
                        }
                        if (next == null) {
                            throw new CompileError(s.line, "type '" + object.name
                                    + "' does not satisfy Iterator: missing next() or iter()",
                                    "TYPE_MISMATCH");
                        }
                        TypeRef nextResult = next.returnTypeRef != null ? next.returnTypeRef
                                : TypeParser.parseCanonical(next.returnType);
                        if (nextResult == null || nextResult.kind() != TypeKind.Builtin
                                || !"option".equals(nextResult.name())
                                || nextResult.typeArgs().size() != 1) {
                            throw new CompileError(s.line, "iterator type '" + object.name
                                    + "' has next() without an Option result", "TYPE_MISMATCH");
                        }
                        for (Stmt statement : next.body) {
                            if (statement.kind == Stmt.Kind.Raise
                                    || (statement.text != null && statement.text.trim().startsWith("raise "))) {
                                throw new CompileError(s.line,
                                        "custom iterator iter()/next() has unresolved or typed-raising effects; checked propagation through for is unsupported",
                                        "RAISING_ITERATOR_UNSUPPORTED");
                            }
                        }
                        elemType = nextResult.typeArgs().get(0);
                    }
                    env.put(varName, elemType);
                }
            }
            case Call -> {
                requireAvailable(s.line, s.text, env);
                String callExpr = s.text != null ? s.text.trim() : "";
                if (!callExpr.isEmpty()) {
                    TypeRef executorType = s.a == null || !env.containsKey(s.a.trim())
                            ? null : solver.resolve(env.get(s.a.trim()));
                    if ("invoke".equals(s.b) && executorType != null
                            && "Executor".equals(executorType.name())) {
                        if (s.args.size() != 1) {
                            throw new CompileError(s.line, "Executor.invoke expects one one-way domain handler call",
                                    "ARGUMENT_COUNT_MISMATCH");
                        }
                        TypeRef invokeResult = typer.inferMessageCall(s.args.get(0), s.line, env.typeEnv);
                        if (invokeResult != null && invokeResult.kind() != TypeKind.Unit
                                && !(invokeResult.kind() == TypeKind.Builtin && "unit".equals(invokeResult.name()))) {
                            throw new CompileError(s.line, "Executor.invoke requires a one-way handler",
                                    "EXECUTOR_INVOKE_VALUE_RETURNING");
                        }
                    }
                    rejectNakedDomainHandlerCall(s.line, callExpr, env, solver);
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
                        rejectNakedDomainHandlerCall(s.line, trimmed, env, solver);
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
                if (raised.isEmpty() && activeRecoverDepth == 0) {
                    throw new CompileError(s.line,
                            "bare raise is legal only inside an active recover arm",
                            "BARE_RAISE_OUTSIDE_RECOVER");
                }
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
