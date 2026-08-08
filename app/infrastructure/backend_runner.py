"""Bridges to backend executables for artifact generation and interactive execution.

- BackendRunner: Runs the `compileone` compiler as a short-lived, blocking
  process to generate phase artifacts (tokens, AST, etc.). It uses
  Python's standard `subprocess` module.

- InteractiveProcessRunner: Runs a compiled user program (e.g., a.exe, a
  Java class) as a long-lived, non-blocking process. It uses PyQt's
  `QProcess` to handle real-time stdin, stdout, and stderr, connecting
  them to the UI's RunOutputPanel.
"""

from __future__ import annotations

import json
import logging
import subprocess
from pathlib import Path

from PyQt5.QtCore import QObject, QProcess, pyqtSignal

from app.infrastructure.json_loader import ArtifactError, load_json
from app.infrastructure.tool_detector import Toolchain, ToolInfo
from app.ui.panels.run_output import RunOutputPanel

logger = logging.getLogger("compileone.backend")


# ===================================================================
# Runner for short-lived artifact generation (e.g., 'compileone lex')
# ===================================================================

class BackendError(RuntimeError):
    """The backend failed to run or produced no artifact."""


class PhaseNotImplemented(BackendError):
    """The phase is registered in the pipeline but not yet implemented."""


