"""Schema-aware JSON deserialization.

The compileone backend and the UI communicate exclusively through JSON
artifacts. This module loads artifact files and converts them into the
typed domain models (app/domain/artifacts.py), validating structure as
it goes. It never performs lexical/syntactic analysis of source code.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from app.domain.artifacts import (
    AbstractSyntaxTree,
    AsmInstruction,
    AssemblyInfo,
    ASTNode,
    Execution,
    ExecutionError,
    IrInfo,
    IrInstruction,
    LexicalError,
    OptInfo,
    OptPass,
    ParseTree,
    ParseTreeNode,
    SemanticDiagnostic,
    SemanticInfo,
    SymbolInfo,
    Token,
    TokenStream,
)


class ArtifactError(ValueError):
    """Raised when an artifact file cannot be parsed or validated."""


def load_json(path: Path) -> dict[str, Any]:
    """Read and parse a JSON file, raising ArtifactError on failure."""
    try:
        with open(path, "r", encoding="utf-8") as fh:
            return json.load(fh)
    except FileNotFoundError as exc:
        raise ArtifactError(f"artifact file not found: {path}") from exc
    except json.JSONDecodeError as exc:
        raise ArtifactError(f"artifact file is not valid JSON: {path} ({exc})") from exc


def _expect(data: dict[str, Any], key: str) -> Any:
    if key not in data:
        raise ArtifactError(f"artifact missing required field: {key!r}")
    return data[key]


def _parse_token(raw: dict[str, Any]) -> Token:
    """Parses a raw dictionary into a Token object."""
    off = _expect(raw, "offset")
    return Token(
        id=int(_expect(raw, "id")),
        line=int(_expect(raw, "line")),
        column=int(_expect(raw, "column")),
        lexeme=str(_expect(raw, "lexeme")),
        token=str(_expect(raw, "token")),
        category=str(_expect(raw, "category")),
        offset_start=int(_expect(off, "start")),
        offset_end=int(_expect(off, "end")),
    )


def parse_token_stream(data: dict[str, Any]) -> TokenStream:
    """Validate and convert a token-stream artifact into a TokenStream."""
    _expect(data, "schema")
    
    tokens: list[Token] = [_parse_token(raw) for raw in data.get("tokens", [])]

    errors: list[LexicalError] = []
    for raw in data.get("errors", []):
        errors.append(
            LexicalError(
                line=int(_expect(raw, "line")),
                column=int(_expect(raw, "column")),
                lexeme=str(_expect(raw, "lexeme")),
                message=str(_expect(raw, "message")),
            )
        )

    return TokenStream(
        schema=str(data.get("schema", "")),
        phase=str(data.get("phase", "")),
        language=str(data.get("language", "")),
        source_file=str(data.get("source_file", "")),
        generated_by=str(data.get("generated_by", "")),
        duration_ms=float(data.get("duration_ms", 0.0)),
        tokens=tokens,
        statistics=dict(data.get("statistics", {})),
        errors=errors,
    )


def _parse_cst_node(raw_node: dict[str, Any]) -> ParseTreeNode:
    """Recursively parse a raw dictionary into a ParseTreeNode."""
    if raw_node.get("token"):
        # Terminal node
        token = _parse_token(_expect(raw_node, "token"))
        return ParseTreeNode(token=token)
    
    # Non-terminal node
    rule_name = str(_expect(raw_node, "rule_name"))
    children = [
        _parse_cst_node(child) for child in raw_node.get("children", [])
    ]
    return ParseTreeNode(rule_name=rule_name, children=children)


def parse_cst(data: dict[str, Any]) -> ParseTree:
    """Validate and convert a parse-tree artifact into a ParseTree."""
    _expect(data, "schema")

    root_node = None
    if "root" in data and data["root"] is not None:
        root_node = _parse_cst_node(data["root"])

    # Placeholder for syntax error parsing
    errors = list(data.get("errors", []))

    return ParseTree(
        schema=str(data.get("schema", "")),
        phase=str(data.get("phase", "parse")),
        language=str(data.get("language", "")),
        source_file=str(data.get("source_file", "")),
        generated_by=str(data.get("generated_by", "")),
        duration_ms=float(data.get("duration_ms", 0.0)),
        root=root_node,
        errors=errors,
    )


def _parse_ast_node(raw_node: dict[str, Any]) -> ASTNode:
    """Recursively parse a raw dictionary into an ASTNode."""
    node_type = str(_expect(raw_node, "node_type"))
    
    token = None
    if raw_node.get("token"):
        token = _parse_token(raw_node["token"])

    attributes = dict(raw_node.get("attributes", {}))
    
    children = [
        _parse_ast_node(child) for child in raw_node.get("children", [])
    ]
    
    return ASTNode(
        node_type=node_type,
        token=token,
        attributes=attributes,
        children=children
    )


def parse_ast(data: dict[str, Any]) -> AbstractSyntaxTree:
    """Validate and convert an abstract-syntax-tree artifact into an AbstractSyntaxTree."""
    _expect(data, "schema")

    root_node = None
    if "root" in data and data["root"] is not None:
        root_node = _parse_ast_node(data["root"])

    errors = list(data.get("errors", []))

    return AbstractSyntaxTree(
        schema=str(data.get("schema", "")),
        phase=str(data.get("phase", "ast")),
        language=str(data.get("language", "")),
        source_file=str(data.get("source_file", "")),
        generated_by=str(data.get("generated_by", "")),
        duration_ms=float(data.get("duration_ms", 0.0)),
        root=root_node,
        errors=errors,
    )


def parse_execution(data: dict[str, Any]) -> Execution:
    """Validate and convert an execution artifact into an Execution."""
    _expect(data, "schema")

    errors: list[ExecutionError] = []
    for raw in data["errors"]:
        errors.append(
            ExecutionError(
                line=int(raw.get("line", 1)),
                column=int(raw.get("column", 1)),
                message=str(_expect(raw, "message")),
            )
        )

    return Execution(
        schema=str(data.get("schema", "")),
        phase=str(data.get("phase", "")),
        language=str(data.get("language", "")),
        source_file=str(data.get("source_file", "")),
        generated_by=str(data.get("generated_by", "")),
        duration_ms=float(data.get("duration_ms", 0.0)),
        status=str(data.get("status", "ok")),
        exit_code=int(data.get("exit_code", 0)),
        steps=int(data.get("steps", 0)),
        output=[str(line) for line in data["output"]],
        errors=errors,
    )


def parse_semantic(data: dict[str, Any]) -> SemanticInfo:
    """Validate and convert a semantic-analysis artifact into a SemanticInfo."""
    _expect(data, "schema")

    symbols: list[SymbolInfo] = []
    for raw in data.get("symbols", []):
        symbols.append(
            SymbolInfo(
                name=str(_expect(raw, "name")),
                type=str(_expect(raw, "type")),
                scope=str(raw.get("scope", "global")),
                scope_level=int(raw.get("scope_level", 0)),
                is_const=bool(raw.get("is_const", False)),
                line=int(raw.get("line", 1)),
                column=int(raw.get("column", 1)),
            )
        )

    diagnostics: list[SemanticDiagnostic] = []
    for raw in data.get("diagnostics", []):
        diagnostics.append(
            SemanticDiagnostic(
                severity=str(_expect(raw, "severity")),
                code=str(_expect(raw, "code")),
                message=str(_expect(raw, "message")),
                line=int(raw.get("line", 1)),
                column=int(raw.get("column", 1)),
            )
        )

    return SemanticInfo(
        schema=str(data.get("schema", "")),
        phase=str(data.get("phase", "semantic")),
        language=str(data.get("language", "")),
        source_file=str(data.get("source_file", "")),
        generated_by=str(data.get("generated_by", "")),
        duration_ms=float(data.get("duration_ms", 0.0)),
        valid=bool(data.get("valid", True)),
        symbols=symbols,
        diagnostics=diagnostics,
    )


def _parse_ir_instruction(raw: dict[str, Any]) -> IrInstruction:
    """Parse one TAC instruction (accepts both the `tac` arg1/arg2/result
    form and the compact `quadruples` a/b/r form)."""
    return IrInstruction(
        index=int(_expect(raw, "index")),
        op=str(_expect(raw, "op")),
        arg1=str(raw.get("arg1", raw.get("a", ""))),
        arg2=str(raw.get("arg2", raw.get("b", ""))),
        result=str(raw.get("result", raw.get("r", ""))),
    )


def parse_ir(data: dict[str, Any]) -> IrInfo:
    """Validate and convert an IR artifact into an IrInfo."""
    _expect(data, "schema")

    tac = [_parse_ir_instruction(raw) for raw in data.get("tac", [])]
    quadruples = [_parse_ir_instruction(raw) for raw in data.get("quadruples", [])]

    return IrInfo(
        schema=str(data.get("schema", "")),
        phase=str(data.get("phase", "ir")),
        language=str(data.get("language", "")),
        source_file=str(data.get("source_file", "")),
        generated_by=str(data.get("generated_by", "")),
        duration_ms=float(data.get("duration_ms", 0.0)),
        tac=tac,
        quadruples=quadruples,
        temporaries=[str(t) for t in data.get("temporaries", [])],
        labels=[str(lbl) for lbl in data.get("labels", [])],
        errors=list(data.get("errors", [])),
    )


def parse_optimization(data: dict[str, Any]) -> OptInfo:
    """Validate and convert an optimization artifact into an OptInfo."""
    _expect(data, "schema")

    passes: list[OptPass] = []
    for raw in data.get("passes", []):
        passes.append(
            OptPass(
                name=str(_expect(raw, "name")),
                applied=bool(raw.get("applied", False)),
                explanation=str(raw.get("explanation", "")),
                instruction_reduction=int(raw.get("instruction_reduction", 0)),
                removed_instructions=[
                    _parse_ir_instruction(q)
                    for q in raw.get("removed_instructions", [])
                ],
                before=[str(line) for line in raw.get("before", [])],
                after=[str(line) for line in raw.get("after", [])],
            )
        )

    return OptInfo(
        schema=str(data.get("schema", "")),
        phase=str(data.get("phase", "optimization")),
        language=str(data.get("language", "")),
        source_file=str(data.get("source_file", "")),
        generated_by=str(data.get("generated_by", "")),
        duration_ms=float(data.get("duration_ms", 0.0)),
        before_instruction_count=int(data.get("before_instruction_count", 0)),
        after_instruction_count=int(data.get("after_instruction_count", 0)),
        instruction_reduction_pct=float(data.get("instruction_reduction_pct", 0.0)),
        passes=passes,
        errors=list(data.get("errors", [])),
    )


def parse_assembly(data: dict[str, Any]) -> AssemblyInfo:
    """Validate and convert an assembly artifact into an AssemblyInfo."""
    _expect(data, "schema")

    instructions: list[AsmInstruction] = []
    for raw in data.get("instructions", []):
        instructions.append(
            AsmInstruction(
                address=str(raw.get("address", "")),
                mnemonic=str(_expect(raw, "mnemonic")),
                operands=[str(op) for op in raw.get("operands", [])],
                class_name=str(raw.get("class", "")),
                comment=raw.get("comment"),
                label=raw.get("label"),
            )
        )

    return AssemblyInfo(
        schema=str(data.get("schema", "")),
        phase=str(data.get("phase", "codegen")),
        language=str(data.get("language", "")),
        source_file=str(data.get("source_file", "")),
        generated_by=str(data.get("generated_by", "")),
        duration_ms=float(data.get("duration_ms", 0.0)),
        arch=str(data.get("arch", "x86_64")),
        syntax=str(data.get("syntax", "att")),
        text=str(data.get("text", "")),
        instructions=instructions,
        prologue=[str(line) for line in data.get("prologue", [])],
        epilogue=[str(line) for line in data.get("epilogue", [])],
        stack_layout=dict(data.get("stack_layout", {})),
        errors=list(data.get("errors", [])),
    )
