"""End-to-end lex phase tests against the real compileone backend.

Golden artifacts live in tests/integration/golden/. The backend is
normalized before comparison (volatile fields like duration_ms and the
absolute source_file path are dropped) so the tests are stable across
machines while still pinning exact token behaviour.
"""

from __future__ import annotations

import json
import subprocess
from pathlib import Path

import pytest

from app.infrastructure.paths import BACKEND_EXE, EXAMPLES_DIR

pytestmark = pytest.mark.integration

GOLDEN_DIR = Path(__file__).parent / "golden"

GOLDEN_CASES = ["hello.mc", "fib.mc", "comments.mc", "invalid.mc"]

_TOKEN_FIELDS = (
    "id", "line", "column", "lexeme", "token", "category", "subtype",
    "length", "scope", "scope_level", "color", "description",
    "offset_start", "offset_end",
)


@pytest.fixture(scope="module")
def backend():
    if not BACKEND_EXE.is_file():
        pytest.skip("backend executable not built — run 'make all' first")
    return BACKEND_EXE


def _normalize(artifact: dict) -> dict:
    return {
        "phase": artifact["phase"],
        "language": artifact["language"],
        "token_count": len(artifact["tokens"]),
        "error_count": len(artifact["errors"]),
        "tokens": [_normalize_token(t) for t in artifact["tokens"]],
        "errors": artifact["errors"],
        "by_category": artifact.get("statistics", {}).get("by_category", {}),
    }


def _normalize_token(token: dict) -> dict:
    return {
        field: token[field] if field not in ("offset_start", "offset_end")
        else token["offset"]["start" if field == "offset_start" else "end"]
        for field in _TOKEN_FIELDS
    }


def _run_lex(backend: Path, source: Path, output: Path) -> dict:
    proc = subprocess.run(
        [
            str(backend), "lex",
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
def test_lex_matches_golden(backend, tmp_path, case):
    source = EXAMPLES_DIR / "study" / case
    output = tmp_path / f"{case}.json"
    artifact = _run_lex(backend, source, output)

    assert _normalize(artifact) == json.loads(
        (GOLDEN_DIR / f"{case}.json").read_text(encoding="utf-8")
    )


def test_lex_errors_point_at_expected_locations(backend, tmp_path):
    source = EXAMPLES_DIR / "study" / "invalid.mc"
    artifact = _run_lex(backend, source, tmp_path / "invalid.json")

    messages = {(e["line"], e["column"], e["message"]) for e in artifact["errors"]}
    assert messages == {(4, 7, "Unknown character"), (5, 7, "Unterminated string literal")}
