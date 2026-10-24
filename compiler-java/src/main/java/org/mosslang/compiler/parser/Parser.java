package org.mosslang.compiler.parser;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.eclipse.collections.impl.list.mutable.primitive.IntArrayList;
import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.diagnostics.CompileError;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.Reader;
import java.io.StringReader;
import java.util.*;

import static org.mosslang.compiler.parser.ParserSupport.*;

public class Parser {
    private final List<Line> lines;
    private int i = 0;
    private int indentUnit = 2;

    public Parser(List<Line> lines, Set<String> standardAliases) {
        this.lines = lines != null ? lines : new ArrayList<>();
        normalizeStandardErrorLines(this.lines, standardAliases);
        int unit = 0;
        for (Line line : this.lines) {
            if (line.indent > 0) {
                unit = unit == 0 ? line.indent : gcd(unit, line.indent);
            }
        }
        indentUnit = unit > 0 ? unit : 2;
        if (indentUnit < 2) indentUnit = 2;
    }

    public Parser(List<Line> lines) {
        this(lines, Collections.emptySet());
    }

    private static int gcd(int a, int b) {
        while (b != 0) {
            int t = b;
            b = a % b;
            a = t;
        }
        return Math.abs(a);
    }

    private static IntArrayList copyContinuationLines(IntArrayList existing) {
        return existing != null ? IntArrayList.newList(existing) : new IntArrayList();
    }

    public Program parse() {
        Program p = new Program();
        p.sourceLines.addAll(lines);
        while (i < lines.size()) {
            Line L = lines.get(i);
            if (L.indent != 0) fail(L, "top-level declaration must start at indentation 0");
            if (startsWith(L.text, "module ")) {
                String name = trim(L.text.substring(7));
                if (!plainIdentifier(name)) fail(L, "module name must be an identifier");
                if ("std".equals(name)) {
                    CompileError error = new CompileError(L.no,
                            "module 'std' is reserved for compiler-defined standard types",
                            "RESERVED_STANDARD_NAMESPACE", L.sourceFile);
                    throw error;
                }
                if (p.explicitModule && !p.moduleName.equals(name)) {
                    fail(L, "a source file may declare only one module");
                }
                p.moduleName = name;
                p.explicitModule = true;
                ++i;
            } else if (startsWith(L.text, "import ")) {
                String name = trim(L.text.substring(7));
                if (!plainIdentifier(name)) fail(L, "import name must be an identifier");
                if (!"std".equals(name)) {
                    p.imports.add(new ModuleImport(name, "", L.no, L.sourceFile));
                }
                ++i;
            } else {
                boolean exported = false;
                String declaration = L.text;
                if (startsWith(declaration, "export ")) {
                    exported = true;
                    declaration = trim(declaration.substring(7));
                    if (declaration.isEmpty()) fail(L, "export requires a declaration");
                } else if (startsWith(declaration, "pub ")) {
                    exported = true;
                    declaration = trim(declaration.substring(4));
                    if (declaration.isEmpty()) fail(L, "pub requires a declaration");
                }

                if (startsWith(declaration, "domain ")) {
                    p.domains.add(parseDomain(exported));
                } else if (startsWith(declaration, "type ")) {
                    p.objects.add(parseObject(exported));
                } else if (startsWith(declaration, "enum ")) {
                    p.enums.add(parseEnum(exported));
                } else if (startsWith(declaration, "trait ")) {
                    p.traits.add(parseTrait(exported));
                } else if (startsWith(declaration, "fn ")) {
                    Function function = parseFunction(exported);
                    if ("main".equals(function.name)) {
                        if (p.main != null) fail(L, "duplicate main function");
                        if (function.expressionBody) {
                            fail(L, "main must use an indented statement body");
                        }
                        if (function.resultExpression != null) {
                            Stmt statement = new Stmt();
                            statement.line = function.resultLine;
                            statement.indent = 0;
                            statement.text = function.resultExpression;
                            statement.continuationLines = copyContinuationLines(function.resultContinuationLines);
                            MemberCallResult mcr = new MemberCallResult();
                            SimpleCallResult scr = new SimpleCallResult();
                            if (parseMessageCall(statement.text, mcr)) {
                                statement.kind = Stmt.Kind.Call;
                                statement.a = mcr.receiver;
                                statement.b = mcr.handler;
                                statement.args.addAll(mcr.arguments);
                            } else if (parseSimpleCall(statement.text, scr)) {
                                statement.kind = Stmt.Kind.Call;
                                statement.a = scr.callee;
                                statement.args.addAll(scr.arguments);
                            } else {
                                statement.kind = Stmt.Kind.Raw;
                            }
                            function.body.add(statement);
                            function.resultExpression = null;
                        }
                        if (function.body.isEmpty()) {
                            fail(L, "main must use an indented statement body");
                        }
                        p.main = new MainProc(function.body, function.line, function.header, function.sourceFile);
                    } else {
                        p.functions.add(function);
                    }
                } else if (startsWith(declaration, "test ")) {
                    if (exported) fail(L, "tests cannot be exported");
                    p.tests.add(parseTest());
                } else if (startsWith(declaration, "bench ")) {
                    if (exported) fail(L, "benchmarks cannot be exported");
                    p.benchmarks.add(parseBenchmark());
                } else if ("proc main()".equals(declaration) || "proc main():".equals(declaration)) {
                    if (exported) fail(L, "main cannot be exported");
                    if (p.main != null) fail(L, "duplicate proc main()");
                    p.main = parseMain();
                } else {
                    fail(L, "expected 'module', 'import', 'domain', 'type Name:', 'enum Name:', 'trait', 'fn', 'export', 'test', 'bench', or 'proc main()'");
                }
            }
        }
        addStandardEnums(p);
        return p;
    }

