"""Diagnostic model shared by every pipeline phase.

Diagnostics are surfaced in the Problems panel, as editor squiggles,
and inside exported reports.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum


class Severity(IntEnum):
    ERROR = 0
    WARNING = 1
    INFO = 2


SEVERITY_NAMES = {Severity.ERROR: "error", Severity.WARNING: "warning", Severity.INFO: "info"}


@dataclass
class Diagnostic:
    severity: Severity
    code: str
    message: str
    line: int
    column: int
    end_line: int = 0
    end_column: int = 0
    phase: str = ""
    source: str = ""

    @property
    def severity_name(self) -> str:
        return SEVERITY_NAMES[self.severity]

    def location(self) -> str:
        return f"{self.line}:{self.column}"
