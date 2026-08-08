"""
IR View Widget

Shows the intermediate-representation artifact: the three-address-code
(TAC) instruction listing plus the temporaries and labels the backend
allocated for the program.
"""

from __future__ import annotations

from PyQt5.QtWidgets import (
    QHeaderView,
    QLabel,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from app.domain.artifacts import IrInfo


class IRView(QWidget):
    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)

        self._summary = QLabel("No IR generated yet.")
        self._summary.setStyleSheet("padding: 4px; font-weight: bold;")

        self._table = QTableWidget()
        self._table.setColumnCount(5)
        self._table.setHorizontalHeaderLabels(
            ["Index", "Op", "Arg 1", "Arg 2", "Result"]
        )
        self._table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self._table.verticalHeader().setVisible(False)
        self._table.setEditTriggers(QTableWidget.NoEditTriggers)
        self._table.setAlternatingRowColors(True)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self._summary)
        layout.addWidget(self._table)
        self.setLayout(layout)

    def set_ir(self, ir: IrInfo | None, language: str | None = None) -> None:
        self._table.setRowCount(0)

        if ir is None:
            self._summary.setText("No IR generated yet.")
            return

        self._summary.setText(
            f"TAC: {len(ir.tac)} instruction(s), "
            f"{len(ir.temporaries)} temporary(s), {len(ir.labels)} label(s)"
        )

        self._table.setRowCount(len(ir.tac))
        for row, instruction in enumerate(ir.tac):
            values = [
                str(instruction.index),
                instruction.op,
                instruction.arg1,
                instruction.arg2,
                instruction.result,
            ]
            for col, value in enumerate(values):
                self._table.setItem(row, col, QTableWidgetItem(value))

        self._table.resizeColumnsToContents()
