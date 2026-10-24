package org.mosslang.compiler.driver;

import org.mosslang.compiler.ast.SourceFile;
import org.mosslang.compiler.symbols.SymbolTable;
import org.mosslang.compiler.types.TypeSolver;
import org.mosslang.compiler.ir.IrModule;

public class CompilerContext {
    private final CompilerConfig config;
    private final DiagnosticSink diagnostics;
    private SourceFile ast;
    private SymbolTable symbolTable;
    private TypeSolver typeSolver;
    private IrModule irModule;
    private byte[] abiBytes;

    public CompilerContext(CompilerConfig config, DiagnosticSink diagnostics) {
        this.config = config;
        this.diagnostics = diagnostics;
    }

    public CompilerConfig getConfig() { return config; }
    public DiagnosticSink getDiagnostics() { return diagnostics; }
    public SourceFile getAst() { return ast; }
    public void setAst(SourceFile ast) { this.ast = ast; }
    public SymbolTable getSymbolTable() { return symbolTable; }
    public void setSymbolTable(SymbolTable symbolTable) { this.symbolTable = symbolTable; }
    public TypeSolver getTypeSolver() { return typeSolver; }
    public void setTypeSolver(TypeSolver typeSolver) { this.typeSolver = typeSolver; }
    public IrModule getIrModule() { return irModule; }
    public void setIrModule(IrModule irModule) { this.irModule = irModule; }
    public byte[] getAbiBytes() { return abiBytes; }
    public void setAbiBytes(byte[] abiBytes) { this.abiBytes = abiBytes; }
}
