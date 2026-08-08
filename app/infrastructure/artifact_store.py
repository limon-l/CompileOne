"""Persistence and caching of pipeline artifacts.

Artifacts are stored under Output/ (one JSON file per phase) and can be
replayed or inspected at any time. An incremental cache under
Temp/cache/ keys artifacts by (source hash, phase) so recompiling
unchanged source skips unchanged phases.
"""

from __future__ import annotations

import hashlib
import json
import logging
from pathlib import Path
from typing import Any

from app.infrastructure.paths import CACHE_DIR, OUTPUT_DIR

logger = logging.getLogger("compileone.store")


class ArtifactStore:
    def __init__(self, output_dir: Path | None = None, cache_dir: Path | None = None) -> None:
        self.output_dir = output_dir or OUTPUT_DIR
        self.cache_dir = cache_dir or CACHE_DIR
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.cache_dir.mkdir(parents=True, exist_ok=True)

    # ------------------------------------------------------ paths

    def artifact_path(self, artifact_id: str) -> Path:
        """Path of the named artifact (e.g. 'token_stream' -> Output/token_stream.json)."""
        return self.output_dir / f"{artifact_id}.json"

    def workdir(self) -> Path:
        """Directory used to stage native executables and run them."""
        return self.output_dir

    # ------------------------------------------------------ persistence

    def save(self, artifact_id: str, data: dict[str, Any]) -> Path:
        path = self.artifact_path(artifact_id)
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(data, fh, ensure_ascii=False, indent=2)
        logger.debug("artifact saved: %s", path)
        return path

    def load(self, artifact_id: str) -> dict[str, Any]:
        with open(self.artifact_path(artifact_id), "r", encoding="utf-8") as fh:
            return json.load(fh)

    def artifact_exists(self, artifact_id: str) -> bool:
        return self.artifact_path(artifact_id).is_file()

    # ------------------------------------------------------ incremental cache

    @staticmethod
    def source_hash(source_text: str) -> str:
        return hashlib.sha256(source_text.encode("utf-8")).hexdigest()[:16]

    def cache_key(self, source_hash: str, phase: str) -> str:
        return f"{source_hash}-{phase}"

    def cached_artifact(self, source_hash: str, phase: str) -> dict[str, Any] | None:
        path = self.cache_dir / f"{self.cache_key(source_hash, phase)}.json"
        if path.is_file():
            try:
                with open(path, "r", encoding="utf-8") as fh:
                    return json.load(fh)
            except (OSError, json.JSONDecodeError):
                logger.warning("corrupt cache entry, ignoring: %s", path)
        return None

    def store_cached_artifact(self, source_hash: str, phase: str, data: dict[str, Any]) -> Path:
        path = self.cache_dir / f"{self.cache_key(source_hash, phase)}.json"
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(data, fh, ensure_ascii=False, indent=2)
        return path
