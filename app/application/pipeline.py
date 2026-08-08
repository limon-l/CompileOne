"""The compiler pipeline definition and execution driver.

A pipeline is an ordered list of Phase objects. Each phase maps to a
compileone backend subcommand and one output artifact. The pipeline
supports breakpoints, per-phase re-run (resume), and step-by-step
execution — the driver makes these trivial because every phase is a
standalone subprocess fed by the previous phase's JSON artifact.
"""

from __future__ import annotations

from dataclasses import dataclass

from app.infrastructure.backend_runner import PhaseNotImplemented
from app.infrastructure.json_loader import load_json
from app.infrastructure.paths import PHASE_ARTIFACTS

NATIVE_SOURCE_PHASES = {"parse", "ast", "semantic", "ir", "opt", "codegen"}
NATIVE_LANGUAGES = {"c", "c++", "java"}


@dataclass(frozen=True)
class Phase:
    id: str
    title: str
    input_kind: str          # "source" for the first phase, "artifact" thereafter
    output_artifact: str
    available: bool          # implemented in the backend yet?
    input_artifact: str | None = None
    mini_c_only: bool = False
    # When True the phase only runs for the mini-c study language
    # (parse / ast / semantic / run). Native languages (C, C++, ...)
    # lex through the shared scanner but are compiled/run by their own
    # external toolchain, not the mini-c front end.
    # input_artifact: overrides the artifact consumed instead of the
    # previous phase's output. The parse / ast / semantic phases all
    # derive from the token stream (the backend front end runs once and
    # yields CST + AST + symbols in-process), so they share
    # "token_stream" as their input.


STUDY_PHASES: list[Phase] = [
    Phase("lex", "Lexical Analysis", "source", "token_stream", True),
    Phase("parse", "Parsing (Parse Tree)", "artifact", "parse_tree", True,
          input_artifact="token_stream"),
    Phase("ast", "Abstract Syntax Tree", "artifact", "ast", True,
          input_artifact="token_stream"),
    Phase("semantic", "Semantic Analysis", "artifact", "semantic", True,
          input_artifact="token_stream"),
    Phase("ir", "Intermediate Representation", "artifact", "ir", True,
          input_artifact="token_stream"),
    Phase("opt", "Optimization", "artifact", "optimization", True,
          input_artifact="token_stream"),
    Phase("codegen", "Code Generation", "artifact", "assembly", True,
          input_artifact="token_stream"),
    Phase("run", "Execution", "source", "execution", True, mini_c_only=True),
]


class PhaseStatus:
    PENDING = "pending"
    RUNNING = "running"
    OK = "ok"
    ERROR = "error"
    SKIPPED = "skipped"
    UNAVAILABLE = "unavailable"
    CACHED = "cached"


@dataclass
class PhaseResult:
    phase: Phase
    status: str = PhaseStatus.PENDING
    duration_ms: float = 0.0
    error: str = ""

    @property
    def done(self) -> bool:
        return self.status in (PhaseStatus.OK, PhaseStatus.CACHED)


