"""Orchestrator use-case tests (with a fake backend runner)."""

from __future__ import annotations

import csv

from app.application.compile_session import CompileSession
from app.application.orchestrator import Orchestrator
from app.application.pipeline import PhaseStatus
from app.infrastructure.artifact_store import ArtifactStore
from tests.conftest import make_execution_data, make_token_stream_data


def _session(tmp_path, runner):
    return CompileSession(
        source_path=tmp_path / "hello.mc",
        language="mini-c",
        mode="study",
    ), ArtifactStore(output_dir=tmp_path / "out", cache_dir=tmp_path / "cache"), runner


def test_compile_all_populates_session(tmp_path, fake_runner):
    session, store, runner = _session(tmp_path, fake_runner)
    orchestrator = Orchestrator(runner=runner, store=store)
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
    orchestrator = Orchestrator(runner=runner, store=store)
    orchestrator.compile_all(session)

    assert session.error_count() == 1
    diag = session.diagnostics[0]
    assert diag.severity_name == "error"
    assert diag.code == "LEX001"
    assert diag.line == 4
    assert diag.phase == "lexical"


def test_inspect_unknown_phase_raises(fake_runner, tmp_path):
    session, store, runner = _session(tmp_path, fake_runner)
    orchestrator = Orchestrator(runner=runner, store=store)
    try:
        orchestrator.inspect_phase(session, "nope")
    except ValueError as exc:
        assert "unknown phase" in str(exc)
    else:
        raise AssertionError("expected ValueError")


def test_execute_runs_program_and_exposes_output(tmp_path, fake_runner):
    session, store, runner = _session(tmp_path, fake_runner)
    orchestrator = Orchestrator(runner=runner, store=store)
    orchestrator.execute(session)

    assert fake_runner.calls == ["run"]
    execution = orchestrator.execution_of(session)
    assert execution is not None
    assert execution.ok
    assert execution.output == ["7", "9"]
    assert execution.exit_code == 0
    assert session.error_count() == 0


def test_execute_surfaces_runtime_errors(tmp_path):
    runner = _Runner(make_execution_data(
        output=["halfway there"],
        status="runtime-error",
        errors=[{"line": 4, "column": 7, "message": "division by zero"}],
    ))
    session, store, _ = _session(tmp_path, runner)
    orchestrator = Orchestrator(runner=runner, store=store)
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
    orchestrator = Orchestrator(runner=runner, store=store)
    orchestrator.compile_all(session)

    out = tmp_path / "tokens.csv"
    orchestrator.export_tokens_csv(session, out)
    with open(out, encoding="utf-8", newline="") as fh:
        rows = list(csv.reader(fh))
    assert rows[0][0] == "id"
    assert len(rows) == 3  # header + 2 tokens
    assert rows[1][4] == "KEYWORD_INT"


class _Runner:
    def __init__(self, data):
        self._data = data

    def available(self):
        return True

    def run_phase(self, phase, input_path, output_path, language="mini-c"):
        return self._data
