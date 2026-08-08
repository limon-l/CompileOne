# CompileOne

CompileOne is an educational compiler IDE built as a real compiler pipeline
with a Python/PyQt5 frontend and a C-based backend. The project is designed to
show every compiler phase through JSON artifacts produced by the backend and
rendered by the frontend.

## What this project contains

- `backend/`
  - C compiler backend with a phase-based driver (`compileone.exe`)
  - Flex lexer and supporting parser/semantic/IR/codegen modules
  - JSON artifact writer for compiler phases
- `app/`
  - Python frontend using PyQt5
  - Application services for compilation, backend orchestration, tooling,
    theming, and settings
  - UI panels for source editing, token/AST/IR/assembly views, problems, and run
    output
- `examples/study/`
  - Mini-C example programs used by the study-mode pipeline and integration
    tests
- `tests/`
  - Frontend unit tests and backend integration tests validated with `pytest`
- `docs/`
  - Architecture and design notes in `docs/DESIGN.md`

## Key features

- Phase-driven backend: `compileone.exe` exposes subcommands such as
  `lex`, `parse`, `ast`, `semantic`, `ir`, `opt`, `codegen`, and `run`.
- JSON artifact pipeline: every compiler phase produces a structured JSON output
  artifact consumed by the next phase and by the UI.
- Mini-C study mode: a pedagogical language powered by the project's own lexer
  and interpreter backend.
- PyQt5 desktop IDE: editor, diagnostics, phase views, run output, and backend
  orchestration in one interface.
- Integration tests that compare backend artifacts against golden JSON outputs.

## Build requirements

- Python 3.14
- PyQt5
- GCC / MinGW
- flex
- `make`
- `pytest`
- `ruff` (optional for linting)

## Build and run

From the repository root:

```bash
make            # build the backend executable
make gui        # build and launch the IDE
make test       # build and run pytest frontend + integration
make pytest     # run pytest without rebuilding
make clean      # remove generated build artifacts
```

If you do not have `make`, you can still build the backend manually by
invoking the commands from `Makefile`.

## Running the backend CLI

The backend exposes a short-lived CLI driver for each compiler phase.
Example:

```bash
Build/compileone.exe lex --input examples/study/hello.mc --output Output/tokens.json --language mini-c
Build/compileone.exe run --input examples/study/loop.mc --output Output/execution.json --language mini-c
Build/compileone.exe --list-phases
```

## Project layout

```text
CompileOne/
├── Makefile
├── pyproject.toml
├── README.md
├── backend/              # compiler backend sources and generated lexer
├── app/                  # Python frontend application and UI
├── docs/                 # design and architecture documentation
├── examples/study/       # mini-c study language example programs
├── tests/                # frontend and integration tests
├── Build/                # generated backend binary and object files
├── Output/               # generated output artifacts
└── Temp/                 # temporary runtime files
```

## Testing

Run the test suite with:

```bash
.venv\Scripts\python.exe -m pytest -q
```

Or run only the integration lex tests:

```bash
.venv\Scripts\python.exe -m pytest tests/integration/test_backend_lex.py -q
```

## Notes

- The backend driver is designed to emit JSON artifacts for each compiler phase so
  the UI can visualize real compiler state.
- The frontend is built to display phase artifacts rather than reimplement
  compiler logic in Python.
- The `docs/DESIGN.md` file contains detailed architecture and roadmap notes.

## Contribution

If you want to extend CompileOne:

1. Build the backend with `make all`.
2. Run the IDE with `make gui`.
3. Add or update tests under `tests/frontend/` or `tests/integration/`.
4. Keep JSON artifact schemas aligned with the frontend data models.

---

Happy hacking with compiler pipelines!