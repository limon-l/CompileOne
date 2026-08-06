"""Run Output panel — program stdout from the execution phase.

Rendered from the execution artifact: a summary header (status, exit
code, step count) followed by the program's own output lines, then any
runtime errors the interpreter reported.
"""

from __future__ import annotations

from PyQt5.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QPlainTextEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from app.domain.artifacts import Execution


class RunOutputPanel(QWidget):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)

        self._status = QLabel("no run yet")
        self._text = QPlainTextEdit()
        self._text.setReadOnly(True)
        self._text.setLineWrapMode(QPlainTextEdit.NoWrap)
        self._text.setMaximumBlockCount(5000)

        self._clear_button = QPushButton("Clear")
        self._clear_button.clicked.connect(self.clear)

        header = QHBoxLayout()
        header.addWidget(self._status)
        header.addStretch(1)
        header.addWidget(self._clear_button)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(4)
        layout.addLayout(header)
        layout.addWidget(self._text, 1)

    def show_execution(self, execution: Execution | None) -> None:
        self._text.clear()
        if execution is None:
            self._status.setText("no run yet")
            return

        if execution.ok:
            self._status.setText(
                f"ok · exit {execution.exit_code} · {execution.steps} steps"
            )
            self._status.setStyleSheet("")
        else:
            self._status.setText(
                f"{execution.status} · exit {execution.exit_code} · "
                f"{execution.steps} steps"
            )
            self._status.setStyleSheet("color: #f44747;")

        for line in execution.output:
            self._text.appendPlainText(line)
        for error in execution.errors:
            self._text.appendPlainText(
                f"[{execution.status}] {error.line}:{error.column} {error.message}"
            )

    def clear(self) -> None:
        self._text.clear()
        self._status.setText("no run yet")
        self._status.setStyleSheet("")
