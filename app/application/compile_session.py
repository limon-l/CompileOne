"""One compilation run: source -> per-phase artifacts -> diagnostics."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from app.application.pipeline import PhaseResult
from app.domain.diagnostics import Diagnostic


@dataclass
class CompileSession:
    source_path: Path
    source_text: str = ""
    language: str = "mini-c"
    mode: str = "study"
    artifacts: dict[str, dict] = field(default_factory=dict)
    diagnostics: list[Diagnostic] = field(default_factory=list)
    phase_results: list[PhaseResult] = field(default_factory=list)
    timings: dict[str, float] = field(default_factory=dict)

    def phase_status(self, phase_id: str) -> str:
        for result in self.phase_results:
            if result.phase.id == phase_id:
                return result.status
        return "pending"

    def error_count(self) -> int:
        return sum(1 for d in self.diagnostics if d.severity == d.severity.ERROR)
