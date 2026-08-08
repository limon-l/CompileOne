"""CompileOne main window.

The MainWindow is the application's central controller. It owns all UI
widgets and application-level services like the Orchestrator and runners.
It is responsible for connecting user actions (e.g., clicking 'Run') to
the appropriate backend workflows.
"""

from __future__ import annotations

from pathlib import Path

from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtWidgets import (
    QAction,
    QComboBox,
    QFileDialog,
    QLabel,
    QMainWindow,
    QMessageBox,
    QSplitter,
    QTabWidget,
    QToolBar,
)

from app.application.compile_session import CompileSession
from app.application.demo_code import DEMO_CODE, EXTENSIONS, LANGUAGES
from app.application.orchestrator import Orchestrator
from app.application.pipeline import Pipeline
from app.domain.artifacts import Token
from app.domain.diagnostics import Diagnostic
from app.infrastructure.artifact_store import ArtifactStore
from app.infrastructure.backend_runner import BackendRunner, InteractiveProcessRunner
from app.infrastructure.paths import EXAMPLES_DIR, PROJECT_ROOT, TEMP_DIR
from app.infrastructure.tool_detector import detect_toolchain
from app.services.settings_service import Settings
from app.ui.editor.editor_widget import EditorWidget
from app.ui.panels.console import ConsolePanel
from app.ui.panels.problems import ProblemsPanel
from app.ui.panels.run_output import RunOutputPanel
from app.ui.views.ast_view import ASTView
from app.ui.views.assembly_view import AssemblyView
from app.ui.views.cst_view import CSTView
from app.ui.views.ir_view import IRView
from app.ui.views.optimization_view import OptimizationView
from app.ui.views.semantic_view import SemanticView
from app.ui.views.token_view import TokenTableView
DEFAULT_SOURCE = EXAMPLES_DIR / "study" / "hello.mc"