    public static void addStandardEnums(Program p) {
        boolean hasFileError = false;
        boolean hasParseError = false;
        boolean hasConversionError = false;
        for (EnumType e : p.enums) {
            if ("std__FileError".equals(e.name)) hasFileError = true;
            if ("std__ParseError".equals(e.name)) hasParseError = true;
            if ("std__ConversionError".equals(e.name)) hasConversionError = true;
        }
        if (!hasFileError) {
            EnumType e = new EnumType("std__FileError", "", 0, "", false);
            for (String c : List.of("NotFound", "PermissionDenied", "NotRegularFile", "InUse", "Full", "IO")) {
                e.cases.add(new EnumCase(c, 0, ""));
            }
            p.enums.add(e);
        }
        if (!hasParseError) {
            EnumType e = new EnumType("std__ParseError", "", 0, "", false);
            for (String c : List.of("Invalid", "Overflow")) {
                e.cases.add(new EnumCase(c, 0, ""));
            }
            p.enums.add(e);
        }
        if (!hasConversionError) {
            EnumType e = new EnumType("std__ConversionError", "", 0, "", false);
            for (String c : List.of("NonFinite", "Overflow")) {
                e.cases.add(new EnumCase(c, 0, ""));
            }
            p.enums.add(e);
        }
    }

    private void fail(Line L, String msg) {
        CompileError error = new CompileError(L.no, msg);
        error.setSourceFile(L.sourceFile);
        throw error;
    }

    private static boolean identifier(String s) {
        if (s == null || s.isEmpty() || (!Character.isLetter(s.charAt(0)) && s.charAt(0) != '_')) {
            return false;
        }
        for (int idx = 1; idx < s.length(); ++idx) {
            char c = s.charAt(idx);
            if (!Character.isLetterOrDigit(c) && c != '_') return false;
        }
        return true;
    }

    private static boolean parseMessageCall(String text, MemberCallResult out) {
        if (!parseMemberCall(text, out)) return false;
        return identifier(out.receiver) && identifier(out.handler);
    }

    private static boolean containsWordOutsideString(String text, String word) {
        boolean inStr = false, esc = false;
        for (int i = 0; i + word.length() <= text.length(); ++i) {
            char ch = text.charAt(i);
            if (inStr) {
                if (esc) esc = false;
                else if (ch == '\\') esc = true;
                else if (ch == '"') inStr = false;
                continue;
            }
            if (ch == '"') {
                inStr = true;
                continue;
            }
            if (!text.startsWith(word, i)) continue;
            boolean left = (i == 0) || (!Character.isLetterOrDigit(text.charAt(i - 1)) && text.charAt(i - 1) != '_');
            int after = i + word.length();
            boolean right = (after == text.length()) || (!Character.isLetterOrDigit(text.charAt(after)) && text.charAt(after) != '_');
            if (left && right) return true;
        }
        return false;
    }

    private static MutableList<Param> parseParams(Line L, String inside) {
        MutableList<Param> ps = new FastList<>();
        if (trim(inside).isEmpty()) return ps;
        for (String part : splitTopLevel(inside, ',')) {
            int c = part.indexOf(':');
            Param p = new Param();
            if (c == -1) {
                p.name = trim(part);
            } else {
                p.name = trim(part.substring(0, c));
                p.setType(canonicalTypeName(trim(part.substring(c + 1))));
            }
            if (p.name.isEmpty() || !identifier(p.name)) {
                CompileError error = new CompileError(L.no, "invalid parameter: " + part);
                error.setSourceFile(L.sourceFile);
                throw error;
            }
            ps.add(p);
        }
        return ps;
    }

    private ObjectType parseObject(boolean exported) {
        Line head = lines.get(i++);
        String headText = head.text;
        if (startsWith(headText, "export ")) headText = trim(headText.substring(7));
        if (startsWith(headText, "pub ")) headText = trim(headText.substring(4));
        String rest;
        if (startsWith(headText, "type ")) rest = trim(headText.substring(5));
        else rest = trim(headText);

        int eq = rest.indexOf('=');
        boolean legacy = eq != -1;
        if (legacy) {
            if (!"object".equals(trim(rest.substring(eq + 1)))) {
                fail(head, "object declaration must be 'type Name:' or 'type Name = object'");
            }
        } else if (!endsWith(rest, ":")) {
            fail(head, "object declaration must be 'type Name:'");
        }

        ObjectType o = new ObjectType();
        o.header = head.text;
        o.name = legacy ? trim(rest.substring(0, eq)) : trim(rest.substring(0, rest.length() - 1));
        if (!identifier(o.name)) fail(head, "invalid type name '" + o.name + "'");
        o.line = head.no;
        o.exported = exported;
        o.sourceFile = head.sourceFile;

        while (i < lines.size() && lines.get(i).indent > head.indent) {
            Line L = lines.get(i++);
            if (L.indent != head.indent + indentUnit) {
                fail(L, "object fields must use one indentation level");
            }
            if (startsWith(L.text, "fn ")) {
                String sig = trim(L.text.substring(3));
                int lp = sig.indexOf('(');
                int rp = matchingParen(sig, lp);
                if (lp == -1 || rp == -1) fail(L, "object method requires a signature");
                Method m = new Method();
                m.name = trim(sig.substring(0, lp));
                m.params = parseParams(L, sig.substring(lp + 1, rp));
                m.line = L.no;
                m.owner = o.name;
                m.sourceFile = L.sourceFile;
                String suffix = trim(sig.substring(rp + 1));
                if (endsWith(suffix, ":")) suffix = suffix.substring(0, suffix.length() - 1);
                suffix = trim(suffix);
                if (!suffix.isEmpty() && startsWith(suffix, "->")) {
                    m.setReturnType(canonicalTypeName(trim(suffix.substring(2))));
                }
                m.body = parseStmtBlock(L.indent + indentUnit);
                if (!m.body.isEmpty() && (m.body.getLast().kind == Stmt.Kind.Raw || m.body.getLast().kind == Stmt.Kind.Call)) {
                    Stmt last = m.body.remove(m.body.size() - 1);
                    m.resultExpression = last.text;
                    m.resultLine = last.line;
                    m.resultContinuationLines = copyContinuationLines(last.continuationLines);
                }
                o.methods.add(m);
                continue;
            }

            boolean fieldExported = exported;
            String lineText = L.text;
            if (startsWith(lineText, "pub ")) {
                fieldExported = true;
                lineText = trim(lineText.substring(4));
            } else if (startsWith(lineText, "export ")) {
                fieldExported = true;
                lineText = trim(lineText.substring(7));
            }

            int c = lineText.indexOf(':');
            Field f = new Field();
            f.header = L.text;
            f.exported = fieldExported;
            f.sourceFile = L.sourceFile;
            if (c == -1) {
                f.name = trim(lineText);
                f.inferred = true;
            } else {
                f.name = trim(lineText.substring(0, c));
                f.type = canonicalTypeName(trim(lineText.substring(c + 1)));
            }
            if (!identifier(f.name)) fail(L, "object field must be 'name' or 'name: type'");
            f.line = L.no;
            o.fields.add(f);
        }
        return o;
    }

