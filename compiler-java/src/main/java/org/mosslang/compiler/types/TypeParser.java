package org.mosslang.compiler.types;

import java.util.*;

public class TypeParser {
    public static TypeRef parseCanonical(String raw) {
        if (raw == null) return null;
        Parser p = new Parser(raw);
        TypeRef parsed = p.parse(0);
        p.space();
        return (parsed != null && p.pos == raw.length()) ? parsed : null;
    }

    private static class Parser {
        private final String text;
        private int pos = 0;

        Parser(String text) {
            this.text = text;
        }

        private void space() {
            while (pos < text.length() && Character.isWhitespace(text.charAt(pos))) {
                pos++;
            }
        }

        private String token() {
            space();
            int begin = pos;
            while (pos < text.length()) {
                char ch = text.charAt(pos);
                if (ch == '[' || ch == ']' || ch == ',' || ch == '=' || Character.isWhitespace(ch)) {
                    break;
                }
                pos++;
            }
            return text.substring(begin, pos);
        }

        TypeRef parse(int depth) {
            if (depth >= 128) return null;
            String head = token();
            if (head.isEmpty()) return null;
            space();
            if (pos < text.length() && text.charAt(pos) == '[') {
                pos++;
                List<TypeRef> args = new ArrayList<>();
                List<TypeRef.FieldSub> fields = new ArrayList<>();
                boolean nominal = false;
                boolean positional = false;

                while (pos < text.length()) {
                    space();
                    if (pos < text.length() && text.charAt(pos) == ']') {
                        pos++;
                        break;
                    }
                    int saved = pos;
                    String fieldName = token();
                    space();
                    if (pos < text.length() && text.charAt(pos) == '=') {
                        nominal = true;
                        if (positional) return null;
                        pos++;
                        space();
                        TypeRef sub = parse(depth + 1);
                        if (sub == null) return null;
                        fields.add(new TypeRef.FieldSub(fieldName, sub));
                    } else {
                        positional = true;
                        if (nominal) return null;
                        pos = saved;
                        TypeRef sub = parse(depth + 1);
                        if (sub == null) return null;
                        args.add(sub);
                    }
                    space();
                    if (pos < text.length() && text.charAt(pos) == ',') {
                        pos++;
                    } else if (pos < text.length() && text.charAt(pos) == ']') {
                        pos++;
                        break;
                    } else {
                        return null;
                    }
                }

                if (nominal) {
                    TypeContext ctx = TypeContext.current();
                    NominalDeclaration decl = ctx != null ? ctx.find(head) : null;
                    if (decl == null) return null;
                    return TypeRef.nominalBound(decl, head, fields);
                }

                if ("Vector".equals(head)) head = "vector";
                else if ("Map".equals(head)) head = "map";
                else if ("Queue".equals(head)) head = "queue";
                else if ("Set".equals(head)) head = "set";

                Integer arity = TypeRef.collectionArity(head);
                if (arity != null && args.size() != arity) {
                    return null;
                }
                return TypeRef.builtin(head, args);
            }

            if ("unit".equals(head)) return TypeRef.unit();
            if ("Int".equalsIgnoreCase(head)) return TypeRef.primitive("int");
            if ("Float".equalsIgnoreCase(head)) return TypeRef.primitive("float");
            if ("Bool".equalsIgnoreCase(head)) return TypeRef.primitive("bool");
            if ("String".equalsIgnoreCase(head)) return TypeRef.primitive("string");
            if ("Char".equalsIgnoreCase(head)) return TypeRef.primitive("char");
            if ("Range".equals(head)) return TypeRef.primitive("Range");
            if ("RangeBatch".equals(head)) return TypeRef.primitive("RangeBatch");
            if ("FileIO".equals(head)) return TypeRef.primitive("FileIO");
            if ("Executor".equals(head)) return TypeRef.primitive("Executor");
            if ("executor".equals(head)) return TypeRef.primitive("executor");

            if ("Vector".equals(head)) head = "vector";
            else if ("Map".equals(head)) head = "map";
            else if ("Queue".equals(head)) head = "queue";
            else if ("Set".equals(head)) head = "set";

            if ("vector".equals(head) || "map".equals(head) || "queue".equals(head) || "set".equals(head)) {
                return TypeRef.builtin(head, Collections.emptyList());
            }

            TypeContext ctx = TypeContext.current();
            if (ctx != null) {
                NominalDeclaration decl = ctx.find(head);
                if (decl != null) {
                    if (decl.getKind() == DeclarationKind.Record) {
                        return TypeRef.nominalBound(decl, head, Collections.emptyList());
                    }
                    if (decl.getKind() == DeclarationKind.Enum) {
                        return TypeRef.enumeration(head);
                    }
                    return TypeRef.domain(head);
                }
                if (ctx.hasTrait(head)) {
                    return TypeRef.traitConstraint(head);
                }
            }

            if (head.startsWith("_typevar:")) {
                String ids = head.substring("_typevar:".length());
                String[] parts = ids.split(":");
                if (parts.length == 3) {
                    try {
                        int s = Integer.parseInt(parts[0]);
                        int o = Integer.parseInt(parts[1]);
                        int ord = Integer.parseInt(parts[2]);
                        return TypeRef.var(new TypeVarId(s, o, ord));
                    } catch (NumberFormatException ignored) {}
                }
                return null;
            }

            if (head.startsWith("_")) {
                return TypeRef.primitive(head);
            }

            return null;
        }
    }
}
