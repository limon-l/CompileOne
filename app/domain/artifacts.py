"""Domain models for compiler pipeline artifacts.

Pure data containers only — no I/O, no Qt, no subprocess. These mirror
the JSON artifacts emitted by the compileone backend (see
app/domain/schemas/). The UI and the services both depend on these
models, never on raw dicts.
"""

from __future__ import annotations

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
class PlaceholderArtifact:
    """Used for registered-but-not-yet-implemented phases."""

    phase: str
    message: str