class MainWindow(QMainWindow):
    def __init__(self, app, settings: Settings) -> None:
        super().__init__()
        self._app = app
        self._actions: list[QAction] = []
        
        # --- Initialize core services ---
        self._toolchain = detect_toolchain(PROJECT_ROOT)
        self._store = ArtifactStore(TEMP_DIR)
        self._pipeline = Pipeline()
        self._backend_runner = BackendRunner(self._toolchain, cwd=PROJECT_ROOT)
        self._orchestrator = Orchestrator(
            pipeline=self._pipeline,
            runner=self._backend_runner,
            store=self._store,
            toolchain=self._toolchain,
        )
        
        self._session = CompileSession(source_path=TEMP_DIR / "compileone_source.mc", language="mini-c", mode="study")
        self._current_path: Path | None = None

        self.setWindowTitle("CompileOne")
        self.resize(1280, 800)

        self._build_ui()
        
        # Must happen after UI construction
        self._interactive_runner = InteractiveProcessRunner(self.run_output, self)

        self._build_menu()
        self._build_toolbar()

        # Debounced live recompile while the user types.
        self._loading = False
        self._auto_compile_timer = QTimer(self)
        self._auto_compile_timer.setSingleShot(True)
        self._auto_compile_timer.setInterval(600)
        self._auto_compile_timer.timeout.connect(self.compile)

        self._connect_signals()

        self._load_example(DEFAULT_SOURCE)

    # ------------------------------------------------------ UI construction

    def _build_ui(self) -> None:
        # --- Left Panel ---
        self.editor = EditorWidget()
        
        self.run_output = RunOutputPanel()
        self.console = ConsolePanel()
        self.problems = ProblemsPanel()

        left_bottom_tabs = QTabWidget()
        left_bottom_tabs.addTab(self.run_output, "Run/Interactive")
        left_bottom_tabs.addTab(self.console, "Compiler Log")
        left_bottom_tabs.addTab(self.problems, "Problems")

        left_splitter = QSplitter(Qt.Vertical)
        left_splitter.addWidget(self.editor)
        left_splitter.addWidget(left_bottom_tabs)
        left_splitter.setStretchFactor(0, 4)
        left_splitter.setStretchFactor(1, 1)

        # --- Right Panel (Compiler Phases) ---
        self.token_view = TokenTableView()
        self.cst_view = CSTView()
        self.ast_view = ASTView()
        self.semantic_view = SemanticView()
        self.ir_view = IRView()
        self.optimization_view = OptimizationView()
        self.assembly_view = AssemblyView()
        
        right_tabs = QTabWidget()
        right_tabs.addTab(self.token_view, "Lexical Analysis")
        right_tabs.addTab(self.cst_view, "Parse Tree (CST)")
        right_tabs.addTab(self.ast_view, "Syntax Tree (AST)")
        right_tabs.addTab(self.semantic_view, "Semantic Analysis")
        right_tabs.addTab(self.ir_view, "IR (TAC)")
        right_tabs.addTab(self.optimization_view, "Optimization")
        right_tabs.addTab(self.assembly_view, "Target Assembly")

        # --- Main Layout ---
        main_splitter = QSplitter(Qt.Horizontal)
        main_splitter.addWidget(left_splitter)
        main_splitter.addWidget(right_tabs)
        main_splitter.setStretchFactor(0, 1)
        main_splitter.setStretchFactor(1, 1)

        self.setCentralWidget(main_splitter)
        self._build_status_bar()

    def _build_status_bar(self) -> None:
        self._mode_label = QLabel()
        self._backend_label = QLabel()
        self._token_label = QLabel("0 tokens")
        self._cursor_label = QLabel("Ln 1, Col 1")

        self.statusBar().addPermanentWidget(self._mode_label)
        self.statusBar().addPermanentWidget(self._backend_label)
        self.statusBar().addPermanentWidget(self._token_label)
        self.statusBar().addPermanentWidget(self._cursor_label)

        if self._backend_runner.available():
            self._backend_label.setText("backend: OK")
        else:
            self._backend_label.setText("backend: MISSING (run make)")
            self._backend_label.setStyleSheet("color: #f44747;")

    def _build_menu(self) -> None:
        # ... (menu construction is unchanged)
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
        # ... (toolbar construction is unchanged)
        toolbar = QToolBar("Main")
        toolbar.setMovable(False)
        self.addToolBar(toolbar)
        toolbar.addAction(self._action("Compile", "F5", self.compile))
        toolbar.addAction(self._action("Run", "F6", self.run))
        toolbar.addSeparator()
        toolbar.addWidget(QLabel("Language:"))
        self._lang_combo = QComboBox()
        for label, language in LANGUAGES.items():
            self._lang_combo.addItem(label, language)
        self._lang_combo.currentIndexChanged.connect(self._on_language_selected)
        toolbar.addWidget(self._lang_combo)

    def _action(self, text: str, shortcut: str | None, slot) -> QAction:
        action = QAction(text, self)
        if shortcut:
            action.setShortcut(shortcut)
        action.triggered.connect(slot)
        self._actions.append(action)
        return action

    def _connect_signals(self) -> None:
        self.editor.cursorMoved.connect(self._on_cursor_moved)
        self.editor.textChanged.connect(self._on_editor_text_changed)
        self.token_view.tokenActivated.connect(self._on_token_activated)
        self.problems.diagnosticActivated.connect(self._on_diagnostic_activated)

    # ------------------------------------------------------ file handling

    def _load_example(self, path: Path) -> None:
        """Load a built-in example through a temp copy.

        Editing the default demo must never modify the example file on disk,
        so the working buffer is a throwaway copy under Temp/.
        """
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as exc:
            QMessageBox.critical(self, "Open failed", f"Cannot read {path}:\n{exc}")
            return
        ext = path.suffix.lower() or ".mc"
        copy = TEMP_DIR / f"example_copy{ext}"
        try:
            copy.write_text(text, encoding="utf-8")
        except OSError as exc:
            QMessageBox.critical(self, "Open failed", f"Cannot stage {path}:\n{exc}")
            return
        self._load_file(copy)

    def _load_file(self, path: Path) -> None:
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as exc:
            QMessageBox.critical(self, "Open failed", f"Cannot read {path}:\n{exc}")
            return
        
        # Update session, language, and UI
        self._current_path = path
        self._session.source_text = text
        self._session.source_path = path
        self._set_language_from_path(path)

        self._loading = True
        self.editor.setPlainText(text)
        self._loading = False
        self.setWindowTitle(f"CompileOne — {path.name}")
        self.compile()

    def _set_language_from_path(self, path: Path) -> None:
        ext = path.suffix.lower()
        lang_map = {".c": "c", ".cpp": "c++", ".java": "java", ".mc": "mini-c"}
        language = lang_map.get(ext, "mini-c")
        
        self._session.language = language
        self.editor.set_language(language)
        self._mode_label.setText(f"Study · {language}")
        self._sync_lang_combo(language)

    def _sync_lang_combo(self, language: str) -> None:
        """Update the toolbar combo without re-triggering its handler."""
        idx = self._lang_combo.findData(language)
        if idx < 0 or idx == self._lang_combo.currentIndex():
            return
        self._lang_combo.blockSignals(True)
        self._lang_combo.setCurrentIndex(idx)
        self._lang_combo.blockSignals(False)

    def _on_language_selected(self, index: int) -> None:
        """User picked a language from the dropdown: swap in its demo code."""
        language = self._lang_combo.itemData(index)
        if language is None or language == self._session.language:
            return
        self._load_demo(language)

    def _load_demo(self, language: str) -> None:
        """Replace the editor with the demo program for `language`."""
        text = DEMO_CODE.get(language, "")
        path = TEMP_DIR / f"demo{EXTENSIONS[language]}"
        try:
            path.write_text(text, encoding="utf-8")
        except OSError:
            pass

        self._current_path = path
        self._session.language = language
        self._session.source_path = path
        self._session.source_text = text
        self.editor.set_language(language)
        self._loading = True
        self.editor.setPlainText(text)
        self._loading = False
        self._mode_label.setText(f"Study · {language}")
        self.setWindowTitle(f"CompileOne — {path.name}")
        self.compile()

    def _open_file(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Open source", str(self._current_path or EXAMPLES_DIR),
            "All supported (*.c *.cpp *.java *.mc);;C source (*.c);;C++ source (*.cpp);;Java source (*.java);;Mini-C source (*.mc)"
        )
        if path:
            self._load_file(Path(path))

    def _save_file(self) -> None:
        if not self._current_path:
            self._save_file_as()
            return
        self._current_path.write_text(self.editor.toPlainText(), encoding="utf-8")
        self.statusBar().showMessage(f"Saved {self._current_path}", 3000)

    def _save_file_as(self) -> None:
        path, _ = QFileDialog.getSaveFileName(self, "Save source", "untitled.c", "All files (*)")
        if path:
            self._current_path = Path(path)
            self._save_file()

    # ------------------------------------------------------ compilation & execution

    def compile(self, silent: bool = False) -> None:
        """Runs the artifact-generation pipeline (lex, parse, etc.).

        Always compiles the *current editor content*: the editor is written
        to a fresh working copy which becomes the session's source.
        `silent` suppresses status-bar/dialog noise (used by auto-recompile).
        """
        if not self._save_working_copy():
            return
        if not silent:
            self.statusBar().showMessage("Compiling…")
        try:
            self._orchestrator.compile_all(self._session)
        except Exception as exc:  # noqa: BLE001 — surface unexpected failures in the UI
            if not silent:
                QMessageBox.critical(self, "Compile failed", str(exc))
            return
        finally:
            self._refresh_views()
        if not silent:
            self.statusBar().showMessage("Ready", 2000)

    def run(self) -> None:
        """Compiles and runs the current file."""
        self._save_working_copy()
        self.run_output.clear()
        self.statusBar().showMessage("Running…")
        
        lang = self._session.language
        if lang == "mini-c":
            self._run_interpreted()
        elif lang == "c":
            self._run_native_c()
        elif lang == "c++":
            self._run_native_cpp()
        else:
            QMessageBox.information(self, "Not Implemented", f"Running '{lang}' programs is not yet supported.")

    def _run_interpreted(self) -> None:
        """Runs the Mini-C interpreter via the orchestrator."""
        try:
            self._orchestrator.execute(self._session)
        except Exception as exc:  # noqa: BLE001 — surface unexpected failures in the UI
            QMessageBox.critical(self, "Run failed", str(exc))
            return
        
        execution = self._orchestrator.execution_of(self._session)
        if execution:
            self.run_output.show_final_status(execution)
            for line in execution.output:
                self.run_output.append_output(line + '\n')
        self._refresh_views()

    def _run_native_c(self) -> None:
        """Compiles and runs a C source file interactively."""
        # Step 1: Compile the executable
        executable_path, errors = self._orchestrator.compile_c_source(self._session)
        
        if errors:
            self.run_output.append_output(errors)
            self.run_output.set_input_enabled(False)
            self.statusBar().showMessage("Compilation failed.", 5000)
            return
        
        if not executable_path:
            # This case should not happen if errors is None, but as a safeguard:
            QMessageBox.critical(self, "Run failed", "Compilation produced no executable.")
            return

        # Step 2: Run the executable interactively
        self.statusBar().showMessage("Executing…")
        self._interactive_runner.start(
            program=str(executable_path),
            args=[],
            cwd=str(self._store.workdir())
        )

    def _run_native_cpp(self) -> None:
        """Compiles and runs a C++ source file interactively."""
        # Step 1: Compile the executable
        executable_path, errors = self._orchestrator.compile_cpp_source(self._session)
        
        if errors:
            self.run_output.append_output(errors)
            self.run_output.set_input_enabled(False)
            self.statusBar().showMessage("Compilation failed.", 5000)
            return
        
        if not executable_path:
            QMessageBox.critical(self, "Run failed", "Compilation produced no executable.")
            return

        # Step 2: Run the executable interactively
        self.statusBar().showMessage("Executing…")
        self._interactive_runner.start(
            program=str(executable_path),
            args=[],
            cwd=str(self._store.workdir())
        )

    def _save_working_copy(self) -> bool:
        """Saves the editor content to a temporary file for the backend.

        The session's source path is pointed at this working copy so every
        phase (lex, parse, ast, semantic, run) consumes the *current editor
        content* rather than the demo/opened file on disk.
        """
        text = self.editor.toPlainText()
        self._session.source_text = text
        path = self._working_copy_path()
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        except OSError as exc:
            QMessageBox.critical(self, "Error", f"Could not write working copy:\n{exc}")
            return False
        self._session.source_path = path
        return True

    def _working_copy_path(self) -> Path:
        ext = EXTENSIONS.get(self._session.language, ".mc")
        return TEMP_DIR / f"compileone_source{ext}"

    def _refresh_views(self) -> None:
        # Update token stream
        stream = self._orchestrator.token_stream_of(self._session)
        self.token_view.set_stream(stream)
        self._token_label.setText(f"{stream.token_count if stream else 0} tokens")
        
        # Update CST view
        cst = self._orchestrator.cst_of(self._session)
        self.cst_view.set_tree(cst)

        # Update AST view
        ast = self._orchestrator.ast_of(self._session)
        self.ast_view.set_tree(ast)

        # Update semantic view
        semantic = self._orchestrator.semantic_of(self._session)
        self.semantic_view.set_semantic(semantic)

        # Update IR / optimization / assembly views
        self.ir_view.set_ir(self._orchestrator.ir_of(self._session))
        self.optimization_view.set_optimization(
            self._orchestrator.optimization_of(self._session)
        )
        self.assembly_view.set_assembly(self._orchestrator.assembly_of(self._session))

        # Update problems view
        self.problems.set_diagnostics(self._session.diagnostics)
        self.editor.set_diagnostics(self._session.diagnostics)

    # ------------------------------------------------------ navigation & other actions

    def _on_cursor_moved(self, line: int, column: int) -> None:
        self._cursor_label.setText(f"Ln {line}, Col {column}")

    def _on_editor_text_changed(self) -> None:
        """Debounced live recompile so lexical/parse/AST/semantic views follow edits."""
        if self._loading or not self._backend_runner.available():
            return
        self._auto_compile_timer.start()
        
    def _on_token_activated(self, token: Token) -> None:
        if token:
            self.editor.navigate_to(token.line, token.column)

    def _on_diagnostic_activated(self, diagnostic: Diagnostic) -> None:
        if diagnostic:
            self.editor.navigate_to(diagnostic.line, diagnostic.column)

    def _export_tokens(self) -> None:
        path, _ = QFileDialog.getSaveFileName(self, "Export Tokens", "tokens.csv", "CSV files (*.csv)")
        if not path:
            return
        try:
            self._orchestrator.export_tokens_csv(self._session, Path(path))
            self.statusBar().showMessage(f"Exported tokens to {path}", 3000)
        except Exception as exc:  # noqa: BLE001 — surface unexpected failures in the UI
            QMessageBox.critical(self, "Export failed", str(exc))

    def _about(self) -> None:
        QMessageBox.about(self, "About", "CompileOne - A Multi-Language Compiler IDE")

