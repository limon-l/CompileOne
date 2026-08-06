"""Workspace management: open files, save, language/mode detection."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from app.infrastructure.paths import TEMP_DIR


@dataclass
class OpenFile:
    path: Path
    display_name: str
    language: str
    mode: str
    modified: bool = False


def detect_language(path: Path) -> str:
    ext = path.suffix.lower()
    if ext == ".mc":
        return "mini-c"
    if ext == ".c":
        return "c"
    if ext in (".cpp", ".cc", ".cxx", ".hpp"):
        return "cpp"
    return "mini-c"


def detect_mode(language: str) -> str:
    return "study" if language == "mini-c" else "real"


class Workspace:
    def __init__(self, temp_dir: Path | None = None) -> None:
        self._temp_dir = temp_dir or TEMP_DIR
        self.open_files: list[OpenFile] = []
        self.current: OpenFile | None = None

    def open(self, path: Path) -> OpenFile:
        info = OpenFile(
            path=path,
            display_name=path.name,
            language=detect_language(path),
            mode=detect_mode(detect_language(path)),
        )
        if path not in (f.path for f in self.open_files):
            self.open_files.append(info)
        self.current = info
        return info

    def open_text(self, text: str, name: str = "untitled.mc") -> OpenFile:
        """Create a temp-backed file from unsaved editor content."""
        self._temp_dir.mkdir(parents=True, exist_ok=True)
        path = self._temp_dir / name
        path.write_text(text, encoding="utf-8")
        return self.open(path)

    def read(self, info: OpenFile) -> str:
        return info.path.read_text(encoding="utf-8")

    def save(self, info: OpenFile, text: str) -> None:
        info.path.write_text(text, encoding="utf-8")
        info.modified = False

    def close(self, info: OpenFile) -> None:
        if info in self.open_files:
            self.open_files.remove(info)
        if self.current is info:
            self.current = self.open_files[-1] if self.open_files else None
