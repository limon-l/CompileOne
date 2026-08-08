"""Toolchain detection — reports the versions of every backend tool
(flex, bison, gcc, g++, clang...) so the UI can label artifacts honestly
and warn when a required tool is missing."""

from __future__ import annotations

import shutil
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

# Singleton instance to cache the detected toolchain
_toolchain_instance: Toolchain | None = None


@dataclass
class ToolInfo:
    """Represents a single detected tool."""
    name: str
    path: str | None = None
    version: str | None = None

    @property
    def available(self) -> bool:
        """Returns True if the tool was found."""
        return self.path is not None


@dataclass
class Toolchain:
    """Represents the collection of all detected tools."""
    tools: dict[str, ToolInfo] = field(default_factory=dict)
    project_root: Path | None = None

    def get(self, name: str) -> ToolInfo | None:
        """Gets info for a tool by name."""
        return self.tools.get(name)

    def available(self, name: str) -> bool:
        """Checks if a specific tool is available."""
        info = self.tools.get(name)
        return bool(info and info.available)


def _get_version(command: list[str]) -> str | None:
    """Runs a command with a version flag to get its version string."""
    try:
        # Many tools (like javac) print version to stderr, so we capture both.
        proc = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=5,
            errors="replace",
            check=False,
        )
        # Combine stdout and stderr, take the first non-empty line
        output = proc.stdout.strip() or proc.stderr.strip()
        lines = output.splitlines()
        return lines[0].strip() if lines else None
    except (OSError, subprocess.TimeoutExpired):
        return None


def _find_system_tool(name: str, version_flag: str = "--version") -> ToolInfo:
    """Finds a tool using shutil.which and gets its version."""
    path = shutil.which(name)
    if not path:
        return ToolInfo(name=name)
    
    version = _get_version([path, version_flag])
    return ToolInfo(name=name, path=path, version=version)


def _find_compileone_backend(project_root: Path) -> ToolInfo:
    """
    Finds the compileone backend executable.
    
    Searches common build directories before falling back to the system PATH.
    This allows a project-local build to be preferred over a global install.
    """
    name = "compileone"
    # Windows executable name
    exe_name = f"{name}.exe"
    
    # Priority search paths for the backend executable
    search_paths = [
        project_root / "build" / "Release",
        project_root / "build" / "Debug",
        project_root / "build",
        project_root / "Build",
        project_root / "output",
        project_root / "Output",
        project_root / "bin",
    ]

    for directory in search_paths:
        if (exe_path := directory / exe_name).is_file():
            return ToolInfo(name=name, path=str(exe_path), version="local build")

    # If not found in project dirs, fall back to system-wide search
    return _find_system_tool(name)


def detect_toolchain(project_root: Path | str, force_redetect: bool = False) -> Toolchain:
    """
    Detects all required tools for the compiler pipeline.

    Caches the result in a singleton for efficiency. To re-run detection,
    set force_redetect=True.
    """
    global _toolchain_instance
    if _toolchain_instance and not force_redetect:
        return _toolchain_instance

    root_path = Path(project_root)
    tools = {"compileone": _find_compileone_backend(root_path)}

    # Tools with a standard '--version' flag
    for name in ("gcc", "g++", "make", "clang", "clang++", "python"):
        tools[name] = _find_system_tool(name)

    # Tools with a non-standard '-version' flag
    for name in ("java", "javac", "flex", "bison"):
        tools[name] = _find_system_tool(name, version_flag="-version")
    
    _toolchain_instance = Toolchain(tools=tools, project_root=root_path)
    return _toolchain_instance
