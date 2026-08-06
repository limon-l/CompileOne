"""Problems (diagnostics) panel.

A read-only table listing every Diagnostic produced by the last
compile. Double-clicking a row emits `diagnosticActivated` so the main
window can jump the editor to the source location.
"""

from __future__ import annotations

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtGui import QBrush, QColor, QStandardItem, QStandardItemModel
from PyQt5.QtWidgets import (
    QAbstractItemView,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QTableView,
    QVBoxLayout,
    QWidget,
)

from app.domain.diagnostics import Diagnostic, Severity

SEVERITY_COLORS = {
    Severity.ERROR: "#f44747",
    Severity.WARNING: "#ffcc00",
    Severity.INFO: "#75beff",
}

_COLUMNS = ["Severity", "Code", "Message", "Location", "Phase", "Source"]


class ProblemsPanel(QWidget):
    diagnosticActivated = pyqtSignal(object)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)

        self._diagnostics: list[Diagnostic] = []
        self._model = QStandardItemModel(0, len(_COLUMNS), self)
        self._model.setHorizontalHeaderLabels(_COLUMNS)

        self._empty_label = QLabel("No problems detected.")
        self._empty_label.setAlignment(Qt.AlignCenter)
        self._empty_label.setStyleSheet("color: #6a9955;")

        self._table = QTableView()
        self._table.setModel(self._model)
        self._table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self._table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self._table.setSelectionMode(QAbstractItemView.SingleSelection)
        self._table.verticalHeader().setVisible(False)
        self._table.horizontalHeader().setStretchLastSection(True)
        self._table.doubleClicked.connect(self._on_double_clicked)

        self._count_label = QLabel()

        self._clear_button = QPushButton("Clear")
        self._clear_button.setFixedWidth(60)
        self._clear_button.clicked.connect(self.clear)

        controls = QHBoxLayout()
        controls.addWidget(self._empty_label)
        controls.addStretch(1)
        controls.addWidget(self._count_label)
        controls.addWidget(self._clear_button)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(4)
        layout.addLayout(controls)
        layout.addWidget(self._table, 1)

        self.clear()

    # ------------------------------------------------------ population

    def set_diagnostics(self, diagnostics: list[Diagnostic]) -> None:
        self._diagnostics = list(diagnostics)
        self._model.removeRows(0, self._model.rowCount())

        for diag in self._diagnostics:
            severity_item = QStandardItem(diag.severity_name.upper())
            severity_item.setForeground(QBrush(QColor(SEVERITY_COLORS.get(diag.severity, "#cccccc"))))
            severity_item.setTextAlignment(Qt.AlignCenter)

            code_item = QStandardItem(diag.code)
            code_item.setTextAlignment(Qt.AlignCenter)

            message_item = QStandardItem(diag.message)
            font = message_item.font()
            font.setFamily("Consolas")
            font.setPointSize(font.pointSize() + 1)
            message_item.setFont(font)

            location_item = QStandardItem(diag.location())
            location_item.setTextAlignment(Qt.AlignCenter)

            phase_item = QStandardItem(diag.phase)
            source_item = QStandardItem(diag.source)

            for item in (severity_item, code_item, message_item, location_item, phase_item, source_item):
                item.setData(diag, Qt.UserRole)

            self._model.appendRow([severity_item, code_item, message_item, location_item, phase_item, source_item])

        self._empty_label.setVisible(not self._diagnostics)
        self._count_label.setText(f"{len(self._diagnostics)} problem(s)")

    def clear(self) -> None:
        self.set_diagnostics([])

    # ------------------------------------------------------ interaction

    def _on_double_clicked(self, index) -> None:
        item = self._model.item(index.row(), 0)
        if item is None:
            return
        diag = item.data(Qt.UserRole)
        if isinstance(diag, Diagnostic):
            self.diagnosticActivated.emit(diag)
