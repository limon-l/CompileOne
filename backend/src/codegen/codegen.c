/* ============================================================
   Code generation phase: TAC -> x86-64 assembly (AT&T syntax).

   Strategy (kept deliberately simple for the study language):

     - Every variable and temporary owns a fixed stack slot. Slots
       are allocated downward from %rbp: 4 bytes for int/bool/char,
       8 bytes for float.
     - Integer expressions evaluate in %eax (32-bit), float
       expressions in %xmm0 (scalar double).
     - Integer division uses the idivl sequence (%edx:%eax),
       modulo takes the remainder from %edx.
     - Comparisons materialise a 0/1 integer with setcc.
     - print/read map onto calls to the C runtime (printf/scanf)
       with format strings emitted in .rodata.

   The output is both a printable AT&T listing and a structured
   instruction array so the UI can highlight and annotate each
   line (assembly.json, schema "compileone/assembly/1.0").
   ============================================================ */

#include "codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "strbuf.h"

DARRAY_DEFINE(AsmInstruction, AsmInstructionList)
DARRAY_DEFINE(AsmSlot, AsmSlotList)

/* ---------------------------------------------------------------- symbol table */

typedef struct CGEntry {
    char *name;             /* owned */
    int is_var;             /* declared variable vs. inferred temporary */
    const char *type;       /* static: "int" | "float" | "bool" | "char" */
    int offset;             /* negative %rbp displacement */
    int size;
} CGEntry;

typedef struct CGSym {
    CGEntry *items;
    size_t len;
    size_t cap;
} CGSym;

static CGEntry *cg_find(CGSym *s, const char *name) {
    for (size_t i = 0; i < s->len; i++) {
        if (strcmp(s->items[i].name, name) == 0) {
            return &s->items[i];
        }
    }
    return NULL;
}

static CGEntry *cg_add(CGSym *s, const char *name, int is_var, const char *type,
                       int *next_offset) {
    if (s->len == s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 16;
        s->items = (CGEntry *)realloc(s->items, nc * sizeof(CGEntry));
        s->cap = nc;
    }
    CGEntry *e = &s->items[s->len];
    int size = (type && strcmp(type, "float") == 0) ? 8 : 4;
    *next_offset -= size;
    e->name = co1_strdup(name);
    e->is_var = is_var;
    e->type = type;
    e->offset = *next_offset;
    e->size = size;
    s->len++;
    return e;
}

/* ---------------------------------------------------------------- string/float literals */

typedef struct CGLit {
    char *text;             /* owned: raw literal text */
    char *label;            /* owned: ".LCn" or ".LFn" */
} CGLit;

typedef struct CGLitList {
    CGLit *items;
    size_t len;
    size_t cap;
} CGLitList;

static CGLit *lit_find(CGLitList *l, const char *text) {
    for (size_t i = 0; i < l->len; i++) {
        if (strcmp(l->items[i].text, text) == 0) {
            return &l->items[i];
        }
    }
    return NULL;
}

static CGLit *lit_add(CGLitList *l, const char *text, const char *label_prefix,
                      int counter) {
    if (l->len == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 8;
        l->items = (CGLit *)realloc(l->items, nc * sizeof(CGLit));
        l->cap = nc;
    }
    CGLit *e = &l->items[l->len];
    e->text = co1_strdup(text);
    char buf[32];
    snprintf(buf, sizeof(buf), "%s%d", label_prefix, counter);
    e->label = co1_strdup(buf);
    l->len++;
    return e;
}

/* ---------------------------------------------------------------- codegen context */

typedef struct CG {
    CGSym sym;
    CGLitList strings;
    CGLitList floats;
    int next_string;
    int next_float;
    int next_offset;        /* grows downward from 0 */
    AsmDoc *doc;
    StrBuf text;
    int addr;
    char *pending_label;    /* owned */
} CG;

/* ---------------------------------------------------------------- helpers */

