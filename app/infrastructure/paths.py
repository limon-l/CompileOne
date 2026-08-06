"""Project layout constants.

Kept separate so every other module can resolve the project root
without importing Qt or the backend.
"""

from __future__ import annotations

from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]

BUILD_DIR = PROJECT_ROOT / "Build"
OUTPUT_DIR = PROJECT_ROOT / "Output"
TEMP_DIR = PROJECT_ROOT / "Temp"
CACHE_DIR = TEMP_DIR / "cache"
LOG_DIR = TEMP_DIR / "logs"
EXAMPLES_DIR = PROJECT_ROOT / "examples"

BACKEND_EXE = BUILD_DIR / "compileone.exe"

PHASE_ARTIFACTS = {
    "lex": "token_stream",
    "parse": "parse_tree",
    "ast": "ast",
    "semantic": "semantic",
    "ir": "ir",
    "opt": "optimization",
    "codegen": "assembly",
    "run": "execution",
}
