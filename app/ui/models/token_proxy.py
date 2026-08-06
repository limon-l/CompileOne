"""Sorting/filtering proxy for the token table.

Combines a free-text filter (matched case-insensitively against the
lexeme, token name, category, subtype, scope and description) with an
optional category filter. Works on top of TokenTableModel so the source
model stays untouched and CSV export sees the *filtered* view.
"""

from __future__ import annotations

from PyQt5.QtCore import QModelIndex, QSortFilterProxyModel, Qt

SEARCHABLE_COLUMNS = (3, 4, 5, 6, 8, 10)  # lexeme, token, category, subtype, scope, description


class TokenFilterProxy(QSortFilterProxyModel):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._filter_text: str = ""
        self._category: str | None = None
        self.setDynamicSortFilter(True)
        self.setSortCaseSensitivity(Qt.CaseInsensitive)
        self.setFilterCaseSensitivity(Qt.CaseInsensitive)

    # ------------------------------------------------------ filters

    def set_filter_text(self, text: str) -> None:
        self._filter_text = text.strip().lower()
        self.invalidateFilter()

    def set_category_filter(self, category: str | None) -> None:
        self._category = category if category else None
        self.invalidateFilter()

    def clear_filters(self) -> None:
        self._filter_text = ""
        self._category = None
        self.invalidateFilter()

    # ------------------------------------------------------ matching

    def filterAcceptsRow(self, source_row: int, source_parent: QModelIndex) -> bool:
        model = self.sourceModel()
        index = model.index(source_row, 0, source_parent)
        token = model.data(index, Qt.UserRole + 1)

        if self._category and token.category != self._category:
            return False
        if not self._filter_text:
            return True
        haystack = " ".join(
            str(getattr(token, attr, "")) for attr in ("lexeme", "token", "category", "subtype", "scope", "description")
        ).lower()
        return self._filter_text in haystack
