"""Orchestrator use-case tests (with a fake backend runner)."""

from __future__ import annotations

import csv

from app.application.compile_session import CompileSession
from app.application.orchestrator import Orchestrator
from app.application.pipeline import PhaseStatus, Pipeline
from app.infrastructure.artifact_store import ArtifactStore
from app.infrastructure.tool_detector import Toolchain
from tests.conftest import (
    make_assembly_data,
    make_execution_data,
    make_ir_data,
    make_optimization_data,
    make_token_stream_data,
)


def _session(tmp_path, runner):
    return CompileSession(
        source_path=tmp_path / "hello.mc",
        language="mini-c",
        mode="study",
    ), ArtifactStore(output_dir=tmp_path / "out", cache_dir=tmp_path / "cache"), runner


def _orchestrator(runner, store):
    return Orchestrator(
        pipeline=Pipeline(),
        runner=runner,
        store=store,
        toolchain=Toolchain(),
    )


def test_compile_all_populates_session(tmp_path, fake_runner):
    session, store, runner = _session(tmp_path, fake_runner)
    orchestrator = _orchestrator(runner, store)
    orchestrator.compile_all(session)

    assert session.artifacts["token_stream"] is fake_runner.token_stream_data
    assert session.phase_results[0].status == PhaseStatus.OK
    assert session.error_count() == 0
    stream = orchestrator.token_stream_of(session)
    assert stream.token_count == 2


def test_compile_all_surfaces_lexical_errors(tmp_path):
    data = make_token_stream_data(errors=[
        {"line": 4, "column": 7, "lexeme": "@", "message": "Unknown character"},
    ])
    runner = _Runner(data)
    session, store, _ = _session(tmp_path, runner)
    orchestrator = _orchestrator(runner, store)
    orchestrator.compile_all(session)

    assert session.error_count() == 1
    diag = session.diagnostics[0]
    assert diag.severity_name == "error"
    assert diag.code == "LEX001"
    assert diag.line == 4
    assert diag.phase == "lexical"


def test_compile_all_surfaces_semantic_diagnostics(tmp_path):
    runner = _Runner(make_token_stream_data(), semantic_diagnostics=[
        {"severity": "error", "code": "SEM003", "line": 8, "column": 1,
         "message": "cannot assign to const variable 'b'"},
        {"severity": "warning", "code": "SEM009", "line": 12, "column": 1,
         "message": "variable 'x' is declared but never used"},
    ])
    session, store, _ = _session(tmp_path, runner)
    orchestrator = _orchestrator(runner, store)
    orchestrator.compile_all(session)

    errors = [d for d in session.diagnostics if d.severity_name == "error"]
    warnings = [d for d in session.diagnostics if d.severity_name == "warning"]
    assert len(errors) == 1
    assert errors[0].code == "SEM003"
    assert errors[0].phase == "semantic"
    assert errors[0].line == 8
    assert len(warnings) == 1
    assert warnings[0].code == "SEM009"


def test_inspect_unknown_phase_raises(fake_runner, tmp_path):
    session, store, runner = _session(tmp_path, fake_runner)
    orchestrator = _orchestrator(runner, store)
    try:
        orchestrator.inspect_phase(session, "nope")
    except ValueError as exc:
        assert "unknown phase" in str(exc)
    else:
        raise AssertionError("expected ValueError")


def test_execute_runs_program_and_exposes_output(tmp_path, fake_runner):
    session, store, runner = _session(tmp_path, fake_runner)
    orchestrator = _orchestrator(runner, store)
    orchestrator.execute(session)

    assert fake_runner.calls == ["run"]
    execution = orchestrator.execution_of(session)
    assert execution is not None
    assert execution.ok
    assert execution.output == ["7", "9"]
    assert execution.exit_code == 0
    assert session.error_count() == 0


def test_execute_surfaces_runtime_errors(tmp_path):
    runner = _Runner(make_token_stream_data(), execution_data=make_execution_data(
        output=["halfway there"],
        status="runtime-error",
        errors=[{"line": 4, "column": 7, "message": "division by zero"}],
    ))
    session, store, _ = _session(tmp_path, runner)
    orchestrator = _orchestrator(runner, store)
    orchestrator.execute(session)

    execution = orchestrator.execution_of(session)
    assert execution is not None
    assert not execution.ok
    assert execution.errors[0].message == "division by zero"
    assert session.error_count() == 1
    diag = session.diagnostics[0]
    assert diag.code == "RUN001"
    assert diag.line == 4
    assert diag.column == 7
    assert diag.phase == "execution"


def test_export_tokens_csv(tmp_path, fake_runner):
    session, store, runner = _session(tmp_path, fake_runner)
    orchestrator = _orchestrator(runner, store)
    orchestrator.compile_all(session)

    out = tmp_path / "tokens.csv"
    orchestrator.export_tokens_csv(session, out)
    with open(out, encoding="utf-8", newline="") as fh:
        rows = list(csv.reader(fh))
    assert rows[0] == ["line", "column", "lexeme", "token", "category", "offset_start", "offset_end"]
    assert len(rows) == 3  # header + 2 tokens
    assert rows[1][3] == "KEYWORD_INT"


class _Runner:
    def __init__(self, data, execution_data=None, semantic_diagnostics=None):
        self._data = data
        self._execution_data = execution_data or make_execution_data()
        self._semantic_diagnostics = semantic_diagnostics

    def available(self):
        return True

    def run_phase(self, phase, input_path, output_path, language="mini-c"):
        if phase == "lex":
            return self._data
        if phase == "parse":
            return {
                "schema": "compileone/parse-tree/1.0", "phase": "parse",
                "language": language, "source_file": str(input_path),
                "generated_by": "compileone parse", "duration_ms": 0.1,
                "root": {"rule_name": "program", "children": []}, "errors": [],
            }
        if phase == "ast":
            return {
                "schema": "compileone/ast/1.0", "phase": "ast",
                "language": language, "source_file": str(input_path),
                "generated_by": "compileone ast", "duration_ms": 0.1,
                "root": {"node_type": "Program", "children": []}, "errors": [],
            }
        if phase == "semantic":
            return {
                "schema": "compileone/semantic/1.0", "phase": "semantic",
                "language": language, "source_file": str(input_path),
                "generated_by": "compileone semantic", "duration_ms": 0.1,
                "valid": True,
                "symbols": [],
                "diagnostics": self._semantic_diagnostics or [],
            }
        if phase == "ir":
            return make_ir_data()
        if phase == "opt":
            return make_optimization_data()
        if phase == "codegen":
            return make_assembly_data()
        return self._execution_data
