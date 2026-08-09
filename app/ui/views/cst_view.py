"""
CST View Widget

A QTreeWidget that visualizes a Concrete Syntax Tree (ParseTree).
"""

from __future__ import annotations

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import QTreeWidget, QTreeWidgetItem

from app.domain.artifacts import ParseTree, ParseTreeNode


class CSTView(QTreeWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setHeaderLabels(["Node", "Details"])
        self.header().setDefaultSectionSize(200)

    def set_tree(self, tree: ParseTree | None, language: str | None = None) -> None:
        """Populates the tree view from a ParseTree domain model."""
        self.clear()
        if tree and tree.root:
            self.setUpdatesEnabled(False)  # Optimize for large trees
            root_item = self._create_tree_item(tree.root)
            self.addTopLevelItem(root_item)
            self.expandToDepth(2) # Expand the first few levels by default
            self.setUpdatesEnabled(True)
        self.resizeColumnToContents(0)
        self.resizeColumnToContents(1)

    def _create_tree_item(self, node: ParseTreeNode) -> QTreeWidgetItem:
        """Recursively creates a QTreeWidgetItem for a ParseTreeNode."""
        if node.is_terminal:
            # This is a leaf node representing a token
            token = node.token
            item = QTreeWidgetItem([f"TOKEN: {token.token}", f"'{token.lexeme}'"])
            item.setToolTip(0, f"L{token.line}:C{token.column} | {token.category}")
            item.setData(0, Qt.UserRole, token) # Store the token object
        else:
            # This is a non-terminal node representing a grammar rule
            item = QTreeWidgetItem([f"RULE: {node.rule_name}", ""])
            item.setToolTip(0, f"Grammar Rule: {node.rule_name}")
            item.setData(0, Qt.UserRole, node) # Store the node object

            # Recursively add children
            for child_node in node.children:
                child_item = self._create_tree_item(child_node)
                item.addChild(child_item)
        
        return item
