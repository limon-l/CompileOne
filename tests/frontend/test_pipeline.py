"""Pipeline definition and execution tests."""

from __future__ import annotations

from app.application.pipeline import STUDY_PHASES, PhaseStatus, Pipeline
from app.infrastructure.artifact_store import ArtifactStore


def test_study_pipeline_layout():
    assert [p.id for p in STUDY_PHASES] == [
        "lex", "parse", "ast", "semantic", "ir", "opt", "codegen", "run",
    ]
    assert STUDY_PHASES[0].input_kind == "source"
    assert STUDY_PHASES[-1].input_kind == "source"  # execution reads source again
    assert all(p.input_kind == "artifact" for p in STUDY_PHASES[1:-1])
    assert STUDY_PHASES[0].available is True
    assert STUDY_PHASES[-1].available is True  # interpreter implemented
    assert all(not p.available for p in STUDY_PHASES[1:-1])


def test_previous_phase():
    pipeline = Pipeline()
    lex, parse = pipeline.phases[0], pipeline.phases[1]
    assert pipeline.previous_phase(parse) is lex
    assert pipeline.previous_phase(lex) is None


def test_phase_artifact_mapping():
    pipeline = Pipeline()
    assert pipeline.artifact_for("lex") == "token_stream"
    assert pipeline.artifact_for("parse") == "parse_tree"


def test_run_with_fake_runner_runs_lex_and_execution(tmp_path, fake_runner):
    pipeline = Pipeline()
    store = ArtifactStore(output_dir=tmp_path / "out", cache_dir=tmp_path / "cache")
    results = pipeline.run(
        source_path=tmp_path / "hello.mc",
        store=store,
        runner=fake_runner,
        session_artifacts={},
    )
    assert fake_runner.calls == ["lex", "run"]
    assert results[0].status == PhaseStatus.OK
    assert all(r.status == PhaseStatus.UNAVAILABLE for r in results[1:-1])
    assert results[-1].status == PhaseStatus.OK
    assert (tmp_path / "out" / "token_stream.json").is_file()
    assert (tmp_path / "out" / "execution.json").is_file()


def test_run_phase_skipped_for_native_languages(tmp_path, fake_runner):
    """
    The mini-c interpreter must not be invoked when lexing/compiling
    native languages (C/C++/Java); those are run by their own toolchain.
    """
    pipeline = Pipeline()
    store = ArtifactStore(output_dir=tmp_path / "out", cache_dir=tmp_path / "cache")
    results = pipeline.run(
        source_path=tmp_path / "hello.cpp",
        store=store,
        runner=fake_runner,
        session_artifacts={},
        language="c++",
    )
    assert fake_runner.calls == ["lex"]
    assert results[-1].status == PhaseStatus.SKIPPED
    assert "native" in results[-1].error


def test_breakpoint_halts_pipeline(fake_runner, tmp_path):
    pipeline = Pipeline()
    pipeline.breakpoints = {"lex"}
    results = pipeline.run(
        source_path=tmp_path / "hello.mc",
        store=ArtifactStore(output_dir=tmp_path / "out", cache_dir=tmp_path / "cache"),
        runner=fake_runner,
        session_artifacts={},
    )
    assert all(r.status == PhaseStatus.SKIPPED for r in results)
    assert fake_runner.calls == []
