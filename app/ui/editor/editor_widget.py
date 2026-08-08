"""Code editor widget.

Built on QPlainTextEdit with:
  - a line-number gutter
  - multi-language syntax highlighting via a factory
  - zoom (Ctrl+= / Ctrl+-)
  - diagnostic squiggles rendered as extra selections
  - a cursor-position signal for the status bar
"""

from __future__ import annotations

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtGui import QColor, QFont, QPainter, QSyntaxHighlighter, QTextCharFormat, QTextCursor
from PyQt5.QtWidgets import QPlainTextEdit, QTextEdit, QWidget

from app.domain import lexicon
from app.domain.diagnostics import Diagnostic, Severity
from app.ui.editor.syntax import create_highlighter


class _LineNumberArea(QWidget):
    def __init__(self, editor: EditorWidget) -> None:
        super().__init__(editor)
        self._editor = editor

    def sizeHint(self):
        return self._editor._line_number_area_size_hint()

    def paintEvent(self, event) -> None:
        self._editor._paint_line_numbers(event)


class EditorWidget(QPlainTextEdit):
    cursorMoved = pyqtSignal(int, int)
    zoomChanged = pyqtSignal(float)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._highlighter: QSyntaxHighlighter | None = None

        self._base_point_size = 11
        font = QFont()
        for family in ("JetBrains Mono", "Consolas", "Courier New"):
            font = QFont(family, self._base_point_size)
            if font.family() == family:
                break
        font.setFixedPitch(True)
        self.setFont(font)
        self.setTabStopDistance(4 * self.fontMetrics().horizontalAdvance(" "))

        self.setLineWrapMode(QPlainTextEdit.NoWrap)
        self.setFrameShape(QTextEdit.NoFrame)

        self.set_language("mini-c")  # Set default language
        self._line_number_area = _LineNumberArea(self)

        self.blockCountChanged.connect(self._update_line_number_area_width)
        self.updateRequest.connect(self._update_line_number_area)
        self.cursorPositionChanged.connect(self._emit_cursor_position)

        self._update_line_number_area_width(0)
        self._emit_cursor_position()

    def set_language(self, language: str) -> None:
        """Sets the syntax highlighter for the chosen language."""
        self._highlighter = create_highlighter(language, self.document())
        self._highlighter.rehighlight()

    # ------------------------------------------------------ line numbers

    def _line_number_area_size_hint(self) -> int:
        digits = max(2, len(str(max(1, self.blockCount()))))
        return 10 + self.fontMetrics().horizontalAdvance("9") * digits

    def _update_line_number_area_width(self, _count: int) -> None:
        self.setViewportMargins(self._line_number_area_size_hint(), 0, 0, 0)

    def _update_line_number_area(self, rect, dy: int) -> None:
        if dy:
            self._line_number_area.scroll(0, dy)
        else:
            self._line_number_area.update(0, rect.y(), self._line_number_area.width(), rect.height())
        if rect.contains(self.viewport().rect()):
            self._update_line_number_area_width(0)

    def resizeEvent(self, event) -> None:
        super().resizeEvent(event)
        cr = self.contentsRect()
        self._line_number_area.setGeometry(
            cr.left(), cr.top(), self._line_number_area_size_hint(), cr.height()
        )

    def _paint_line_numbers(self, event) -> None:
        painter = QPainter(self._line_number_area)
        painter.fillRect(event.rect(), QColor("#2d2d2d"))

        block = self.firstVisibleBlock()
        block_number = block.blockNumber()
        top = round(self.blockBoundingGeometry(block).translated(self.contentOffset()).top())
        bottom = top + round(self.blockBoundingRect(block).height())

        active_color = QColor("#c6c6c6")
        inactive_color = QColor("#858585")
        current_block = self.textCursor().blockNumber()

        while block.isValid() and top <= event.rect().bottom():
            if block.isVisible() and bottom >= event.rect().top():
                is_current = block_number == current_block
                painter.setPen(active_color if is_current else inactive_color)
                painter.drawText(
                    0, top, self._line_number_area.width() - 6,
                    self.fontMetrics().height(), Qt.AlignRight, str(block_number + 1),
                )
            block = block.next()
            top = bottom
            bottom = top + round(self.blockBoundingRect(block).height())
            block_number += 1

    # ------------------------------------------------------ zoom

    def wheelEvent(self, event) -> None:
        if event.modifiers() & Qt.ControlModifier:
            delta = event.angleDelta().y()
            if delta > 0:
                self.zoom_in()
            elif delta < 0:
                self.zoom_out()
            event.accept()
            return
        super().wheelEvent(event)

    def keyPressEvent(self, event) -> None:
        if event.key() in (Qt.Key_Plus, Qt.Key_Equal) and event.modifiers() & Qt.ControlModifier:
            self.zoom_in()
            return
        if event.key() == Qt.Key_Minus and event.modifiers() & Qt.ControlModifier:
            self.zoom_out()
            return
        super().keyPressEvent(event)

    def zoom_in(self) -> None:
        self._base_point_size = min(28, self._base_point_size + 1)
        font = self.font()
        font.setPointSize(self._base_point_size)
        self.setFont(font)
        self.zoomChanged.emit(self._base_point_size / 11.0)

    def zoom_out(self) -> None:
        self._base_point_size = max(8, self._base_point_size - 1)
        font = self.font()
        font.setPointSize(self._base_point_size)
        self.setFont(font)
        self.zoomChanged.emit(self._base_point_size / 11.0)

    # ------------------------------------------------------ diagnostics

    def set_diagnostics(self, diagnostics: list[Diagnostic]) -> None:
        """Render error/warning squiggles under the affected source lines."""
        selections: list[QTextEdit.ExtraSelection] = []

        for diag in diagnostics:
            if diag.severity not in (Severity.ERROR, Severity.WARNING):
                continue
            cursor = self._cursor_for_diagnostic(diag)
            if cursor.isNull():
                continue
            
            fmt = QTextCharFormat()
            if diag.severity == Severity.ERROR:
                color = QColor(lexicon.COLOR_ERROR)
                fmt.setBackground(QColor(color.red(), color.green(), color.blue(), 40))
            else:
                color = QColor("#ffcc00")
                fmt.setBackground(QColor(color.red(), color.green(), color.blue(), 25))

            fmt.setUnderlineColor(color)
            fmt.setUnderlineStyle(QTextCharFormat.SpellCheckUnderline)
            
            selection = QTextEdit.ExtraSelection()
            selection.cursor = cursor
            selection.format = fmt
            selections.append(selection)

        self.setExtraSelections(selections)

    def _cursor_for_diagnostic(self, diag: Diagnostic) -> QTextCursor:
        end_line = max(diag.end_line, diag.line)
        end_column = diag.end_column if diag.end_column > diag.column else diag.column + 1
        start = self._position_of(diag.line, diag.column)
        end = self._position_of(end_line, end_column)
        if start is None or end is None:
            return QTextCursor()
        cursor = QTextCursor(self.document())
        cursor.setPosition(start)
        cursor.setPosition(end, QTextCursor.KeepAnchor)
        return cursor

    def _position_of(self, line: int, column: int) -> int | None:
        block = self.document().findBlockByNumber(max(0, line - 1))
        if not block.isValid():
            return None
        return block.position() + max(0, column - 1)

    def navigate_to(self, line: int, column: int) -> None:
        """Move the cursor to a 1-based line/column and centre it."""
        block = self.document().findBlockByNumber(max(0, line - 1))
        if not block.isValid():
            return
        cursor = QTextCursor(self.document())
        cursor.setPosition(block.position() + max(0, column - 1))
        self.setTextCursor(cursor)
        self.ensureCursorVisible()
        self.setFocus()

    # ------------------------------------------------------ cursor signal

    def _emit_cursor_position(self) -> None:
        cursor = self.textCursor()
        self.cursorMoved.emit(cursor.blockNumber() + 1, cursor.columnNumber() + 1)
