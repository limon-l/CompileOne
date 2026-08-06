"""Bridge to the compileone backend executable.

Every compiler phase is a subcommand of Build/compileone.exe. This class
owns subprocess creation, captures stderr (for the Output/Compiler
panels), and returns the phase artifact as a validated dict. It is the
single injection point for tests (subclass and override run_phase with a
fake that returns canned artifacts).
"""

from __future__ import annotations

import logging
import subprocess
from pathlib import Path

from app.infrastructure.json_loader import ArtifactError, load_json
from app.infrastructure.paths import PROJECT_ROOT

logger = logging.getLogger("compileone.backend")


class BackendError(RuntimeError):
    """The backend failed to run or produced no artifact."""


class PhaseNotImplemented(BackendError):
    """The phase is registered in the pipeline but not yet implemented."""


class BackendRunner:
    def __init__(self, exe: Path | None = None, cwd: Path | None = None) -> None:
        self._exe = Path(exe) if exe else PROJECT_ROOT / "Build" / "compileone.exe"
        self._cwd = cwd or PROJECT_ROOT

    @property
    def executable(self) -> Path:
        return self._exe

    def available(self) -> bool:
        return self._exe.is_file()

    def run_phase(
        self,
        phase: str,
        input_path: Path,
        output_path: Path,
        language: str = "mini-c",
    ) -> dict:
        """Run one backend phase and return its parsed artifact dict."""
        if not self.available():
            raise BackendError(
                f"backend executable not found: {self._exe} — run 'make' first"
            )

        cmd = [
            str(self._exe),
            phase,
            "--input", str(input_path),
            "--output", str(output_path),
            "--language", language,
        ]
        logger.debug("backend: %s", " ".join(cmd))

        proc = subprocess.run(
            cmd,
            cwd=str(self._cwd),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )

        if proc.stderr:
            logger.info("backend[%s]: %s", phase, proc.stderr.strip())

        if proc.returncode == 2:
            raise PhaseNotImplemented(
                f"phase '{phase}' is registered but not implemented yet"
            )
        if proc.returncode != 0:
            raise BackendError(
                f"phase '{phase}' failed (exit {proc.returncode}): "
                f"{proc.stderr.strip()}"
            )
        if not output_path.is_file():
            raise ArtifactError(f"phase '{phase}' wrote no artifact to {output_path}")

        return load_json(output_path)
