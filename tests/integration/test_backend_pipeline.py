"""End-to-end parse / ast / semantic phase tests against the real backend.

Runs the full mini-c front-end chain (lex -> parse -> ast -> semantic)
and validates each artifact with the same schema-aware loaders the UI
uses, then checks semantic diagnostics on a purposefully buggy program.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from app.infrastructure.backend_runner import BackendRunner
from app.infrastructure.json_loader import (
    load_json,
    parse_ast,
    parse_cst,
    parse_semantic,
)
from app.infrastructure.paths import EXAMPLES_DIR, PROJECT_ROOT
from app.infrastructure.tool_detector import detect_toolchain

pytestmark = pytest.mark.integration


@pytest.fixture(scope="module")
def backend_runner() -> BackendRunner:
    toolchain = detect_toolchain(PROJECT_ROOT, force_redetect=True)
    runner = BackendRunner(toolchain)
    if not runner.available():
        pytest.skip("compileone backend not found, skipping integration tests.")
    return runner


def _run_frontend(backend_runner: BackendRunner, source: Path, tmp_path: Path):
    """lex -> parse -> ast -> semantic; returns (tokens, parse, ast, semantic)."""
    tokens_path = tmp_path / "tokens.json"
    parse_path = tmp_path / "parse_tree.json"
    ast_path = tmp_path / "ast.json"
    semantic_path = tmp_path / "semantic.json"

    backend_runner.run_phase("lex", source, tokens_path, "mini-c")
    backend_runner.run_phase("parse", tokens_path, parse_path, "mini-c")
    backend_runner.run_phase("ast", tokens_path, ast_path, "mini-c")
    backend_runner.run_phase("semantic", tokens_path, semantic_path, "mini-c")

    return (load_json(tokens_path), load_json(parse_path),
            load_json(ast_path), load_json(semantic_path))


def test_frontend_chain_produces_loadable_artifacts(
    backend_runner: BackendRunner, tmp_path: Path,
):
    source_path = EXAMPLES_DIR / "study" / "loop.mc"
    tokens, parse_data, ast_data, semantic_data = _run_frontend(
        backend_runner, source_path, tmp_path
    )

    # Token stream is the shared input of the three front-end phases.
    assert tokens["schema"] == "compileone/token-stream/1.0"

    # CST: root is the "program" production; terminals carry tokens.
    cst = parse_cst(parse_data)
    assert cst.root is not None and cst.root.rule_name == "program"
    assert cst.errors == []

    # AST: Program root with VarDecl / While / Print children.
    ast = parse_ast(ast_data)
    assert ast.root is not None and ast.root.node_type == "Program"
    node_types = {c.node_type for c in ast.root.children}
    assert {"VarDecl", "While", "Print"} <= node_types

    # Semantic: valid program with both variables used and no errors.
    semantic = parse_semantic(semantic_data)
    assert semantic.valid
    names = {s.name for s in semantic.symbols}
    assert {"total", "i"} <= names
    assert all(s.scope == "global" for s in semantic.symbols)
    assert semantic.error_count == 0


def test_semantic_flags_undeclared_const_and_redecl(
    backend_runner: BackendRunner, tmp_path: Path,
):
    source = tmp_path / "buggy.mc"
    source.write_text(
        "int a = 1;\n"
        "const int b = 2;\n"
        "b = 3;\n"          # SEM003: assign to const
        "c = a;\n"          # SEM001: undeclared
        "int a;\n",         # SEM002: redeclaration
        encoding="utf-8",
    )

    _, _, _, semantic_data = _run_frontend(backend_runner, source, tmp_path)

    semantic = parse_semantic(semantic_data)
    assert not semantic.valid
    codes = {d.code for d in semantic.diagnostics}
    assert {"SEM003", "SEM001", "SEM002"} <= codes
    errors = [d for d in semantic.diagnostics if d.severity == "error"]
    assert len(errors) >= 3
    assert all(d.line > 0 and d.column > 0 for d in errors)


def test_semantic_scope_blocks_shadow_globals(
    backend_runner: BackendRunner, tmp_path: Path,
):
    source = tmp_path / "scopes.mc"
    source.write_text(
        "int x = 1;\n"
        "while (x > 0) {\n"
        "    int x = 5;\n"
        "    x = x - 1;\n"
        "}\n"
        "print x;\n",
        encoding="utf-8",
    )

    _, _, _, semantic_data = _run_frontend(backend_runner, source, tmp_path)

    semantic = parse_semantic(semantic_data)
    assert semantic.error_count == 0
    x_symbols = [s for s in semantic.symbols if s.name == "x"]
    assert len(x_symbols) == 2
    assert {s.scope for s in x_symbols} == {"global", "block:1"}
    assert all(not s.is_const for s in x_symbols)
