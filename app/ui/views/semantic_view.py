"""
Semantic View Widget

Shows the semantic-analysis artifact: a validity summary, the declared
symbol table (name, type, scope, constness, location) and the list of
diagnostics (errors and warnings) produced by the semantic analyzer.
"""

from __future__ import annotations

from PyQt5.QtCore import Qt
from PyQt5.QtGui import QBrush, QColor
from PyQt5.QtWidgets import (
    QHeaderView,
    QLabel,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from app.domain.artifacts import SemanticInfo


class SemanticView(QWidget):
    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)

        self._summary = QLabel("No semantic analysis yet.")
        self._summary.setStyleSheet("padding: 4px; font-weight: bold;")

        # --- Symbol table ---
        self._symbols_table = QTableWidget()
        self._symbols_table.setColumnCount(6)
        self._symbols_table.setHorizontalHeaderLabels(
            ["Name", "Type", "Scope", "Level", "Const", "Location"]
        )
        self._symbols_table.horizontalHeader().setSectionResizeMode(
            QHeaderView.Stretch
        )
        self._symbols_table.verticalHeader().setVisible(False)
        self._symbols_table.setEditTriggers(QTableWidget.NoEditTriggers)

        # --- Diagnostics tree ---
        self._diag_tree = QTreeWidget()
        self._diag_tree.setHeaderLabels(["Code", "Severity", "Location", "Message"])
        self._diag_tree.setColumnWidth(0, 80)
        self._diag_tree.setColumnWidth(1, 70)
        self._diag_tree.setColumnWidth(2, 90)

        splitter = QSplitter(Qt.Vertical)
        splitter.addWidget(self._symbols_table)
        splitter.addWidget(self._diag_tree)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self._summary)
        layout.addWidget(splitter)
        self.setLayout(layout)

    def set_semantic(self, semantic: SemanticInfo | None, language: str | None = None) -> None:
        self._symbols_table.setRowCount(0)
        self._diag_tree.clear()

        if semantic is None or (
            language is not None and language != "mini-c"
            and semantic.generated_by.endswith("native placeholder")
        ):
            if language is not None and language != "mini-c":
                self._summary.setText(
                    f"Semantic analysis is not available for {language}. "
                    "Full semantic support is only available for Mini-C."
                )
            else:
                self._summary.setText("No semantic analysis yet.")
            return

        status = "VALID" if semantic.valid else "INVALID"
        self._summary.setText(
            f"Semantic analysis: {status} — "
            f"{len(semantic.symbols)} symbol(s), "
            f"{semantic.error_count} error(s), {semantic.warning_count} warning(s)"
        )

        self._symbols_table.setRowCount(len(semantic.symbols))
        for row, symbol in enumerate(semantic.symbols):
            values = [
                symbol.name,
                symbol.type,
                symbol.scope,
                str(symbol.scope_level),
                "const" if symbol.is_const else "",
                f"{symbol.line}:{symbol.column}",
            ]
            for col, value in enumerate(values):
                self._symbols_table.setItem(row, col, QTableWidgetItem(value))

        for diagnostic in semantic.diagnostics:
            item = QTreeWidgetItem(
                [diagnostic.code,
                 diagnostic.severity,
                 f"{diagnostic.line}:{diagnostic.column}",
                 diagnostic.message]
            )
            if diagnostic.severity == "error":
                item.setForeground(0, QBrush(QColor("#f44747")))
            self._diag_tree.addTopLevelItem(item)

        self._diag_tree.resizeColumnToContents(0)
        self._diag_tree.resizeColumnToContents(1)
