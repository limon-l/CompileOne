#ifndef CO1_SEMANTIC_H
#define CO1_SEMANTIC_H

#include "darray.h"
#include "parser.h"

/* ============================================================
   Semantic analyzer for the mini-c study language.

   Walks the AST produced by the parser, builds a scoped symbol
   table, infers expression types (annotating the AST), and
   emits diagnostics for the classic classroom errors:
   undeclared/redeclared identifiers, assignment to const,
   invalid operand types, narrowing conversions, non-boolean
   conditions, and a handful of warnings.

   Results are serialised by the driver into semantic.json
   (schema "compileone/semantic/1.0").
   ============================================================ */

typedef struct Symbol {
    const char *name;           /* owned */
    const char *type;           /* "int" | "float" | "bool" | "char" */
    const char *scope;          /* static string: "global" | "block:N" */
    int scope_level;
    int is_const;
    int line;
    int column;
    int used;                   /* set when referenced by an expression */
    size_t out_index;           /* index of this symbol in all_symbols */
} Symbol;

DARRAY_DECLARE(Symbol, SymbolList)

typedef struct SemanticDiagnostic {
    int line;
    int column;
    const char *severity;       /* "error" | "warning" */
    const char *code;           /* e.g. "SEM001" */
    char *message;              /* owned */
} SemanticDiagnostic;

DARRAY_DECLARE(SemanticDiagnostic, SemanticDiagnosticList)

typedef struct SemanticResult {
    SymbolList symbols;
    SemanticDiagnosticList diagnostics;
    int valid;                  /* no errors (warnings do not invalidate) */
} SemanticResult;

/* Analyse `program` (the "Program" root node from the parser).
   Populates `out`; the caller owns the result (see semantic_result_free). */
void semantic_analyze(const ASTNode *program, SemanticResult *out);

void semantic_result_free(SemanticResult *r);

#endif /* CO1_SEMANTIC_H */
