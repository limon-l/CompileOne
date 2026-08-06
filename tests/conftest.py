"""Shared test fixtures.

Sets QT_QPA_PLATFORM=offscreen before any Qt import so widget/model
tests can run headless, and provides factory helpers that build domain
models and raw backend-shaped artifacts.
"""

from __future__ import annotations

import os

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from typing import Any

import pytest

from app.domain.artifacts import Token


def make_token(**overrides: Any) -> Token:
    fields = {
        "id": 1,
        "line": 1,
        "column": 1,
        "lexeme": "int",
        "token": "KEYWORD_INT",
        "category": "keyword",
        "subtype": "",
        "length": 3,
        "scope": "global",
        "scope_level": 0,
        "color": "#569cd6",
        "description": "the int keyword",
        "offset_start": 0,
        "offset_end": 3,
    }
    fields.update(overrides)
    return Token(**fields)


def make_token_raw(**overrides: Any) -> dict[str, Any]:
    """Raw backend-shaped token dict (offsets nested under 'offset')."""
    base = {
        "id": 1,
        "line": 1,
        "column": 1,
        "lexeme": "int",
        "token": "KEYWORD_INT",
        "category": "keyword",
        "subtype": "",
        "length": 3,
        "scope": "global",
        "scope_level": 0,
        "color": "#569cd6",
        "description": "the int keyword",
        "offset_start": 0,
        "offset_end": 3,
    }
    base.update(overrides)
    offset = {"start": base.pop("offset_start"), "end": base.pop("offset_end")}
    base["offset"] = offset
    return base


def make_token_stream_data(
    tokens: list[dict[str, Any]] | None = None,
    errors: list[dict[str, Any]] | None = None,
    language: str = "mini-c",
) -> dict[str, Any]:
    if tokens is None:
        tokens = [
            make_token_raw(),
            make_token_raw(
                id=2,
                lexeme="main",
                token="IDENTIFIER",
                category="identifier",
                column=5,
                length=4,
                offset_start=4,
                offset_end=8,
            ),
        ]
    return {
        "schema": "compileone/token-stream/1.0",
        "phase": "lexical",
        "language": language,
        "source_file": "example.mc",
        "generated_by": "compileone lex (flex)",
        "duration_ms": 0.42,
        "tokens": tokens,
        "statistics": {"token_count": len(tokens), "by_category": {"keyword": 1, "identifier": 1}},
        "errors": errors or [],
    }


def make_execution_data(
    output: list[str] | None = None,
    errors: list[dict[str, Any]] | None = None,
    status: str = "ok",
) -> dict[str, Any]:
    """Raw backend-shaped execution artifact."""
    return {
        "schema": "compileone/execution/1.0",
        "phase": "execution",
        "language": "mini-c",
        "source_file": "example.mc",
        "generated_by": "compileone.exe v0.1.0 (interpreter)",
        "duration_ms": 0.9,
        "status": status,
        "exit_code": 1 if status != "ok" else 0,
        "steps": 4,
        "output": output if output is not None else ["7", "9"],
        "errors": errors or [],
    }


class FakeRunner:
    """BackendRunner substitute: returns canned artifacts, records calls."""

    def __init__(self, token_stream_data: dict[str, Any] | None = None,
                 execution_data: dict[str, Any] | None = None) -> None:
        self.token_stream_data = token_stream_data or make_token_stream_data()
        self.execution_data = execution_data or make_execution_data()
        self.calls: list[str] = []

    def available(self) -> bool:
        return True

    def run_phase(self, phase: str, input_path, output_path, language: str = "mini-c") -> dict[str, Any]:
        self.calls.append(phase)
        if phase == "lex":
            return self.token_stream_data
        if phase == "run":
            return self.execution_data
        from app.infrastructure.backend_runner import PhaseNotImplemented

        raise PhaseNotImplemented(f"phase '{phase}' not implemented")


@pytest.fixture
def fake_runner() -> FakeRunner:
    return FakeRunner()
