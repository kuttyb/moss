package org.mosslang.compiler.types;

import java.util.*;

public class TypeParser {
    public static TypeRef parseCanonical(String raw) {
        if (raw == null) return null;
        Parser p = new Parser(raw);
        return p.parse(0);
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

                while (true) {
                    space();
                    if (pos >= text.length() || text.charAt(pos) == ']') return null;
                    int start = pos;
                    String candidate = token();
                    space();
                    if (pos < text.length() && text.charAt(pos) == '=') {
                        if (positional || candidate.isEmpty()) return null;
                        nominal = true;
                        pos++;
                        TypeRef val = parse(depth + 1);
                        if (val == null) return null;
                        for (TypeRef.FieldSub f : fields) {
                            if (f.name().equals(candidate)) return null;
                        }
                        fields.add(new TypeRef.FieldSub(candidate, val));
                    } else {
                        if (nominal) return null;
                        positional = true;
                        pos = start;
                        TypeRef val = parse(depth + 1);
                        if (val == null) return null;
                        args.add(val);
                    }
                    space();
                    if (pos < text.length() && text.charAt(pos) == ']') {
                        pos++;
                        break;
                    }
                    if (pos >= text.length() || text.charAt(pos) != ',') return null;
                    pos++;
                }

                if (nominal) {
                    return TypeRef.nominal(head, fields);
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
            }
            return TypeRef.primitive(head);
        }
    }
}