    private EnumType parseEnum(boolean exported) {
        Line head = lines.get(i++);
        String headText = head.text;
        if (startsWith(headText, "export ")) headText = trim(headText.substring(7));
        if (startsWith(headText, "pub ")) headText = trim(headText.substring(4));
        if (!endsWith(headText, ":")) fail(head, "enum declaration must end with ':'");

        EnumType result = new EnumType();
        result.name = trim(headText.substring(5, headText.length() - 1));
        if (!identifier(result.name)) fail(head, "invalid enum name '" + result.name + "'");
        result.header = head.text;
        result.line = head.no;
        result.sourceFile = head.sourceFile;
        result.exported = exported;

        while (i < lines.size() && lines.get(i).indent > head.indent) {
            Line line = lines.get(i++);
            if (line.indent != head.indent + indentUnit) {
                fail(line, "enum cases must use one indentation level");
            }
            if (startsWith(line.text, "fn ")) {
                fail(line, "enum methods are not supported; use an ordinary function and match on the enum");
            }
            EnumCase item = new EnumCase();
            item.line = line.no;
            item.sourceFile = line.sourceFile;
            int lp = line.text.indexOf('(');
            if (lp == -1) {
                item.name = trim(line.text);
            } else {
                int rp = matchingParen(line.text, lp);
                if (rp == -1 || rp + 1 != line.text.length()) {
                    fail(line, "enum case fields must be named and typed");
                }
                if (rp == lp + 1) {
                    fail(line, "tag-only enum cases are declared without parentheses");
                }
                item.name = trim(line.text.substring(0, lp));
                for (Param param : parseParams(line, line.text.substring(lp + 1, rp))) {
                    if (param.type.isEmpty()) fail(line, "enum case field '" + param.name + "' requires a type");
                    for (Field prior : item.fields) {
                        if (prior.name.equals(param.name)) fail(line, "duplicate enum case field '" + param.name + "'");
                    }
                    Field field = new Field();
                    field.name = param.name;
                    field.type = param.type;
                    field.line = line.no;
                    field.sourceFile = line.sourceFile;
                    item.fields.add(field);
                }
            }
            if (!identifier(item.name)) fail(line, "invalid enum case name '" + item.name + "'");
            for (EnumCase prior : result.cases) {
                if (prior.name.equals(item.name)) fail(line, "duplicate enum case '" + item.name + "'");
            }
            result.cases.add(item);
        }
        if (result.cases.isEmpty()) fail(head, "enum must declare at least one case");
        return result;
    }

    private Trait parseTrait(boolean exported) {
        Line head = lines.get(i++);
        String headText = head.text;
        if (startsWith(headText, "export ")) headText = trim(headText.substring(7));
        if (startsWith(headText, "pub ")) headText = trim(headText.substring(4));
        String rest = trim(headText.substring(6));
        if (endsWith(rest, ":")) rest = rest.substring(0, rest.length() - 1);

        Trait t = new Trait();
        t.name = trim(rest);
        t.header = head.text;
        t.line = head.no;
        t.sourceFile = head.sourceFile;
        t.exported = exported;
        if (!identifier(t.name)) fail(head, "invalid trait name '" + t.name + "'");

        while (i < lines.size() && lines.get(i).indent > head.indent) {
            Line L = lines.get(i++);
            if (L.indent != head.indent + indentUnit || !startsWith(L.text, "fn ")) {
                fail(L, "trait members must be method declarations at one indentation level");
            }
            String sig = trim(L.text.substring(3));
            int lp = sig.indexOf('(');
            int rp = matchingParen(sig, lp);
            if (lp == -1 || rp == -1) fail(L, "trait method requires a signature");
            TraitMethod m = new TraitMethod();
            m.name = trim(sig.substring(0, lp));
            m.line = L.no;
            m.sourceFile = L.sourceFile;
            if (!identifier(m.name)) fail(L, "invalid trait method name '" + m.name + "'");
            m.params = parseParams(L, sig.substring(lp + 1, rp));
            String suffix = trim(sig.substring(rp + 1));
            if (endsWith(suffix, ":")) suffix = suffix.substring(0, suffix.length() - 1);
            suffix = trim(suffix);
            if (!suffix.isEmpty()) {
                if (!startsWith(suffix, "->")) fail(L, "trait method return type must follow '->'");
                m.setReturnType(canonicalTypeName(trim(suffix.substring(2))));
            }
            t.methods.add(m);
        }
        if (t.methods.isEmpty()) fail(head, "trait must declare at least one method");
        return t;
    }

    private Domain parseDomain(boolean exported) {
        Line head = lines.get(i++);
        String headText = head.text;
        if (startsWith(headText, "export ")) headText = trim(headText.substring(7));
        if (startsWith(headText, "pub ")) headText = trim(headText.substring(4));
        Domain d = new Domain();
        d.header = head.text;
        d.name = trim(headText.substring(7));
        if (endsWith(d.name, ":")) d.name = trim(d.name.substring(0, d.name.length() - 1));
        d.line = head.no;
        d.sourceFile = head.sourceFile;
        d.exported = exported;
        if (d.name.isEmpty()) fail(head, "domain name is required");

        while (i < lines.size() && lines.get(i).indent > head.indent) {
            Line L = lines.get(i);
            if (L.indent != head.indent + indentUnit) {
                fail(L, "domain members must use one indentation level");
            }
            if (startsWith(L.text, "var ")) {
                d.state.add(parseStateField());
            } else if (startsWith(L.text, "domainroutes")) {
                parseDomainRoutes(d, head.indent + indentUnit);
            } else if (startsWith(L.text, "on ")) {
                d.handlers.add(parseHandler(head.indent + indentUnit, "on"));
            } else if (startsWith(L.text, "fn ")) {
                d.handlers.add(parseHandler(head.indent + indentUnit, "fn"));
            } else if ("on_fail".equals(L.text) || "on_fail:".equals(L.text) || startsWith(L.text, "on_fail ")) {
                fail(L, "on_fail must immediately follow the handler it attaches to");
            } else {
                d.state.add(parseStateField());
            }
        }
        return d;
    }

