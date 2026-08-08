"""Widget rendering tests for the compiler-phase views (headless Qt)."""

from __future__ import annotations

from app.domain.artifacts import SemanticInfo, SymbolInfo, SemanticDiagnostic
from app.infrastructure.json_loader import parse_ast, parse_assembly, parse_ir, parse_optimization, parse_semantic
from app.ui.views.assembly_view import AssemblyView
from app.ui.views.ast_view import ASTView
from app.ui.views.ir_view import IRView
from app.ui.views.optimization_view import OptimizationView
from app.ui.views.semantic_view import SemanticView
from tests.conftest import make_assembly_data, make_ir_data, make_optimization_data


def _semantic_info() -> SemanticInfo:
    return parse_semantic(
        {
            "schema": "compileone/semantic/1.0",
            "phase": "semantic",
            "language": "mini-c",
            "source_file": "example.mc",
            "generated_by": "compileone semantic",
            "duration_ms": 0.5,
            "valid": True,
            "symbols": [
                {
                    "name": "x",
                    "type": "int",
                    "scope": "global",
                    "scope_level": 0,
                    "is_const": False,
                    "line": 1,
                    "column": 5,
                }
            ],
            "diagnostics": [
                {
                    "severity": "warning",
                    "code": "SEM009",
                    "message": "unused symbol 'x'",
                    "line": 1,
                    "column": 5,
                }
            ],
        }
    )


def test_ast_view_renders_tree_and_attributes(qapp):
    tree = parse_ast(
        {
            "schema": "compileone/ast/1.0",
            "phase": "ast",
            "language": "mini-c",
            "source_file": "example.mc",
            "generated_by": "compileone ast",
            "duration_ms": 0.3,
            "root": {
                "node_type": "Program",
                "children": [
                    {
                        "node_type": "VarDecl",
                        "attributes": {"type_name": "int", "const": False, "name": "x"},
                        "children": [],
                    }
                ],
            },
            "errors": [],
        }
    )
    view = ASTView()
    view.set_tree(tree)
    assert view._tree.topLevelItemCount() == 1
    root_item = view._tree.topLevelItem(0)
    assert root_item.text(0) == "Program"
    assert root_item.childCount() == 1
    child = root_item.child(0)
    assert child.text(0) == "VarDecl"

    view._tree.setCurrentItem(child)
    assert view._props_table.rowCount() >= 2
    assert view._props_table.item(0, 0).text() == "type_name"


def test_semantic_view_renders_symbols_and_diagnostics(qapp):
    semantic = _semantic_info()
    view = SemanticView()
    view.set_semantic(semantic)

    assert view._symbols_table.rowCount() == 1
    assert view._symbols_table.item(0, 0).text() == "x"
    assert view._symbols_table.item(0, 4).text() == ""
    assert view._diag_tree.topLevelItemCount() == 1
    assert view._diag_tree.topLevelItem(0).text(0) == "SEM009"


def test_semantic_view_clears_on_none(qapp):
    view = SemanticView()
    view.set_semantic(_semantic_info())
    view.set_semantic(None)
    assert view._symbols_table.rowCount() == 0
    assert view._diag_tree.topLevelItemCount() == 0


def test_ir_view_renders_tac_listing(qapp):
    ir = parse_ir(make_ir_data())
    view = IRView()
    view.set_ir(ir)

    assert ir.instruction_count == 3
    assert view._table.rowCount() == 3
    assert view._table.item(2, 1).text() == "add"
    assert view._table.item(2, 4).text() == "t2"
    assert "3 instruction(s)" in view._summary.text()


def test_ir_view_clears_on_none(qapp):
    view = IRView()
    view.set_ir(parse_ir(make_ir_data()))
    view.set_ir(None)
    assert view._table.rowCount() == 0


def test_optimization_view_renders_pass_evidence(qapp):
    opt = parse_optimization(make_optimization_data())
    view = OptimizationView()
    view.set_optimization(opt)

    assert view._pass_tree.topLevelItemCount() == 1
    pass_item = view._pass_tree.topLevelItem(0)
    assert pass_item.text(0) == "constant-folding"
    assert pass_item.text(1) == "applied"
    assert "33.3% fewer" in view._summary.text()


def test_optimization_view_clears_on_none(qapp):
    view = OptimizationView()
    view.set_optimization(parse_optimization(make_optimization_data()))
    view.set_optimization(None)
    assert view._pass_tree.topLevelItemCount() == 0


def test_assembly_view_renders_listing_and_stack(qapp):
    assembly = parse_assembly(make_assembly_data())
    view = AssemblyView()
    view.set_assembly(assembly)

    assert assembly.stack_size == 0
    assert view._listing.toPlainText() == assembly.text
    assert "x86_64 (att)" in view._summary.text()
    assert view._stack_table.rowCount() == 0


def test_assembly_view_clears_on_none(qapp):
    view = AssemblyView()
    view.set_assembly(parse_assembly(make_assembly_data()))
    view.set_assembly(None)
    assert view._listing.toPlainText() == ""
    assert view._stack_table.rowCount() == 0
