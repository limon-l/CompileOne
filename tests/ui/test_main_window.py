"""MainWindow regression tests: the editor content must drive every phase.

The historic bug: loading a demo/example set ``session.source_path`` to the
demo file on disk, while ``compile()`` wrote the editor content to a separate
working copy. The pipeline then recompiled the stale demo file, so lexical,
parse, AST and semantic views never reflected edits.
"""

from __future__ import annotations

from pathlib import Path


def _build_window(qapp, monkeypatch):
    from PyQt5.QtWidgets import QMessageBox

    monkeypatch.setattr(QMessageBox, "critical", lambda *a, **k: None)

    from app.services.settings_service import Settings
    from app.ui.main_window import MainWindow

    return MainWindow(qapp, Settings())


def test_save_working_copy_points_session_at_editor_content(qapp, monkeypatch, tmp_path):
    w = _build_window(qapp, monkeypatch)

    w._load_demo("mini-c")
    w.editor.setPlainText("int a = 1;\nprint a;\n")
    assert w._save_working_copy()

    # The session must now compile THIS file (the editor content).
    assert w._session.source_path == w._working_copy_path()
    assert w._session.source_text == "int a = 1;\nprint a;\n"
    assert w._session.source_path.read_text(encoding="utf-8") == "int a = 1;\nprint a;\n"


def test_working_copy_path_matches_language(qapp, monkeypatch):
    w = _build_window(qapp, monkeypatch)

    w._session.language = "mini-c"
    assert w._working_copy_path().name.endswith(".mc")
    w._session.language = "c"
    assert w._working_copy_path().name.endswith(".c")
    w._session.language = "c++"
    assert w._working_copy_path().name.endswith(".cpp")


def test_demo_staged_under_temp_not_examples(qapp, monkeypatch):
    w = _build_window(qapp, monkeypatch)

    w._load_demo("mini-c")
    assert "Temp" in str(w._current_path)
    assert "examples" not in str(w._current_path)


def test_loading_example_stages_a_temp_copy(qapp, monkeypatch, tmp_path):
    w = _build_window(qapp, monkeypatch)

    # Create a throwaway "example" and load it via _load_example.
    example = tmp_path / "sample.mc"
    example.write_text("print 1;\n", encoding="utf-8")
    w._load_example(example)

    # The example file on disk must remain untouched (it is a built-in demo).
    assert example.read_text(encoding="utf-8") == "print 1;\n"
    assert "Temp" in str(w._current_path)