static int is_integer_literal(const char *s) {
    if (!s || !*s) {
        return 0;
    }
    if (*s == '-') {
        s++;
    }
    if (!*s) {
        return 0;
    }
    for (; *s; s++) {
        if (*s < '0' || *s > '9') {
            return 0;
        }
    }
    return 1;
}

static int is_float_literal(const char *s) {
    if (!s || !*s) {
        return 0;
    }
    return strchr(s, '.') != NULL || strchr(s, 'e') != NULL ||
           strchr(s, 'E') != NULL;
}

static const char *cg_type_of(CG *cg, const char *name) {
    CGEntry *e = cg_find(&cg->sym, name);
    if (e) {
        return e->type;
    }
    if (is_float_literal(name)) {
        return "float";
    }
    return "int";
}

static int cg_is_float(CG *cg, const char *name) {
    return strcmp(cg_type_of(cg, name), "float") == 0;
}

/* Resolve an operand to its AT&T source form (caller frees). */
static char *cg_src(CG *cg, const char *name) {
    CGEntry *e = cg_find(&cg->sym, name);
    if (e) {
        StrBuf sb;
        strbuf_init(&sb);
        strbuf_append_char(&sb, '-');
        strbuf_append_int(&sb, -e->offset);
        strbuf_append(&sb, "(%rbp)");
        char *out = co1_strdup(strbuf_cstr(&sb));
        strbuf_free(&sb);
        return out;
    }
    if (is_integer_literal(name)) {
        StrBuf sb;
        strbuf_init(&sb);
        strbuf_append_char(&sb, '$');
        strbuf_append(&sb, name);
        char *out = co1_strdup(strbuf_cstr(&sb));
        strbuf_free(&sb);
        return out;
    }
    if (is_float_literal(name)) {
        CGLit *lit = lit_find(&cg->floats, name);
        if (lit) {
            StrBuf sb;
            strbuf_init(&sb);
            strbuf_append(&sb, lit->label);
            strbuf_append(&sb, "(%rip)");
            char *out = co1_strdup(strbuf_cstr(&sb));
            strbuf_free(&sb);
            return out;
        }
    }
    return co1_strdup("-4(%rbp)"); /* unresolved symbol fallback */
}

static void text_line(CG *cg, const char *line) {
    strbuf_append(&cg->text, line);
    strbuf_append_char(&cg->text, '\n');
}

/* ---------------------------------------------------------------- emitters */

static void asm_label(CG *cg, const char *name) {
    free(cg->pending_label);
    cg->pending_label = co1_strdup(name);
    char line[128];
    snprintf(line, sizeof(line), "%s:", name);
    text_line(cg, line);
}

static void asm_insn(CG *cg, const char *mnemonic, const char *op1,
                     const char *op2, const char *klass, const char *comment) {
    AsmInstruction ins;
    ins.address = cg->addr++;
    ins.label = NULL;
    if (cg->pending_label) {
        ins.label = cg->pending_label;
        cg->pending_label = NULL;
    }
    ins.mnemonic = co1_strdup(mnemonic);
    ins.noperands = 0;
    ins.operands = NULL;
    if (op1) {
        ins.operands = (char **)malloc(2 * sizeof(char *));
        ins.operands[0] = co1_strdup(op1);
        ins.noperands = 1;
        if (op2) {
            ins.operands[1] = co1_strdup(op2);
            ins.noperands = 2;
        }
    }
    ins.class_name = klass;
    ins.comment = comment ? co1_strdup(comment) : NULL;
    AsmInstructionList_push(&cg->doc->instructions, ins);

    StrBuf sb;
    strbuf_init(&sb);
    strbuf_append(&sb, "    ");
    strbuf_append(&sb, mnemonic);
    while (strbuf_len(&sb) < 16) {
        strbuf_append_char(&sb, ' ');
    }
    if (op1) {
        strbuf_append(&sb, op1);
        if (op2) {
            strbuf_append(&sb, ", ");
            strbuf_append(&sb, op2);
        }
    }
    if (comment) {
        while (strbuf_len(&sb) < 40) {
            strbuf_append_char(&sb, ' ');
        }
        strbuf_append(&sb, "# ");
        strbuf_append(&sb, comment);
    }
    text_line(cg, strbuf_cstr(&sb));
    strbuf_free(&sb);
}

