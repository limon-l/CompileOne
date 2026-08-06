"""Structured logging to file and console.

Logs go to Temp/logs/compileone.log and are also available to the
Console panel through a handler that forwards records to Qt signals
(used by ui/panels/console.py in a later milestone).
"""

from __future__ import annotations

import logging
import sys
from pathlib import Path

from app.infrastructure.paths import LOG_DIR

LOGGER_NAME = "compileone"


class ListLogHandler(logging.Handler):
    """Buffers records in memory for the Console panel / tests."""

    def __init__(self) -> None:
        super().__init__()
        self.records: list[logging.LogRecord] = []

    def emit(self, record: logging.LogRecord) -> None:
        self.records.append(record)


_handlers: list[logging.Handler] = []


def setup_logging(log_dir: Path = LOG_DIR, level: int = logging.DEBUG) -> list[logging.Handler]:
    """Configure the 'compileone' logger. Safe to call more than once."""
    log_dir.mkdir(parents=True, exist_ok=True)

    logger = logging.getLogger(LOGGER_NAME)
    logger.setLevel(level)
    logger.propagate = False

    # Avoid duplicate handlers on re-entry (tests / import reloads).
    for handler in list(logger.handlers):
        logger.removeHandler(handler)
    _handlers.clear()

    file_handler = logging.FileHandler(log_dir / "compileone.log", encoding="utf-8")
    file_handler.setFormatter(
        logging.Formatter("%(asctime)s %(levelname)-8s %(name)s: %(message)s")
    )
    stream_handler = logging.StreamHandler(sys.stderr)
    stream_handler.setFormatter(logging.Formatter("%(levelname)-8s %(name)s: %(message)s"))
    list_handler = ListLogHandler()

    logger.addHandler(file_handler)
    logger.addHandler(stream_handler)
    logger.addHandler(list_handler)
    _handlers.extend([file_handler, stream_handler, list_handler])

    return _handlers


def get_logger() -> logging.Logger:
    return logging.getLogger(LOGGER_NAME)


def buffered_records() -> list[logging.LogRecord]:
    for handler in logging.getLogger(LOGGER_NAME).handlers:
        if isinstance(handler, ListLogHandler):
            return handler.records
    return []
