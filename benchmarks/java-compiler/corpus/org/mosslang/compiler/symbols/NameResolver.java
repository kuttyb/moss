package org.mosslang.compiler.symbols;

import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.driver.DiagnosticSink;
import org.mosslang.compiler.driver.DiagnosticCode;

public class NameResolver extends AstVisitorBase<Void, SymbolTable> {
    private final DiagnosticSink diagnostics;

    public NameResolver(DiagnosticSink diagnostics) {
        this.diagnostics = diagnostics;
    }

    @Override
    public Void visitVariableExpr(VariableExpr expr, SymbolTable table) {
        Symbol s = table.lookup(expr.name());
        if (s == null) {
            diagnostics.error(DiagnosticCode.UNDECLARED_SYMBOL, "Undefined variable: " + expr.name(), expr.span());
        } else {
            expr.setResolvedSymbolId(s.symbolId());
        }
        return null;
    }

    @Override
    public Void visitFunctionDecl(FunctionDecl decl, SymbolTable table) {
        table.declare(decl.name(), SymbolKind.FUNCTION, decl.returnType(),
                      Visibility.PUBLIC, decl.span().start(), false, diagnostics, decl.span());
        table.enterScope();
        for (ParameterDecl p : decl.parameters()) {
            p.accept(this, table);
        }
        decl.body().accept(this, table);
        table.exitScope();
        return null;
    }

    @Override
    public Void visitParameterDecl(ParameterDecl decl, SymbolTable table) {
        table.declare(decl.name(), SymbolKind.PARAMETER, decl.type(),
                      Visibility.PRIVATE, decl.span().start(), false, diagnostics, decl.span());
        return null;
    }

    @Override
    public Void visitLetStmt(LetStmt stmt, SymbolTable table) {
        if (stmt.init() != null) stmt.init().accept(this, table);
        table.declare(stmt.name(), SymbolKind.VARIABLE, stmt.typeAnnotation(),
                      Visibility.PRIVATE, stmt.span().start(), false, diagnostics, stmt.span());
        return null;
    }

    @Override
    public Void visitVarStmt(VarStmt stmt, SymbolTable table) {
        if (stmt.init() != null) stmt.init().accept(this, table);
        table.declare(stmt.name(), SymbolKind.VARIABLE, stmt.typeAnnotation(),
                      Visibility.PRIVATE, stmt.span().start(), true, diagnostics, stmt.span());
        return null;
    }
}
