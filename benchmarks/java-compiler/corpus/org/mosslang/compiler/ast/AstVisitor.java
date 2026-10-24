package org.mosslang.compiler.ast;

public interface AstVisitor<R, C> {
    R visitLiteralExpr(LiteralExpr expr, C context);
    R visitVariableExpr(VariableExpr expr, C context);
    R visitBinaryExpr(BinaryExpr expr, C context);
    R visitUnaryExpr(UnaryExpr expr, C context);
    R visitCallExpr(CallExpr expr, C context);
    R visitPipelineExpr(PipelineExpr expr, C context);
    R visitMessageExpr(MessageExpr expr, C context);

    R visitBlockStmt(BlockStmt stmt, C context);
    R visitLetStmt(LetStmt stmt, C context);
    R visitVarStmt(VarStmt stmt, C context);
    R visitReturnStmt(ReturnStmt stmt, C context);
    R visitIfStmt(IfStmt stmt, C context);
    R visitWhileStmt(WhileStmt stmt, C context);
    R visitForStmt(ForStmt stmt, C context);
    R visitReplyStmt(ReplyStmt stmt, C context);

    R visitParameterDecl(ParameterDecl decl, C context);
    R visitFunctionDecl(FunctionDecl decl, C context);
    R visitDomainDecl(DomainDecl decl, C context);
    R visitHandlerDecl(HandlerDecl decl, C context);
    R visitSourceFile(SourceFile file, C context);
}
