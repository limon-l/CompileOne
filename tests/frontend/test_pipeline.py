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
    assert STUDY_PHASES[1].available is True   # parse implemented
    assert STUDY_PHASES[2].available is True   # ast implemented
    assert STUDY_PHASES[3].available is True   # semantic implemented
    assert STUDY_PHASES[4].available is True   # ir implemented
    assert STUDY_PHASES[5].available is True   # opt implemented
    assert STUDY_PHASES[6].available is True   # codegen implemented


def test_parse_ast_semantic_consume_token_stream():
    """These phases re-derive their output from the token stream, so
    they must pin 'token_stream' as their input artifact."""
    for phase_id in ("parse", "ast", "semantic", "ir", "opt", "codegen"):
        phase = Pipeline().phase_by_id(phase_id)
        assert phase.input_artifact == "token_stream"


def test_previous_phase():
    pipeline = Pipeline()
    lex, parse = pipeline.phases[0], pipeline.phases[1]
    assert pipeline.previous_phase(parse) is lex
    assert pipeline.previous_phase(lex) is None


def test_phase_artifact_mapping():
    pipeline = Pipeline()
    assert pipeline.artifact_for("lex") == "token_stream"
    assert pipeline.artifact_for("parse") == "parse_tree"


def test_run_with_fake_runner_runs_all_available_phases(tmp_path, fake_runner):
    pipeline = Pipeline()
    store = ArtifactStore(output_dir=tmp_path / "out", cache_dir=tmp_path / "cache")
    results = pipeline.run(
        source_path=tmp_path / "hello.mc",
        store=store,
        runner=fake_runner,
        session_artifacts={},
    )
    assert fake_runner.calls == ["lex", "parse", "ast", "semantic", "ir", "opt", "codegen", "run"]
    assert all(r.status == PhaseStatus.OK for r in results)
    assert (tmp_path / "out" / "token_stream.json").is_file()
    assert (tmp_path / "out" / "parse_tree.json").is_file()
    assert (tmp_path / "out" / "ast.json").is_file()
    assert (tmp_path / "out" / "semantic.json").is_file()
    assert (tmp_path / "out" / "ir.json").is_file()
    assert (tmp_path / "out" / "optimization.json").is_file()
    assert (tmp_path / "out" / "assembly.json").is_file()
    assert (tmp_path / "out" / "execution.json").is_file()


def test_run_marks_roadmap_phases_unavailable_when_backend_missing(tmp_path):
    """A phase the backend has not shipped yet must surface as UNAVAILABLE
    (not ERROR) and must not leave a stale artifact behind."""
    from app.infrastructure.backend_runner import PhaseNotImplemented
    from tests.conftest import FakeRunner

    class PartialRunner(FakeRunner):
        def run_phase(self, phase, input_path, output_path, language="mini-c"):
            if phase in ("ir", "opt", "codegen"):
                raise PhaseNotImplemented(f"phase '{phase}' not implemented")
            return super().run_phase(phase, input_path, output_path, language=language)

    pipeline = Pipeline()
    store = ArtifactStore(output_dir=tmp_path / "out", cache_dir=tmp_path / "cache")
    session_artifacts = {"ir": {"schema": "compileone/ir/1.0", "tac": []}}
    results = pipeline.run(
        source_path=tmp_path / "hello.mc",
        store=store,
        runner=PartialRunner(),
        session_artifacts=session_artifacts,
    )
    by_id = {r.phase.id: r for r in results}
    assert by_id["ir"].status == PhaseStatus.UNAVAILABLE
    assert by_id["opt"].status == PhaseStatus.UNAVAILABLE
    assert by_id["codegen"].status == PhaseStatus.UNAVAILABLE
    assert "not implemented" in by_id["ir"].error
    assert "ir" not in session_artifacts  # stale artifact dropped
    assert not (tmp_path / "out" / "ir.json").is_file()


def test_run_halts_and_drops_stale_artifacts_on_phase_error(tmp_path, fake_runner):
    class FailingRunner(type(fake_runner)):
        def run_phase(self, phase, input_path, output_path, language="mini-c"):
            if phase == "parse":
                raise RuntimeError("parse failed")
            return super().run_phase(phase, input_path, output_path, language=language)

    pipeline = Pipeline()
    store = ArtifactStore(output_dir=tmp_path / "out", cache_dir=tmp_path / "cache")
    session_artifacts = {
        "parse_tree": {"schema": "compileone/parse-tree/1.0", "phase": "parse", "errors": []},
        "ast": {"schema": "compileone/ast/1.0", "phase": "ast", "errors": []},
    }
    results = pipeline.run(
        source_path=tmp_path / "hello.mc",
        store=store,
        runner=FailingRunner(),
        session_artifacts=session_artifacts,
    )

    by_id = {r.phase.id: r for r in results}
    assert by_id["lex"].status == PhaseStatus.OK
    assert by_id["parse"].status == PhaseStatus.ERROR
    assert by_id["parse"].error == "parse failed"
    assert by_id["ast"].status == PhaseStatus.SKIPPED
    assert by_id["semantic"].status == PhaseStatus.SKIPPED
    assert by_id["ir"].status == PhaseStatus.SKIPPED
    assert "parse_tree" not in session_artifacts
    assert "ast" not in session_artifacts


