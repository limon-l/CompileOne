"""Language switching and stale-result prevention across the real pipeline.

These tests drive the full Orchestrator (Pipeline + BackendRunner +
ArtifactStore) and verify two properties the UI depends on:

1. Per-language phase regeneration: when the same session switches from
   C -> C++ -> Java, each compilation re-lexes the current source and
   produces a token stream whose language matches the session language.
2. Stale-result prevention: recompiling a *different* C source in the
   same session must replace (never keep) the previous token stream, and
   mini-c-only artifacts must be dropped for native languages.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from app.application.compile_session import CompileSession
from app.application.orchestrator import Orchestrator
from app.application.pipeline import Pipeline, PhaseStatus
from app.infrastructure.artifact_store import ArtifactStore
from app.infrastructure.backend_runner import BackendRunner
from app.infrastructure.paths import PROJECT_ROOT
from app.infrastructure.tool_detector import detect_toolchain

pytestmark = pytest.mark.integration

C_SRC_A = "#include <stdio.h>\nint main() { int x = 1; return x; }\n"
C_SRC_B = "#include <stdio.h>\nint main() { int y = 99; return y; }\n"
CPP_SRC = "#include <iostream>\nint main() { std::cout << 5; return 0; }\n"
JAVA_SRC = "public class Main { public static void main(String[] a) { int k = 7; } }\n"


@pytest.fixture()
def env(tmp_path: Path):
    toolchain = detect_toolchain(PROJECT_ROOT, force_redetect=True)
    runner = BackendRunner(toolchain)
    if not runner.available():
        pytest.skip("compileone backend not found, skipping integration tests.")

    store = ArtifactStore(output_dir=tmp_path / "output", cache_dir=tmp_path / "cache")
    pipeline = Pipeline()
    orch = Orchestrator(pipeline=pipeline, runner=runner, store=store, toolchain=toolchain)
    return orch, runner, pipeline, store


def _session(source_path: Path, source_text: str, language: str) -> CompileSession:
    return CompileSession(source_path=source_path, source_text=source_text, language=language)


def _write(tmp_path: Path, name: str, text: str) -> Path:
    p = tmp_path / name
    p.write_text(text, encoding="utf-8")
    return p


def _lexemes(art: dict) -> list[str]:
    return [t["lexeme"] for t in art["tokens"]]


# ---------------------------------------------------------------- per-language regeneration

def test_switch_language_regenerates_token_stream(env, tmp_path):
    orch, _, pipeline, store = env
    c_path = _write(tmp_path, "a.c", C_SRC_A)

    # C
    sess = orch.compile_all(_session(c_path, C_SRC_A, "c"))
    assert sess.artifacts["token_stream"]["language"] == "c"
    c_lexemes = _lexemes(sess.artifacts["token_stream"])
    assert "int" in c_lexemes and "1" in c_lexemes

    # C++ in the same session -> token stream must become c++ language
    cpp_path = _write(tmp_path, "b.cpp", CPP_SRC)
    sess2 = orch.compile_all(_session(cpp_path, CPP_SRC, "c++"))
    assert sess2.artifacts["token_stream"]["language"] == "c++"
    cpp_lexemes = _lexemes(sess2.artifacts["token_stream"])
    assert "cout" in cpp_lexemes and "5" in cpp_lexemes
    # The C lexemes must not have leaked into the C++ stream.
    assert "1" not in cpp_lexemes

    # Java
    java_path = _write(tmp_path, "Main.java", JAVA_SRC)
    sess3 = orch.compile_all(_session(java_path, JAVA_SRC, "java"))
    assert sess3.artifacts["token_stream"]["language"] == "java"
    java_lexemes = _lexemes(sess3.artifacts["token_stream"])
    assert "class" in java_lexemes and "Main" in java_lexemes
    assert "cout" not in java_lexemes and "int" in java_lexemes


# ---------------------------------------------------------------- stale result on same-language change

def test_recompile_replaces_stale_token_stream(env, tmp_path):
    """Changing the C source in the same session must not keep the old tokens."""
    orch, _, pipeline, store = env

    path_a = _write(tmp_path, "a.c", C_SRC_A)
    sess_a = orch.compile_all(_session(path_a, C_SRC_A, "c"))
    assert "x" in _lexemes(sess_a.artifacts["token_stream"])
    assert "y" not in _lexemes(sess_a.artifacts["token_stream"])

    # Same session, new C source with 'y' instead of 'x'.
    path_b = _write(tmp_path, "b.c", C_SRC_B)
    sess_b = orch.compile_all(_session(path_b, C_SRC_B, "c"))
    lexemes_b = _lexemes(sess_b.artifacts["token_stream"])
    assert "y" in lexemes_b
    assert "x" not in lexemes_b


# ---------------------------------------------------------------- mini-c artifacts dropped for native

def test_native_run_phase_skipped_and_dropped(env, tmp_path):
    """Native languages skip the mini-c 'run' phase and drop its stale artifact."""
    orch, _, pipeline, store = env
    c_path = _write(tmp_path, "a.c", C_SRC_A)
    sess = orch.compile_all(_session(c_path, C_SRC_A, "c"))

    run_result = next(r for r in sess.phase_results if r.phase.id == "run")
    assert run_result.status == PhaseStatus.SKIPPED
    assert "execution" not in sess.artifacts