/* mov  value -> %eax / %xmm0 (by type) */
static void asm_load(CG *cg, const char *name) {
    char *src = cg_src(cg, name);
    if (cg_is_float(cg, name)) {
        asm_insn(cg, "movsd", src, "%xmm0", "data-move", NULL);
    } else {
        asm_insn(cg, "movl", src, "%eax", "data-move", NULL);
    }
    free(src);
}

/* mov  %eax / %xmm0 -> result slot (by result type) */
static void asm_store(CG *cg, const char *result) {
    char *dst = cg_src(cg, result);
    if (cg_is_float(cg, result)) {
        asm_insn(cg, "movsd", "%xmm0", dst, "data-move", NULL);
    } else {
        asm_insn(cg, "movl", "%eax", dst, "data-move", NULL);
    }
    free(dst);
}

static void asm_normalize_bool_rax(CG *cg, const char *name) {
    char *src = cg_src(cg, name);
    asm_insn(cg, "movl", src, "%eax", "data-move", NULL);
    asm_insn(cg, "testl", "%eax", "%eax", "compare", NULL);
    asm_insn(cg, "setne", "%al", NULL, "compare", "normalise to 0/1");
    asm_insn(cg, "movzbl", "%al", "%eax", "data-move", NULL);
    free(src);
}

/* ---------------------------------------------------------------- quad -> asm */

static void cg_assign(CG *cg, const IrQuad *q) {
    asm_load(cg, q->arg1);
    asm_store(cg, q->result);
}

static void cg_arith(CG *cg, const IrQuad *q) {
    const char *mn = NULL;
    if (strcmp(q->op, "add") == 0) mn = "addl";
    else if (strcmp(q->op, "sub") == 0) mn = "subl";
    else if (strcmp(q->op, "mul") == 0) mn = "imull";

    if (cg_is_float(cg, q->arg1) || cg_is_float(cg, q->arg2)) {
        const char *fmn = NULL;
        if (strcmp(q->op, "add") == 0) fmn = "addsd";
        else if (strcmp(q->op, "sub") == 0) fmn = "subsd";
        else if (strcmp(q->op, "mul") == 0) fmn = "mulsd";
        else if (strcmp(q->op, "div") == 0) fmn = "divsd";

        char *a = cg_src(cg, q->arg1);
        char *b = cg_src(cg, q->arg2);
        asm_insn(cg, "movsd", a, "%xmm0", "data-move", NULL);
        asm_insn(cg, "movsd", b, "%xmm1", "data-move", NULL);
        asm_insn(cg, fmn, "%xmm1", "%xmm0", "arith-float", NULL);
        asm_store(cg, q->result);
        free(a);
        free(b);
        return;
    }

    if (strcmp(q->op, "div") == 0 || strcmp(q->op, "mod") == 0) {
        char *a = cg_src(cg, q->arg1);
        char *b = cg_src(cg, q->arg2);
        asm_insn(cg, "movl", a, "%eax", "data-move", NULL);
        asm_insn(cg, "cltd", NULL, NULL, "arith", "sign-extend %eax -> %edx:%eax");
        asm_insn(cg, "idivl", b, NULL, "arith", NULL);
        if (strcmp(q->op, "mod") == 0) {
            asm_insn(cg, "movl", "%edx", "%eax", "data-move", "remainder");
        }
        asm_store(cg, q->result);
        free(a);
        free(b);
        return;
    }

    char *a = cg_src(cg, q->arg1);
    char *b = cg_src(cg, q->arg2);
    asm_insn(cg, "movl", a, "%eax", "data-move", NULL);
    asm_insn(cg, mn, b, "%eax", "arith", NULL);
    asm_store(cg, q->result);
    free(a);
    free(b);
}

