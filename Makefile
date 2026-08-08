# ============================================================
#  CompileOne — top-level build (backend + tests + GUI)
#  Relative paths only, so the build is safe inside a parent
#  path that contains spaces (e.g. OneDrive).
# ============================================================

CC        := gcc
FLEX      := flex

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
        $(BUILD_DIR)/interp.o

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

$(BUILD_DIR)/%.o: backend/src/exec/%.c $(BACKEND_HDRS) | build_dirs
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
# Convenience targets
# ------------------------------------------------------------
.PHONY: all build_dirs test pytest gui clean

# Backend smoke test + full frontend test suite
test: all
	python -m pytest tests/frontend tests/integration -q

pytest:
	python -m pytest tests/frontend tests/integration -q

gui: all
	python app/main.py

clean:
	@echo "[clean] removing build artifacts..."
	-@rm -rf $(BUILD_DIR) $(GEN_DIR) 2>NUL || true
	-@rmdir /s /q $(BUILD_DIR) 2>NUL || true
	-@rmdir /s /q $(GEN_DIR) 2>NUL || true
	@echo "Clean complete."
