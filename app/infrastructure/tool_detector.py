"""Toolchain detection — reports the versions of every backend tool
(flex, bison, gcc, g++, clang...) so the UI can label artifacts honestly
and warn when a required tool is missing."""

from __future__ import annotations

import shutil
import subprocess
from dataclasses import dataclass, field


@dataclass
class ToolInfo:
    name: str
    path: str | None = None
    version: str | None = None

    @property
    def available(self) -> bool:
        return self.path is not None


@dataclass
class Toolchain:
    tools: dict[str, ToolInfo] = field(default_factory=dict)

    def get(self, name: str) -> ToolInfo | None:
        return self.tools.get(name)

    def available(self, name: str) -> bool:
        info = self.tools.get(name)
        return bool(info and info.available)


def _version(command: list) -> str | None:
    try:
        proc = subprocess.run(
            command, capture_output=True, text=True, timeout=10,
            errors="replace", check=False,
        )
        line = (proc.stdout or proc.stderr).strip().splitlines()
        return line[0].strip() if line else None
    except (OSError, subprocess.TimeoutExpired):
        return None


def detect_toolchain() -> Toolchain:
    tools = Toolchain()
    for name in ("gcc", "g++", "flex", "bison", "make", "clang", "clang++", "python"):
        path = shutil.which(name)
        if not path:
            tools.tools[name] = ToolInfo(name=name)
            continue
        version_flag = "--version"
        tools.tools[name] = ToolInfo(
            name=name, path=path, version=_version([path, version_flag])
        )
    return tools
