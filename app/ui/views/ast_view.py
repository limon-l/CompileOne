"""
AST View Widget

A compound widget that visualizes an Abstract Syntax Tree (AST). It
contains a QTreeWidget to show the tree structure and a QTableWidget
to display the attributes of the currently selected node.
"""

from __future__ import annotations

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from app.domain.artifacts import AbstractSyntaxTree, ASTNode


class ASTView(QWidget):
    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)

        # --- Widgets ---
        self._tree = QTreeWidget()
        self._tree.setHeaderLabels(["Node Type", "Details"])
        self._tree.header().setDefaultSectionSize(180)

        self._props_table = QTableWidget()
        self._props_table.setColumnCount(2)
        self._props_table.setHorizontalHeaderLabels(["Attribute", "Value"])
        self._props_table.horizontalHeader().setStretchLastSection(True)
        self._props_table.verticalHeader().setVisible(False)
        self._props_table.setEditTriggers(QTableWidget.NoEditTriggers)

        # --- Layout ---
        splitter = QSplitter(Qt.Horizontal)
        splitter.addWidget(self._tree)
        splitter.addWidget(self._props_table)
        splitter.setStretchFactor(0, 2)
        splitter.setStretchFactor(1, 1)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(splitter)
        self.setLayout(layout)

        # --- Connections ---
        self._tree.itemSelectionChanged.connect(self._on_selection_changed)

    def set_tree(self, tree: AbstractSyntaxTree | None, language: str | None = None) -> None:
        """Populates the tree view from an AbstractSyntaxTree domain model."""
        self._tree.clear()
        self._props_table.setRowCount(0)
        
        if tree and tree.root:
            self._tree.setUpdatesEnabled(False)
            root_item = self._create_tree_item(tree.root)
            self._tree.addTopLevelItem(root_item)
            self._tree.expandToDepth(3)
            self._tree.setUpdatesEnabled(True)
        elif language is not None and language != "mini-c":
            placeholder = QTreeWidgetItem([
                f"AST not available for {language}",
                "Full AST support is only implemented for Mini-C"
            ])
            self._tree.addTopLevelItem(placeholder)
        
        self._tree.resizeColumnToContents(0)
        self._tree.resizeColumnToContents(1)

    def _create_tree_item(self, node: ASTNode) -> QTreeWidgetItem:
        """Recursively creates a QTreeWidgetItem for an ASTNode."""
        details = ""
        if node.token:
            details = f"'{node.token.lexeme}'"
        elif "value" in node.attributes:
            details = str(node.attributes["value"])

        item = QTreeWidgetItem([node.node_type, details])
        item.setData(0, Qt.UserRole, node)
        
        if node.token:
            item.setToolTip(0, f"L{node.token.line}:C{node.token.column}")

        for child_node in node.children:
            child_item = self._create_tree_item(child_node)
            item.addChild(child_item)
            
        return item

    def _on_selection_changed(self) -> None:
        """When a new tree item is selected, show its attributes in the table."""
        selected_items = self._tree.selectedItems()
        if not selected_items:
            self._props_table.setRowCount(0)
            return

        item = selected_items[0]
        node: ASTNode | None = item.data(0, Qt.UserRole)

        if not node:
            self._props_table.setRowCount(0)
            return

        # Combine node and token attributes for inspection
        attrs = {}
        attrs.update(node.attributes)

        if node.token:
            attrs["token"] = node.token.token
            attrs["lexeme"] = f"'{node.token.lexeme}'"
            attrs["line"] = node.token.line
            attrs["column"] = node.token.column
        
        self._props_table.setRowCount(len(attrs))
        for row, (key, value) in enumerate(attrs.items()):
            key_item = QTableWidgetItem(str(key))
            value_item = QTableWidgetItem(str(value))
            self._props_table.setItem(row, 0, key_item)
            self._props_table.setItem(row, 1, value_item)