static void cg_compare(CG *cg, const IrQuad *q) {
    const char *setcc = NULL;
    if (strcmp(q->op, "lt") == 0) setcc = "setl";
    else if (strcmp(q->op, "le") == 0) setcc = "setle";
    else if (strcmp(q->op, "gt") == 0) setcc = "setg";
    else if (strcmp(q->op, "ge") == 0) setcc = "setge";
    else if (strcmp(q->op, "eq") == 0) setcc = "sete";
    else if (strcmp(q->op, "ne") == 0) setcc = "setne";

    if (cg_is_float(cg, q->arg1) || cg_is_float(cg, q->arg2)) {
        char *a = cg_src(cg, q->arg1);
        char *b = cg_src(cg, q->arg2);
        asm_insn(cg, "movsd", a, "%xmm0", "data-move", NULL);
        asm_insn(cg, "movsd", b, "%xmm1", "data-move", NULL);
        asm_insn(cg, "ucomisd", "%xmm1", "%xmm0", "compare", NULL);
        asm_insn(cg, setcc, "%al", NULL, "compare", NULL);
        asm_insn(cg, "movzbl", "%al", "%eax", "data-move", NULL);
        asm_store(cg, q->result);
        free(a);
        free(b);
        return;
    }

    char *a = cg_src(cg, q->arg1);
    char *b = cg_src(cg, q->arg2);
    asm_insn(cg, "movl", a, "%eax", "data-move", NULL);
    asm_insn(cg, "cmpl", b, "%eax", "compare", NULL);
    asm_insn(cg, setcc, "%al", NULL, "compare", NULL);
    asm_insn(cg, "movzbl", "%al", "%eax", "data-move", NULL);
    asm_store(cg, q->result);
    free(a);
    free(b);
}

static void cg_bool(CG *cg, const IrQuad *q) {
    asm_normalize_bool_rax(cg, q->arg1);            /* %eax = (a != 0) */
    asm_insn(cg, "movl", "%eax", "%ecx", "data-move", "save lhs truth value");
    asm_normalize_bool_rax(cg, q->arg2);            /* %eax = (b != 0) */
    asm_insn(cg, strcmp(q->op, "and") == 0 ? "andl" : "orl", "%ecx", "%eax",
             "arith", NULL);
    asm_store(cg, q->result);
}

static void cg_neg(CG *cg, const IrQuad *q) {
    if (cg_is_float(cg, q->arg1)) {
        char *a = cg_src(cg, q->arg1);
        asm_insn(cg, "movsd", a, "%xmm0", "data-move", NULL);
        asm_insn(cg, "xorpd", "%xmm1", "%xmm1", "arith-float", NULL);
        asm_insn(cg, "subsd", "%xmm1", "%xmm0", "arith-float", "0 - a = -a");
        asm_store(cg, q->result);
        free(a);
        return;
    }
    char *a = cg_src(cg, q->arg1);
    asm_insn(cg, "movl", a, "%eax", "data-move", NULL);
    asm_insn(cg, "negl", "%eax", NULL, "arith", NULL);
    asm_store(cg, q->result);
    free(a);
}

static void cg_print(CG *cg, const IrQuad *q) {
    if (cg_is_float(cg, q->arg1)) {
        char *a = cg_src(cg, q->arg1);
        asm_insn(cg, "movsd", a, "%xmm0", "data-move", NULL);
        asm_insn(cg, "leaq", ".Lfmt_float(%rip)", "%rdi", "data-move", "printf format");
        asm_insn(cg, "call", "printf", NULL, "call", NULL);
        free(a);
        return;
    }
    char *a = cg_src(cg, q->arg1);
    asm_insn(cg, "movl", a, "%esi", "data-move", "printf value arg");
    asm_insn(cg, "leaq", ".Lfmt_int(%rip)", "%rdi", "data-move", "printf format");
    asm_insn(cg, "call", "printf", NULL, "call", NULL);
    free(a);
}

