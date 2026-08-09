"""Domain models for compiler pipeline artifacts.

Pure data containers only — no I/O, no Qt, no subprocess. These mirror
the JSON artifacts emitted by the compileone backend (see
app/domain/schemas/). The UI and the services both depend on these
models, never on raw dicts.
"""

from __future__ import annotations

import dataclasses
from dataclasses import dataclass
from typing import Any


@dataclass
class Token:
    """One lexical token as emitted by the flex scanner."""

    id: int
    line: int
    column: int
    lexeme: str
    token: str
    category: str
    offset_start: int
    offset_end: int


@dataclass
class LexicalError:
    """A lexical diagnostic produced by the scanner."""

    line: int
    column: int
    lexeme: str
    message: str


@dataclass
class TokenStream:
    """The full lexical-analysis artifact."""

    schema: str
    phase: str
    language: str
    source_file: str
    generated_by: str
    duration_ms: float
    tokens: list[Token]
    statistics: dict[str, Any]
    errors: list[LexicalError]

    @property
    def error_count(self) -> int:
        return len(self.errors)

    @property
    def token_count(self) -> int:
        return len(self.tokens)


@dataclass
class ExecutionError:
    """A runtime (or residual lexical) error reported by the interpreter."""

    line: int
    column: int
    message: str


@dataclass
class Execution:
    """The execution-phase artifact: program stdout plus the run summary."""

    schema: str
    phase: str
    language: str
    source_file: str
    generated_by: str
    duration_ms: float
    status: str
    exit_code: int
    steps: int
    output: list[str]
    errors: list[ExecutionError]

    @property
    def ok(self) -> bool:
        return self.status == "ok"




@dataclass
class ParseTreeNode:
    """A node in the Concrete Syntax Tree (CST)."""
    
    rule_name: str | None = None
    token: Token | None = None
    children: list[ParseTreeNode] = dataclasses.field(default_factory=list)

    @property
    def is_terminal(self) -> bool:
        """A terminal node holds a token and has no children."""
        return self.token is not None


@dataclass
class ParseTree:
    """The full parse-tree artifact."""

    schema: str
    phase: str
    language: str
    source_file: str
    generated_by: str
    duration_ms: float
    root: ParseTreeNode | None = None
    errors: list[Any] = dataclasses.field(default_factory=list)  # Placeholder for syntax errors


@dataclass
class ASTNode:
    """A node in the Abstract Syntax Tree (AST)."""
    
    node_type: str
    token: Token | None = None
    attributes: dict[str, Any] = dataclasses.field(default_factory=dict)
    children: list[ASTNode] = dataclasses.field(default_factory=list)


@dataclass
class AbstractSyntaxTree:
    """The full abstract-syntax-tree artifact."""

    schema: str
    phase: str
    language: str
    source_file: str
    generated_by: str
    duration_ms: float
    root: ASTNode | None = None
    errors: list[Any] = dataclasses.field(default_factory=list)


@dataclass
class SymbolInfo:
    """One declared symbol as reported by the semantic analyzer."""

    name: str
    type: str
    scope: str
    scope_level: int
    is_const: bool
    line: int
    column: int


@dataclass
class SemanticDiagnostic:
    """A semantic analyzer diagnostic (error or warning)."""

    severity: str
    code: str
    message: str
    line: int
    column: int


@dataclass
class SemanticInfo:
    """The full semantic-analysis artifact."""

    schema: str
    phase: str
    language: str
    source_file: str
    generated_by: str
    duration_ms: float
    valid: bool
    symbols: list[SymbolInfo] = dataclasses.field(default_factory=list)
    diagnostics: list[SemanticDiagnostic] = dataclasses.field(default_factory=list)

    @property
    def error_count(self) -> int:
        return sum(1 for d in self.diagnostics if d.severity == "error")

    @property
    def warning_count(self) -> int:
        return sum(1 for d in self.diagnostics if d.severity == "warning")


