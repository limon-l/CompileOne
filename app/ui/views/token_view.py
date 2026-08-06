"""Token-stream data grid.

Wraps the TokenTableModel + TokenFilterProxy in a panel with a search
box, a category filter, a live count label, and CSV export of the
visible (filtered) rows. Double-clicking a row emits `tokenActivated`
so the main window can jump the editor to that token.
"""

from __future__ import annotations

import csv
import logging

from PyQt5.QtCore import pyqtSignal
from PyQt5.QtWidgets import (
    QComboBox,
    QFileDialog,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QPushButton,
    QTableView,
    QVBoxLayout,
    QWidget,
)

from app.domain.artifacts import Token, TokenStream
from app.ui.models.token_model import USER_ROLE, TokenTableModel
from app.ui.models.token_proxy import TokenFilterProxy

logger = logging.getLogger("compileone.token_view")


class TokenTableView(QWidget):
    tokenActivated = pyqtSignal(object)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)

        self._source_model = TokenTableModel(self)
        self._proxy = TokenFilterProxy(self)
        self._proxy.setSourceModel(self._source_model)

        self._build_ui()
        self._refresh_count()

    # ------------------------------------------------------ UI construction

    def _build_ui(self) -> None:
        self._search = QLineEdit()
        self._search.setPlaceholderText("Filter tokens…")
        self._search.setClearButtonEnabled(True)
        self._search.textChanged.connect(self._proxy.set_filter_text)

        self._category_combo = QComboBox()
        self._category_combo.setMinimumWidth(150)
        self._category_combo.currentTextChanged.connect(self._on_category_changed)

        self._count_label = QLabel()

        self._export_button = QPushButton("Export CSV…")
        self._export_button.clicked.connect(self.export_csv)

        controls = QHBoxLayout()
        controls.addWidget(self._search, 1)
        controls.addWidget(self._category_combo)
        controls.addWidget(self._count_label)
        controls.addWidget(self._export_button)

        self._table = QTableView()
        self._table.setModel(self._proxy)
        self._table.setAlternatingRowColors(True)
        self._table.setSortingEnabled(True)
        self._table.setEditTriggers(QTableView.NoEditTriggers)
        self._table.setSelectionBehavior(QTableView.SelectRows)
        self._table.verticalHeader().setVisible(False)
        self._table.doubleClicked.connect(self._on_double_clicked)

        header = self._table.horizontalHeader()
        header.setSectionResizeMode(QHeaderView.ResizeToContents)
        header.setStretchLastSection(True)
        header.setSectionsClickable(True)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(4)
        layout.addLayout(controls)
        layout.addWidget(self._table, 1)

    # ------------------------------------------------------ population

    def set_stream(self, stream: TokenStream | None) -> None:
        tokens: list[Token] = stream.tokens if stream else []
        self._source_model.set_tokens(tokens)
        self._update_category_combo({t.category for t in tokens})
        self._refresh_count()
        if stream and stream.errors:
            logger.info("token view: %d token(s), %d error(s)", len(tokens), len(stream.errors))

    def clear(self) -> None:
        self._source_model.clear()
        self._update_category_combo(set())
        self._refresh_count()

    # ------------------------------------------------------ filtering

    def _update_category_combo(self, categories) -> None:
        current = self._category_combo.currentText()
        self._category_combo.blockSignals(True)
        self._category_combo.clear()
        self._category_combo.addItem("All categories")
        ordered = [
            c for c in ("keyword", "type", "identifier", "literal", "operator", "delimiter", "comment", "error")
            if c in categories
        ]
        ordered += sorted(c for c in categories if c not in ordered)
        for category in ordered:
            self._category_combo.addItem(category)
        index = self._category_combo.findText(current)
        self._category_combo.setCurrentIndex(max(index, 0))
        self._category_combo.blockSignals(False)

    def _on_category_changed(self, text: str) -> None:
        self._proxy.set_category_filter(None if text in ("", "All categories") else text)
        self._refresh_count()

    def _refresh_count(self) -> None:
        shown = self._proxy.rowCount()
        total = self._source_model.rowCount()
        if total and shown != total:
            self._count_label.setText(f"{shown} / {total} tokens")
        else:
            self._count_label.setText(f"{total} tokens")

    # ------------------------------------------------------ interaction

    def _on_double_clicked(self, index) -> None:
        source_index = self._proxy.mapToSource(index)
        if not source_index.isValid():
            return
        token = self._source_model.data(source_index, USER_ROLE)
        if token is not None:
            self.tokenActivated.emit(token)

    # ------------------------------------------------------ export

    def export_csv(self) -> None:
        if self._proxy.rowCount() == 0:
            return
        path, _ = QFileDialog.getSaveFileName(
            self, "Export Tokens to CSV", "tokens.csv", "CSV files (*.csv)"
        )
        if not path:
            return

        columns = [
            "id", "line", "column", "lexeme", "token", "category",
            "subtype", "length", "scope", "scope_level", "color",
            "description", "offset_start", "offset_end",
        ]
        with open(path, "w", encoding="utf-8", newline="") as fh:
            writer = csv.writer(fh)
            writer.writerow(columns)
            for row in range(self._proxy.rowCount()):
                source_index = self._proxy.mapToSource(self._proxy.index(row, 0))
                token = self._source_model.data(source_index, USER_ROLE)
                if token is None:
                    continue
                writer.writerow(
                    [token.id, token.line, token.column, token.lexeme,
                     token.token, token.category, token.subtype, token.length,
                     token.scope, token.scope_level, token.color,
                     token.description, token.offset_start, token.offset_end]
                )
        self._count_label.setText(f"Exported {self._proxy.rowCount()} tokens")
        logger.info("exported %d tokens to %s", self._proxy.rowCount(), path)
