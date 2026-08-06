"""Persistent application settings backed by QSettings.

Also acts as a lightweight dependency container: services register
singletons here and resolve them by name, keeping wiring in one place.
"""

from __future__ import annotations

from typing import Any

from PyQt5.QtCore import QSettings


class Settings:
    def __init__(self, organization: str = "CompileOne", application: str = "CompileOne") -> None:
        self._qsettings = QSettings(organization, application)
        self._container: dict[str, Any] = {}

    # ------------------------------------------------------ QSettings access

    def get(self, key: str, default: Any = None) -> Any:
        value = self._qsettings.value(key, default)
        return value

    def set(self, key: str, value: Any) -> None:
        self._qsettings.setValue(key, value)
        self._qsettings.sync()

    # ------------------------------------------------------ dependency container

    def register(self, name: str, instance: Any) -> None:
        self._container[name] = instance

    def resolve(self, name: str) -> Any:
        if name not in self._container:
            raise KeyError(f"service not registered: {name}")
        return self._container[name]