class Pipeline:
    def __init__(self, phases: list[Phase] | None = None) -> None:
        self.phases: list[Phase] = phases if phases is not None else list(STUDY_PHASES)
        self.breakpoints: set[str] = set()

    # ------------------------------------------------------ queries

    def phase_by_id(self, phase_id: str) -> Phase | None:
        for phase in self.phases:
            if phase.id == phase_id:
                return phase
        return None

    def available_phases(self) -> list[Phase]:
        return [p for p in self.phases if p.available]

    def previous_phase(self, phase: Phase) -> Phase | None:
        index = self.phases.index(phase)
        return self.phases[index - 1] if index > 0 else None

    def artifact_producer(self, artifact_id: str) -> Phase | None:
        for phase in self.phases:
            if phase.output_artifact == artifact_id:
                return phase
        return None

    # ------------------------------------------------------ execution

    def run(
        self,
        source_path,
        store,
        runner,
        session_artifacts: dict[str, dict],
        language: str = "mini-c",
        breakpoint_halt=None,
    ) -> list[PhaseResult]:
        """Run the pipeline from its first phase.

        `source_path` is the source file for the lex phase; later phases
        read the previous phase's artifact. Returns per-phase results and
        fills `session_artifacts` with the produced artifact dicts.
        """
        results: list[PhaseResult] = []
        halted = False

        def drop_stale(phase: Phase) -> None:
            """Remove a leftover artifact from a previous run so the UI
            never shows stale data for a phase that did not execute
            (e.g. mini-c phases skipped for native languages)."""
            session_artifacts.pop(phase.output_artifact, None)

        for phase in self.phases:
            result = PhaseResult(phase)
            results.append(result)

            if halted:
                result.status = PhaseStatus.SKIPPED
                drop_stale(phase)
                continue
            if phase.id in self.breakpoints:
                halted = True
                result.status = PhaseStatus.SKIPPED
                drop_stale(phase)
                continue
            if not phase.available:
                result.status = PhaseStatus.UNAVAILABLE
                result.error = "registered but not implemented yet (roadmap Phase C+)"
                drop_stale(phase)
                continue
            if phase.mini_c_only and language != "mini-c":
                result.status = PhaseStatus.SKIPPED
                result.error = (
                    "mini-c study phase; native languages use their own "
                    "toolchain instead"
                )
                drop_stale(phase)
                continue
            if phase.id == "run" and language != "mini-c":
                result.status = PhaseStatus.SKIPPED
                result.error = "interpreter supports mini-c only; native run used instead"
                drop_stale(phase)
                continue

            if phase.input_kind == "artifact":
                prev = self.previous_phase(phase)
                required_artifact = phase.input_artifact or prev.output_artifact

                if required_artifact not in session_artifacts:
                    producer = self.artifact_producer(required_artifact)
                    if producer is not None:
                        producing_result = next(
                            (r for r in results if r.phase == producer), None
                        )
                        if producing_result is not None:
                            if producing_result.status == PhaseStatus.UNAVAILABLE:
                                result.status = PhaseStatus.UNAVAILABLE
                                result.error = (
                                    f"required artifact '{required_artifact}' is unavailable "
                                    f"because phase '{producer.id}' is unavailable"
                                )
                                drop_stale(phase)
                                continue
                            if producing_result.status == PhaseStatus.ERROR:
                                result.status = PhaseStatus.SKIPPED
                                result.error = (
                                    f"required artifact '{required_artifact}' is missing because "
                                    f"phase '{producer.id}' failed"
                                )
                                drop_stale(phase)
                                continue
                    result.status = PhaseStatus.SKIPPED
                    result.error = (
                        f"required artifact '{required_artifact}' is missing; "
                        "the producing phase did not complete"
                    )
                    drop_stale(phase)
                    continue

                input_path = store.artifact_path(required_artifact)
            else:
                input_path = source_path

            result.status = PhaseStatus.RUNNING
            try:
                output_path = store.artifact_path(phase.output_artifact)
                data = runner.run_phase(
                    phase.id, input_path, output_path, language=language
                )
                result.duration_ms = float(data.get("duration_ms", 0.0))
                store.save(phase.output_artifact, data)
                session_artifacts[phase.output_artifact] = data
                result.status = PhaseStatus.OK
            except PhaseNotImplemented as exc:
                result.status = PhaseStatus.UNAVAILABLE
                result.error = str(exc)
                drop_stale(phase)
            except Exception as exc:  # noqa: BLE001 — surface every failure to the UI
                result.status = PhaseStatus.ERROR
                result.error = str(exc)
                drop_stale(phase)
                halted = True

        return results

    # ------------------------------------------------------ helpers

    def load_artifact(self, store, artifact_id: str) -> dict:
        """Re-read a stored artifact (used for replay/breakpoint resume)."""
        return load_json(store.artifact_path(artifact_id))

    @staticmethod
    def artifact_for(phase_id: str) -> str:
        return PHASE_ARTIFACTS.get(phase_id, phase_id)
