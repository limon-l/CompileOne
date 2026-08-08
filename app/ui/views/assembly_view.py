"""
Target Assembly View Widget

Shows the code-generation artifact: the raw assembly listing plus a
summary of the stack frame layout (slot names, offsets and sizes) and
the prologue/epilogue the code generator emitted.
"""

from __future__ import annotations

from PyQt5.QtWidgets import (
    QHeaderView,
    QLabel,
    QPlainTextEdit,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from app.domain.artifacts import AssemblyInfo


class AssemblyView(QWidget):
    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)

        self._summary = QLabel("No assembly generated yet.")
        self._summary.setStyleSheet("padding: 4px; font-weight: bold;")

        self._listing = QPlainTextEdit()
        self._listing.setReadOnly(True)
        self._listing.setLineWrapMode(QPlainTextEdit.NoWrap)
        font = self._listing.font()
        font.setFamily("Consolas")
        self._listing.setFont(font)

        self._stack_table = QTableWidget()
        self._stack_table.setColumnCount(3)
        self._stack_table.setHorizontalHeaderLabels(["Slot", "Offset", "Size"])
        self._stack_table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self._stack_table.verticalHeader().setVisible(False)
        self._stack_table.setEditTriggers(QTableWidget.NoEditTriggers)

        splitter = QSplitter()
        splitter.addWidget(self._listing)
        splitter.addWidget(self._stack_table)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self._summary)
        layout.addWidget(splitter)
        self.setLayout(layout)

    def set_assembly(self, assembly: AssemblyInfo | None, language: str | None = None) -> None:
        self._listing.clear()
        self._stack_table.setRowCount(0)

        if assembly is None:
            self._summary.setText("No assembly generated yet.")
            return

        self._summary.setText(
            f"{assembly.arch} ({assembly.syntax}) — "
            f"{len(assembly.instructions)} instruction(s), "
            f"stack {assembly.stack_size} byte(s)"
        )

        self._listing.setPlainText(assembly.text)

        slots = assembly.slots
        self._stack_table.setRowCount(len(slots))
        for row, slot in enumerate(slots):
            for col, value in enumerate(
                [slot.name, str(slot.offset), str(slot.size)]
            ):
                self._stack_table.setItem(row, col, QTableWidgetItem(value))