static void cg_print_str(CG *cg, const IrQuad *q) {
    CGLit *lit = lit_find(&cg->strings, q->arg1);
    const char *ref = lit ? lit->label : ".LC0";
    StrBuf lbl;
    strbuf_init(&lbl);
    strbuf_append(&lbl, ref);
    strbuf_append(&lbl, "(%rip)");
    char *s = co1_strdup(strbuf_cstr(&lbl));
    strbuf_free(&lbl);
    asm_insn(cg, "leaq", s, "%rsi", "data-move", "printf string arg");
    asm_insn(cg, "leaq", ".Lfmt_str(%rip)", "%rdi", "data-move", "printf format");
    asm_insn(cg, "call", "printf", NULL, "call", NULL);
    free(s);
}

static void cg_read(CG *cg, const IrQuad *q) {
    char *dst = cg_src(cg, q->result);
    asm_insn(cg, "leaq", dst, "%rsi", "data-move", "scanf destination");
    asm_insn(cg, "leaq", ".Lfmt_int(%rip)", "%rdi", "data-move", "scanf format");
    asm_insn(cg, "call", "scanf", NULL, "call", NULL);
    free(dst);
}

static void cg_return(CG *cg, const IrQuad *q) {
    if (q->arg1) {
        char *a = cg_src(cg, q->arg1);
        asm_insn(cg, "movl", a, "%eax", "data-move", "return value");
        free(a);
    } else {
        asm_insn(cg, "movl", "$0", "%eax", "data-move", "return 0");
    }
    asm_insn(cg, "leave", NULL, NULL, "stack", "tear down frame");
    asm_insn(cg, "ret", NULL, NULL, "control", NULL);
}

static void cg_quad(CG *cg, const IrQuad *q) {
    if (strcmp(q->op, "declare") == 0) {
        /* slots were allocated in the layout pass; nothing to emit */
        return;
    }
    if (strcmp(q->op, "label") == 0) {
        asm_label(cg, q->result);
        return;
    }
    if (strcmp(q->op, "goto") == 0) {
        asm_insn(cg, "jmp", q->result, NULL, "control", NULL);
        return;
    }
    if (strcmp(q->op, "if_false") == 0) {
        if (cg_is_float(cg, q->arg1)) {
            char *a = cg_src(cg, q->arg1);
            asm_insn(cg, "movsd", a, "%xmm0", "data-move", NULL);
            asm_insn(cg, "xorpd", "%xmm1", "%xmm1", "arith-float", "compare vs 0.0");
            asm_insn(cg, "ucomisd", "%xmm1", "%xmm0", "compare", NULL);
            asm_insn(cg, "je", q->result, NULL, "control", "jump when false");
            free(a);
            return;
        }
        char *a = cg_src(cg, q->arg1);
        asm_insn(cg, "movl", a, "%eax", "data-move", NULL);
        asm_insn(cg, "testl", "%eax", "%eax", "compare", NULL);
        asm_insn(cg, "je", q->result, NULL, "control", "jump when false");
        free(a);
        return;
    }
    if (strcmp(q->op, "assign") == 0) {
        cg_assign(cg, q);
        return;
    }
    if (strcmp(q->op, "add") == 0 || strcmp(q->op, "sub") == 0 ||
        strcmp(q->op, "mul") == 0 || strcmp(q->op, "div") == 0 ||
        strcmp(q->op, "mod") == 0) {
        cg_arith(cg, q);
        return;
    }
    if (strcmp(q->op, "lt") == 0 || strcmp(q->op, "le") == 0 ||
        strcmp(q->op, "gt") == 0 || strcmp(q->op, "ge") == 0 ||
        strcmp(q->op, "eq") == 0 || strcmp(q->op, "ne") == 0) {
        cg_compare(cg, q);
        return;
    }
    if (strcmp(q->op, "and") == 0 || strcmp(q->op, "or") == 0) {
        cg_bool(cg, q);
        return;
    }
    if (strcmp(q->op, "neg") == 0) {
        cg_neg(cg, q);
        return;
    }
    if (strcmp(q->op, "not") == 0) {
        asm_normalize_bool_rax(cg, q->arg1);
        asm_insn(cg, "xorl", "$1", "%eax", "arith", "a ^ 1 flips 0<->1");
        asm_store(cg, q->result);
        return;
    }
    if (strcmp(q->op, "print") == 0) {
        cg_print(cg, q);
        return;
    }
    if (strcmp(q->op, "print_str") == 0) {
        cg_print_str(cg, q);
        return;
    }
    if (strcmp(q->op, "read") == 0) {
        cg_read(cg, q);
        return;
    }
    if (strcmp(q->op, "return") == 0) {
        cg_return(cg, q);
        return;
    }
}

