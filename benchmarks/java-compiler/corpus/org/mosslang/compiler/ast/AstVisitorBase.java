package org.mosslang.compiler.ast;

public class AstVisitorBase<R, C> implements AstVisitor<R, C> {
    @Override public R visitLiteralExpr(LiteralExpr expr, C context) { return null; }
    @Override public R visitVariableExpr(VariableExpr expr, C context) { return null; }
    @Override public R visitBinaryExpr(BinaryExpr expr, C context) {
        expr.left().accept(this, context);
        expr.right().accept(this, context);
        return null;
    }
    @Override public R visitUnaryExpr(UnaryExpr expr, C context) {
        expr.operand().accept(this, context);
        return null;
    }
    @Override public R visitCallExpr(CallExpr expr, C context) {
        for (Expr a : expr.arguments()) a.accept(this, context);
        return null;
    }
    @Override public R visitPipelineExpr(PipelineExpr expr, C context) {
        expr.initial().accept(this, context);
        for (Expr s : expr.stages()) s.accept(this, context);
        return null;
    }
    @Override public R visitMessageExpr(MessageExpr expr, C context) {
        for (Expr a : expr.arguments()) a.accept(this, context);
        return null;
    }
    @Override public R visitBlockStmt(BlockStmt stmt, C context) {
        for (Stmt s : stmt.statements()) s.accept(this, context);
        return null;
    }
    @Override public R visitLetStmt(LetStmt stmt, C context) {
        if (stmt.init() != null) stmt.init().accept(this, context);
        return null;
    }
    @Override public R visitVarStmt(VarStmt stmt, C context) {
        if (stmt.init() != null) stmt.init().accept(this, context);
        return null;
    }
    @Override public R visitReturnStmt(ReturnStmt stmt, C context) {
        if (stmt.value() != null) stmt.value().accept(this, context);
        return null;
    }
    @Override public R visitIfStmt(IfStmt stmt, C context) {
        stmt.condition().accept(this, context);
        stmt.thenBranch().accept(this, context);
        if (stmt.elseBranch() != null) stmt.elseBranch().accept(this, context);
        return null;
    }
    @Override public R visitWhileStmt(WhileStmt stmt, C context) {
        stmt.condition().accept(this, context);
        stmt.body().accept(this, context);
        return null;
    }
    @Override public R visitForStmt(ForStmt stmt, C context) {
        stmt.collection().accept(this, context);
        stmt.body().accept(this, context);
        return null;
    }
    @Override public R visitReplyStmt(ReplyStmt stmt, C context) {
        if (stmt.value() != null) stmt.value().accept(this, context);
        return null;
    }
    @Override public R visitParameterDecl(ParameterDecl decl, C context) { return null; }
    @Override public R visitFunctionDecl(FunctionDecl decl, C context) {
        for (ParameterDecl p : decl.parameters()) p.accept(this, context);
        decl.body().accept(this, context);
        return null;
    }
    @Override public R visitDomainDecl(DomainDecl decl, C context) {
        for (HandlerDecl h : decl.handlers()) h.accept(this, context);
        return null;
    }
    @Override public R visitHandlerDecl(HandlerDecl decl, C context) {
        for (ParameterDecl p : decl.parameters()) p.accept(this, context);
        decl.body().accept(this, context);
        return null;
    }
    @Override public R visitSourceFile(SourceFile file, C context) {
        for (DomainDecl d : file.domains()) d.accept(this, context);
        for (FunctionDecl f : file.functions()) f.accept(this, context);
        return null;
    }
}
