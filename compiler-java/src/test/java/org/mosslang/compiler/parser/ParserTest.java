package org.mosslang.compiler.parser;

import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.diagnostics.CompileError;

import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.Arrays;
import java.util.List;

public class ParserTest {
    public static void main(String[] args) throws Exception {
        System.out.println("Running ParserTest...");

        testBasicPrograms();
        testFunctionVarieties();
        testDomainsAndHandlers();
        testObjectsAndEnums();
        testTraits();
        testNumericAndCharLiterals();
        testStatementsAndControlFlow();
        testAstDualTypeRepresentation();
        testContinuationLinesDefensiveCopy();
        testParseErrors();
        testParseRealMossFiles();

        System.out.println("ParserTest PASSED: All tests passed successfully.");
    }

    private static void assertEquals(Object expected, Object actual, String message) {
        if (expected == null && actual == null) return;
        if (expected != null && expected.equals(actual)) return;
        throw new AssertionError(message + " - expected: " + expected + ", actual: " + actual);
    }

    private static void assertTrue(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    private static void testBasicPrograms() {
        String src = "fn main():\n  echo \"hello world\"\n";
        Program p = Parser.parseSource(src, "test.moss");
        assertTrue(p.main != null, "Program main should not be null");
        assertEquals(1, p.main.body.size(), "Main should have 1 statement");
        assertEquals(Stmt.Kind.Echo, p.main.body.get(0).kind, "Statement should be Echo");
        assertEquals("\"hello world\"", p.main.body.get(0).args.get(0), "Echo arg mismatch");
    }

    private static void testFunctionVarieties() {
        String src = ""
                + "fn add(a: int, b: int) -> int:\n"
                + "  return a + b\n"
                + "\n"
                + "fn square(x: Int) = x * x\n"
                + "\n"
                + "fn untyped(x):\n"
                + "  x\n";
        Program p = Parser.parseSource(src, "funcs.moss");
        assertEquals(3, p.functions.size(), "Should have 3 functions");

        Function f1 = p.functions.get(0);
        assertEquals("add", f1.name, "f1 name");
        assertEquals("int", f1.returnType, "f1 return type");
        assertEquals(2, f1.params.size(), "f1 params count");
        assertEquals("a", f1.params.get(0).name, "f1 p0 name");
        assertEquals("int", f1.params.get(0).type, "f1 p0 type");

        Function f2 = p.functions.get(1);
        assertEquals("square", f2.name, "f2 name");
        assertTrue(f2.expressionBody, "f2 should be expression body");
        assertEquals("x * x", f2.resultExpression, "f2 result expr");

        Function f3 = p.functions.get(2);
        assertEquals("untyped", f3.name, "f3 name");
        assertEquals("x", f3.resultExpression, "f3 result expr");
    }

    private static void testDomainsAndHandlers() {
        String src = ""
                + "domain Worker:\n"
                + "  var count = 0\n"
                + "  domainroutes(downstream: Sink)\n"
                + "  fn Process(item: int) -> bool:\n"
                + "    count = count + 1\n"
                + "    reply true\n"
                + "  on_fail std.FileError.NotFound:\n"
                + "    reply false\n";
        Program p = Parser.parseSource(src, "domain.moss");
        assertEquals(1, p.domains.size(), "Should have 1 domain");

        Domain d = p.domains.get(0);
        assertEquals("Worker", d.name, "domain name");
        assertEquals(1, d.state.size(), "state fields count");
        assertEquals("count", d.state.get(0).name, "state field name");
        assertEquals("0", d.state.get(0).init, "state field init");

        assertEquals(1, d.routes.size(), "routes count");
        assertEquals("downstream", d.routes.get(0).name, "route name");
        assertEquals("Sink", d.routes.get(0).type, "route type");

        assertEquals(1, d.handlers.size(), "handlers count");
        Handler h = d.handlers.get(0);
        assertEquals("Process", h.name, "handler name");
        assertEquals("bool", h.replyType, "reply type");
        assertEquals(1, h.failureArms.size(), "failure arms count");
        assertEquals("std__FileError.NotFound", h.failureArms.get(0).patterns.get(0), "pattern");
    }

    private static void testObjectsAndEnums() {
        String src = ""
                + "type Point:\n"
                + "  x: int\n"
                + "  y: int\n"
                + "  fn dist_sq() -> int:\n"
                + "    return x * x + y * y\n"
                + "\n"
                + "enum Color:\n"
                + "  Red\n"
                + "  Green\n"
                + "  Rgb(r: int, g: int, b: int)\n";
        Program p = Parser.parseSource(src, "types.moss");
        assertEquals(1, p.objects.size(), "objects count");
        ObjectType obj = p.objects.get(0);
        assertEquals("Point", obj.name, "obj name");
        assertEquals(2, obj.fields.size(), "fields count");
        assertEquals("x", obj.fields.get(0).name, "f0 name");
        assertEquals(1, obj.methods.size(), "methods count");
        assertEquals("dist_sq", obj.methods.get(0).name, "m0 name");

        assertEquals(1, p.enums.size(), "enums count");
        EnumType en = p.enums.get(0);
        assertEquals("Color", en.name, "enum name");
        assertEquals(3, en.cases.size(), "enum cases count");
        assertEquals("Red", en.cases.get(0).name, "case 0");
        assertEquals("Rgb", en.cases.get(2).name, "case 2");
        assertEquals(3, en.cases.get(2).fields.size(), "case 2 fields count");
    }

    private static void testTraits() {
        String src = ""
                + "trait Printable:\n"
                + "  fn print()\n"
                + "  fn to_string() -> string\n";
        Program p = Parser.parseSource(src, "traits.moss");
        assertEquals(1, p.traits.size(), "traits count");
        Trait t = p.traits.get(0);
        assertEquals("Printable", t.name, "trait name");
        assertEquals(2, t.methods.size(), "trait methods count");
        assertEquals("print", t.methods.get(0).name, "method 0");
        assertEquals("to_string", t.methods.get(1).name, "method 1");
        assertEquals("string", t.methods.get(1).returnType, "method 1 return");
    }

    private static void testNumericAndCharLiterals() {
        String src = ""
                + "fn literals():\n"
                + "  let a = 1_000\n"
                + "  let b = 0x10_ff\n"
                + "  let c = 2.5e-3\n"
                + "  let d = 'a'\n"
                + "  let e = '\\n'\n"
                + "  let f = -9223372036854775808\n";
        Program p = Parser.parseSource(src, "literals.moss");
        Function f = p.functions.get(0);
        assertEquals(6, f.body.size(), "statements count");

        // Canonical decimal rewriting
        assertEquals("1000", f.body.get(0).b, "decimal rewrite");
        assertEquals("4351", f.body.get(1).b, "hex rewrite (0x10ff == 4351)");
        assertEquals("2.5e-3", f.body.get(2).b, "float literal");
        assertEquals("'a'", f.body.get(3).b, "char 'a'");
        assertEquals("'\\u{A}'", f.body.get(4).b, "char '\\n' canonicalized");
    }

    private static void testStatementsAndControlFlow() {
        String src = ""
                + "fn control(c: Color):\n"
                + "  if x > 0:\n"
                + "    pass\n"
                + "  else:\n"
                + "    pass\n"
                + "  while y < 10:\n"
                + "    y = y + 1\n"
                + "  for item in items:\n"
                + "    echo item\n"
                + "  match c:\n"
                + "    case Red:\n"
                + "      pass\n"
                + "    case Rgb(r, g, b):\n"
                + "      pass\n"
                + "  try:\n"
                + "    raise MyError()\n"
                + "  recover err:\n"
                + "    pass\n";
        Program p = Parser.parseSource(src, "control.moss");
        Function f = p.functions.get(0);
        assertTrue(f.body.size() > 5, "Should have several statements");
    }

    private static void testAstDualTypeRepresentation() {
        org.mosslang.compiler.types.TypeContext ctx = new org.mosslang.compiler.types.TypeContext();
        try (org.mosslang.compiler.types.TypeContext.Scope scope = org.mosslang.compiler.types.TypeContext.openScope(ctx)) {
            Param param = new Param("x", "int");
            param.sync();
            assertTrue(param.typeRef != null, "Param typeRef should be bound after sync");
            assertEquals("int", param.typeRef.toCanonicalString(), "Param canonical string");
            param.assertConsistent();

            Field field = new Field();
            field.name = "val";
            field.setType("string");
            assertTrue(field.typeRef != null, "Field typeRef should be bound");
            assertEquals("string", field.typeRef.toCanonicalString(), "Field canonical string");
            field.assertConsistent();

            Function fn = new Function();
            fn.name = "foo";
            fn.setReturnType("vector[int]");
            assertTrue(fn.returnTypeRef != null, "Function returnTypeRef should be bound");
            assertEquals("vector[int]", fn.returnTypeRef.toCanonicalString(), "Function returnTypeRef canonical string");
            fn.assertConsistent();

            Method m = new Method();
            m.name = "bar";
            m.setReturnType("bool");
            assertTrue(m.returnTypeRef != null, "Method returnTypeRef should be bound");
            m.assertConsistent();

            Handler h = new Handler();
            h.name = "Msg";
            h.setReplyType("int");
            assertTrue(h.replyTypeRef != null, "Handler replyTypeRef should be bound");
            h.assertConsistent();

            TraitMethod tm = new TraitMethod();
            tm.name = "calc";
            tm.setReturnType("float");
            assertTrue(tm.returnTypeRef != null, "TraitMethod returnTypeRef should be bound");
            tm.assertConsistent();

            Program p = new Program();
            p.functions.add(fn);
            p.assertDeclarationTypeConsistency();

            // Test inconsistency detection
            param.type = "float";
            boolean threw = false;
            try {
                param.assertConsistent();
            } catch (IllegalStateException e) {
                threw = true;
            }
            assertTrue(threw, "assertConsistent should throw on mismatch");
        }
    }

    private static void testContinuationLinesDefensiveCopy() {
        String src = "fn main():\n  x = 1\n  |> add(2)\n";
        Program p = Parser.parseSource(src, "pipeline.moss");
        Stmt stmt = p.main.body.get(0);
        assertEquals(1, stmt.continuationLines.size(), "Pipeline continuation size");
        stmt.continuationLines.add(100);
        assertEquals(0, p.sourceLines.get(0).continuationLines.size(), "Source lines continuation should not be mutated");
    }

    private static void testParseErrors() {
        assertThrowsCompileError("module std\n", "RESERVED_STANDARD_NAMESPACE");
        assertThrowsCompileError("fn main():\n\tpass\n", null); // tab error
        assertThrowsCompileError("fn main():\n  async run()\n", null); // forbidden keyword
        assertThrowsCompileError("fn main():\n  assert x\n", "INVALID_ASSERT_SYNTAX");
        assertThrowsCompileError("fn main():\n  let x = 9999999999999999999999999999\n", "INT_LITERAL_OUT_OF_RANGE");
    }

    private static void assertThrowsCompileError(String src, String expectedCode) {
        try {
            Parser.parseSource(src, "error_test.moss");
            throw new AssertionError("Expected CompileError but parsing succeeded for: " + src);
        } catch (CompileError err) {
            if (expectedCode != null) {
                assertEquals(expectedCode, err.getCode(), "Diagnostic code mismatch for: " + src);
            }
        }
    }

    private static void testParseRealMossFiles() throws IOException {
        File examplesDir = new File("examples");
        File[] mossFiles = examplesDir.listFiles((dir, name) -> name.endsWith(".moss"));
        assertTrue(mossFiles != null && mossFiles.length > 0, "No .moss files found in examples/");

        Arrays.sort(mossFiles, (a, b) -> a.getName().compareTo(b.getName()));
        System.out.println("Verifying " + mossFiles.length + " real Moss files from examples/...");

        int count = 0;
        for (File file : mossFiles) {
            try (FileReader reader = new FileReader(file)) {
                Program program = Parser.parseSource(reader, file.getPath(), null);
                assertTrue(program != null, "Program should not be null for " + file.getName());
                assertTrue(program.sourceLines.size() > 0, "Source lines should not be empty for " + file.getName());
                count++;
            } catch (Exception e) {
                System.err.println("FAILED parsing " + file.getPath() + ": " + e.getMessage());
                throw e;
            }
        }
        System.out.println("Successfully parsed all " + count + " Moss example files into valid Program ASTs!");
    }
}
