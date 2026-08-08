#ifndef CO1_IR_H
#define CO1_IR_H

#include <stddef.h>

#include "darray.h"
#include "parser.h"

/* ============================================================
   Intermediate representation for the mini-c study language.

   The IR phase lowers the AST produced by the parser into linear
   three-address code (TAC): one quadruple per line, temporaries
   t1..tN, labels L1..LN. Every mid-pipeline phase consumes the
   token-stream artifact and re-derives the front end in-process,
   so ir_build() takes the AST root directly.

   The artifact written by the driver ("compileone/ir/1.0") is
   consumed by the optimization and codegen phases and rendered by
   the IR view in the UI.
   ============================================================ */

typedef struct IrQuad {
    int index;          /* 1-based position in the instruction list */
    const char *op;     /* static string: "assign" | "add" | ... | "label" */
    char *arg1;         /* owned or NULL */
    char *arg2;         /* owned or NULL */
    char *result;       /* owned or NULL (labels are stored in `result`) */
} IrQuad;

DARRAY_DECLARE(IrQuad, IrQuadList)

/* Lower `program` (the "Program" AST root) to TAC. The caller owns
   the produced list (see ir_list_free). */
void ir_build(const ASTNode *program, IrQuadList *out);

/* Render the whole instruction list as readable TAC text (caller frees). */
char *ir_render(const IrQuadList *quads);

/* Render a single instruction in TAC source form (caller frees). */
char *ir_quad_text(const IrQuad *q);

void ir_list_free(IrQuadList *quads);

#endif /* CO1_IR_H */
