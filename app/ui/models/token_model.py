"""Qt table model for the token stream.

Serves the TokenTableView data grid. Supports sorting (via
QSortFilterProxyModel in the view) and exposes the full Token object per
row for CSV export.
"""

from __future__ import annotations

from PyQt5.QtCore import QAbstractTableModel, QModelIndex, Qt, QVariant

from app.domain.artifacts import Token

COL_LINE = 0
COL_LEXEME = 1
COL_TOKEN = 2
COL_ROLE = 3

COLUMN_COUNT = 4

HEADERS = [
    "Line", "Lexeme", "Token Type", "Role",
]

USER_ROLE = Qt.UserRole + 1


class TokenTableModel(QAbstractTableModel):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._tokens: list[Token] = []

    # ------------------------------------------------------ population

    def set_tokens(self, tokens: list[Token]) -> None:
        self.beginResetModel()
        self._tokens = list(tokens)
        self.endResetModel()

    def clear(self) -> None:
        self.set_tokens([])

    # ------------------------------------------------------ Qt interface

    def rowCount(self, parent: QModelIndex = QModelIndex()) -> int:  # noqa: B008
        return 0 if parent.isValid() else len(self._tokens)

    def columnCount(self, parent: QModelIndex = QModelIndex()) -> int:  # noqa: B008
        return 0 if parent.isValid() else COLUMN_COUNT

    def data(self, index: QModelIndex, role: int = Qt.DisplayRole):
        if not index.isValid():
            return QVariant()
        token = self._tokens[index.row()]
        column = index.column()

        if role == USER_ROLE:
            return token
        if role == Qt.DisplayRole:
            return self._display(token, column)
        if role == Qt.ForegroundRole:
            return self._foreground(token, column)
        if role == Qt.TextAlignmentRole and column == COL_LINE:
            return int(Qt.AlignCenter)
        if role == Qt.ToolTipRole:
            return f"{token.token}\n{token.category}"
        return QVariant()

    def headerData(self, section, orientation, role=Qt.DisplayRole):
        if role == Qt.DisplayRole and orientation == Qt.Horizontal and 0 <= section < COLUMN_COUNT:
            return HEADERS[section]
        return QVariant()

    # ------------------------------------------------------ helpers

    @staticmethod
    def _display(token: Token, column: int):
        mapping = {
            COL_LINE: token.line,
            COL_LEXEME: token.lexeme,
            COL_TOKEN: token.token,
            COL_ROLE: token.category,
        }
        return mapping.get(column, QVariant())

    @staticmethod
    def _foreground(token: Token, column: int):
        from PyQt5.QtGui import QBrush, QColor

        category_colors = {
            "keyword": "#569cd6",
            "type": "#4ec9b0",
            "identifier": "#9cdcfe",
            "literal": "#b5cea8",
            "operator": "#d4d4d4",
            "delimiter": "#d4d4d4",
            "preprocessor": "#c586c0",
            "comment": "#6a9955",
            "error": "#f44747",
        }
        color = category_colors.get(token.category, "#cccccc")
        if column == COL_LEXEME or column == COL_ROLE:
            return QBrush(QColor(color))
        return QVariant()

    # ------------------------------------------------------ row access

    def token_at(self, row: int) -> Token | None:
        if 0 <= row < len(self._tokens):
            return self._tokens[row]
        return None

    def all_tokens(self) -> list[Token]:
        return list(self._tokens)
