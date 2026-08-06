"""CompileOne main window.

Layout:
  - menu bar + toolbar
  - central splitter: editor (left) | token grid (right)
  - bottom tab widget: Problems | Console | Run Output
  - status bar (mode, language, backend, token count, cursor position)

Shortcuts:
  Ctrl+O open source     Ctrl+S save          Ctrl+Shift+S save as
  Ctrl+B / F5 compile    Ctrl+R / F6 run      Ctrl+E export tokens
  Ctrl++ / Ctrl+- zoom
"""

from __future__ import annotations

from pathlib import Path

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (
    QAction,
    QFileDialog,
    QLabel,
    QMainWindow,
    QMessageBox,
    QSplitter,
    QTabWidget,
    QToolBar,
)

from app.application.compile_session import CompileSession
from app.domain.artifacts import Token
from app.domain.diagnostics import Diagnostic
from app.infrastructure.paths import EXAMPLES_DIR, TEMP_DIR
from app.ui.editor.editor_widget import EditorWidget
from app.ui.panels.console import ConsolePanel
from app.ui.panels.problems import ProblemsPanel
from app.ui.panels.run_output import RunOutputPanel
from app.ui.views.token_view import TokenTableView

DEFAULT_SOURCE = EXAMPLES_DIR / "study" / "hello.mc"
WORKING_COPY = TEMP_DIR / "compileone_source.mc"


