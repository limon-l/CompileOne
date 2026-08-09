#!/usr/bin/env python3
"""CompileOne — environment / dependency setup helper.

Used by ``make bold-build``. It deliberately does the *least* amount of
work needed: it checks what is present and only installs the genuinely
missing pieces. It never blindly reinstalls existing dependencies.

Subcommands
-----------
check
    Verify the native build tools (make, gcc, flex, python). On platforms
    where we cannot install them safely (e.g. Windows without a package
    manager) it prints the exact install instruction and exits non-zero so
    the build stops cleanly instead of failing half-way.

venv
    Ensure a Python virtual environment (``.venv``) exists and install the
    Python packages the project actually imports (pytest, PyQt5). ruff is
    installed too because it is configured in ``pyproject.toml`` but is
    treated as optional (a warning, not a failure).

The script only uses the standard library so it can bootstrap without any
pre-installed Python packages.
"""

from __future__ import annotations

import argparse
import importlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

# --------------------------------------------------------------------------
# Project facts (keep in sync with the Makefile / pyproject.toml / README).
# --------------------------------------------------------------------------
ROOT = Path(__file__).resolve().parent.parent
VENV_DIR = ROOT / ".venv"

# Python packages actually imported by app/ or required by the test config.
PYTHON_DEPS = ["pytest", "PyQt5"]
OPTIONAL_DEPS = ["ruff"]

# Native tools the backend build genuinely needs.
NATIVE_TOOLS = ["make", "gcc", "flex"]


def _which(name: str) -> str | None:
    return shutil.which(name)


def _venv_python() -> Path:
    if os.name == "nt":
        return VENV_DIR / "Scripts" / "python.exe"
    return VENV_DIR / "bin" / "python"


def _venv_pip(python: Path) -> Path:
    return python.parent / ("pip.exe" if os.name == "nt" else "pip")


def _module_available(python: Path, module: str) -> bool:
    try:
        proc = subprocess.run(
            [str(python), "-c", f"import {module}"],
            check=False, capture_output=True,
        )
        return proc.returncode == 0
    except OSError:
        return False


# --------------------------------------------------------------------------
# check: native tool chain
# --------------------------------------------------------------------------
def _print_install_help(tool: str) -> str:
    if os.name == "nt":
        if tool == "make":
            return ("Install make via MSYS2 (`pacman -S make`) or add Git's "
                    "usr/bin to PATH, or use WSL.")
        return (
            f"{tool} was not found. Install the MinGW-w64 toolchain via "
            f"MSYS2 (`pacman -S {tool}`) and add it to PATH, or use WSL."
        )
    if shutil.which("apt-get"):
        return f"Run: sudo apt-get install {tool}"
    if shutil.which("brew"):
        return f"Run: brew install {tool}"
    if shutil.which("dnf"):
        return f"Run: sudo dnf install {tool}"
    return f"Install {tool} with your platform's package manager."


def cmd_check(args: argparse.Namespace) -> int:
    missing = []
    for tool in NATIVE_TOOLS:
        if _which(tool):
            print(f"  [ok]    {tool}: {_which(tool)}")
        else:
            print(f"  [MISS]  {tool}")
            missing.append(tool)

    py = sys.executable
    print(f"  [ok]    python: {py} (v{sys.version_info.major}.{sys.version_info.minor})")

    if missing:
        print("\nMissing native dependencies:")
        for tool in missing:
            print(f"    - {tool}: {_print_install_help(tool)}")
        print("\nInstall the missing tool(s), then re-run `make bold-build`.")
        return 1
    return 0


# --------------------------------------------------------------------------
# venv: Python environment + packages
# --------------------------------------------------------------------------
def _bootstrap_pip(venv_python: Path) -> int:
    """Install pip into the venv using the base interpreter's `--python`.

    `ensurepip` is unreliable here (it can fail with WinError 2 in paths
    containing spaces / OneDrive), so we bootstrap pip directly through the
    base python, which avoids that code path entirely.
    """
    print("  [..]    bootstrapping pip into the virtual environment ...")
    try:
        subprocess.run(
            [sys.executable, "-m", "pip", "--python", str(venv_python),
             "install", "--upgrade", "pip"],
            check=True, capture_output=True,
        )
        return 0
    except subprocess.CalledProcessError as exc:
        print("  [FAIL]  could not bootstrap pip into the virtual environment.")
        print(exc.stderr.decode(errors="replace") if exc.stderr else exc)
        return 1


def cmd_venv(args: argparse.Namespace) -> int:
    python = _venv_python()
    created = False

    if python.is_file():
        print(f"  [ok]    virtualenv present: {python}")
    else:
        print("  [..]    creating virtual environment (.venv) ...")
        try:
            # --without-pip: pip is bootstrapped separately to avoid the
            # ensurepip bug on paths with spaces (see _bootstrap_pip).
            subprocess.run(
                [sys.executable, "-m", "venv", "--without-pip", str(VENV_DIR)],
                check=True, capture_output=True,
            )
            created = True
        except subprocess.CalledProcessError as exc:
            print("  [FAIL]  could not create virtual environment.")
            print(exc.stderr.decode(errors="replace") if exc.stderr else exc)
            return 1
        print("  [ok]    virtual environment created.")

    # Ensure pip exists inside the venv.
    pip = _venv_pip(python)
    if not pip.is_file():
        if _bootstrap_pip(python) != 0:
            return 1

    # Install only packages that are missing.
    def install(name: str) -> None:
        print(f"  [..]    installing {name} ...")
        subprocess.run([str(python), "-m", "pip", "install", name], check=False)

    for pkg in PYTHON_DEPS:
        if _module_available(python, pkg):
            print(f"  [ok]    {pkg}: already available")
        else:
            install(pkg)

    for pkg in OPTIONAL_DEPS:
        if _module_available(python, pkg):
            print(f"  [ok]    {pkg}: already available")
        else:
            print(f"  [..]    (optional) installing {pkg} ...")
            install(pkg)

    if created:
        print("\nPython environment ready. You may now run the GUI with: make gui")
    return 0


# --------------------------------------------------------------------------
# entry point
# --------------------------------------------------------------------------
def main() -> int:
    parser = argparse.ArgumentParser(
        prog="setup.py", description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("check", help="verify native build tools")
    sub.add_parser("venv", help="create venv and install Python deps")

    args = parser.parse_args()
    if args.command == "check":
        return cmd_check(args)
    if args.command == "venv":
        return cmd_venv(args)
    return 1


if __name__ == "__main__":
    sys.exit(main())
