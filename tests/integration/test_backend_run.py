"""End-to-end execution-phase tests against the real compileone backend.

Golden artifacts live in tests/integration/golden/run_*.json. The
execution artifact is normalized before comparison (volatile fields like
duration_ms, source_file, generated_by are dropped) so the tests pin the
interpreter's observable behaviour: status, exit code, steps, stdout
lines, and any runtime/lex errors.
"""

from __future__ import annotations

import json
import subprocess
from pathlib import Path

import pytest

from app.infrastructure.paths import BACKEND_EXE, EXAMPLES_DIR

pytestmark = pytest.mark.integration

GOLDEN_DIR = Path(__file__).parent / "golden"

GOLDEN_CASES = ["hello.mc", "loop.mc", "fib.mc", "comments.mc", "invalid.mc"]


@pytest.fixture(scope="module")
def backend():
    if not BACKEND_EXE.is_file():
        pytest.skip("backend executable not built — run 'make all' first")
    return BACKEND_EXE


def _normalize(artifact: dict) -> dict:
    return {
        "phase": artifact["phase"],
        "language": artifact["language"],
        "status": artifact["status"],
        "exit_code": artifact["exit_code"],
        "steps": artifact["steps"],
        "output": artifact["output"],
        "errors": artifact["errors"],
    }


def _run_execution(backend: Path, source: Path, output: Path) -> dict:
    proc = subprocess.run(
        [
            str(backend), "run",
            "--input", str(source),
            "--output", str(output),
            "--language", "mini-c",
        ],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    assert proc.returncode == 0, f"backend failed for {source.name}: {proc.stderr}"
    return json.loads(output.read_text(encoding="utf-8"))


@pytest.mark.parametrize("case", GOLDEN_CASES)
def test_run_matches_golden(backend, tmp_path, case):
    source = EXAMPLES_DIR / "study" / case
    output = tmp_path / f"run_{case}.json"
    artifact = _run_execution(backend, source, output)

    assert _normalize(artifact) == json.loads(
        (GOLDEN_DIR / f"run_{case}.json").read_text(encoding="utf-8")
    )


def test_hello_program_output_is_correct(backend, tmp_path):
    source = EXAMPLES_DIR / "study" / "hello.mc"
    artifact = _run_execution(backend, source, tmp_path / "hello_run.json")

    assert artifact["status"] == "ok"
    assert artifact["exit_code"] == 0
    assert artifact["output"] == ["7", "9"]
    assert artifact["errors"] == []


def test_loop_sums_one_to_ten(backend, tmp_path):
    source = EXAMPLES_DIR / "study" / "loop.mc"
    artifact = _run_execution(backend, source, tmp_path / "loop_run.json")

    assert artifact["status"] == "ok"
    assert artifact["output"] == ["five reached: ", "5", "total = ", "55"]


def test_unsupported_function_call_reports_runtime_error(backend, tmp_path):
    source = EXAMPLES_DIR / "study" / "fib.mc"
    artifact = _run_execution(backend, source, tmp_path / "fib_run.json")

    assert artifact["status"] == "runtime-error"
    assert artifact["exit_code"] == 1
    assert artifact["output"] == []
    (line, column, message) = (
        artifact["errors"][0]["line"],
        artifact["errors"][0]["column"],
        artifact["errors"][0]["message"],
    )
    assert (line, column) == (11, 7)
    assert "not supported" in message


def test_const_assignment_is_a_runtime_error(backend, tmp_path):
    source = EXAMPLES_DIR / "study" / "comments.mc"
    artifact = _run_execution(backend, source, tmp_path / "comments_run.json")

    assert artifact["status"] == "runtime-error"
    assert artifact["exit_code"] == 1
    assert artifact["errors"][0]["message"] == (
        "cannot assign to const variable 'MAX'"
    )
    assert (artifact["errors"][0]["line"], artifact["errors"][0]["column"]) == (11, 5)