    private void parseDomainRoutes(Domain domain, int memberIndent) {
        Line head = lines.get(i++);
        String rest = trim(head.text.substring("domainroutes".length()));
        if (rest.isEmpty()) rest = "(";
        if (rest.charAt(0) != '(') {
            fail(head, "domainroutes must be a declaration such as 'domainroutes(worker: Worker)'");
        }

        java.util.function.BiConsumer<Line, String> addRoute = (line, text) -> {
            text = trim(text);
            if (text.isEmpty() || ",".equals(text)) return;
            if (endsWith(text, ",")) text = text.substring(0, text.length() - 1);
            int colon = topLevelColon(text);
            if (colon == -1) fail(line, "domain route must be 'name: DomainType'");
            DomainRoute route = new DomainRoute();
            route.name = trim(text.substring(0, colon));
            route.type = canonicalTypeName(trim(text.substring(colon + 1)));
            route.line = line.no;
            route.sourceFile = line.sourceFile;
            if (!plainIdentifier(route.name) || route.type.isEmpty()) {
                fail(line, "domain route must be 'name: DomainType'");
            }
            domain.routes.add(route);
        };

        String inlineBody = rest.substring(1);
        if (!inlineBody.isEmpty()) {
            if (!endsWith(inlineBody, ")")) {
                fail(head, "domainroutes declaration must close with ')'");
            }
            inlineBody = inlineBody.substring(0, inlineBody.length() - 1);
            for (String item : splitTopLevel(inlineBody, ',')) {
                addRoute.accept(head, item);
            }
            return;
        }

        boolean closed = false;
        while (i < lines.size() && (lines.get(i).indent > head.indent || ")".equals(trim(lines.get(i).text)))) {
            Line line = lines.get(i++);
            if (")".equals(trim(line.text))) {
                closed = true;
                break;
            }
            if (line.indent != memberIndent + indentUnit && line.indent != memberIndent) {
                fail(line, "domain route declarations must use one indentation level");
            }
            addRoute.accept(line, line.text);
        }
        if (!closed) fail(head, "domainroutes declaration must close with ')'");
    }

    private Field parseStateField() {
        Line L = lines.get(i++);
        String rest = trim(L.text);
        if (startsWith(rest, "var ")) rest = trim(rest.substring(4));
        if (rest.isEmpty()) fail(L, "state declaration requires a field name");

        Field f = new Field();
        f.header = L.text;
        f.line = L.no;
        f.sourceFile = L.sourceFile;
        int eq = topLevelAssignment(rest);
        int colon = topLevelColon(rest);
        if (colon != -1 && (eq == -1 || colon < eq)) {
            f.name = trim(rest.substring(0, colon));
            String rhs = trim(rest.substring(colon + 1));
            if (eq != -1) {
                f.type = canonicalTypeName(trim(rest.substring(colon + 1, eq)));
                f.init = trim(rest.substring(eq + 1));
            } else {
                f.type = canonicalTypeName(rhs);
            }
        } else if (eq != -1) {
            f.name = trim(rest.substring(0, eq));
            f.init = trim(rest.substring(eq + 1));
        } else {
            f.name = trim(rest);
        }
        if (!plainIdentifier(f.name)) {
            fail(L, "state declaration must be 'name = value' or 'name: type = value'");
        }
        if ((colon != -1 && f.type.isEmpty()) || (eq != -1 && f.init.isEmpty())) {
            fail(L, "state declaration requires a non-empty type or initializer");
        }
        return f;
    }

    private Handler parseHandler(int memberIndent, String keyword) {
        Line head = lines.get(i++);
        String sig = trim(head.text.substring(keyword.length()));
        int lp = sig.indexOf('(');
        int rp = matchingParen(sig, lp);
        if (lp == -1 || rp == -1 || rp < lp) {
            fail(head, "handler must be 'on Name(args...)' or 'fn Name(args...) -> Type'");
        }
        Handler h = new Handler();
        h.header = head.text;
        h.name = trim(sig.substring(0, lp));
        if (!identifier(h.name)) fail(head, "invalid handler name '" + h.name + "'");
        h.params = parseParams(head, sig.substring(lp + 1, rp));
        String suffix = trim(sig.substring(rp + 1));
        if (endsWith(suffix, ":")) suffix = trim(suffix.substring(0, suffix.length() - 1));
        if (!suffix.isEmpty()) {
            if (!startsWith(suffix, "->")) {
                fail(head, "handler must be 'on Name(args...)' or 'fn Name(args...) -> Type'");
            }
            String type = trim(suffix.substring(2));
            if (type.isEmpty()) fail(head, "reply type is required after '->'");
            h.setReplyType(canonicalTypeName(type));
        }
        h.line = head.no;
        h.sourceFile = head.sourceFile;
        h.body = parseStmtBlock(memberIndent + indentUnit);
        if (h.body.isEmpty()) fail(head, "handler body may not be empty");

        while (i < lines.size() && lines.get(i).indent == memberIndent &&
                ("on_fail".equals(lines.get(i).text) || "on_fail:".equals(lines.get(i).text) || startsWith(lines.get(i).text, "on_fail "))) {
            Line armHead = lines.get(i++);
            if (!endsWith(armHead.text, ":")) {
                fail(armHead, "on_fail arm must end with ':'");
            }
            String selector = trim(armHead.text.substring("on_fail".length(), armHead.text.length() - 1));
            FailureArm arm = new FailureArm();
            arm.line = armHead.no;
            arm.sourceFile = armHead.sourceFile;
            if (!selector.isEmpty()) {
                MutableList<String> alternatives = splitTopLevel(selector, ',');
                boolean allQualified = true;
                for (String alt : alternatives) {
                    if (trim(alt).indexOf('.') == -1) {
                        allQualified = false;
                        break;
                    }
                }
                if (allQualified) {
                    arm.patterns.addAll(alternatives);
                } else if (alternatives.size() == 1 && plainIdentifier(selector)) {
                    arm.binding = selector;
                } else {
                    fail(armHead, "on_fail requires qualified error variants, a single error binding, or a catch-all");
                }
            }
            arm.body = parseStmtBlock(memberIndent + indentUnit);
            if (arm.body.isEmpty()) fail(armHead, "on_fail arm body may not be empty");
            h.failureArms.add(arm);
        }
        return h;
    }

