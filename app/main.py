"""CompileOne application entry point.

Boots the PyQt5 application, wires the dependency container, applies the
theme, and shows the main window.
"""

from __future__ import annotations

import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from PyQt5.QtWidgets import QApplication, QMessageBox

from app.application.orchestrator import Orchestrator
from app.infrastructure.artifact_store import ArtifactStore
from app.infrastructure.backend_runner import BackendRunner
from app.services.logging_service import setup_logging
from app.services.settings_service import Settings
from app.services.theme_service import ThemeService
from app.ui.main_window import MainWindow


def main() -> int:
    setup_logging()

    app = QApplication(sys.argv)
    app.setApplicationName("CompileOne")
    app.setOrganizationName("CompileOne")

    settings = Settings()
    runner = BackendRunner()
    settings.register("runner", runner)
    settings.register("store", ArtifactStore())
    settings.register("orchestrator", Orchestrator(runner=runner))

    ThemeService().apply(app)

    window = MainWindow(app, settings)
    window.show()

    if not runner.available():
        QMessageBox.warning(
            window,
            "Backend not found",
            "Build/compileone.exe is missing.\n\n"
            "Run 'make' in the project root to build the compiler backend.",
        )

    return app.exec_()


if __name__ == "__main__":
    sys.exit(main())
