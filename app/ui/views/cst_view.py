"""
CST View Widget

A panel that visualizes a Concrete Syntax Tree (ParseTree). When the
parse artifact carries syntax errors (e.g. a missing semicolon), an error
banner is shown above the tree and the rule/token nodes located at the
reported line are highlighted in red.
"""

from __future__ import annotations

from PyQt5.QtCore import Qt
from PyQt5.QtGui import QBrush, QColor
from PyQt5.QtWidgets import QLabel, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget

from app.domain.artifacts import ParseTree, ParseTreeNode

ERROR_COLOR = "#f44747"


class CSTView(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)

        self._banner = QLabel()
        self._banner.setWordWrap(True)
        self._banner.setVisible(False)
        self._banner.setStyleSheet(
            "background:#3d1d1d; color:#f44747; padding:4px 6px;"
            "border:1px solid #f44747; border-radius:3px;"
        )

        self._tree = QTreeWidget()
        self._tree.setHeaderLabels(["Node", "Details"])
        self._tree.header().setDefaultSectionSize(200)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(4)
        layout.addWidget(self._banner)
        layout.addWidget(self._tree, 1)
        self.setLayout(layout)

    def set_tree(self, tree: ParseTree | None, language: str | None = None) -> None:
        """Populates the tree view from a ParseTree domain model and shows
        any syntax errors the artifact carried."""
        self._tree.clear()
        self._set_errors(tree.errors if tree else [])
        if tree and tree.root:
            self._tree.setUpdatesEnabled(False)  # Optimize for large trees
            root_item = self._create_tree_item(tree.root)
            self._tree.addTopLevelItem(root_item)
            self._tree.expandToDepth(2)  # Expand the first few levels by default
            self._tree.setUpdatesEnabled(True)
        self._tree.resizeColumnToContents(0)
        self._tree.resizeColumnToContents(1)
        self._mark_error_nodes(tree.errors if tree else [])

    def _set_errors(self, errors) -> None:
        if not errors:
            self._banner.clear()
            self._banner.setVisible(False)
            return
        lines = []
        for err in errors:
            if isinstance(err, dict):
                loc = f"L{err.get('line', '?')}"
                if err.get("column") is not None:
                    loc += f":{err['column']}"
                message = err.get("message", "syntax error")
                lines.append(f"{loc} — {message}".strip())
            else:
                lines.append(str(err))
        self._banner.setText("\n".join(lines))
        self._banner.setVisible(True)

    def _mark_error_nodes(self, errors) -> None:
        """Highlight tree nodes whose token matches a reported error location."""
        locs = [
            (err.get("line"), err.get("column"))
            for err in errors
            if isinstance(err, dict) and err.get("line") is not None
        ]
        if not locs:
            return
        brush = QBrush(QColor(ERROR_COLOR))

        def visit(item: QTreeWidgetItem) -> None:
            token = item.data(0, Qt.UserRole)
            if token is not None and getattr(token, "line", None) is not None:
                for line, col in locs:
                    if token.line == line and (col is None or token.column == col):
                        item.setForeground(0, brush)
                        item.setForeground(1, brush)
                        break
            for i in range(item.childCount()):
                visit(item.child(i))

        for i in range(self._tree.topLevelItemCount()):
            visit(self._tree.topLevelItem(i))

    def _create_tree_item(self, node: ParseTreeNode) -> QTreeWidgetItem:
        """Recursively creates a QTreeWidgetItem for a ParseTreeNode."""
        if node.is_terminal:
            # This is a leaf node representing a token
            token = node.token
            item = QTreeWidgetItem([f"TOKEN: {token.token}", f"'{token.lexeme}'"])
            item.setToolTip(0, f"L{token.line}:C{token.column} | {token.category}")
            item.setData(0, Qt.UserRole, token)  # Store the token object
        else:
            # This is a non-terminal node representing a grammar rule
            item = QTreeWidgetItem([f"RULE: {node.rule_name}", ""])
            item.setToolTip(0, f"Grammar Rule: {node.rule_name}")
            item.setData(0, Qt.UserRole, node)  # Store the node object

            # Recursively add children
            for child_node in node.children:
                child_item = self._create_tree_item(child_node)
                item.addChild(child_item)

        return item
