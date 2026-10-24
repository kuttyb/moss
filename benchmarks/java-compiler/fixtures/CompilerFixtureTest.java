package org.mosslang.compiler.fixtures;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.driver.*;
import org.mosslang.compiler.symbols.*;
import org.mosslang.compiler.types.*;
import org.mosslang.compiler.semantics.*;
import org.mosslang.compiler.abi.*;
import org.mosslang.compiler.generics.*;
import org.mosslang.compiler.ir.*;

public class CompilerFixtureTest {

    public static void main(String[] args) {
        System.out.println("Running Moss Compiler Fixture Tests...");
        testTypeSolverAndUnification();
        testSymbolResolutionAndScopes();
        testOccurrenceLedgerAndAbiCodec();
        testControlFlowAnalysis();
        testEndToEndCompilerDriver();
        testSubsystemExtensions();
        System.out.println("All Compiler Fixture Tests PASSED successfully.");
    }

    private static void testTypeSolverAndUnification() {
        System.out.print("  [1/6] TypeSolver & Unification... ");
        TypeSolver solver = new TypeSolver();
        TypeVar v1 = solver.freshTypeVar("T");
        TypeVar v2 = solver.freshTypeVar("U");

        MutableList<TypeRef> p1 = FastList.newListWith(PrimitiveType.INT, v1);
        FunctionType fn1 = new FunctionType(100, p1, PrimitiveType.STRING);

        MutableList<TypeRef> p2 = FastList.newListWith(v2, PrimitiveType.INT);
        FunctionType fn2 = new FunctionType(101, p2, PrimitiveType.STRING);

        boolean ok = TypeUnifier.unify(fn1, fn2, solver.getSubstitution());
        if (!ok) throw new AssertionError("Expected fn1 and fn2 to unify");

        TypeRef resolvedV1 = solver.resolve(v1);
        TypeRef resolvedV2 = solver.resolve(v2);
        if (!resolvedV1.equals(PrimitiveType.INT)) throw new AssertionError("v1 should resolve to Int");
        if (!resolvedV2.equals(PrimitiveType.INT)) throw new AssertionError("v2 should resolve to Int");

        // Occurs check test
        TypeVar cycleVar = solver.freshTypeVar("C");
        ArrayType cycleArr = new ArrayType(200, cycleVar);
        boolean cycleOk = TypeUnifier.unify(cycleVar, cycleArr, solver.getSubstitution());
        if (cycleOk) throw new AssertionError("Occurs check should have failed on cycle");
        System.out.println("PASS");
    }

    private static void testSymbolResolutionAndScopes() {
        System.out.print("  [2/6] Symbol Resolution & Lexical Scopes... ");
        DiagnosticSink sink = new DiagnosticSink();
        SymbolTable table = new SymbolTable();
        SourceSpan span = SourceSpan.NONE;

        Symbol s1 = table.declare("count", SymbolKind.VARIABLE, PrimitiveType.INT, Visibility.PUBLIC, SourceLocation.UNKNOWN, true, sink, span);
        if (s1 == null || sink.hasErrors()) throw new AssertionError("Failed to declare count");

        table.enterScope();
        Symbol s2 = table.declare("item", SymbolKind.VARIABLE, PrimitiveType.STRING, Visibility.PRIVATE, SourceLocation.UNKNOWN, false, sink, span);
        if (table.lookup("count") == null) throw new AssertionError("Parent symbol not visible");
        if (table.lookup("item") == null) throw new AssertionError("Local symbol not visible");

        table.exitScope();
        if (table.lookup("item") != null) throw new AssertionError("Local symbol leaked to outer scope");
        System.out.println("PASS");
    }

    private static void testOccurrenceLedgerAndAbiCodec() {
        System.out.print("  [3/6] Occurrence Ledger & ABI-v10 Codec... ");
        OccurrenceLedger ledger = new OccurrenceLedger();
        int ord1 = ledger.record(OccurrenceRole.DECLARATION, 10, 1);
        int ord2 = ledger.record(OccurrenceRole.READ, 10, 1);
        int ord3 = ledger.record(OccurrenceRole.RETURN_VALUE, 10, 1);

        if (ledger.size() != 3) throw new AssertionError("Ledger size mismatch");
        OccurrenceRecord rec = ledger.getByOrdinal(ord2);
        if (rec.role() != OccurrenceRole.READ || rec.symbolId() != 10) throw new AssertionError("Record mismatch");

        byte[] body = new byte[] { 0x10, 0x20, 0x30, 0x40 };
        byte[] encoded = CheckedBodyCodec.encode(ledger, body);

        if (!CheckedBodyCodec.validate(encoded)) throw new AssertionError("Encoded ABI block failed validation");

        // Mutate byte and verify fail-closed
        byte[] corrupted = encoded.clone();
        corrupted[10] ^= 0xFF;
        if (CheckedBodyCodec.validate(corrupted)) throw new AssertionError("Corrupted ABI block must fail validation");
        System.out.println("PASS");
    }