/* ---------------------------------------------------------------- layout pass */

static void layout_quad(CG *cg, const IrQuad *q) {
    if (strcmp(q->op, "declare") == 0) {
        cg_add(&cg->sym, q->result, 1, q->arg1, &cg->next_offset);
        return;
    }
    if (q->result && !cg_find(&cg->sym, q->result)) {
        /* temporary produced by a computation: infer its type */
        const char *type = "int";
        if (strcmp(q->op, "assign") == 0 ||
            strcmp(q->op, "neg") == 0) {
            type = cg_type_of(cg, q->arg1);
        } else if (strcmp(q->op, "add") == 0 ||
                   strcmp(q->op, "sub") == 0 ||
                   strcmp(q->op, "mul") == 0 ||
                   strcmp(q->op, "div") == 0 ||
                   strcmp(q->op, "mod") == 0) {
            if (cg_is_float(cg, q->arg1) || cg_is_float(cg, q->arg2)) {
                type = "float";
            }
        }
        cg_add(&cg->sym, q->result, 0, type, &cg->next_offset);
    }
}

static void collect_literals(CG *cg, const IrQuad *q) {
    if (strcmp(q->op, "print_str") == 0 && q->arg1) {
        if (!lit_find(&cg->strings, q->arg1)) {
            lit_add(&cg->strings, q->arg1, ".LC", cg->next_string++);
        }
        return;
    }
    const char *ops[] = {q->arg1, q->arg2};
    for (int i = 0; i < 2; i++) {
        const char *a = ops[i];
        if (a && is_float_literal(a) && !cg_find(&cg->sym, a)) {
            if (!lit_find(&cg->floats, a)) {
                lit_add(&cg->floats, a, ".LF", cg->next_float++);
            }
        }
    }
}

/* ---------------------------------------------------------------- public API */

