package org.mosslang.compiler.semantics;

import org.eclipse.collections.api.map.primitive.MutableIntObjectMap;
import org.eclipse.collections.api.set.primitive.MutableIntSet;
import org.eclipse.collections.impl.map.mutable.primitive.IntObjectHashMap;
import org.eclipse.collections.impl.set.mutable.primitive.IntHashSet;
import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.symbols.*;
import org.mosslang.compiler.types.*;
import org.mosslang.compiler.driver.DiagnosticSink;
import org.mosslang.compiler.driver.DiagnosticCode;

public class SemanticChecker extends AstVisitorBase<TypeRef, SemanticContext> {
    private final DiagnosticSink diagnostics;
    private final TypeSolver typeSolver;
    private final MutableIntSet consumedSymbols = new IntHashSet();
    private final MutableIntObjectMap<Effect> inferredEffects = new IntObjectHashMap<>();

    public SemanticChecker(DiagnosticSink diagnostics, TypeSolver typeSolver) {
        this.diagnostics = diagnostics;
        this.typeSolver = typeSolver;
    }

    @Override
    public TypeRef visitLiteralExpr(LiteralExpr expr, SemanticContext ctx) {
        return expr.type();
    }

    @Override
    public TypeRef visitVariableExpr(VariableExpr expr, SemanticContext ctx) {
        Symbol sym = expr.resolvedSymbolId() > 0 
            ? ctx.symbolTable().getById(expr.resolvedSymbolId())
            : ctx.symbolTable().lookup(expr.name());
        if (sym == null) {
            diagnostics.error(DiagnosticCode.UNDECLARED_SYMBOL, "Undefined variable: " + expr.name(), expr.span());
            return PrimitiveType.NEVER;
        }
        if (consumedSymbols.contains(sym.symbolId())) {
            diagnostics.error(DiagnosticCode.USE_AFTER_CONSUME, "Use of moved variable: " + expr.name(), expr.span());
            return PrimitiveType.NEVER;
        }
        return sym.type();
    }

    @Override
    public TypeRef visitBinaryExpr(BinaryExpr expr, SemanticContext ctx) {
        TypeRef leftType = expr.left().accept(this, ctx);
        TypeRef rightType = expr.right().accept(this, ctx);

        switch (expr.op()) {
            case ADD, SUB, MUL, DIV, MOD -> {
                typeSolver.addConstraint(leftType, PrimitiveType.INT, ConstraintKind.EQUAL, "arithmetic left operand");
                typeSolver.addConstraint(rightType, PrimitiveType.INT, ConstraintKind.EQUAL, "arithmetic right operand");
                return PrimitiveType.INT;
            }
            case EQ, NEQ -> {
                typeSolver.addConstraint(leftType, rightType, ConstraintKind.EQUAL, "equality comparison");
                return PrimitiveType.BOOL;
            }
            case LT, LTE, GT, GTE -> {
                typeSolver.addConstraint(leftType, PrimitiveType.INT, ConstraintKind.EQUAL, "relational left operand");
                typeSolver.addConstraint(rightType, PrimitiveType.INT, ConstraintKind.EQUAL, "relational right operand");
                return PrimitiveType.BOOL;
            }
            case AND, OR -> {
                typeSolver.addConstraint(leftType, PrimitiveType.BOOL, ConstraintKind.EQUAL, "boolean operator");
                typeSolver.addConstraint(rightType, PrimitiveType.BOOL, ConstraintKind.EQUAL, "boolean operator");
                return PrimitiveType.BOOL;
            }
            default -> {
                return PrimitiveType.INT;
            }
        }
    }

    @Override
    public TypeRef visitIfStmt(IfStmt stmt, SemanticContext ctx) {
        TypeRef condType = stmt.condition().accept(this, ctx);
        typeSolver.addConstraint(condType, PrimitiveType.BOOL, ConstraintKind.EQUAL, "if condition");
        stmt.thenBranch().accept(this, ctx);
        if (stmt.elseBranch() != null) {
            stmt.elseBranch().accept(this, ctx);
        }
        return PrimitiveType.UNIT;
    }

    @Override
    public TypeRef visitReturnStmt(ReturnStmt stmt, SemanticContext ctx) {
        TypeRef valType = stmt.value() != null ? stmt.value().accept(this, ctx) : PrimitiveType.UNIT;
        if (ctx.currentReturnType() != null) {
            typeSolver.addConstraint(valType, ctx.currentReturnType(), ConstraintKind.SUBTYPE, "return expression");
        }
        return valType;
    }

    @Override
    public TypeRef visitFunctionDecl(FunctionDecl decl, SemanticContext ctx) {
        SemanticContext inner = ctx.withReturnType(decl.returnType());
        decl.body().accept(this, inner);
        return decl.returnType();
    }
}