def test_native_languages_run_compile_phases_and_skip_interpreter(tmp_path, fake_runner):
    """
    Native languages run every compile phase through the pipeline, while
    the internal mini-c interpreter run phase remains skipped.
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
    assert fake_runner.calls == [
        "lex", "parse", "ast", "semantic", "ir", "opt", "codegen"
    ]
    assert results[1].status == PhaseStatus.OK
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


def test_embedded_lex_errors_attributed_and_block_downstream(tmp_path, fake_runner):
    """A lexer that finishes but embeds errors must be marked ERROR, and
    every phase that depends on it must be skipped."""
    lex_data = dict(fake_runner.token_stream_data)
    lex_data["errors"] = [
        {"message": "illegal character '@'", "line": 3, "column": 5},
    ]

    class LexerWithErrors(type(fake_runner)):
        def __init__(self):
            super().__init__()
            self.token_stream_data = lex_data

    runner = LexerWithErrors()
    pipeline = Pipeline()
    store = ArtifactStore(output_dir=tmp_path / "out", cache_dir=tmp_path / "cache")
    session_artifacts = {}
    results = pipeline.run(
        source_path=tmp_path / "hello.mc",
        store=store,
        runner=runner,
        session_artifacts=session_artifacts,
    )
    by_id = {r.phase.id: r for r in results}

    assert by_id["lex"].status == PhaseStatus.ERROR
    assert "illegal character" in by_id["lex"].error
    # Every artifact-input phase derives from the token stream -> blocked.
    for phase_id in ("parse", "ast", "semantic", "ir", "opt", "codegen", "run"):
        assert by_id[phase_id].status == PhaseStatus.SKIPPED, phase_id
        assert "lex" in by_id[phase_id].error
    # The errored artifact is kept so the UI can render partial data.
    assert "token_stream" in session_artifacts
    assert runner.calls == ["lex"]


def test_semantic_errors_attribute_only_to_semantic(tmp_path, fake_runner):
    """Semantic diagnostics of error severity fail semantic; warnings do not,
    and the interpreter (run) is skipped because semantic failed."""
    semantic_data = {
        "schema": "compileone/semantic/1.0",
        "phase": "semantic",
        "duration_ms": 0.3,
        "diagnostics": [
            {"severity": "warning", "code": "W001", "message": "unused var",
             "line": 1, "column": 1},
            {"severity": "error", "code": "E001", "message": "undefined: x",
             "line": 2, "column": 3},
        ],
    }

    class SemanticFailingRunner(type(fake_runner)):
        def run_phase(self, phase, input_path, output_path, language="mini-c"):
            if phase == "semantic":
                return semantic_data
            return super().run_phase(phase, input_path, output_path, language=language)

    pipeline = Pipeline()
    store = ArtifactStore(output_dir=tmp_path / "out", cache_dir=tmp_path / "cache")
    results = pipeline.run(
        source_path=tmp_path / "hello.mc",
        store=store,
        runner=SemanticFailingRunner(),
        session_artifacts={},
    )
    by_id = {r.phase.id: r for r in results}

    assert by_id["lex"].status == PhaseStatus.OK
    assert by_id["parse"].status == PhaseStatus.OK
    assert by_id["ast"].status == PhaseStatus.OK
    assert by_id["ir"].status == PhaseStatus.OK
    assert by_id["opt"].status == PhaseStatus.OK
    assert by_id["codegen"].status == PhaseStatus.OK
    assert by_id["semantic"].status == PhaseStatus.ERROR
    assert "undefined: x" in by_id["semantic"].error
    assert "E001" in by_id["semantic"].error
    # run depends on semantic -> blocked.
    assert by_id["run"].status == PhaseStatus.SKIPPED


def test_artifact_errors_detection():
    from app.application.pipeline import Pipeline

    assert Pipeline.artifact_errors({"errors": []}, "parse") == []
    assert Pipeline.artifact_errors({"errors": [{"message": "boom", "line": 2}]}, "parse") == \
        ["line 2: boom"]
    assert Pipeline.artifact_errors({"diagnostics": [{"severity": "warning"}]}, "semantic") == []
    assert Pipeline.artifact_errors(
        {"diagnostics": [{"severity": "error", "message": "m", "line": 4, "column": 1, "code": "X"}]},
        "semantic",
    ) == ["line 4:1: m [X]"]
    assert Pipeline.artifact_errors({"status": "ok", "errors": []}, "execution") == []
    assert Pipeline.artifact_errors(
        {"status": "runtime-error", "errors": [{"message": "div by zero", "line": 9, "column": 1}]},
        "execution",
    ) == ["line 9:1: div by zero"]

