"""End-to-end lex phase tests against the real compileone backend.

Golden artifacts live in tests/integration/golden/. The backend is
normalized before comparison (volatile fields like duration_ms and the
absolute source_file path are dropped) so the tests are stable across
machines while still pinning exact token behaviour.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from app.infrastructure.backend_runner import BackendRunner
from app.infrastructure.paths import EXAMPLES_DIR, PROJECT_ROOT
from app.infrastructure.tool_detector import detect_toolchain

pytestmark = pytest.mark.integration

GOLDEN_DIR = Path(__file__).parent / "golden"
GOLDEN_CASES = ["hello.mc", "fib.mc", "comments.mc", "invalid.mc"]

# The specific set of fields we care about for golden file comparison.
# This must match the fields expected by the golden files.
_TOKEN_FIELDS = (
    "id", "line", "column", "lexeme", "token", "category",
    "offset_start", "offset_end",
)


@pytest.fixture(scope="module")
def backend_runner() -> BackendRunner:
    """Fixture to provide an initialized BackendRunner."""
    toolchain = detect_toolchain(PROJECT_ROOT, force_redetect=True)
    runner = BackendRunner(toolchain)
    if not runner.available():
        pytest.skip("compileone backend not found, skipping integration tests.")
    return runner


def _normalize_artifact(artifact: dict) -> dict:
    """Strips volatile/irrelevant fields for stable comparison."""
    def _normalize_token(token: dict) -> dict:
        # The raw artifact has a nested offset dict: {"offset": {"start": x, "end": y}}.
        # The golden file expects a flattened structure: {"offset_start": x, "offset_end": y}.
        # This function performs that transformation.
        flat_token = {field: token.get(field) for field in _TOKEN_FIELDS}
        if "offset" in token and isinstance(token["offset"], dict):
            flat_token["offset_start"] = token["offset"].get("start")
            flat_token["offset_end"] = token["offset"].get("end")
        return flat_token

    return {
        "phase": artifact.get("phase"),
        "language": artifact.get("language"),
        "token_count": len(artifact.get("tokens", [])),
        "error_count": len(artifact.get("errors", [])),
        "tokens": [_normalize_token(t) for t in artifact.get("tokens", [])],
        "errors": artifact.get("errors", []),
        "by_category": artifact.get("statistics", {}).get("by_category", {}),
    }


@pytest.mark.parametrize("case", GOLDEN_CASES)
def test_lex_matches_golden(backend_runner: BackendRunner, tmp_path: Path, case: str):
    """
    Runs the lex phase on a file and compares the output to a golden file.
    """
    source_path = EXAMPLES_DIR / "study" / case
    output_path = tmp_path / f"{case}.json"

    # Run the phase using our infrastructure component
    artifact = backend_runner.run_phase(
        phase="lex",
        input_path=source_path,
        output_path=output_path,
        language="mini-c",
    )

    # Load the corresponding golden file
    golden_path = GOLDEN_DIR / f"{case}.json"
    golden_artifact = json.loads(golden_path.read_text(encoding="utf-8"))

    assert _normalize_artifact(artifact) == golden_artifact


def test_lex_errors_point_at_expected_locations(backend_runner: BackendRunner, tmp_path: Path):
    """
    Checks that specific, known syntax errors are reported correctly.
    """
    source_path = EXAMPLES_DIR / "study" / "invalid.mc"
    output_path = tmp_path / "invalid.json"

    artifact = backend_runner.run_phase(
        phase="lex",
        input_path=source_path,
        output_path=output_path,
        language="mini-c"
    )

    messages = {(e["line"], e["column"], e["message"]) for e in artifact["errors"]}
    assert messages == {
        (4, 7, "Unknown character"),
        (5, 7, "Unterminated string literal"),
    }


@pytest.mark.parametrize(
    "case,language",
    [("hello.cpp", "c++"), ("interactive.c", "c")],
)
def test_preprocessor_files_lex_without_errors(
    backend_runner: BackendRunner, tmp_path: Path, case: str, language: str
):
    """
    Real C/C++ sources containing preprocessor directives and native
    operators must tokenize without spurious lexical errors.
    """
    source_path = EXAMPLES_DIR / "study" / case
    output_path = tmp_path / f"{case}.json"

    artifact = backend_runner.run_phase(
        phase="lex",
        input_path=source_path,
        output_path=output_path,
        language=language,
    )

    assert artifact["language"] == language
    assert artifact["errors"] == []
