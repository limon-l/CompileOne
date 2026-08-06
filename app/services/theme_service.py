"""Theme management.

Applies the VS Code Dark stylesheet to the QApplication and exposes the
font stack (JetBrains Mono -> Consolas fallback). Keeping theme logic in
one service means the UI code never hard-codes colours or fonts.
"""

from __future__ import annotations

import logging
from pathlib import Path

from PyQt5.QtGui import QFont

from app.infrastructure.paths import PROJECT_ROOT

logger = logging.getLogger("compileone.theme")

STYLESHEET_PATH = PROJECT_ROOT / "app" / "ui" / "theme" / "vs_dark.qss"


class ThemeService:
    def __init__(self, stylesheet: Path | None = None) -> None:
        self.stylesheet = stylesheet or STYLESHEET_PATH
        self._qss: str = ""

    @property
    def qss(self) -> str:
        if not self._qss and self.stylesheet.is_file():
            self._qss = self.stylesheet.read_text(encoding="utf-8")
        return self._qss

    def apply(self, app) -> None:
        app.setStyleSheet(self.qss)
        logger.debug("applied stylesheet %s", self.stylesheet)

    # ------------------------------------------------------ fonts

    @staticmethod
    def code_font(point_size: int = 11) -> QFont:
        """The editor font: JetBrains Mono when installed, else Consolas."""
        for family in ("JetBrains Mono", "Consolas", "Courier New"):
            candidate = QFont(family, point_size)
            if candidate.family() == family or family == "Courier New":
                return candidate
        return QFont("Consolas", point_size)

    @staticmethod
    def ui_font(point_size: int = 10) -> QFont:
        for family in ("Segoe UI Variable Text", "Segoe UI", "Arial"):
            candidate = QFont(family, point_size)
            if candidate.family() == family or family == "Arial":
                return candidate
        return QFont("Segoe UI", point_size)
