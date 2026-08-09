# ============================================================
#  CompileOne — top-level build (backend + tests + GUI)
#  Relative paths only, so the build is safe inside a parent
#  path that contains spaces (e.g. OneDrive).
# ============================================================

CC        := gcc
FLEX      := flex
ifneq ($(wildcard .venv/Scripts/python.exe),)
PYTHON := .venv/Scripts/python.exe
else
PYTHON ?= python
endif

# Python interpreter inside the venv created by `make bold-build`
# (used so verify runs against the environment where deps are installed).
ifeq ($(OS),Windows_NT)
VENV_PY := .venv/Scripts/python.exe
else
VENV_PY := .venv/bin/python
endif

BUILD_DIR := Build
GEN_DIR   := backend/build

TARGET    := $(BUILD_DIR)/compileone.exe

LEXER_L   := backend/src/lexer/lexer.l
LEXER_C   := $(GEN_DIR)/lex.yy.c

BACKEND_INCS := -Ibackend/include
CFLAGS       := -Wall -O2 -std=gnu99 $(BACKEND_INCS) -D__USE_MINGW_ANSI_STDIO
BACKEND_HDRS := $(wildcard backend/include/*.h)

# Backend object files (basenames are unique => no collisions)
OBJS := $(BUILD_DIR)/compileone.o \
        $(BUILD_DIR)/lex.yy.o \
        $(BUILD_DIR)/token.o \
        $(BUILD_DIR)/strbuf.o \
        $(BUILD_DIR)/json_writer.o \
        $(BUILD_DIR)/json_reader.o \
        $(BUILD_DIR)/artifact_loader.o \
        $(BUILD_DIR)/parser.o \
        $(BUILD_DIR)/frontend.o \
        $(BUILD_DIR)/semantic.o \
        $(BUILD_DIR)/interp.o \
        $(BUILD_DIR)/ir.o \
        $(BUILD_DIR)/ir_lang.o \
        $(BUILD_DIR)/optimize.o \
        $(BUILD_DIR)/codegen.o

# ------------------------------------------------------------
# Default target
# ------------------------------------------------------------
all: build_dirs $(TARGET)

# ------------------------------------------------------------
# Flex: generate the scanner from lexer.l
# ------------------------------------------------------------
$(LEXER_C): $(LEXER_L) | build_dirs
	@echo "[flex] $(LEXER_L)"
	$(FLEX) -o $(LEXER_C) $(LEXER_L)

# Object files depend on the headers so a header change (e.g. token.h)
# triggers a full rebuild instead of linking stale objects.
$(BUILD_DIR)/%.o: backend/src/driver/%.c $(BACKEND_HDRS) | build_dirs
	@echo "[gcc]  $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: backend/src/util/%.c $(BACKEND_HDRS) | build_dirs
	@echo "[gcc]  $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: backend/src/json/%.c $(BACKEND_HDRS) | build_dirs
	@echo "[gcc]  $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: backend/src/parser/%.c $(BACKEND_HDRS) | build_dirs
	@echo "[gcc]  $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: backend/src/semantic/%.c $(BACKEND_HDRS) | build_dirs
	@echo "[gcc]  $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: backend/src/exec/%.c $(BACKEND_HDRS) | build_dirs
	@echo "[gcc]  $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: backend/src/ir/%.c $(BACKEND_HDRS) | build_dirs
	@echo "[gcc]  $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: backend/src/optimize/%.c $(BACKEND_HDRS) | build_dirs
	@echo "[gcc]  $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: backend/src/codegen/%.c $(BACKEND_HDRS) | build_dirs
	@echo "[gcc]  $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/lex.yy.o: $(LEXER_C) $(BACKEND_HDRS) | build_dirs
	@echo "[gcc]  $@"
	$(CC) $(CFLAGS) -c $(LEXER_C) -o $@

# ------------------------------------------------------------
# Link
# ------------------------------------------------------------
$(TARGET): $(OBJS)
	@echo "[link] $@"
	$(CC) $(OBJS) -o $@

build_dirs:
	@mkdir -p $(BUILD_DIR) $(GEN_DIR) Output Temp

# ------------------------------------------------------------
# bold-build: one-command setup + build for new contributors.
#   - checks native tools (make / gcc / flex / python)
#   - prepares directories
#   - generates the flex scanner
#   - builds the backend
#   - prepares the Python environment (.venv + pytest/PyQt5)
#   - verifies the backend and runs the test suite
# Only missing pieces are installed; nothing is reinstalled blindly.
# The sub-targets are invoked as literal `make` (NOT $(MAKE)) because the
# project lives under a path containing spaces and recursive $(MAKE)
# expansion mangles on MSYS.
# ------------------------------------------------------------
.PHONY: all build_dirs test pytest gui clean bold-build

bold-build:
	@echo "[1/6] Checking environment and dependencies..."
	@$(PYTHON) scripts/setup.py check
	@echo "[2/6] Preparing build directories..."
	@make build_dirs
	@echo "[3/6] Generating Flex scanner..."
	@make $(LEXER_C)
	@echo "[4/6] Building backend..."
	@make all
	@echo "[5/6] Preparing Python environment and dependencies..."
	@$(PYTHON) scripts/setup.py venv
	@echo "[6/6] Verifying build..."
	@mkdir -p Output
	@$(TARGET) lex --input examples/study/hello.mc --output Output/tokens.json --language mini-c
	@echo "[verify] running test suite (frontend + integration)..."
	@$(VENV_PY) -m pytest tests/frontend tests/integration -q
	@echo ""
	@echo "bold-build completed successfully."

# Backend smoke test + full frontend test suite
test: all
	"$(PYTHON)" -m pytest tests/frontend tests/integration -q

pytest:
	"$(PYTHON)" -m pytest tests/frontend tests/integration -q

gui: all
	"$(PYTHON)" app/main.py

clean:
	@echo "[clean] removing build artifacts..."
	-@rm -rf $(BUILD_DIR) $(GEN_DIR) 2>NUL || true
	-@rmdir /s /q $(BUILD_DIR) 2>NUL || true
	-@rmdir /s /q $(GEN_DIR) 2>NUL || true
	@echo "Clean complete."