    private Function parseFunction(boolean exported) {
        Line head = lines.get(i++);
        String headText = head.text;
        if (startsWith(headText, "export ")) headText = trim(headText.substring(7));
        if (startsWith(headText, "pub ")) headText = trim(headText.substring(4));
        String sig = trim(headText.substring(3));
        int lp = sig.indexOf('(');
        int rp = matchingParen(sig, lp);
        if (lp == -1 || rp == -1 || rp < lp) {
            fail(head, "function must be 'fn Name(args...)', optionally followed by '-> Type', ':' or '= expression'");
        }
        Function f = new Function();
        f.name = trim(sig.substring(0, lp));
        f.header = head.text;
        if (!identifier(f.name)) fail(head, "invalid function name '" + f.name + "'");
        f.params = parseParams(head, sig.substring(lp + 1, rp));
        String suffix = trim(sig.substring(rp + 1));
        if (startsWith(suffix, "->")) {
            suffix = trim(suffix.substring(2));
            int colon = suffix.indexOf(':');
            int equal = suffix.indexOf('=');
            int end = suffix.length();
            if (colon != -1) end = Math.min(end, colon);
            if (equal != -1) end = Math.min(end, equal);
            String type = trim(suffix.substring(0, end));
            if (type.isEmpty()) fail(head, "function return type is required after '->'");
            f.setReturnType(canonicalTypeName(type));
            suffix = trim(suffix.substring(end));
        }
        f.line = head.no;
        f.sourceFile = head.sourceFile;
        f.exported = exported;

        if (startsWith(suffix, "=")) {
            String expression = trim(suffix.substring(1));
            if (expression.isEmpty()) fail(head, "expression-bodied function requires an expression after '='");
            f.expressionBody = true;
            f.resultExpression = expression;
            f.resultLine = head.no;
            return f;
        }
        if (!suffix.isEmpty() && !":".equals(suffix)) {
            fail(head, "function header must end with ':', '= expression', or an optional '-> Type'");
        }
        f.body = parseStmtBlock(head.indent + indentUnit);
        if (f.body.isEmpty()) fail(head, "function body may not be empty");
        if (!f.body.isEmpty() && f.body.getLast().indent == 0 &&
                (f.body.getLast().kind == Stmt.Kind.Raw || f.body.getLast().kind == Stmt.Kind.Call)) {
            Stmt last = f.body.remove(f.body.size() - 1);
            f.resultExpression = last.text;
            f.resultLine = last.line;
            f.resultContinuationLines = copyContinuationLines(last.continuationLines);
        }
        if (f.body.isEmpty() && f.resultExpression == null) {
            fail(head, "function body may not be empty");
        }
        return f;
    }

    private MainProc parseMain() {
        Line head = lines.get(i++);
        MainProc m = new MainProc();
        m.line = head.no;
        m.header = head.text;
        m.sourceFile = head.sourceFile;
        m.body = parseStmtBlock(indentUnit);
        if (m.body.isEmpty()) fail(head, "main body may not be empty");
        return m;
    }

    private String parseNamedBlockName(Line head, String keyword) {
        String rest = trim(head.text.substring(keyword.length()));
        if (!endsWith(rest, ":")) {
            fail(head, keyword + " declaration must end with ':'");
        }
        rest = trim(rest.substring(0, rest.length() - 1));
        if (rest.length() < 2 || rest.charAt(0) != '"' || rest.charAt(rest.length() - 1) != '"') {
            fail(head, keyword + " declaration requires a quoted name");
        }
        String name = rest.substring(1, rest.length() - 1);
        if (name.isEmpty()) fail(head, keyword + " name may not be empty");
        if (name.indexOf('"') != -1 || name.indexOf('\t') != -1) {
            fail(head, keyword + " name contains an unsupported character");
        }
        return name;
    }

    private TestDecl parseTest() {
        Line head = lines.get(i++);
        TestDecl test = new TestDecl();
        test.name = parseNamedBlockName(head, "test");
        test.header = head.text;
        test.line = head.no;
        test.sourceFile = head.sourceFile;
        test.semanticIdentity = "test:" + test.name + "@" + test.line;
        test.body = parseStmtBlock(head.indent + indentUnit);
        if (test.body.isEmpty()) fail(head, "test body may not be empty");
        return test;
    }

    private BenchDecl parseBenchmark() {
        Line head = lines.get(i++);
        BenchDecl benchmark = new BenchDecl();
        benchmark.name = parseNamedBlockName(head, "bench");
        benchmark.header = head.text;
        benchmark.line = head.no;
        benchmark.sourceFile = head.sourceFile;
        benchmark.semanticIdentity = "bench:" + benchmark.name + "@" + benchmark.line;
        benchmark.body = parseStmtBlock(head.indent + indentUnit);
        if (benchmark.body.isEmpty()) fail(head, "benchmark body may not be empty");
        return benchmark;
    }

    private MutableList<Stmt> parseStmtBlock(int baseIndent) {
        MutableList<Stmt> out = new FastList<>();
        while (i < lines.size()) {
            Line L = lines.get(i);
            if (L.indent < baseIndent) break;
            if (L.indent % indentUnit != 0) fail(L, "indentation must use consistent spaces");
            if (L.indent < baseIndent) break;
            ++i;
            out.add(parseStmt(L, baseIndent));
        }
        return out;
    }

