"""Schema-aware JSON deserialization.

The compileone backend and the UI communicate exclusively through JSON
artifacts. This module loads artifact files and converts them into the
typed domain models (app/domain/artifacts.py), validating structure as
it goes. It never performs lexical/syntactic analysis of source code.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from app.domain.artifacts import (
    Execution,
    ExecutionError,
    LexicalError,
    Token,
    TokenStream,
)


class ArtifactError(ValueError):
    """Raised when an artifact file cannot be parsed or validated."""


def load_json(path: Path) -> dict[str, Any]:
    """Read and parse a JSON file, raising ArtifactError on failure."""
    try:
        with open(path, "r", encoding="utf-8") as fh:
            return json.load(fh)
    except FileNotFoundError as exc:
        raise ArtifactError(f"artifact file not found: {path}") from exc
    except json.JSONDecodeError as exc:
        raise ArtifactError(f"artifact file is not valid JSON: {path} ({exc})") from exc


def _expect(data: dict[str, Any], key: str) -> Any:
    if key not in data:
        raise ArtifactError(f"artifact missing required field: {key!r}")
    return data[key]


def parse_token_stream(data: dict[str, Any]) -> TokenStream:
    """Validate and convert a token-stream artifact into a TokenStream."""
    _expect(data, "schema")
    _expect(data, "tokens")
    _expect(data, "statistics")

    tokens: list[Token] = []
    for raw in data["tokens"]:
        off = _expect(raw, "offset")
        tokens.append(
            Token(
                id=int(_expect(raw, "id")),
                line=int(_expect(raw, "line")),
                column=int(_expect(raw, "column")),
                lexeme=str(_expect(raw, "lexeme")),
                token=str(_expect(raw, "token")),
                category=str(_expect(raw, "category")),
                subtype=str(_expect(raw, "subtype")),
                length=int(_expect(raw, "length")),
                scope=str(_expect(raw, "scope")),
                scope_level=int(_expect(raw, "scope_level")),
                color=str(_expect(raw, "color")),
                description=str(_expect(raw, "description")),
                offset_start=int(_expect(off, "start")),
                offset_end=int(_expect(off, "end")),
            )
        )

    errors: list[LexicalError] = []
    for raw in data.get("errors", []):
        errors.append(
            LexicalError(
                line=int(_expect(raw, "line")),
                column=int(_expect(raw, "column")),
                lexeme=str(_expect(raw, "lexeme")),
                message=str(_expect(raw, "message")),
            )
        )

    return TokenStream(
        schema=str(data.get("schema", "")),
        phase=str(data.get("phase", "")),
        language=str(data.get("language", "")),
        source_file=str(data.get("source_file", "")),
        generated_by=str(data.get("generated_by", "")),
        duration_ms=float(data.get("duration_ms", 0.0)),
        tokens=tokens,
        statistics=dict(data.get("statistics", {})),
        errors=errors,
    )


def parse_execution(data: dict[str, Any]) -> Execution:
    """Validate and convert an execution artifact into an Execution."""
    _expect(data, "schema")
    _expect(data, "output")
    _expect(data, "errors")

    errors: list[ExecutionError] = []
    for raw in data["errors"]:
        errors.append(
            ExecutionError(
                line=int(raw.get("line", 1)),
                column=int(raw.get("column", 1)),
                message=str(_expect(raw, "message")),
            )
        )

    return Execution(
        schema=str(data.get("schema", "")),
        phase=str(data.get("phase", "")),
        language=str(data.get("language", "")),
        source_file=str(data.get("source_file", "")),
        generated_by=str(data.get("generated_by", "")),
        duration_ms=float(data.get("duration_ms", 0.0)),
        status=str(data.get("status", "ok")),
        exit_code=int(data.get("exit_code", 0)),
        steps=int(data.get("steps", 0)),
        output=[str(line) for line in data["output"]],
        errors=errors,
    )
