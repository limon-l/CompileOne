"""Syntax highlighters for all supported languages.

A factory function, create_highlighter, returns a QSyntaxHighlighter
instance appropriate for the given language name (e.g., "c", "java").

The implementation uses a base class for the common highlighting logic
(comments, strings, numbers) and stateful multi-line comment handling,
and subclasses provide language-specific keyword sets.
"""

from __future__ import annotations

from PyQt5.QtCore import QRegularExpression, QRegularExpressionMatch
from PyQt5.QtGui import QColor, QFont, QSyntaxHighlighter, QTextCharFormat, QTextDocument

from app.domain import lexicon

# Enum for block states, used for multi-line comments
NORMAL_STATE = -1
COMMENT_STATE = 0


class SyntaxHighlighterBase(QSyntaxHighlighter):
    """
    Base class for language highlighters, handling common tokens
    like comments, strings, numbers, and operators. Subclasses must
    provide a keyword map.
    """

    def __init__(self, document: QTextDocument, keyword_map: dict[str, str]) -> None:
        super().__init__(document)
        self._rules: list[tuple[QRegularExpression, QTextCharFormat]] = []
        self._keyword_map = keyword_map

        # For multi-line comments
        self.comment_start = QRegularExpression(r"/\*")
        self.comment_end = QRegularExpression(r"\*/")
        self.comment_format = self._format(lexicon.COLOR_COMMENT)

        self._build_rules()

    @staticmethod
    def _format(color: str, bold: bool = False) -> QTextCharFormat:
        fmt = QTextCharFormat()
        fmt.setForeground(QColor(color))
        if bold:
            fmt.setFontWeight(QFont.Bold)
        return fmt

    def _add_rule(self, pattern: str, color: str, bold: bool = False) -> None:
        fmt = self._format(color, bold)
        self._rules.append((QRegularExpression(pattern), fmt))

    def _build_rules(self) -> None:
        """Builds highlighting rules for the language."""
        # Note: Order matters. More specific rules should come first.
        
        # Keywords and Types from the provided map
        keyword_pattern = r"\b(?:{})\b".format("|".join(sorted(self._keyword_map.keys())))
        self._add_rule(keyword_pattern, lexicon.COLOR_KEYWORD, bold=True)
        
        # Specific color overrides for types
        for word, color in self._keyword_map.items():
            if color == lexicon.COLOR_TYPE:
                self._add_rule(f"\\b{word}\\b", color, bold=True)

        # Common language constructs
        self._add_rule(r'"(?:\\.|[^"\\\n])*"', lexicon.COLOR_STRING)  # Strings
        self._add_rule(r"'[^']'", lexicon.COLOR_STRING)  # Chars
        self._add_rule(r"\b\d+(\.\d+)?[fL]?\b", lexicon.COLOR_NUMBER)  # Numbers
        self._add_rule(r"//[^\n]*", lexicon.COLOR_COMMENT)  # Single-line comments

        # Identifiers and Operators
        self._add_rule(r"\b[A-Za-z_][A-Za-z0-9_]*\b", lexicon.COLOR_IDENTIFIER)
        self._add_rule(r"<=|>=|==|!=|&&|\|\||\+\+|--|[+\-*/%<>=!;{},()\[\]\.]", lexicon.COLOR_OPERATOR)

    def highlightBlock(self, text: str) -> None:
        # Apply all single-line regex rules first
        for regex, fmt in self._rules:
            iterator: QRegularExpressionMatch = regex.globalMatch(text)
            while iterator.hasNext():
                match = iterator.next()
                self.setFormat(match.capturedStart(), match.capturedLength(), fmt)

        # Then, handle multi-line comments statefully
        self.setCurrentBlockState(NORMAL_STATE)
        self._highlight_multiline_comment(text)

    def _highlight_multiline_comment(self, text: str):
        start_index = 0
        if self.previousBlockState() != COMMENT_STATE:
            match = self.comment_start.match(text)
            start_index = match.capturedStart() if match else -1
        
        while start_index >= 0:
            match = self.comment_end.match(text, start_index)
            end_index = match.capturedStart() if match else -1

            comment_len = len(text) - start_index
            if end_index != -1:
                comment_len = end_index - start_index + match.capturedLength()
                self.setCurrentBlockState(NORMAL_STATE)
            else:
                self.setCurrentBlockState(COMMENT_STATE)

            self.setFormat(start_index, comment_len, self.comment_format)
            
            match = self.comment_start.match(text, start_index + comment_len)
            start_index = match.capturedStart() if match else -1


class MiniCHighlighter(SyntaxHighlighterBase):
    def __init__(self, document: QTextDocument) -> None:
        super().__init__(document, lexicon.MINIC_KEYWORDS)


class CHighlighter(SyntaxHighlighterBase):
    def __init__(self, document: QTextDocument) -> None:
        super().__init__(document, lexicon.C_KEYWORDS)
        # Add C-specific rules, like preprocessor directives
        self._add_rule(r"^\s*#\w+", lexicon.COLOR_KEYWORD, bold=True)


class CppHighlighter(SyntaxHighlighterBase):
    def __init__(self, document: QTextDocument) -> None:
        super().__init__(document, lexicon.CPP_KEYWORDS)
        # Add C++-specific rules
        self._add_rule(r"^\s*#\w+", lexicon.COLOR_KEYWORD, bold=True)
        self._add_rule(r"\b[A-Z_][A-Z0-9_]*\b", lexicon.COLOR_TYPE) # Convention for constants/macros


class JavaHighlighter(SyntaxHighlighterBase):
    def __init__(self, document: QTextDocument) -> None:
        super().__init__(document, lexicon.JAVA_KEYWORDS)
        # Add Java-specific rules, like annotations
        self._add_rule(r"@[A-Za-z]+", lexicon.COLOR_KEYWORD, bold=True)


def create_highlighter(language: str, document: QTextDocument) -> QSyntaxHighlighter:
    """Factory for creating a syntax highlighter for a given language."""
    if language.lower() == "c":
        return CHighlighter(document)
    if language.lower() in ("c++", "cpp"):
        return CppHighlighter(document)
    if language.lower() == "java":
        return JavaHighlighter(document)
    
    # Default to MiniC
    return MiniCHighlighter(document)