    private Stmt parseStmt(Line L, int baseIndent) {
        Stmt s = new Stmt();
        s.line = L.no;
        s.indent = (L.indent - baseIndent) / indentUnit;
        s.text = L.text;
        s.continuationLines = copyContinuationLines(L.continuationLines);
        s.sourceFile = L.sourceFile;

        String[] forbidden = {"async ", "yield ", "lock ", "shared ", "thread "};
        for (String k : forbidden) {
            if (startsWith(L.text, k)) {
                fail(L, "'" + trim(k) + "' is not part of Moss's concurrency model");
            }
        }

        if ("pass".equals(L.text)) {
            s.kind = Stmt.Kind.Pass;
            return s;
        }

        if ("try".equals(L.text) || "try:".equals(L.text)) {
            if (!"try:".equals(L.text)) fail(L, "try statement must end with ':'");
            s.kind = Stmt.Kind.Try;
            return s;
        }

        if ("recover".equals(L.text) || "recover:".equals(L.text) || startsWith(L.text, "recover ")) {
            if (!endsWith(L.text, ":")) fail(L, "recover arm must end with ':'");
            s.kind = Stmt.Kind.Recover;
            s.a = trim(L.text.substring("recover".length(), L.text.length() - 1));
            return s;
        }

        if ("raise".equals(L.text)) {
            s.kind = Stmt.Kind.Raise;
            return s;
        }

        if (startsWith(L.text, "raise ")) {
            s.kind = Stmt.Kind.Raise;
            s.a = trim(L.text.substring(6));
            if (s.a.isEmpty()) fail(L, "raise requires an error value");
            return s;
        }

        if (startsWith(L.text, "echo ")) {
            s.kind = Stmt.Kind.Echo;
            s.args.addAll(splitTopLevel(trim(L.text.substring(5)), ','));
            return s;
        }

        if (startsWith(L.text, "if ")) {
            s.kind = Stmt.Kind.If;
            s.a = trim(L.text.substring(3));
            if (endsWith(s.a, ":")) s.a = trim(s.a.substring(0, s.a.length() - 1));
            return s;
        }

        if (startsWith(L.text, "match ")) {
            if (!endsWith(L.text, ":")) fail(L, "match statement must end with ':'");
            s.a = trim(L.text.substring(6, L.text.length() - 1));
            if (startsWith(s.a, "consume ")) {
                s.a = trim(s.a.substring(8));
                s.b = "consume";
            }
            if (s.a.isEmpty()) fail(L, "match requires an enum scrutinee");
            s.kind = Stmt.Kind.Match;
            return s;
        }

        if (startsWith(L.text, "case ")) {
            if (!endsWith(L.text, ":")) fail(L, "case statement must end with ':'; guards are unsupported");
            String pattern = trim(L.text.substring(5, L.text.length() - 1));
            if ("_".equals(pattern)) {
                fail(L, "wildcard enum patterns are unsupported; list every case explicitly");
            }
            int lp = pattern.indexOf('(');
            if (lp == -1) {
                s.a = pattern;
            } else {
                int rp = matchingParen(pattern, lp);
                if (rp == -1 || rp + 1 != pattern.length()) {
                    fail(L, "enum patterns support only a case and plain field bindings");
                }
                if (rp == lp + 1) {
                    fail(L, "tag-only enum patterns are written without parentheses");
                }
                s.a = trim(pattern.substring(0, lp));
                String inside = pattern.substring(lp + 1, rp);
                if (!trim(inside).isEmpty()) {
                    for (String binding : splitTopLevel(inside, ',')) {
                        binding = trim(binding);
                        if ("_".equals(binding)) {
                            fail(L, "wildcard enum patterns are not supported");
                        }
                        if (!identifier(binding)) {
                            if (startsWith(binding, "move ") || startsWith(binding, "ref ")) {
                                fail(L, "ownership modifiers are not allowed in enum patterns; use 'match value:' to READ-borrow or 'match consume value:' to consume the whole enum");
                            }
                            fail(L, "enum patterns support only plain field bindings; wildcard and nested patterns are unsupported");
                        }
                        s.args.add(binding);
                    }
                }
            }
            if (!identifier(s.a)) {
                fail(L, "enum patterns require an unqualified case name; wildcard and guards are unsupported");
            }
            s.kind = Stmt.Kind.Case;
            return s;
        }

        if ("else".equals(L.text) || "else:".equals(L.text)) {
            s.kind = Stmt.Kind.Else;
            return s;
        }

        if (startsWith(L.text, "while ")) {
            s.kind = Stmt.Kind.While;
            s.a = trim(L.text.substring(6));
            if (endsWith(s.a, ":")) s.a = trim(s.a.substring(0, s.a.length() - 1));
            return s;
        }

        if (startsWith(L.text, "for ")) {
            String rest = trim(L.text.substring(4));
            if (endsWith(rest, ":")) rest = trim(rest.substring(0, rest.length() - 1));
            int in = rest.indexOf(" in ");
            if (in == -1) fail(L, "for loop must be 'for binding in expression:'");
            s.a = trim(rest.substring(0, in));
            s.b = trim(rest.substring(in + 4));
            if (!plainIdentifier(s.a)) fail(L, "for loop binding must be an identifier");
            if (s.b.isEmpty()) fail(L, "for loop source may not be empty");
            s.kind = Stmt.Kind.For;
            return s;
        }

        if (startsWith(L.text, "message ")) {
            String call = trim(L.text.substring(8));
            MemberCallResult mcr = new MemberCallResult();
            if (!parseMessageCall(call, mcr)) {
                fail(L, "message requires a domain call 'receiver.Handler(args)'");
            }
            s.kind = Stmt.Kind.Message;
            s.a = mcr.receiver;
            s.b = mcr.handler;
            s.args.addAll(mcr.arguments);
            return s;
        }

        if (startsWith(L.text, "let ") || startsWith(L.text, "var ")) {
            boolean isVar = startsWith(L.text, "var ");
            s.kind = isVar ? Stmt.Kind.Var : Stmt.Kind.Let;
            String rest = trim(L.text.substring(4));
            int eq = topLevelAssignment(rest);
            if (eq == -1) fail(L, isVar ? "local var requires an initializer" : "let requires an initializer");
            s.a = trim(rest.substring(0, eq));
            s.b = trim(rest.substring(eq + 1));
            if (s.a.indexOf(':') != -1) {
                CompileError err = new CompileError(L.no,
                        "local type annotations are not supported in this form; use inferred local syntax such as 'var values = Vector[Int]()'",
                        "LOCAL_TYPE_ANNOTATION_UNSUPPORTED", L.sourceFile);
                throw err;
            }
            if (startsWith(s.b, "await ")) {
                fail(L, "await is retired: message is synchronous; use '" + s.a + " = message receiver.Handler(...)'");
            } else if (startsWith(s.b, "message ")) {
                String call = trim(s.b.substring(8));
                MemberCallResult mcr = new MemberCallResult();
                if (!parseMessageCall(call, mcr)) {
                    fail(L, "message requires a domain call 'receiver.Handler(args)'");
                }
                s.kind = Stmt.Kind.Message;
                s.messageResult = s.a;
                s.a = mcr.receiver;
                s.b = mcr.handler;
                s.args.addAll(mcr.arguments);
            } else if (containsWordOutsideString(s.b, "await")) {
                fail(L, "await is retired: message is synchronous");
            }
            return s;
        }

        int assignment = topLevelAssignment(L.text);
        if (assignment != -1) {
            s.a = trim(L.text.substring(0, assignment));
            s.b = trim(L.text.substring(assignment + 1));
            if (s.a.isEmpty() || s.b.isEmpty()) fail(L, "assignment requires a target and an expression");
            if (startsWith(s.b, "await ")) {
                fail(L, "await is retired: message is synchronous; use '" + s.a + " = message receiver.Handler(...)'");
            } else if (startsWith(s.b, "message ")) {
                String call = trim(s.b.substring(8));
                MemberCallResult mcr = new MemberCallResult();
                if (!parseMessageCall(call, mcr)) {
                    fail(L, "message requires a domain call 'receiver.Handler(args)'");
                }
                s.kind = Stmt.Kind.Message;
                s.messageResult = s.a;
                s.a = mcr.receiver;
                s.b = mcr.handler;
                s.args.addAll(mcr.arguments);
            } else if (containsWordOutsideString(s.b, "await")) {
                fail(L, "await is retired: message is synchronous");
            } else {
                s.kind = Stmt.Kind.Assign;
            }
            return s;
        }

        if ("reply".equals(L.text)) fail(L, "reply requires a value");
        if (startsWith(L.text, "reply ")) {
            s.kind = Stmt.Kind.Reply;
            s.a = trim(L.text.substring(6));
            if (s.a.isEmpty()) fail(L, "reply requires a value");
            return s;
        }

        if ("return".equals(L.text)) {
            s.kind = Stmt.Kind.Return;
            return s;
        }
        if (startsWith(L.text, "return ")) {
            s.kind = Stmt.Kind.Return;
            s.a = trim(L.text.substring(7));
            if (s.a.isEmpty()) fail(L, "return requires a value");
            return s;
        }

        if (startsWith(L.text, "await ")) {
            fail(L, "await is retired: message is synchronous; use 'message receiver.Handler(...)'");
        }
        if (containsWordOutsideString(L.text, "await")) {
            fail(L, "await is retired: message is synchronous");
        }

        MemberCallResult mcr = new MemberCallResult();
        if (parseMemberCall(L.text, mcr) && identifier(mcr.handler)) {
            s.kind = Stmt.Kind.Call;
            s.a = mcr.receiver;
            s.b = mcr.handler;
            s.args.addAll(mcr.arguments);
            return s;
        }

        SimpleCallResult scr = new SimpleCallResult();
        if (parseSimpleCall(L.text, scr)) {
            s.kind = Stmt.Kind.Call;
            s.a = scr.callee;
            s.args.addAll(scr.arguments);
            return s;
        }

        for (String assertion : new String[]{"assert", "assertEqual"}) {
            if (assertion.equals(L.text) ||
                    (startsWith(L.text, assertion) && L.text.length() > assertion.length() &&
                            Character.isWhitespace(L.text.charAt(assertion.length())))) {
                CompileError err = new CompileError(L.no,
                        assertion + " uses call syntax; write " + assertion + "(...)",
                        "INVALID_ASSERT_SYNTAX", L.sourceFile);
                throw err;
            }
        }
        s.kind = Stmt.Kind.Raw;
        return s;
    }