class MainWindow(QMainWindow):
    def __init__(self, app, settings) -> None:
        super().__init__()
        self._app = app
        self._settings = settings
        self._orchestrator = settings.resolve("orchestrator")
        self._session = CompileSession(source_path=WORKING_COPY, language="mini-c", mode="study")
        self._current_path: Path | None = None
        self._actions: list[QAction] = []

        self.setWindowTitle("CompileOne")
        self.resize(1280, 800)

        self._build_ui()
        self._build_menu()
        self._build_toolbar()
        self._connect_signals()

        self.editor.cursorMoved.connect(self._on_cursor_moved)

        self._load_file(DEFAULT_SOURCE)

    # ------------------------------------------------------ UI construction

    def _build_ui(self) -> None:
        self.editor = EditorWidget()

        self.token_view = TokenTableView()
        self.problems = ProblemsPanel()
        self.console = ConsolePanel()
        self.run_output = RunOutputPanel()

        top_splitter = QSplitter(Qt.Horizontal)
        top_splitter.addWidget(self.editor)
        top_splitter.addWidget(self.token_view)
        top_splitter.setStretchFactor(0, 3)
        top_splitter.setStretchFactor(1, 2)
        top_splitter.setSizes([760, 520])

        bottom_tabs = QTabWidget()
        bottom_tabs.addTab(self.problems, "Problems")
        bottom_tabs.addTab(self.console, "Console")
        bottom_tabs.addTab(self.run_output, "Run Output")

        main_splitter = QSplitter(Qt.Vertical)
        main_splitter.addWidget(top_splitter)
        main_splitter.addWidget(bottom_tabs)
        main_splitter.setStretchFactor(0, 3)
        main_splitter.setStretchFactor(1, 1)
        main_splitter.setSizes([540, 200])

        self.setCentralWidget(main_splitter)

        # status bar widgets
        self._mode_label = QLabel("Study · mini-c")
        self._backend_label = QLabel()
        self._token_label = QLabel("0 tokens")
        self._cursor_label = QLabel("Ln 1, Col 1")

        self.statusBar().addPermanentWidget(self._mode_label)
        self.statusBar().addPermanentWidget(self._backend_label)
        self.statusBar().addPermanentWidget(self._token_label)
        self.statusBar().addPermanentWidget(self._cursor_label)

        runner = self._settings.resolve("runner")
        if runner.available():
            self._backend_label.setText("backend: OK")
        else:
            self._backend_label.setText("backend: MISSING (run make)")
            self._backend_label.setStyleSheet("color: #f44747;")

    def _build_menu(self) -> None:
        menu = self.menuBar()

        file_menu = menu.addMenu("&File")
        file_menu.addAction(self._action("&Open…", "Ctrl+O", self._open_file))
        file_menu.addAction(self._action("&Save", "Ctrl+S", self._save_file))
        file_menu.addAction(self._action("Save &As…", "Ctrl+Shift+S", self._save_file_as))
        file_menu.addSeparator()
        file_menu.addAction(self._action("E&xit", "Ctrl+Q", self.close))

        compile_menu = menu.addMenu("&Compile")
        compile_menu.addAction(self._action("&Compile", "Ctrl+B", self.compile))
        compile_menu.addAction(self._action("&Run", "Ctrl+R", self.run))
        compile_menu.addSeparator()
        compile_menu.addAction(self._action("&Export Tokens…", "Ctrl+E", self._export_tokens))

        view_menu = menu.addMenu("&View")
        view_menu.addAction(self._action("Zoom &In", "Ctrl+=", self.editor.zoom_in))
        view_menu.addAction(self._action("Zoom &Out", "Ctrl+-", self.editor.zoom_out))

        help_menu = menu.addMenu("&Help")
        help_menu.addAction(self._action("&About CompileOne", None, self._about))

    def _build_toolbar(self) -> None:
        toolbar = QToolBar("Main")
        toolbar.setMovable(False)
        toolbar.setToolButtonStyle(Qt.ToolButtonTextBesideIcon)
        self.addToolBar(toolbar)
        toolbar.addAction(self._action("Compile", "F5", self.compile))
        toolbar.addAction(self._action("Run", "F6", self.run))
        toolbar.addSeparator()
        toolbar.addAction(self._action("Open…", "Ctrl+O", self._open_file))
        toolbar.addAction(self._action("Save", "Ctrl+S", self._save_file))
        toolbar.addAction(self._action("Export CSV", "Ctrl+E", self._export_tokens))

    def _action(self, text: str, shortcut: str | None, slot) -> QAction:
        action = QAction(text, self)
        if shortcut:
            action.setShortcut(shortcut)
        action.triggered.connect(slot)
        self._actions.append(action)
        return action

    def _connect_signals(self) -> None:
        self.token_view.tokenActivated.connect(self._on_token_activated)
        self.problems.diagnosticActivated.connect(self._on_diagnostic_activated)

    # ------------------------------------------------------ file handling

    def _load_file(self, path: Path) -> None:
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as exc:
            QMessageBox.critical(self, "Open failed", f"Cannot read {path}:\n{exc}")
            return
        self._current_path = path
        self.editor.setPlainText(text)
        self._session.source_text = text
        self.setWindowTitle(f"CompileOne — {path.name}")

    def _open_file(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Open source", str(self._current_path or EXAMPLES_DIR),
            "mini-c source (*.mc);;C source (*.c);;All files (*)",
        )
        if path:
            self._load_file(Path(path))

    def _save_file(self) -> None:
        if self._current_path is None:
            self._save_file_as()
            return
        self._current_path.write_text(self.editor.toPlainText(), encoding="utf-8")
        self.statusBar().showMessage(f"Saved {self._current_path}", 3000)

    def _save_file_as(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self, "Save source", "hello.mc", "mini-c source (*.mc);;All files (*)"
        )
        if path:
            self._current_path = Path(path)
            self._save_file()

    # ------------------------------------------------------ compilation

    def compile(self) -> None:
        text = self.editor.toPlainText()
        self._session.source_text = text
        try:
            WORKING_COPY.parent.mkdir(parents=True, exist_ok=True)
            WORKING_COPY.write_text(text, encoding="utf-8")
        except OSError as exc:
            QMessageBox.critical(self, "Compile failed", f"Could not write working copy:\n{exc}")
            return

        self.token_view.clear()
        self.problems.clear()
        self.editor.set_diagnostics([])
        self.statusBar().showMessage("Compiling…")

        try:
            self._orchestrator.compile_all(self._session)
        except Exception as exc:  # noqa: BLE001 — surface unexpected failures
            self.statusBar().showMessage("Compile failed")
            QMessageBox.critical(self, "Compile failed", str(exc))
            return

        self._refresh_views()
        done = sum(1 for r in self._session.phase_results if r.done)
        errors = self._session.error_count()
        self.statusBar().showMessage(
            f"Compiled {self._session.source_path.name}: {done} phase(s), {errors} error(s)",
            5000,
        )

    def run(self) -> None:
        text = self.editor.toPlainText()
        self._session.source_text = text
        try:
            WORKING_COPY.parent.mkdir(parents=True, exist_ok=True)
            WORKING_COPY.write_text(text, encoding="utf-8")
        except OSError as exc:
            QMessageBox.critical(self, "Run failed", f"Could not write working copy:\n{exc}")
            return

        self.run_output.clear()
        self.statusBar().showMessage("Running…")

        try:
            self._orchestrator.execute(self._session)
        except Exception as exc:  # noqa: BLE001 — surface unexpected failures
            self.statusBar().showMessage("Run failed")
            QMessageBox.critical(self, "Run failed", str(exc))
            return

        execution = self._orchestrator.execution_of(self._session)
        self.run_output.show_execution(execution)
        self.problems.set_diagnostics(self._session.diagnostics)
        self.editor.set_diagnostics(self._session.diagnostics)

        if execution is not None:
            self.statusBar().showMessage(
                f"Ran {self._session.source_path.name}: "
                f"{execution.status}, exit {execution.exit_code}, "
                f"{execution.steps} steps",
                5000,
            )

    def _refresh_views(self) -> None:
        stream = self._orchestrator.token_stream_of(self._session)
        if stream is not None:
            self.token_view.set_stream(stream)
            self._token_label.setText(f"{stream.token_count} tokens")
        else:
            self._token_label.setText("0 tokens")

        self.problems.set_diagnostics(self._session.diagnostics)
        self.editor.set_diagnostics(self._session.diagnostics)

    # ------------------------------------------------------ navigation

    def _on_token_activated(self, token: Token) -> None:
        if token is not None:
            self.editor.navigate_to(token.line, token.column)

    def _on_diagnostic_activated(self, diagnostic: Diagnostic) -> None:
        if diagnostic is not None:
            self.editor.navigate_to(diagnostic.line, diagnostic.column)

    def _on_cursor_moved(self, line: int, column: int) -> None:
        self._cursor_label.setText(f"Ln {line}, Col {column}")

    # ------------------------------------------------------ misc actions

    def _export_tokens(self) -> None:
        stream = self._orchestrator.token_stream_of(self._session)
        if stream is None:
            QMessageBox.information(self, "Export", "Compile first to produce tokens.")
            return
        self.token_view.export_csv()

    def _about(self) -> None:
        QMessageBox.about(
            self,
            "About CompileOne",
            "CompileOne\n\nAn educational multi-language compiler IDE.\n\n"
            "Study mode: mini-c, driven by a real Flex + Bison + C backend "
            "that emits JSON artifacts for every compiler phase.",
        )
