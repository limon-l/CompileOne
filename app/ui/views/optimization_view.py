"""
Optimization View Widget

Shows the optimization artifact: the instruction-count reduction
metrics followed by per-pass evidence (before/after instruction
listings and the removed instructions).
"""

from __future__ import annotations

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (
    QLabel,
    QSplitter,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from app.domain.artifacts import OptInfo, OptPass


class OptimizationView(QWidget):
    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)

        self._summary = QLabel("No optimization applied yet.")
        self._summary.setStyleSheet("padding: 4px; font-weight: bold;")
        self._summary.setWordWrap(True)

        self._pass_tree = QTreeWidget()
        self._pass_tree.setHeaderLabels(["Pass", "Status", "Detail"])
        self._pass_tree.setColumnWidth(0, 160)
        self._pass_tree.setColumnWidth(1, 90)

        splitter = QSplitter(Qt.Vertical)
        splitter.addWidget(self._summary)
        splitter.addWidget(self._pass_tree)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(splitter)
        self.setLayout(layout)

    def set_optimization(self, opt: OptInfo | None, language: str | None = None) -> None:
        self._pass_tree.clear()

        if opt is None or (
            language is not None and language != "mini-c"
            and opt.generated_by.endswith("native placeholder")
        ):
            if language is not None and language != "mini-c":
                self._summary.setText(
                    f"Optimization results are not available for {language}. "
                    "Full optimization support is only available for Mini-C."
                )
            else:
                self._summary.setText("No optimization applied yet.")
            return

        self._summary.setText(
            f"Instructions: {opt.before_instruction_count} → "
            f"{opt.after_instruction_count} "
            f"({opt.instruction_reduction_pct:.1f}% fewer) — "
            f"{len(opt.passes)} pass(es)"
        )

        for opt_pass in opt.passes:
            item = self._pass_item(opt_pass)
            self._pass_tree.addTopLevelItem(item)
            item.setExpanded(opt_pass.applied)

        self._pass_tree.resizeColumnToContents(2)

    def _pass_item(self, opt_pass: OptPass) -> QTreeWidgetItem:
        status = "applied" if opt_pass.applied else "skipped"
        detail = opt_pass.explanation or (
            f"{opt_pass.instruction_reduction} instruction(s) removed"
        )
        item = QTreeWidgetItem([opt_pass.name, status, detail])

        if opt_pass.removed_instructions:
            removed = QTreeWidgetItem(
                ["Removed instructions", "",
                 f"{len(opt_pass.removed_instructions)} instruction(s)"]
            )
            for instruction in opt_pass.removed_instructions:
                removed.addChild(QTreeWidgetItem(["", "", instruction.text]))
            item.addChild(removed)

        if opt_pass.before:
            before = QTreeWidgetItem(
                ["Before", "", f"{len(opt_pass.before)} instruction(s)"]
            )
            for line in opt_pass.before:
                before.addChild(QTreeWidgetItem(["", "", line]))
            item.addChild(before)

        if opt_pass.after:
            after = QTreeWidgetItem(
                ["After", "", f"{len(opt_pass.after)} instruction(s)"]
            )
            for line in opt_pass.after:
                after.addChild(QTreeWidgetItem(["", "", line]))
            item.addChild(after)

        return item