    private static final Set<String> KEYWORDS_FOR_NEGATED = new HashSet<>(Arrays.asList(
            "echo", "return", "reply", "and", "or", "xor", "not", "in", "if", "while", "raise"
    ));

    public static String canonicalizeCharLiterals(String line, int lineNo, String sourceFile) {
        StringBuilder out = new StringBuilder(line.length());
        boolean inString = false;
        int[] outCodepoint = new int[1];

        for (int i = 0; i < line.length(); ++i) {
            char c = line.charAt(i);
            if (inString) {
                if (c == '\\') {
                    int next = parseRustEscape(line, i, outCodepoint);
                    if (next == -1) {
                        int end = Math.min(line.length(), i + 10);
                        CompileError err = new CompileError(lineNo, "invalid escape '" + line.substring(i, end) +
                                "'; use \\n \\r \\t \\0 \\\\ \\' \\\" \\xHH (ASCII) or \\u{HEX} (a Unicode scalar)",
                                "INVALID_ESCAPE", sourceFile);
                        throw err;
                    }
                    out.append(line, i, next);
                    i = next - 1;
                    continue;
                }
                out.append(c);
                if (c == '"') inString = false;
                continue;
            }
            if (c == '"') {
                inString = true;
                out.append(c);
                continue;
            }
            if (c == '#') {
                out.append(line.substring(i));
                break;
            }

            if (NumericLiteral.numericLiteralStartsAt(line, i)) {
                NumericLiteral literal = NumericLiteral.scanNumericLiteral(line, i);
                if (!literal.ok) {
                    CompileError err = new CompileError(lineNo, literal.message, literal.code, sourceFile);
                    throw err;
                }
                int end = literal.end;
                if (!NumericLiteral.intLiteralInRange(literal, false)) {
                    boolean negated = false;
                    if (!literal.magnitudeOverflow && literal.magnitude == NumericLiteral.K_NUMERIC_INT_MIN_MAGNITUDE) {
                        int k = i;
                        while (k > 0 && line.charAt(k - 1) == ' ') --k;
                        if (k > 0 && line.charAt(k - 1) == '-') {
                            int before = k - 1;
                            while (before > 0 && line.charAt(before - 1) == ' ') --before;
                            if (before == 0 || "([{,=:+-*/%<>!&|^~".indexOf(line.charAt(before - 1)) != -1) {
                                negated = true;
                            } else if (Character.isLetter(line.charAt(before - 1))) {
                                int word = before;
                                while (word > 0 && (Character.isLetterOrDigit(line.charAt(word - 1)) || line.charAt(word - 1) == '_')) {
                                    --word;
                                }
                                negated = KEYWORDS_FOR_NEGATED.contains(line.substring(word, before));
                            }
                        }
                    }
                    if (!negated) {
                        CompileError err = new CompileError(lineNo, "integer literal '" + line.substring(i, end) +
                                "' does not fit in Int (-9223372036854775808 through 9223372036854775807)",
                                "INT_LITERAL_OUT_OF_RANGE", sourceFile);
                        throw err;
                    }
                }
                out.append(literal.canonical);
                i = end - 1;
                continue;
            }

            if (c != '\'') {
                out.append(c);
                continue;
            }

            int codepoint = 0;
            int end = -1;
            if (i + 1 < line.length() && line.charAt(i + 1) == '\\') {
                int next = parseRustEscape(line, i + 1, outCodepoint);
                if (next == -1) {
                    int invEnd = Math.min(line.length(), i + 1 + 10);
                    CompileError err = new CompileError(lineNo, "invalid escape '" + line.substring(i + 1, invEnd) +
                            "'; use \\n \\r \\t \\0 \\\\ \\' \\\" \\xHH (ASCII) or \\u{HEX} (a Unicode scalar)",
                            "INVALID_ESCAPE", sourceFile);
                    throw err;
                }
                codepoint = outCodepoint[0];
                if (next < line.length() && line.charAt(next) == '\'') end = next;
            } else if (i + 1 < line.length()) {
                int cp = line.codePointAt(i + 1);
                int charCount = Character.charCount(cp);
                if (cp != '\'' && i + 1 + charCount < line.length() && line.charAt(i + 1 + charCount) == '\'') {
                    codepoint = cp;
                    end = i + 1 + charCount;
                }
            }

            if (end == -1) {
                out.append(c);
                continue;
            }

            if (line.charAt(i + 1) != '\\' && codepoint < 0x80 &&
                    (Character.isLetterOrDigit(codepoint) || codepoint == '_')) {
                out.append(line, i, end + 1);
            } else {
                out.append(String.format("'\\u{%X}'", codepoint));
            }
            i = end;
        }
        return out.toString();
    }

