package org.mosslang.compiler.abi;

public enum CheckedExprKind {
    Parameter(0),
    IntegerLiteral(1),
    Add(2),
    Field(3),
    VectorLiteral(4),
    Call(5),
    FloatLiteral(6),
    BoolLiteral(7),
    StringLiteral(8),
    LocalRead(9),
    Unary(10),
    Binary(11),
    Message(12),
    MapLiteral(13),
    NominalConstruct(14),
    CollectionCount(15),
    Index(16),
    MemberCall(17),
    CollectionPush(18),
    PipelineElement(19),
    PipelineMap(20),
    EnumConstruct(21);

    public final int tag;
    CheckedExprKind(int tag) { this.tag = tag; }
}
