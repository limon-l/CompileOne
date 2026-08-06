"""mini-c syntax highlighter.

This is a pure *editor* concern (cosmetic colouring of the text view).
It never feeds the compiler pipeline — tokens come exclusively from the
compileone backend. Colours mirror the backend lexicon
(app/domain/lexicon.py).
"""

from __future__ import annotations

from PyQt5.QtCore import QRegularExpression
from PyQt5.QtGui import QColor, QFont, QSyntaxHighlighter, QTextCharFormat

from app.domain import lexicon


class MiniCHighlighter(QSyntaxHighlighter):
    def __init__(self, document) -> None:
        super().__init__(document)
        self._rules: list = []

        self._build_rules()

    # ------------------------------------------------------ rule helpers

    @staticmethod
    def _format(color: str, bold: bool = False) -> QTextCharFormat:
        fmt = QTextCharFormat()
        fmt.setForeground(QColor(color))
        if bold:
            fmt.setFontWeight(QFont.Bold)
        return fmt

    def _add(self, pattern: str, color: str, bold: bool = False) -> None:
        fmt = self._format(color, bold)
        self._rules.append((QRegularExpression(pattern), fmt))

    def _build_rules(self) -> None:
        keyword_pattern = "\\b(?:{})\\b".format("|".join(sorted(lexicon.MINIC_KEYWORDS)))
        self._add(keyword_pattern, lexicon.COLOR_KEYWORD, bold=True)

        for word, color in lexicon.MINIC_KEYWORDS.items():
            # types get the distinct teal colour
            if word in ("int", "float", "bool", "char"):
                self._add(f"\\b{word}\\b", color, bold=True)

        self._add(r"\b\d+(\.\d+)?\b", lexicon.COLOR_NUMBER)
        self._add(r"//[^\n]*", lexicon.COLOR_COMMENT)
        self._add(
            r"/\*.*?\*/",
            lexicon.COLOR_COMMENT,
        )
        self._add(
            r'"(?:\\.|[^"\\\n])*"',
            lexicon.COLOR_STRING,
        )
        self._add(
            r"\b[A-Za-z_][A-Za-z0-9_]*\b",
            lexicon.COLOR_IDENTIFIER,
        )
        self._add(
            r"<=|>=|==|!=|&&|\|\||\+\+|--|[+\-*/%<>=!;{},()\[\]]",
            lexicon.COLOR_OPERATOR,
        )

    # ------------------------------------------------------ block processing

    def highlightBlock(self, text: str) -> None:
        for regex, fmt in self._rules:
            iterator = regex.globalMatch(text)
            while iterator.hasNext():
                match = iterator.next()
                self.setFormat(match.capturedStart(), match.capturedLength(), fmt)