class BackendRunner:
    NATIVE_PLACEHOLDER_PHASES = {
        "parse",
        "ast",
        "semantic",
        "ir",
        "opt",
        "codegen",
    }

    def __init__(self, toolchain: Toolchain, cwd: Path | None = None) -> None:
        self._toolchain = toolchain
        self._tool_info: ToolInfo | None = self._toolchain.get("compileone")
        self._cwd = cwd or self._toolchain.project_root

    @property
    def executable(self) -> str | None:
        return self._tool_info.path if self._tool_info else None

    def available(self) -> bool:
        return self._tool_info.available if self._tool_info else False

    def _write_json_artifact(self, path: Path, data: dict) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(data, fh, indent=2)

    def _native_placeholder_artifact(
        self,
        phase: str,
        input_path: Path,
        language: str,
    ) -> dict:
        generated_by = f"{language} native placeholder"
        source_file = str(input_path)

        if phase == "parse":
            return {
                "schema": "compileone/parse-tree/1.0",
                "phase": "parse",
                "language": language,
                "source_file": source_file,
                "generated_by": generated_by,
                "duration_ms": 0.0,
                "root": None,
                "errors": [],
            }

        if phase == "ast":
            return {
                "schema": "compileone/ast/1.0",
                "phase": "ast",
                "language": language,
                "source_file": source_file,
                "generated_by": generated_by,
                "duration_ms": 0.0,
                "root": None,
                "errors": [],
            }

        if phase == "semantic":
            return {
                "schema": "compileone/semantic/1.0",
                "phase": "semantic",
                "language": language,
                "source_file": source_file,
                "generated_by": generated_by,
                "duration_ms": 0.0,
                "valid": True,
                "symbols": [],
                "diagnostics": [],
            }

        if phase == "ir":
            return {
                "schema": "compileone/ir/1.0",
                "phase": "ir",
                "language": language,
                "source_file": source_file,
                "generated_by": generated_by,
                "duration_ms": 0.0,
                "tac": [],
                "quadruples": [],
                "temporaries": [],
                "labels": [],
                "errors": [],
            }

        if phase == "opt":
            return {
                "schema": "compileone/optimization/1.0",
                "phase": "optimization",
                "language": language,
                "source_file": source_file,
                "generated_by": generated_by,
                "duration_ms": 0.0,
                "before_instruction_count": 0,
                "after_instruction_count": 0,
                "instruction_reduction_pct": 0.0,
                "passes": [],
                "errors": [],
            }

        if phase == "codegen":
            return {
                "schema": "compileone/assembly/1.0",
                "phase": "codegen",
                "language": language,
                "source_file": source_file,
                "generated_by": generated_by,
                "duration_ms": 0.0,
                "arch": "x86_64",
                "syntax": "att",
                "text": "",
                "instructions": [],
                "prologue": [],
                "epilogue": [],
                "stack_layout": {
                    "total_size": 0,
                    "slots": [],
                },
                "errors": [],
            }

        raise PhaseNotImplemented(f"phase '{phase}' is not supported for native languages")

    def _run_compileone_phase(
        self,
        phase: str,
        input_path: Path,
        output_path: Path,
        language: str,
    ) -> dict:
        """Run a real compileone backend phase for native languages."""
        if not self.available() or not self.executable:
            raise BackendError(
                "compileone backend executable not found. "
                "Ensure it is built and in the project's build/ or bin/ directory, "
                "or in the system PATH."
            )

        cmd = [
            self.executable,
            phase,
            "--input",
            str(input_path),
            "--output",
            str(output_path),
            "--language",
            language,
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

    def run_phase(
        self,
        phase: str,
        input_path: Path,
        output_path: Path,
        language: str = "mini-c",
    ) -> dict:
        """Run one backend phase and return its parsed artifact dict."""
        if language != "mini-c" and phase in self.NATIVE_PLACEHOLDER_PHASES:
            data = self._native_placeholder_artifact(phase, input_path, language)
            self._write_json_artifact(output_path, data)
            return data

        if not self.available() or not self.executable:
            raise BackendError(
                "compileone backend executable not found. "
                "Ensure it is built and in the project's build/ or bin/ directory, "
                "or in the system PATH."
            )

        cmd = [
            self.executable,
            phase,
            "--input",
            str(input_path),
            "--output",
            str(output_path),
            "--language",
            language,
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


# ===================================================================
# Runner for long-lived interactive program execution
# ===================================================================

class InteractiveProcessRunner(QObject):
    """
    Manages a QProcess for running a user's program interactively.
    """
    execution_finished = pyqtSignal(int, QProcess.ExitStatus)

    def __init__(self, output_panel: RunOutputPanel, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._process = QProcess(self)
        self._output_panel = output_panel

        # --- Connect QProcess signals to our slots ---
        self._process.readyReadStandardOutput.connect(self._on_stdout_ready)
        self._process.readyReadStandardError.connect(self._on_stderr_ready)
        self._process.stateChanged.connect(self._on_state_changed)
        self._process.finished.connect(self.execution_finished)

        # --- Connect our UI panel to the QProcess ---
        self._output_panel.input_submitted.connect(self._write_stdin)

    def start(self, program: str, args: list[str], cwd: str) -> None:
        """Starts the interactive process."""
        if self.is_running():
            logger.warning("An interactive process is already running. Cannot start another.")
            return

        self._output_panel.clear()
        self._process.setWorkingDirectory(cwd)
        self._process.start(program, args)

    def stop(self) -> None:
        """Stops the process if it's running."""
        if self.is_running():
            self._process.kill()

    def is_running(self) -> bool:
        """Returns True if the process is currently running."""
        return self._process.state() != QProcess.NotRunning

    def _on_stdout_ready(self) -> None:
        """Called when the process writes to stdout."""
        data = self._process.readAllStandardOutput().data().decode("utf-8", "replace")
        self._output_panel.append_output(data)

    def _on_stderr_ready(self) -> None:
        """Called when the process writes to stderr."""
        data = self._process.readAllStandardError().data().decode("utf-8", "replace")
        self._output_panel.append_output(data)

    def _on_state_changed(self, new_state: QProcess.ProcessState) -> None:
        """Manages the UI's input box based on process state."""
        is_running = new_state == QProcess.Running
        self._output_panel.set_input_enabled(is_running)

    def _write_stdin(self, text: str) -> None:
        """Writes the user's input from the UI to the process's stdin."""
        if self.is_running():
            # Ensure newline, as most console programs expect it
            self._process.write(f"{text}\n".encode())
