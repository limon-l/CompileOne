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
    subtype: str
    length: int
    scope: str
    scope_level: int
    color: str
    description: str
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