    public static List<Line> lexLines(Reader in, String sourceFile) throws IOException {
        BufferedReader reader = (in instanceof BufferedReader) ? (BufferedReader) in : new BufferedReader(in);
        List<Line> out = new ArrayList<>();
        String raw;
        int no = 0;
        while ((raw = reader.readLine()) != null) {
            ++no;
            if (!raw.isEmpty() && raw.charAt(raw.length() - 1) == '\r') {
                raw = raw.substring(0, raw.length() - 1);
            }
            int first = -1;
            for (int k = 0; k < raw.length(); ++k) {
                if (raw.charAt(k) != ' ') {
                    first = k;
                    break;
                }
            }
            if (first == -1) continue;
            if (raw.charAt(first) == '#') continue;
            if (raw.indexOf('\t') != -1) {
                CompileError err = new CompileError(no, "tabs are not allowed; use space indentation");
                err.setSourceFile(sourceFile);
                throw err;
            }

            raw = canonicalizeCharLiterals(raw, no, sourceFile);

            boolean inStr = false, esc = false;
            int cut = raw.length();
            for (int k = first; k < raw.length(); ++k) {
                char ch = raw.charAt(k);
                if (inStr) {
                    if (esc) esc = false;
                    else if (ch == '\\') esc = true;
                    else if (ch == '"') inStr = false;
                } else {
                    if (ch == '"') inStr = true;
                    else if (ch == '#') {
                        cut = k;
                        break;
                    }
                }
            }
            raw = rtrim(raw.substring(0, cut));
            if (trim(raw).isEmpty()) continue;

            int indent = 0;
            while (indent < raw.length() && raw.charAt(indent) == ' ') ++indent;
            String text = trim(raw.substring(indent));
            if (startsWith(text, "|>") && !out.isEmpty()) {
                Line prev = out.get(out.size() - 1);
                prev.text += " " + text;
                prev.continuationLines.add(no);
                continue;
            }
            out.add(new Line(no, indent, text, new IntArrayList(), sourceFile));
        }
        return out;
    }

    public static Program parseSourceLines(List<Line> lines, Set<String> standardAliases) {
        return new Parser(lines, standardAliases).parse();
    }

    public static Program parseSource(Reader input, String sourceFile, Set<String> standardAliases) {
        try {
            return parseSourceLines(lexLines(input, sourceFile), standardAliases);
        } catch (IOException e) {
            throw new RuntimeException("IO error reading source: " + e.getMessage(), e);
        }
    }

    public static Program parseSource(String sourceText, String sourceFile, Set<String> standardAliases) {
        return parseSource(new StringReader(sourceText), sourceFile, standardAliases);
    }

    public static Program parseSource(String sourceText, String sourceFile) {
        return parseSource(sourceText, sourceFile, Collections.emptySet());
    }

    public static Program parseSource(String sourceText) {
        return parseSource(sourceText, "", Collections.emptySet());
    }
}