@dataclass
class IrInstruction:
    """One three-address-code (TAC) instruction / quadruple."""

    index: int
    op: str
    arg1: str = ""
    arg2: str = ""
    result: str = ""

    @property
    def text(self) -> str:
        """Human-readable TAC source form (mirrors the backend renderer)."""
        if self.op == "label":
            return f"{self.result}:"
        if self.op == "goto":
            return f"goto {self.result}"
        if self.op == "if_false":
            return f"if_false {self.arg1} goto {self.result}"
        if self.op == "declare":
            parts = ["declare", self.arg1 or "int"]
            if self.arg2:
                parts.append(self.arg2)
            parts.append(self.result or "?")
            return " ".join(parts)
        if self.result:
            if self.arg2:
                return f"{self.result} = {self.arg1} {self.op} {self.arg2}"
            return f"{self.result} = {self.arg1}"
        if self.arg1:
            if self.arg2:
                return f"{self.op} {self.arg1} {self.arg2}"
            return f"{self.op} {self.arg1}"
        return self.op


@dataclass
class IrInfo:
    """The full intermediate-representation artifact (TAC)."""

    schema: str
    phase: str
    language: str
    source_file: str
    generated_by: str
    duration_ms: float
    tac: list[IrInstruction] = dataclasses.field(default_factory=list)
    quadruples: list[IrInstruction] = dataclasses.field(default_factory=list)
    temporaries: list[str] = dataclasses.field(default_factory=list)
    labels: list[str] = dataclasses.field(default_factory=list)
    errors: list[Any] = dataclasses.field(default_factory=list)

    @property
    def instruction_count(self) -> int:
        return len(self.tac)


@dataclass
class OptPass:
    """Evidence for a single optimizer pass (before/after listing)."""

    name: str
    applied: bool
    explanation: str
    instruction_reduction: int
    removed_instructions: list[IrInstruction] = dataclasses.field(default_factory=list)
    before: list[str] = dataclasses.field(default_factory=list)
    after: list[str] = dataclasses.field(default_factory=list)


@dataclass
class OptInfo:
    """The full optimization artifact: metrics plus pass evidence."""

    schema: str
    phase: str
    language: str
    source_file: str
    generated_by: str
    duration_ms: float
    before_instruction_count: int = 0
    after_instruction_count: int = 0
    instruction_reduction_pct: float = 0.0
    passes: list[OptPass] = dataclasses.field(default_factory=list)
    errors: list[Any] = dataclasses.field(default_factory=list)


@dataclass
class AsmInstruction:
    """One emitted assembly instruction (for highlighting/annotation)."""

    address: str
    mnemonic: str
    operands: list[str] = dataclasses.field(default_factory=list)
    class_name: str = ""
    comment: str | None = None
    label: str | None = None

    @property
    def text(self) -> str:
        parts = [self.mnemonic]
        if self.operands:
            parts.append(", ".join(self.operands))
        return " ".join(parts)


@dataclass
class AsmSlot:
    """A stack slot allocated for a variable or temporary."""

    name: str
    offset: int
    size: int


@dataclass
class AssemblyInfo:
    """The full code-generation artifact: listing + stack layout."""

    schema: str
    phase: str
    language: str
    source_file: str
    generated_by: str
    duration_ms: float
    arch: str = "x86_64"
    syntax: str = "att"
    text: str = ""
    instructions: list[AsmInstruction] = dataclasses.field(default_factory=list)
    prologue: list[str] = dataclasses.field(default_factory=list)
    epilogue: list[str] = dataclasses.field(default_factory=list)
    stack_layout: dict[str, Any] = dataclasses.field(default_factory=dict)
    errors: list[Any] = dataclasses.field(default_factory=list)

    @property
    def stack_size(self) -> int:
        return int(self.stack_layout.get("total_size", 0))

    @property
    def slots(self) -> list[AsmSlot]:
        raw = self.stack_layout.get("slots", [])
        return [AsmSlot(name=str(s.get("name", "?")), offset=int(s.get("offset", 0)),
                        size=int(s.get("size", 4))) for s in raw]
