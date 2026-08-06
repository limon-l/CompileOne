"""High-level compile flows.

The orchestrator owns the use cases the UI invokes: compile all phases,
inspect a single phase, run the program. It wires the Pipeline to the
BackendRunner and ArtifactStore, and converts backend artifacts into the
typed domain models that views render.

The orchestrator is UI-agnostic (no Qt imports) so it can be tested with
a fake runner and reused by CLI tooling.
"""

from __future__ import annotations

import logging
from pathlib import Path

from app.application.compile_session import CompileSession
from app.application.pipeline import PhaseResult, PhaseStatus, Pipeline
from app.domain.artifacts import Execution
from app.domain.diagnostics import Diagnostic, Severity
from app.infrastructure.artifact_store import ArtifactStore
from app.infrastructure.backend_runner import BackendRunner, PhaseNotImplemented
from app.infrastructure.json_loader import parse_execution, parse_token_stream

logger = logging.getLogger("compileone.orchestrator")


class Orchestrator:
    def __init__(
        self,
        pipeline: Pipeline | None = None,
        runner: BackendRunner | None = None,
        store: ArtifactStore | None = None,
    ) -> None:
        self.pipeline = pipeline or Pipeline()
        self.runner = runner or BackendRunner()
        self.store = store or ArtifactStore()

    # ------------------------------------------------------ compile flows

    def compile_all(self, session: CompileSession) -> CompileSession:
        """Run every available phase of the pipeline for this session."""
        results = self.pipeline.run(
            source_path=session.source_path,
            store=self.store,
            runner=self.runner,
            session_artifacts=session.artifacts,
        )
        session.phase_results = results
        session.timings = {
            r.phase.id: r.duration_ms for r in results if r.done
        }
        self._derive_diagnostics(session)
        logger.info(
            "compile_all: %d phases run, %d diagnostic(s)",
            sum(1 for r in results if r.done),
            len(session.diagnostics),
        )
        return session

    def inspect_phase(self, session: CompileSession, phase_id: str) -> CompileSession:
        """Run a single phase, resuming from the previous artifact if needed."""
        phase = self.pipeline.phase_by_id(phase_id)
        if phase is None:
            raise ValueError(f"unknown phase: {phase_id}")

        source_path = session.source_path
        if phase.input_kind == "artifact":
            prev = self.pipeline.previous_phase(phase)
            if prev is None or not self.store.artifact_exists(prev.output_artifact):
                # Ensure the pipeline leading up to this phase exists.
                self.compile_all(session)

        output_path = self.store.artifact_path(phase.output_artifact)
        try:
            data = self.runner.run_phase(
                phase.id, source_path, output_path, language=session.language
            )
        except PhaseNotImplemented as exc:
            logger.warning("inspect_phase(%s): %s", phase_id, exc)
            session.phase_results.append(PhaseResult(phase, status="unavailable", error=str(exc)))
            return session

        session.artifacts[phase.output_artifact] = data
        session.phase_results.append(PhaseResult(phase, status="ok"))
        return session

    def execute(self, session: CompileSession) -> CompileSession:
        """Run the program through the execution phase and capture stdout.

        The execution phase reads the source file directly (input_kind
        "source"), so it works without any prior pipeline phases having
        run. Output lines land in session.artifacts["execution"] and any
        runtime errors become editor diagnostics.
        """
        phase = self.pipeline.phase_by_id("run")
        if phase is None or not phase.available:
            raise ValueError("execution phase is not available")

        output_path = self.store.artifact_path(phase.output_artifact)
        data = self.runner.run_phase(
            phase.id, session.source_path, output_path, language=session.language
        )
        session.artifacts[phase.output_artifact] = data
        session.phase_results = [
            r for r in session.phase_results if r.phase.id != "run"
        ]
        session.phase_results.append(PhaseResult(phase, status=PhaseStatus.OK))
        self._derive_diagnostics(session)
        logger.info(
            "execute: status=%s exit=%d steps=%d",
            data.get("status"), data.get("exit_code"), data.get("steps"),
        )
        return session

    # ------------------------------------------------------ diagnostics

    def _derive_diagnostics(self, session: CompileSession) -> None:
        """Translate backend errors embedded in artifacts into Diagnostics."""
        session.diagnostics = []

        for result in session.phase_results:
            if result.status == "error" and result.error:
                phase = result.phase.id
                session.diagnostics.append(
                    Diagnostic(
                        severity=Severity.ERROR,
                        code="PHASE_ERR",
                        message=result.error,
                        line=1,
                        column=1,
                        phase=phase,
                        source=f"compileone {phase}",
                    )
                )

        stream_data = session.artifacts.get("token_stream")
        if stream_data is not None:
            stream = parse_token_stream(stream_data)
            for error in stream.errors:
                session.diagnostics.append(
                    Diagnostic(
                        severity=Severity.ERROR,
                        code="LEX001",
                        message=error.message,
                        line=error.line,
                        column=error.column,
                        end_line=error.line,
                        end_column=error.column + max(1, len(error.lexeme)),
                        phase="lexical",
                        source="compileone lex (flex)",
                    )
                )

        exec_data = session.artifacts.get("execution")
        if exec_data is not None and exec_data.get("status") in (
            "lex-error", "runtime-error",
        ):
            for error in exec_data.get("errors", []):
                session.diagnostics.append(
                    Diagnostic(
                        severity=Severity.ERROR,
                        code="RUN001",
                        message=str(error["message"]),
                        line=int(error.get("line", 1)),
                        column=int(error.get("column", 1)),
                        end_line=int(error.get("line", 1)),
                        end_column=int(error.get("column", 1)) + 1,
                        phase="execution",
                        source="compileone run (interpreter)",
                    )
                )

    # ------------------------------------------------------ session helpers

    @staticmethod
    def token_stream_of(session: CompileSession):
        data = session.artifacts.get("token_stream")
        return parse_token_stream(data) if data else None

    @staticmethod
    def execution_of(session: CompileSession) -> Execution | None:
        data = session.artifacts.get("execution")
        return parse_execution(data) if data else None

    def export_tokens_csv(self, session: CompileSession, path: Path) -> None:
        """Export the token stream to a CSV file."""
        stream = self.token_stream_of(session)
        if stream is None:
            raise ValueError("no token artifact in session; compile first")

        with open(path, "w", encoding="utf-8", newline="") as fh:
            import csv

            writer = csv.writer(fh)
            writer.writerow(
                ["id", "line", "column", "lexeme", "token", "category",
                 "subtype", "length", "scope", "scope_level", "color",
                 "description", "offset_start", "offset_end"]
            )
            for token in stream.tokens:
                writer.writerow(
                    [token.id, token.line, token.column, token.lexeme,
                     token.token, token.category, token.subtype, token.length,
                     token.scope, token.scope_level, token.color,
                     token.description, token.offset_start, token.offset_end]
                )