void codegen_generate(const IrQuadList *quads, AsmDoc *doc) {
    CG cg;
    cg.sym = (CGSym){0};
    cg.strings = (CGLitList){0};
    cg.floats = (CGLitList){0};
    cg.next_string = 0;
    cg.next_float = 0;
    cg.next_offset = 0;
    cg.doc = doc;
    cg.addr = 0;
    cg.pending_label = NULL;
    strbuf_init(&cg.text);

    doc->instructions = (AsmInstructionList){0};
    doc->slots = (AsmSlotList){0};
    doc->stack_size = 0;
    doc->text = NULL;

    /* pass 1: layout symbols + collect literals */
    for (size_t i = 0; i < quads->len; i++) {
        layout_quad(&cg, &quads->items[i]);
        collect_literals(&cg, &quads->items[i]);
    }
    doc->stack_size = -cg.next_offset;
    if (doc->stack_size % 8 != 0) {
        doc->stack_size += 8 - (doc->stack_size % 8);
    }

    /* prologue */
    text_line(&cg, "    .text");
    text_line(&cg, "    .globl main");
    text_line(&cg, "main:");
    asm_insn(&cg, "pushq", "%rbp", NULL, "stack", "save caller frame");
    asm_insn(&cg, "movq", "%rsp", "%rbp", "stack", "establish frame");
    {
        StrBuf sub;
        strbuf_init(&sub);
        strbuf_append(&sub, "$");
        strbuf_append_int(&sub, doc->stack_size);
        char *sz = co1_strdup(strbuf_cstr(&sub));
        strbuf_free(&sub);
        asm_insn(&cg, "subq", sz, "%rsp", "stack", "allocate locals");
        free(sz);
    }

    /* body */
    for (size_t i = 0; i < quads->len; i++) {
        cg_quad(&cg, &quads->items[i]);
    }

    /* epilogue */
    asm_insn(&cg, "movl", "$0", "%eax", "data-move", "implicit return 0");
    asm_insn(&cg, "leave", NULL, NULL, "stack", "tear down frame");
    asm_insn(&cg, "ret", NULL, NULL, "control", NULL);

    /* read-only data */
    text_line(&cg, "    .section .rodata");
    text_line(&cg, ".Lfmt_int:");
    text_line(&cg, "    .string \"%d\\n\"");
    text_line(&cg, ".Lfmt_float:");
    text_line(&cg, "    .string \"%g\\n\"");
    text_line(&cg, ".Lfmt_str:");
    text_line(&cg, "    .string \"%s\\n\"");
    for (size_t i = 0; i < cg.strings.len; i++) {
        StrBuf line;
        strbuf_init(&line);
        strbuf_append(&line, cg.strings.items[i].label);
        strbuf_append(&line, ":");
        text_line(&cg, strbuf_cstr(&line));
        strbuf_clear(&line);
        strbuf_append(&line, "    .string ");
        strbuf_append(&line, cg.strings.items[i].text);
        text_line(&cg, strbuf_cstr(&line));
        strbuf_free(&line);
    }
    for (size_t i = 0; i < cg.floats.len; i++) {
        StrBuf line;
        strbuf_init(&line);
        strbuf_append(&line, cg.floats.items[i].label);
        strbuf_append(&line, ":");
        text_line(&cg, strbuf_cstr(&line));
        strbuf_clear(&line);
        strbuf_append(&line, "    .double ");
        strbuf_append(&line, cg.floats.items[i].text);
        text_line(&cg, strbuf_cstr(&line));
        strbuf_free(&line);
    }

    doc->text = co1_strdup(strbuf_cstr(&cg.text));

    /* stack slots (variables first, then temporaries) */
    for (size_t i = 0; i < cg.sym.len; i++) {
        CGEntry *e = &cg.sym.items[i];
        if (e->is_var) {
            AsmSlot s;
            s.name = co1_strdup(e->name);
            s.offset = e->offset;
            s.size = e->size;
            AsmSlotList_push(&doc->slots, s);
        }
    }
    for (size_t i = 0; i < cg.sym.len; i++) {
        CGEntry *e = &cg.sym.items[i];
        if (!e->is_var) {
            AsmSlot s;
            s.name = co1_strdup(e->name);
            s.offset = e->offset;
            s.size = e->size;
            AsmSlotList_push(&doc->slots, s);
        }
    }

    /* cleanup */
    strbuf_free(&cg.text);
    free(cg.pending_label);
    for (size_t i = 0; i < cg.sym.len; i++) {
        free(cg.sym.items[i].name);
    }
    free(cg.sym.items);
    for (size_t i = 0; i < cg.strings.len; i++) {
        free(cg.strings.items[i].text);
        free(cg.strings.items[i].label);
    }
    free(cg.strings.items);
    for (size_t i = 0; i < cg.floats.len; i++) {
        free(cg.floats.items[i].text);
        free(cg.floats.items[i].label);
    }
    free(cg.floats.items);
}

void asm_doc_free(AsmDoc *doc) {
    for (size_t i = 0; i < doc->instructions.len; i++) {
        AsmInstruction *ins = &doc->instructions.items[i];
        free(ins->mnemonic);
        for (size_t j = 0; j < ins->noperands; j++) {
            free(ins->operands[j]);
        }
        free(ins->operands);
        free(ins->comment);
    }
    AsmInstructionList_free(&doc->instructions);
    for (size_t i = 0; i < doc->slots.len; i++) {
        free(doc->slots.items[i].name);
    }
    AsmSlotList_free(&doc->slots);
    free(doc->text);
    doc->text = NULL;
}
