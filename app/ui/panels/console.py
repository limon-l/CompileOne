"""Console panel — live log stream from the compileone logger.

Backend stdout/stderr already arrives as logging records on the
`compileone` logger (via logging_service + backend_runner); this panel
attaches a Qt-signal bridge and renders the records in real time.
"""

from __future__ import annotations

import logging

from PyQt5.QtCore import pyqtSignal
from PyQt5.QtWidgets import (
    QHBoxLayout,
    QPlainTextEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from app.services.logging_service import buffered_records

LOGGER_NAME = "compileone"


class _QtLogHandler(logging.Handler):
    def __init__(self, callback) -> None:
        super().__init__()
        self._callback = callback

    def emit(self, record: logging.LogRecord) -> None:
        try:
            line = self.format(record)
        except Exception:  # noqa: BLE001 — never let logging break the UI
            return
        self._callback(line)


class ConsolePanel(QWidget):
    logRecord = pyqtSignal(str)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)

        self._text = QPlainTextEdit()
        self._text.setReadOnly(True)
        self._text.setLineWrapMode(QPlainTextEdit.NoWrap)
        self._text.setMaximumBlockCount(5000)

        self._clear_button = QPushButton("Clear")
        self._clear_button.clicked.connect(self.clear)

        header = QHBoxLayout()
        header.addWidget(self._clear_button)
        header.addStretch(1)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(4)
        layout.addLayout(header)
        layout.addWidget(self._text, 1)

        self._handler = _QtLogHandler(self.logRecord.emit)
        self._handler.setFormatter(logging.Formatter(
            "%(asctime)s %(levelname)-7s %(name)s: %(message)s", "%H:%M:%S"
        ))
        logging.getLogger(LOGGER_NAME).addHandler(self._handler)

        self.logRecord.connect(self._append)
        self._replay_buffered()

    # ------------------------------------------------------ rendering

    def _append(self, line: str) -> None:
        self._text.appendPlainText(line)

    def _replay_buffered(self) -> None:
        formatter = self._handler.formatter
        for record in buffered_records():
            self._append(formatter.format(record))

    def clear(self) -> None:
        self._text.clear()
