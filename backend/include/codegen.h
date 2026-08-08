#ifndef CO1_CODEGEN_H
#define CO1_CODEGEN_H

#include <stddef.h>

#include "darray.h"
#include "ir.h"

/* ============================================================
   Code generation phase: TAC -> x86-64 assembly.

   Each quad expands to a short, readable sequence of x86-64
   (AT&T syntax) instructions. Integer values travel through
   %eax, float values through %xmm0; every variable and every
   temporary owns a stack slot so the emitted code never needs
   a general register allocator.

   The driver serialises the result as assembly.json
   (schema "compileone/assembly/1.0"): the full text listing plus
   a structured instruction array (for syntax highlighting and
   annotation) and the stack layout.
   ============================================================ */

typedef struct AsmInstruction {
    int address;            /* pseudo byte offset */
    const char *label;      /* NULL or the label this instruction begins */
    char *mnemonic;         /* owned, e.g. "movl" */
    char **operands;        /* owned, 0-2 entries */
    size_t noperands;
    const char *class_name; /* static: "stack" | "data-move" | ... */
    char *comment;          /* owned or NULL */
} AsmInstruction;

DARRAY_DECLARE(AsmInstruction, AsmInstructionList)

typedef struct AsmSlot {
    char *name;             /* owned: variable or temporary name */
    int offset;             /* negative displacement from %rbp */
    int size;               /* 4 (int/bool/char) or 8 (float) */
} AsmSlot;

DARRAY_DECLARE(AsmSlot, AsmSlotList)

typedef struct AsmDoc {
    AsmInstructionList instructions;
    AsmSlotList slots;
    int stack_size;         /* bytes reserved in the prologue */
    char *text;             /* owned: complete AT&T listing */
} AsmDoc;

/* Generate assembly for `quads` (IR TAC). The caller owns `doc`. */
void codegen_generate(const IrQuadList *quads, AsmDoc *doc);

void asm_doc_free(AsmDoc *doc);

#endif /* CO1_CODEGEN_H */
