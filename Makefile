# ====================================================================
#  Robust Makefile for Compiler Construction Project (Flex + Bison + C)
# ====================================================================

# Compiler Tools
CC      = gcc
FLEX    = flex
BISON   = bison

# Include directories: Gives GCC access to parser.tab.h and all C headers
INCLUDES = -I. -Isrc/parser -Isrc/ast -Isrc/symbol_table -Isrc/semantic -Isrc/codegen
CFLAGS   = -Wall -Wextra $(INCLUDES)

# Target executable name
TARGET   = compiler.exe

# Source Files Specification
PARSER_Y = src/parser/parser.y
PARSER_C = src/parser/parser.tab.c
PARSER_H = src/parser/parser.tab.h

LEXER_L  = src/lexer/lexer.l
LEXER_C  = src/lexer/lex.yy.c

MODULES  = src/ast/ast.c \
           src/symbol_table/symbol_table.c \
           src/semantic/semantic.c \
           src/codegen/tac.c

OBJS     = $(PARSER_C) $(LEXER_C) $(MODULES)

# Default Rule
all: $(TARGET)

# Rule 1: Generate Parser C and Header files using Bison first
$(PARSER_C) $(PARSER_H): $(PARSER_Y)
	@echo "[1/3] Generating Bison Parser files..."
	$(BISON) -d -o $(PARSER_C) $(PARSER_Y)

# Rule 2: Generate Lexer C file using Flex (Explicitly depends on parser.tab.h)
$(LEXER_C): $(LEXER_L) $(PARSER_H)
	@echo "[2/3] Generating Flex Lexer files..."
	$(FLEX) -o $(LEXER_C) $(LEXER_L)

# Rule 3: Compile all C source files into compiler.exe
$(TARGET): $(PARSER_C) $(LEXER_C) $(MODULES)
	@echo "[3/3] Compiling complete project with GCC..."
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET)
	@echo "--------------------------------------------------------"
	@echo " BUILD SUCCESSFUL: $(TARGET) is ready!"
	@echo "--------------------------------------------------------"

# Run sample input test
test: $(TARGET)
	./$(TARGET) examples/sample.mc

# Launch Python Modern IDE GUI
gui: $(TARGET)
	python gui.py

# Universal Clean Command (Safe for PowerShell, CMD, and Git Bash)
clean:
	@echo "Cleaning temporary build files..."
	-@rm -f $(PARSER_C) $(PARSER_H) $(LEXER_C) $(TARGET) temp_input.mc *.o 2>NUL || true
	-@del /f /q src\parser\parser.tab.c src\parser\parser.tab.h src\lexer\lex.yy.c $(TARGET) temp_input.mc *.o 2>NUL || true
	@echo "Clean complete."

.PHONY: all test gui clean