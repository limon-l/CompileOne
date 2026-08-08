"""Run Output panel — interactive stdin/stdout for a running program.

This widget provides a text area for program stdout/stderr and an input
box for the user to provide stdin.
"""

from __future__ import annotations

from PyQt5.QtCore import pyqtSignal
from PyQt5.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPlainTextEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from app.domain.artifacts import Execution


class RunOutputPanel(QWidget):
    # Emitted when the user presses Enter in the input line
    input_submitted = pyqtSignal(str)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)

        self._status_label = QLabel("Program has not run yet.")
        
        self._output_text = QPlainTextEdit()
        self._output_text.setReadOnly(True)
        self._output_text.setLineWrapMode(QPlainTextEdit.NoWrap)
        self._output_text.setMaximumBlockCount(10000)

        self._input_line = QLineEdit()
        self._input_line.setPlaceholderText("Program input (stdin)...")
        self._input_line.returnPressed.connect(self._on_input_submitted)
        
        self._clear_button = QPushButton("Clear")
        self._clear_button.clicked.connect(self.clear)

        header = QHBoxLayout()
        header.addWidget(self._status_label)
        header.addStretch(1)
        header.addWidget(self._clear_button)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(4)
        layout.addLayout(header)
        layout.addWidget(self._output_text, 1)
        layout.addWidget(self._input_line)

        self.set_input_enabled(False)

    def _on_input_submitted(self) -> None:
        """Internal slot to grab text and emit the signal."""
        text = self._input_line.text()
        self.input_submitted.emit(text)
        self._input_line.clear()
        # Optionally echo input to the output view
        self.append_output(f"> {text}\n")

    def append_output(self, text: str) -> None:
        """Appends a chunk of text from stdout/stderr to the output view."""
        self._output_text.insertPlainText(text)
        self._output_text.ensureCursorVisible() # Auto-scroll

    def set_input_enabled(self, enabled: bool) -> None:
        """Enables or disables the stdin input line."""
        if enabled:
            self._input_line.setEnabled(True)
            self._input_line.setFocus()
        else:
            self._input_line.setEnabled(False)
            self._input_line.clear()

    def show_final_status(self, execution: Execution) -> None:
        """Displays the final status of the execution after it completes."""
        if execution.ok:
            status_text = f"Finished · exit code {execution.exit_code}"
            self._status_label.setStyleSheet("")
        else:
            status_text = f"Failed · {execution.status} (exit code {execution.exit_code})"
            self._status_label.setStyleSheet("color: #f44747;")
        
        self._status_label.setText(status_text)
        self.set_input_enabled(False)

    def clear(self) -> None:
        """Clears the output and resets the status."""
        self._output_text.clear()
        self._status_label.setText("Program has not run yet.")
        self._status_label.setStyleSheet("")
        self.set_input_enabled(False)