    private static void testControlFlowAnalysis() {
        System.out.print("  [4/6] Control Flow Graph & Reachability... ");
        DiagnosticSink sink = new DiagnosticSink();
        SourceSpan span = SourceSpan.NONE;

        MutableList<Stmt> stmts = new FastList<>();
        stmts.add(new LetStmt("x", PrimitiveType.INT, new LiteralExpr(42, PrimitiveType.INT, span), span));
        stmts.add(new ReturnStmt(new VariableExpr("x", span), span));
        stmts.add(new LetStmt("dead", PrimitiveType.INT, new LiteralExpr(0, PrimitiveType.INT, span), span));

        FunctionDecl fn = new FunctionDecl("testFn", new FastList<>(), PrimitiveType.INT, new BlockStmt(stmts, span), span);
        ControlFlowAnalyzer cfa = new ControlFlowAnalyzer(sink);
        ControlFlowGraph cfg = cfa.buildCfg(fn);
        cfa.verifyReachable(cfg, fn);

        if (sink.getWarningCount() != 1) {
            throw new AssertionError("Expected 1 unreachable code warning, got: " + sink.getWarningCount());
        }
        System.out.println("PASS");
    }

    private static void testEndToEndCompilerDriver() {
        System.out.print("  [5/6] End-to-End Compiler Driver Simulation... ");
        SourceSpan span = SourceSpan.NONE;
        MutableList<Stmt> bodyStmts = new FastList<>();
        bodyStmts.add(new LetStmt("a", PrimitiveType.INT, new LiteralExpr(10, PrimitiveType.INT, span), span));
        bodyStmts.add(new LetStmt("b", PrimitiveType.INT, new LiteralExpr(20, PrimitiveType.INT, span), span));
        bodyStmts.add(new ReturnStmt(new BinaryExpr(new VariableExpr("a", span), BinaryOp.ADD, new VariableExpr("b", span), span), span));

        MutableList<ParameterDecl> params = new FastList<>();
        FunctionDecl mainFn = new FunctionDecl("addNumbers", params, PrimitiveType.INT, new BlockStmt(bodyStmts, span), span);

        MutableList<FunctionDecl> fns = new FastList<>();
        fns.add(mainFn);
        SourceFile file = new SourceFile("Main.moss", new FastList<>(), fns, span);

        CompilerConfig config = CompilerConfig.defaultRelease();
        CompilationResult res = CompilerDriver.compile(file, config);

        if (!res.isSuccess()) {
            for (Diagnostic d : res.diagnostics()) {
                System.err.println(d.format());
            }
            throw new AssertionError("Compilation failed");
        }
        if (res.abiPayload() == null || res.abiPayload().length == 0) {
            throw new AssertionError("ABI payload missing");
        }
        if (!CheckedBodyCodec.validate(res.abiPayload())) {
            throw new AssertionError("Generated ABI payload invalid");
        }
        System.out.println("PASS (ABI bytes: " + res.abiPayload().length + ", code: " + res.generatedBytecodeSize() + " B)");
    }

    private static void testSubsystemExtensions() {
        System.out.print("  [6/6] Subsystem Extension Classes Verification... ");
        // Verify reflection-free instantiation of generated extension classes
        org.mosslang.compiler.ast.SyntaxNode_001 node = new org.mosslang.compiler.ast.SyntaxNode_001(SourceSpan.NONE);
        if (node.span() == null) throw new AssertionError("Node span null");

        org.mosslang.compiler.types.SpecializedType_001 st = new org.mosslang.compiler.types.SpecializedType_001(999, "Spec1", PrimitiveType.INT);
        if (!st.isSubtypeOf(PrimitiveType.INT)) throw new AssertionError("SpecializedType should be subtype of parent");

        org.mosslang.compiler.symbols.SymbolResolverExtension_001 symExt = new org.mosslang.compiler.symbols.SymbolResolverExtension_001();
        Symbol s = new Symbol(100, "foo", SymbolKind.VARIABLE, PrimitiveType.INT, Visibility.PUBLIC, SourceLocation.UNKNOWN, false);
        symExt.cacheSymbol(s);
        if (symExt.getCached(100) != s) throw new AssertionError("Symbol cache failed");

        DiagnosticSink sink = new DiagnosticSink();
        org.mosslang.compiler.semantics.SemanticPass_001 pass = new org.mosslang.compiler.semantics.SemanticPass_001(sink);
        if (!pass.analyze(new SourceFile("dummy", new FastList<>(), new FastList<>(), SourceSpan.NONE))) {
            throw new AssertionError("SemanticPass_001 failed");
        }

        org.mosslang.compiler.abi.AbiSection_001 abiSec = new org.mosslang.compiler.abi.AbiSection_001();
        abiSec.appendData(new byte[]{1, 2, 3});
        if (abiSec.size() != 3) throw new AssertionError("AbiSection size mismatch");

        org.mosslang.compiler.ir.IrTransformPass_001 irPass = new org.mosslang.compiler.ir.IrTransformPass_001();
        if (irPass.transform(new IrModule("test")) != 0) throw new AssertionError("IrPass transform mismatch");

        CompilerConfig cfg = CompilerConfig.defaultDebug();
        DiagnosticSink dsink = new DiagnosticSink();
        CompilerContext ctx = new CompilerContext(cfg, dsink);
        if (ctx.getConfig() != cfg) throw new AssertionError("CompilerContext config mismatch");
        System.out.println("PASS");
    }
}
