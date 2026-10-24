package org.mosslang.compiler.types;

import java.util.*;

public class TypeContext {
    private final Map<String, NominalDeclaration> byName = new HashMap<>();
    private final Map<String, NominalDeclaration> byId = new HashMap<>();
    private final Map<String, String> modules = new HashMap<>();
    private final Map<String, List<TypeRef>> fieldAnnotations = new HashMap<>();
    private final Set<String> traits = new HashSet<>();

    private static final ThreadLocal<Deque<TypeContext>> CONTEXT_STACK = ThreadLocal.withInitial(ArrayDeque::new);

    public static TypeContext current() {
        Deque<TypeContext> stack = CONTEXT_STACK.get();
        return stack.isEmpty() ? null : stack.peek();
    }

    public static Scope openScope(TypeContext context) {
        return new Scope(context);
    }

    public static class Scope implements AutoCloseable {
        private final TypeContext context;

        public Scope(TypeContext context) {
            this.context = context;
            if (context != null) {
                CONTEXT_STACK.get().push(context);
            }
        }

        @Override
        public void close() {
            if (context != null) {
                Deque<TypeContext> stack = CONTEXT_STACK.get();
                if (!stack.isEmpty() && stack.peek() == context) {
                    stack.pop();
                } else {
                    stack.remove(context);
                }
            }
        }
    }

    public String internModule(String id) {
        if (!id.startsWith("module-v1:")) {
            throw new IllegalArgumentException("invalid module identity: " + id);
        }
        return modules.computeIfAbsent(id, k -> k);
    }

    public NominalDeclaration declare(DeclarationKind kind, String programName, String moduleId,
                                      String declarationName, List<NominalFieldSlot> fields) {
        String id = declarationIdentity(kind, moduleId, declarationName);
        NominalDeclaration decl = new NominalDeclaration(kind, programName, moduleId, declarationName, id, fields);

        NominalDeclaration existing = byName.get(programName);
        if (existing != null) {
            if (!existing.getId().equals(id)) {
                throw new IllegalArgumentException("conflicting nominal identity for '" + programName + "'");
            }
            return existing;
        }
        byName.put(programName, decl);
        byId.put(id, decl);
        return decl;
    }

    public NominalDeclaration find(String programName) {
        return byName.get(programName);
    }

    public NominalDeclaration findId(String id) {
        return byId.get(id);
    }

    public void registerTrait(String name) {
        traits.add(name);
    }

    public boolean hasTrait(String name) {
        return traits.contains(name);
    }

    public static String declarationIdentity(DeclarationKind kind, String moduleId, String name) {
        return switch (kind) {
            case Record -> "nominal-template-v1:" + moduleId + ":type:" + name;
            case Enum -> "nominal-enum-v1:" + moduleId + ":enum:" + name;
            case Domain -> "nominal-domain-v1:" + moduleId + ":domain:" + name;
        };
    }
}
