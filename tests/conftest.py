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


def make_ir_data() -> dict[str, Any]:
    """Raw backend-shaped IR artifact (TAC + quadruples)."""
    return {
        "schema": "compileone/ir/1.0",
        "phase": "ir",
        "language": "mini-c",
        "source_file": "example.mc",
        "generated_by": "compileone ir",
        "duration_ms": 0.4,
        "tac": [
            {"index": 1, "op": "mov", "arg1": "7", "arg2": "", "result": "t0"},
            {"index": 2, "op": "mov", "arg1": "9", "arg2": "", "result": "t1"},
            {"index": 3, "op": "add", "arg1": "t0", "arg2": "t1", "result": "t2"},
        ],
        "quadruples": [],
        "temporaries": ["t0", "t1", "t2"],
        "labels": [],
        "errors": [],
    }


def make_optimization_data() -> dict[str, Any]:
    """Raw backend-shaped optimization artifact."""
    return {
        "schema": "compileone/optimization/1.0",
        "phase": "optimization",
        "language": "mini-c",
        "source_file": "example.mc",
        "generated_by": "compileone opt",
        "duration_ms": 0.3,
        "before_instruction_count": 3,
        "after_instruction_count": 2,
        "instruction_reduction_pct": 33.3,
        "passes": [
            {
                "name": "constant-folding",
                "applied": True,
                "explanation": "folded 7 + 9 into 16",
                "instruction_reduction": 1,
                "removed_instructions": [],
                "before": ["add t0, t1, t2"],
                "after": ["mov t0, 16"],
            }
        ],
        "errors": [],
    }


def make_assembly_data() -> dict[str, Any]:
    """Raw backend-shaped assembly artifact."""
    return {
        "schema": "compileone/assembly/1.0",
        "phase": "codegen",
        "language": "mini-c",
        "source_file": "example.mc",
        "generated_by": "compileone codegen",
        "duration_ms": 0.5,
        "arch": "x86_64",
        "syntax": "att",
        "text": "movl $7, %eax\nmovl $9, %ebx\naddl %ebx, %eax\n",
        "instructions": [
            {"address": "0x0", "mnemonic": "movl", "operands": ["$7", "%eax"], "class": "", "comment": None, "label": None},
            {"address": "0x5", "mnemonic": "movl", "operands": ["$9", "%ebx"], "class": "", "comment": None, "label": None},
            {"address": "0xa", "mnemonic": "addl", "operands": ["%ebx", "%eax"], "class": "", "comment": None, "label": None},
        ],
        "prologue": [],
        "epilogue": [],
        "stack_layout": {},
        "errors": [],
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

        if phase == "parse":
            return {
                "schema": "compileone/parse-tree/1.0",
                "phase": "parse",
                "language": language,
                "source_file": str(input_path),
                "generated_by": "compileone parse",
                "duration_ms": 0.3,
                "root": {"rule_name": "program", "children": []},
                "errors": [],
            }
        if phase == "ast":
            return {
                "schema": "compileone/ast/1.0",
                "phase": "ast",
                "language": language,
                "source_file": str(input_path),
                "generated_by": "compileone ast",
                "duration_ms": 0.3,
                "root": {"node_type": "Program", "children": []},
                "errors": [],
            }
        if phase == "semantic":
            return {
                "schema": "compileone/semantic/1.0",
                "phase": "semantic",
                "language": language,
                "source_file": str(input_path),
                "generated_by": "compileone semantic",
                "duration_ms": 0.3,
                "valid": True,
                "symbols": [],
                "diagnostics": [],
            }
        if phase == "ir":
            return make_ir_data()
        if phase == "opt":
            return make_optimization_data()
        if phase == "codegen":
            return make_assembly_data()
        raise PhaseNotImplemented(f"phase '{phase}' not implemented")


@pytest.fixture
def fake_runner() -> FakeRunner:
    return FakeRunner()


@pytest.fixture(scope="session")
def qapp():
    """A single headless QApplication shared by all widget tests."""
    from PyQt5.QtWidgets import QApplication

    app = QApplication.instance() or QApplication([])
    yield app
    app.processEvents()
