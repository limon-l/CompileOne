/* ============================================================
   Code generation phase: TAC -> x86-64 assembly (AT&T syntax).

   Strategy (kept deliberately simple for the study language):

     - Every variable and temporary owns a fixed stack slot. Slots
       are allocated downward from %rbp: 4 bytes for int/bool/char,
       8 bytes for float and for pointers (arrays, strings, objects).
     - Integer expressions evaluate in %eax (32-bit), float
       expressions in %xmm0 (scalar double), pointers in %rax.
     - Integer division uses the idivl sequence (%edx:%eax),
       modulo takes the remainder from %edx.
     - Comparisons materialise a 0/1 integer with setcc.
     - print/read map onto calls to the C runtime (printf/scanf)
       with format strings emitted in .rodata.
     - Functions use a simple stack calling convention: arguments
       are pushed right-to-left by the caller and read back at
       (%rbp) + 16 + 8*n by the callee. Return values travel in
       %eax / %xmm0 / %rax.

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
    const char *type;       /* static: "int" | "float" | "bool" | "char" | "ptr" */
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

static void cg_sym_free(CGSym *s) {
    for (size_t i = 0; i < s->len; i++) {
        free(s->items[i].name);
    }
    free(s->items);
    memset(s, 0, sizeof(*s));
}

static CGEntry *cg_add(CGSym *s, const char *name, int is_var, const char *type,
                       int *next_offset) {
    if (s->len == s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 16;
        s->items = (CGEntry *)realloc(s->items, nc * sizeof(CGEntry));
        s->cap = nc;
    }
    CGEntry *e = &s->items[s->len];
    int size = ((type && strcmp(type, "float") == 0) ||
                (type && strcmp(type, "ptr") == 0)) ? 8 : 4;
    *next_offset -= size;
    e->name = co1_strdup(name);
    e->is_var = is_var;
    e->type = type;
    e->offset = *next_offset;
    e->size = size;
    s->len++;
    return e;
}

/* ---------------------------------------------------------------- function table */

typedef struct CGFunc {
    char *label;            /* owned */
    const char *ret;        /* static: "int" | "float" | "ptr" */
} CGFunc;

typedef struct CGTable {
    CGFunc *items;
    size_t len;
    size_t cap;
} CGTable;

static void func_add(CGTable *t, const char *label, const char *ret) {
    if (t->len == t->cap) {
        size_t nc = t->cap ? t->cap * 2 : 8;
        t->items = (CGFunc *)realloc(t->items, nc * sizeof(CGFunc));
        t->cap = nc;
    }
    t->items[t->len].label = co1_strdup(label);
    t->items[t->len].ret = ret;
    t->len++;
}

static const char *func_ret(CGTable *t, const char *label) {
    for (size_t i = 0; i < t->len; i++) {
        if (strcmp(t->items[i].label, label) == 0) {
            return t->items[i].ret;
        }
    }
    return "int";
}

static void func_table_free(CGTable *t) {
    for (size_t i = 0; i < t->len; i++) {
        free(t->items[i].label);
    }
    free(t->items);
    memset(t, 0, sizeof(*t));
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
    CGTable funcs;
    CGLitList strings;
    CGLitList floats;
    int next_string;
    int next_float;
    int next_offset;        /* grows downward from 0 */
    AsmDoc *doc;
    StrBuf text;
    int addr;
    char *pending_label;    /* owned */
    int cur_func_is_main;
    int in_func;
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

static int type_is_ptr(const char *type) {
    if (!type) {
        return 0;
    }
    return strcmp(type, "ptr") == 0;
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

static int cg_is_ptr(CG *cg, const char *name) {
    return type_is_ptr(cg_type_of(cg, name));
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

/* mov  value -> %eax / %xmm0 / %rax (by type) */
static void asm_load(CG *cg, const char *name) {
    char *src = cg_src(cg, name);
    if (cg_is_float(cg, name)) {
        asm_insn(cg, "movsd", src, "%xmm0", "data-move", NULL);
    } else if (cg_is_ptr(cg, name)) {
        asm_insn(cg, "movq", src, "%rax", "data-move", NULL);
    } else {
        asm_insn(cg, "movl", src, "%eax", "data-move", NULL);
    }
    free(src);
}

/* mov  %eax / %xmm0 / %rax -> result slot (by result type) */
static void asm_store(CG *cg, const char *result) {
    char *dst = cg_src(cg, result);
    if (cg_is_float(cg, result)) {
        asm_insn(cg, "movsd", "%xmm0", dst, "data-move", NULL);
    } else if (cg_is_ptr(cg, result)) {
        asm_insn(cg, "movq", "%rax", dst, "data-move", NULL);
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

/* ---------------------------------------------------------------- quads: expressions */

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

static void cg_not(CG *cg, const IrQuad *q) {
    asm_normalize_bool_rax(cg, q->arg1);
    asm_insn(cg, "xorl", "$1", "%eax", "arith", "a ^ 1 flips 0<->1");
    asm_store(cg, q->result);
}

/* ---------------------------------------------------------------- quads: memory */

/* Load the address of a literal into %rax and store to `result`. */
static void cg_string(CG *cg, const IrQuad *q) {
    CGLit *lit = lit_find(&cg->strings, q->arg1);
    const char *ref = lit ? lit->label : ".LC0";
    StrBuf lbl;
    strbuf_init(&lbl);
    strbuf_append(&lbl, ref);
    strbuf_append(&lbl, "(%rip)");
    asm_insn(cg, "leaq", strbuf_cstr(&lbl), "%rax", "data-move", "string literal");
    strbuf_free(&lbl);
    asm_store(cg, q->result);
}

static void cg_alloc(CG *cg, const IrQuad *q) {
    /* calloc(count, size) — objects get 8-byte fields, float arrays 8,
       int arrays 4, char arrays 1 */
    char count[32];
    snprintf(count, sizeof(count), "$%s", q->arg2 ? q->arg2 : "0");
    asm_insn(cg, "movq", count, "%rdi", "data-move", "calloc nmemb");
    const char *t = q->arg1 ? q->arg1 : "int";
    int size;
    if (strcmp(t, "float") == 0) {
        size = 8;
    } else if (strcmp(t, "char") == 0) {
        size = 1;
    } else if (strcmp(t, "int") == 0) {
        size = 4;
    } else {
        size = 8; /* class instance: 8 bytes per field */
    }
    char sz[16];
    snprintf(sz, sizeof(sz), "$%d", size);
    asm_insn(cg, "movq", sz, "%rsi", "data-move", "calloc size");
    asm_insn(cg, "call", "calloc", NULL, "call", "heap allocate");
    asm_store(cg, q->result);
}

/* %rcx = base + index * scale, then load/store the element. */
static void cg_arr_access(CG *cg, const IrQuad *q, const char *base, const char *index,
                          int scale, const char *load, const char *store) {
    char *i = cg_src(cg, index);
    asm_insn(cg, "movl", i, "%eax", "data-move", "array index");
    asm_insn(cg, "cltq", NULL, NULL, "arith", "sign-extend index -> %rax");
    char sc[16];
    snprintf(sc, sizeof(sc), "$%d", scale);
    asm_insn(cg, "imulq", sc, "%rax", "arith", "scale index");
    char *b = cg_src(cg, base);
    asm_insn(cg, "movq", b, "%rcx", "data-move", "array base");
    asm_insn(cg, "addq", "%rax", "%rcx", "arith", "base + index*scale");
    asm_insn(cg, load, "(%rcx)", "%rax", "data-move", NULL);
    free(i);
    free(b);
}

static void cg_arr_load(CG *cg, const IrQuad *q) {
    cg_arr_access(cg, q, q->arg1, q->arg2, 4, "movl", NULL);
    asm_store(cg, q->result);
}

static void cg_arr_loadf(CG *cg, const IrQuad *q) {
    cg_arr_access(cg, q, q->arg1, q->arg2, 8, "movsd", NULL);
    /* value is in %xmm0 for float loads */
    asm_store(cg, q->result);
}

static void cg_arr_store(CG *cg, const IrQuad *q) {
    cg_arr_access(cg, q, q->arg1, q->arg2, 4, "movl", NULL);
    char *v = cg_src(cg, q->result);
    asm_insn(cg, "movl", v, "(%rcx)", "data-move", "store element");
    free(v);
}

static void cg_arr_storef(CG *cg, const IrQuad *q) {
    cg_arr_access(cg, q, q->arg1, q->arg2, 8, "movsd", NULL);
    char *v = cg_src(cg, q->result);
    asm_insn(cg, "movsd", v, "(%rcx)", "data-move", "store float element");
    free(v);
}

/* Object fields: constant offset, 8-byte slots. The member's address is
   computed once in %rcx (stride 8). */
static char *cg_member_addr(CG *cg, const IrQuad *q) {
    char *b = cg_src(cg, q->arg1);
    asm_insn(cg, "movq", b, "%rcx", "data-move", "object base");
    int off = q->arg2 ? atoi(q->arg2) : 0;
    StrBuf sb;
    strbuf_init(&sb);
    strbuf_append_int(&sb, off * 8);
    strbuf_append(&sb, "(%rcx)");
    char *out = co1_strdup(strbuf_cstr(&sb));
    strbuf_free(&sb);
    free(b);
    return out;
}

static void cg_member_load(CG *cg, const IrQuad *q) {
    char *addr = cg_member_addr(cg, q);
    asm_insn(cg, "movl", addr, "%eax", "data-move", "load int field");
    asm_store(cg, q->result);
    free(addr);
}

static void cg_member_loadf(CG *cg, const IrQuad *q) {
    char *addr = cg_member_addr(cg, q);
    asm_insn(cg, "movsd", addr, "%xmm0", "data-move", "load float field");
    asm_store(cg, q->result);
    free(addr);
}

static void cg_member_loadq(CG *cg, const IrQuad *q) {
    char *addr = cg_member_addr(cg, q);
    asm_insn(cg, "movq", addr, "%rax", "data-move", "load pointer field");
    asm_store(cg, q->result);
    free(addr);
}

static void cg_member_store(CG *cg, const IrQuad *q) {
    char *addr = cg_member_addr(cg, q);
    char *v = cg_src(cg, q->result);
    asm_insn(cg, "movl", v, "%eax", "data-move", "store int field");
    asm_insn(cg, "movl", "%eax", addr, "data-move", NULL);
    free(addr);
    free(v);
}

static void cg_member_storef(CG *cg, const IrQuad *q) {
    char *addr = cg_member_addr(cg, q);
    char *v = cg_src(cg, q->result);
    asm_insn(cg, "movsd", v, "%xmm0", "data-move", "store float field");
    asm_insn(cg, "movsd", "%xmm0", addr, "data-move", NULL);
    free(addr);
    free(v);
}

static void cg_member_storeq(CG *cg, const IrQuad *q) {
    char *addr = cg_member_addr(cg, q);
    char *v = cg_src(cg, q->result);
    asm_insn(cg, "movq", v, "%rax", "data-move", "store pointer field");
    asm_insn(cg, "movq", "%rax", addr, "data-move", NULL);
    free(addr);
    free(v);
}

/* ---------------------------------------------------------------- quads: calls */

typedef struct ArgBuf {
    const char **items;
    size_t len;
    size_t cap;
} ArgBuf;

static void argbuf_push(ArgBuf *a, const char *v) {
    if (a->len == a->cap) {
        size_t nc = a->cap ? a->cap * 2 : 4;
        a->items = (const char **)realloc(a->items, nc * sizeof(const char *));
        a->cap = nc;
    }
    a->items[a->len++] = v;
}

static void argbuf_free(ArgBuf *a) {
    free(a->items);
    memset(a, 0, sizeof(*a));
}

/* push one value (by type) onto the stack */
static void cg_push_value(CG *cg, const char *v) {
    if (cg_is_float(cg, v)) {
        char *src = cg_src(cg, v);
        asm_insn(cg, "movsd", src, "%xmm0", "data-move", "float argument");
        asm_insn(cg, "subq", "$8", "%rsp", "stack", "reserve argument slot");
        asm_insn(cg, "movsd", "%xmm0", "(%rsp)", "data-move", NULL);
        free(src);
    } else if (cg_is_ptr(cg, v)) {
        char *src = cg_src(cg, v);
        asm_insn(cg, "movq", src, "%rax", "data-move", "pointer argument");
        asm_insn(cg, "pushq", "%rax", NULL, "stack", NULL);
        free(src);
    } else {
        char *src = cg_src(cg, v);
        asm_insn(cg, "movl", src, "%eax", "data-move", "argument");
        asm_insn(cg, "pushq", "%rax", NULL, "stack", NULL);
        free(src);
    }
}

static void cg_call(CG *cg, const IrQuad *q, ArgBuf *pending) {
    /* push args right-to-left */
    for (size_t i = pending->len; i > 0; i--) {
        cg_push_value(cg, pending->items[i - 1]);
    }
    argbuf_free(pending);

    asm_insn(cg, "call", q->arg1, NULL, "call", NULL);
    long nargs = q->arg2 ? atol(q->arg2) : 0;
    if (nargs > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "$%ld", nargs * 8);
        asm_insn(cg, "addq", buf, "%rsp", "stack", "pop arguments");
    }
    if (q->result) {
        asm_store(cg, q->result);
    }
}

/* ---------------------------------------------------------------- quads: I/O */

/* mini-c `print`: newline-terminated output */
static void cg_print_newline(CG *cg, const IrQuad *q) {
    if (cg_is_float(cg, q->arg1)) {
        char *a = cg_src(cg, q->arg1);
        asm_insn(cg, "movsd", a, "%xmm0", "data-move", NULL);
        asm_insn(cg, "leaq", ".Lfmt_float(%rip)", "%rdi", "data-move",
                 "printf format");
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

/* mini-c `print_str`: newline-terminated output */
static void cg_print_str_newline(CG *cg, const IrQuad *q) {
    CGLit *lit = lit_find(&cg->strings, q->arg1);
    const char *ref = lit ? lit->label : ".LC0";
    StrBuf lbl;
    strbuf_init(&lbl);
    strbuf_append(&lbl, ref);
    strbuf_append(&lbl, "(%rip)");
    asm_insn(cg, "leaq", strbuf_cstr(&lbl), "%rsi", "data-move",
             "printf string arg");
    asm_insn(cg, "leaq", ".Lfmt_str(%rip)", "%rdi", "data-move", "printf format");
    asm_insn(cg, "call", "printf", NULL, "call", NULL);
    strbuf_free(&lbl);
}

static void cg_print_item(CG *cg, const IrQuad *q) {
    if (cg_is_float(cg, q->arg1)) {
        char *a = cg_src(cg, q->arg1);
        asm_insn(cg, "movsd", a, "%xmm0", "data-move", NULL);
        asm_insn(cg, "leaq", ".Lfmt_float_plain(%rip)", "%rdi", "data-move",
                 "printf format");
        asm_insn(cg, "call", "printf", NULL, "call", NULL);
        free(a);
        return;
    }
    char *a = cg_src(cg, q->arg1);
    asm_insn(cg, "movl", a, "%esi", "data-move", "printf value arg");
    asm_insn(cg, "leaq", ".Lfmt_int_plain(%rip)", "%rdi", "data-move",
             "printf format");
    asm_insn(cg, "call", "printf", NULL, "call", NULL);
    free(a);
}

static void cg_print_str_addr(CG *cg, const IrQuad *q) {
    char *a = cg_src(cg, q->arg1);
    asm_insn(cg, "movq", a, "%rsi", "data-move", "printf string arg");
    asm_insn(cg, "leaq", ".Lfmt_str_plain(%rip)", "%rdi", "data-move",
             "printf format");
    asm_insn(cg, "call", "printf", NULL, "call", NULL);
    free(a);
}

static void cg_print_str_item(CG *cg, const IrQuad *q) {
    CGLit *lit = lit_find(&cg->strings, q->arg1);
    const char *ref = lit ? lit->label : ".LC0";
    StrBuf lbl;
    strbuf_init(&lbl);
    strbuf_append(&lbl, ref);
    strbuf_append(&lbl, "(%rip)");
    asm_insn(cg, "leaq", strbuf_cstr(&lbl), "%rsi", "data-move",
             "printf string arg");
    asm_insn(cg, "leaq", ".Lfmt_str_plain(%rip)", "%rdi", "data-move",
             "printf format");
    asm_insn(cg, "call", "printf", NULL, "call", NULL);
    strbuf_free(&lbl);
}

static void cg_println(CG *cg) {
    asm_insn(cg, "leaq", ".Lfmt_newline(%rip)", "%rdi", "data-move", "println");
    asm_insn(cg, "call", "puts", NULL, "call", NULL);
}

static void cg_read(CG *cg, const IrQuad *q) {
    char *dst = cg_src(cg, q->result);
    asm_insn(cg, "leaq", dst, "%rsi", "data-move", "scanf destination");
    asm_insn(cg, "leaq", ".Lfmt_scan_int(%rip)", "%rdi", "data-move",
             "scanf format");
    asm_insn(cg, "call", "scanf", NULL, "call", NULL);
    free(dst);
}

static void cg_read_str(CG *cg, const IrQuad *q) {
    char *src = cg_src(cg, q->result);
    asm_insn(cg, "movq", src, "%rsi", "data-move", "scanf destination buffer");
    asm_insn(cg, "leaq", ".Lfmt_scan_str(%rip)", "%rdi", "data-move",
             "scanf format");
    asm_insn(cg, "call", "scanf", NULL, "call", NULL);
    free(src);
}

static void cg_return(CG *cg, const IrQuad *q) {
    if (q->arg1) {
        if (cg_is_float(cg, q->arg1)) {
            char *a = cg_src(cg, q->arg1);
            asm_insn(cg, "movsd", a, "%xmm0", "data-move", "return value");
            free(a);
        } else if (cg_is_ptr(cg, q->arg1)) {
            char *a = cg_src(cg, q->arg1);
            asm_insn(cg, "movq", a, "%rax", "data-move", "return value");
            free(a);
        } else {
            char *a = cg_src(cg, q->arg1);
            asm_insn(cg, "movl", a, "%eax", "data-move", "return value");
            free(a);
        }
    } else {
        asm_insn(cg, "movl", "$0", "%eax", "data-move", "return 0");
    }
    asm_insn(cg, "leave", NULL, NULL, "stack", "tear down frame");
    asm_insn(cg, "ret", NULL, NULL, "control", NULL);
}

/* ---------------------------------------------------------------- quad dispatch */

static void cg_quad(CG *cg, const IrQuad *q, ArgBuf *pending) {
    if (strcmp(q->op, "declare") == 0) {
        /* slots were allocated in the layout pass; nothing to emit */
        return;
    }
    if (strcmp(q->op, "param") == 0) {
        /* handled in the prologue emission (function boundary) */
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
        cg_not(cg, q);
        return;
    }
    if (strcmp(q->op, "string") == 0) {
        cg_string(cg, q);
        return;
    }
    if (strcmp(q->op, "alloc") == 0) {
        cg_alloc(cg, q);
        return;
    }
    if (strcmp(q->op, "arr_load") == 0) {
        cg_arr_load(cg, q);
        return;
    }
    if (strcmp(q->op, "arr_loadf") == 0) {
        cg_arr_loadf(cg, q);
        return;
    }
    if (strcmp(q->op, "arr_store") == 0) {
        cg_arr_store(cg, q);
        return;
    }
    if (strcmp(q->op, "arr_storef") == 0) {
        cg_arr_storef(cg, q);
        return;
    }
    if (strcmp(q->op, "member_load") == 0) {
        cg_member_load(cg, q);
        return;
    }
    if (strcmp(q->op, "member_loadf") == 0) {
        cg_member_loadf(cg, q);
        return;
    }
    if (strcmp(q->op, "member_loadq") == 0) {
        cg_member_loadq(cg, q);
        return;
    }
    if (strcmp(q->op, "member_store") == 0) {
        cg_member_store(cg, q);
        return;
    }
    if (strcmp(q->op, "member_storef") == 0) {
        cg_member_storef(cg, q);
        return;
    }
    if (strcmp(q->op, "member_storeq") == 0) {
        cg_member_storeq(cg, q);
        return;
    }
    if (strcmp(q->op, "arg") == 0) {
        argbuf_push(pending, q->arg1);
        return;
    }
    if (strcmp(q->op, "call") == 0) {
        cg_call(cg, q, pending);
        return;
    }
    if (strcmp(q->op, "concat") == 0) {
        asm_insn(cg, "nop", NULL, NULL, "optimization",
                 "string concatenation not lowered on x86-64");
        return;
    }
    if (strcmp(q->op, "print") == 0) {
        cg_print_newline(cg, q);
        return;
    }
    if (strcmp(q->op, "print_str") == 0) {
        cg_print_str_newline(cg, q);
        return;
    }
    if (strcmp(q->op, "print_item") == 0 ||
        strcmp(q->op, "print_float_item") == 0) {
        cg_print_item(cg, q);
        return;
    }
    if (strcmp(q->op, "print_str_item") == 0) {
        cg_print_str_item(cg, q);
        return;
    }
    if (strcmp(q->op, "print_str_addr") == 0) {
        cg_print_str_addr(cg, q);
        return;
    }
    if (strcmp(q->op, "println") == 0) {
        cg_println(cg);
        return;
    }
    if (strcmp(q->op, "read") == 0) {
        cg_read(cg, q);
        return;
    }
    if (strcmp(q->op, "read_str") == 0) {
        cg_read_str(cg, q);
        return;
    }
    if (strcmp(q->op, "return") == 0) {
        cg_return(cg, q);
        return;
    }
}

/* ---------------------------------------------------------------- type inference */

/* Return the IR type a declared type string maps to for codegen. */
static const char *decl_to_cg_type(const char *t) {
    if (!t) {
        return "int";
    }
    if (strcmp(t, "float") == 0) {
        return "float";
    }
    if (strcmp(t, "void") == 0) {
        return "int";
    }
    if (strchr(t, '[') != NULL || strcmp(t, "str") == 0 ||
        strcmp(t, "obj") == 0 || strcmp(t, "char") == 0) {
        return "ptr";
    }
    return "int";
}

static const char *ret_to_cg_type(const char *t) {
    if (!t) {
        return "int";
    }
    if (strcmp(t, "float") == 0) {
        return "float";
    }
    if (strcmp(t, "void") == 0) {
        return "int";
    }
    return "ptr";
}

/* ---------------------------------------------------------------- literal collection */

/* Collect the string and float literals referenced by the quad list into
   the rodata pools (.LCn strings, .LFn doubles). */
static void collect_literals(CG *cg, const IrQuadList *quads) {
    for (size_t i = 0; i < quads->len; i++) {
        const IrQuad *q = &quads->items[i];
        if (strcmp(q->op, "string") == 0 || strcmp(q->op, "print_str") == 0 ||
            strcmp(q->op, "print_str_item") == 0) {
            if (q->arg1 && !lit_find(&cg->strings, q->arg1)) {
                lit_add(&cg->strings, q->arg1, ".LC", cg->next_string++);
            }
            continue;
        }
        const char *operands[2] = {q->arg1, q->arg2};
        for (int k = 0; k < 2; k++) {
            const char *v = operands[k];
            if (v && is_float_literal(v) && !lit_find(&cg->floats, v)) {
                lit_add(&cg->floats, v, ".LF", cg->next_float++);
            }
        }
    }
}

/* ---------------------------------------------------------------- layout pass */

static void layout_quad(CG *cg, const IrQuad *q, const IrQuadList *quads, size_t i) {
    if (strcmp(q->op, "declare") == 0) {
        cg_add(&cg->sym, q->result, 1, decl_to_cg_type(q->arg1), &cg->next_offset);
        return;
    }
    if (strcmp(q->op, "param") == 0) {
        cg_add(&cg->sym, q->result, 1, decl_to_cg_type(q->arg1), &cg->next_offset);
        return;
    }
    if (strcmp(q->op, "string") == 0 || strcmp(q->op, "alloc") == 0 ||
        strcmp(q->op, "concat") == 0) {
        if (q->result && !cg_find(&cg->sym, q->result)) {
            cg_add(&cg->sym, q->result, 0, "ptr", &cg->next_offset);
        }
        return;
    }
    if (strcmp(q->op, "call") == 0 && q->result &&
        !cg_find(&cg->sym, q->result)) {
        const char *r = func_ret(&cg->funcs, q->arg1);
        cg_add(&cg->sym, q->result, 0, ret_to_cg_type(r), &cg->next_offset);
        return;
    }
    if (strcmp(q->op, "arr_load") == 0 || strcmp(q->op, "member_load") == 0) {
        if (q->result && !cg_find(&cg->sym, q->result)) {
            cg_add(&cg->sym, q->result, 0, "int", &cg->next_offset);
        }
        return;
    }
    if (strcmp(q->op, "arr_loadf") == 0 || strcmp(q->op, "member_loadf") == 0) {
        if (q->result && !cg_find(&cg->sym, q->result)) {
            cg_add(&cg->sym, q->result, 0, "float", &cg->next_offset);
        }
        return;
    }
    if (strcmp(q->op, "member_loadq") == 0) {
        if (q->result && !cg_find(&cg->sym, q->result)) {
            cg_add(&cg->sym, q->result, 0, "ptr", &cg->next_offset);
        }
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
    (void)quads;
    (void)i;
}

/* ---------------------------------------------------------------- rodata */

static void emit_rodata(CG *cg) {
    text_line(cg, "    .section .rodata");
    text_line(cg, ".Lfmt_int:");
    text_line(cg, "    .string \"%d\\n\"");
    text_line(cg, ".Lfmt_float:");
    text_line(cg, "    .string \"%g\\n\"");
    text_line(cg, ".Lfmt_str:");
    text_line(cg, "    .string \"%s\\n\"");
    text_line(cg, ".Lfmt_int_plain:");
    text_line(cg, "    .string \"%d\"");
    text_line(cg, ".Lfmt_float_plain:");
    text_line(cg, "    .string \"%g\"");
    text_line(cg, ".Lfmt_str_plain:");
    text_line(cg, "    .string \"%s\"");
    text_line(cg, ".Lfmt_scan_int:");
    text_line(cg, "    .string \"%d\"");
    text_line(cg, ".Lfmt_scan_str:");
    text_line(cg, "    .string \"%s\"");
    text_line(cg, ".Lfmt_newline:");
    text_line(cg, "    .string \"\\n\"");
    for (size_t i = 0; i < cg->strings.len; i++) {
        StrBuf line;
        strbuf_init(&line);
        strbuf_append(&line, cg->strings.items[i].label);
        strbuf_append(&line, ":");
        text_line(cg, strbuf_cstr(&line));
        strbuf_clear(&line);
        strbuf_append(&line, "    .string ");
        strbuf_append(&line, cg->strings.items[i].text);
        text_line(cg, strbuf_cstr(&line));
        strbuf_free(&line);
    }
    for (size_t i = 0; i < cg->floats.len; i++) {
        StrBuf line;
        strbuf_init(&line);
        strbuf_append(&line, cg->floats.items[i].label);
        strbuf_append(&line, ":");
        text_line(cg, strbuf_cstr(&line));
        strbuf_clear(&line);
        strbuf_append(&line, "    .double ");
        strbuf_append(&line, cg->floats.items[i].text);
        text_line(cg, strbuf_cstr(&line));
        strbuf_free(&line);
    }
}

static void emit_stack_slots(CG *cg, AsmDoc *doc) {
    doc->stack_size = -cg->next_offset;
    if (doc->stack_size % 8 != 0) {
        doc->stack_size += 8 - (doc->stack_size % 8);
    }
    for (size_t i = 0; i < cg->sym.len; i++) {
        CGEntry *e = &cg->sym.items[i];
        if (e->is_var) {
            AsmSlot s;
            s.name = co1_strdup(e->name);
            s.offset = e->offset;
            s.size = e->size;
            AsmSlotList_push(&doc->slots, s);
        }
    }
    for (size_t i = 0; i < cg->sym.len; i++) {
        CGEntry *e = &cg->sym.items[i];
        if (!e->is_var) {
            AsmSlot s;
            s.name = co1_strdup(e->name);
            s.offset = e->offset;
            s.size = e->size;
            AsmSlotList_push(&doc->slots, s);
        }
    }
}

static void cg_prologue(CG *cg) {
    asm_insn(cg, "pushq", "%rbp", NULL, "stack", "save caller frame");
    asm_insn(cg, "movq", "%rsp", "%rbp", "stack", "establish frame");
    {
        StrBuf sub;
        strbuf_init(&sub);
        strbuf_append(&sub, "$");
        strbuf_append_int(&sub, cg->doc->stack_size);
        char *sz = co1_strdup(strbuf_cstr(&sub));
        strbuf_free(&sub);
        asm_insn(cg, "subq", sz, "%rsp", "stack", "allocate locals");
        free(sz);
    }
}

static void cg_epilogue(CG *cg) {
    asm_insn(cg, "movl", "$0", "%eax", "data-move", "implicit return 0");
    asm_insn(cg, "leave", NULL, NULL, "stack", "tear down frame");
    asm_insn(cg, "ret", NULL, NULL, "control", NULL);
}

/* Emit a function body given the quads in [start, end). `start` points at
   the "func" quad, `end` at the matching "endfunc" (exclusive). */
static void emit_function(CG *cg, const IrQuadList *quads, size_t start, size_t end) {
    const IrQuad *func_q = &quads->items[start];
    const char *name = func_q->arg2;   /* method/function name */
    const char *label = func_q->result; /* label to call */

    /* layout this function's stack */
    cg->next_offset = 0;
    for (size_t i = start + 1; i < end; i++) {
        layout_quad(cg, &quads->items[i], quads, i);
    }
    emit_stack_slots(cg, cg->doc);

    cg->cur_func_is_main = name && strcmp(name, "main") == 0;

    if (cg->cur_func_is_main) {
        text_line(cg, "    .globl main");
        asm_label(cg, "main");
    } else {
        asm_label(cg, label);
    }
    cg_prologue(cg);

    /* copy arguments from the stack into their local slots */
    int param_no = 0;
    for (size_t i = start + 1; i < end; i++) {
        const IrQuad *q = &quads->items[i];
        if (strcmp(q->op, "param") != 0) {
            continue;
        }
        StrBuf src;
        strbuf_init(&src);
        strbuf_append_int(&src, 16 + param_no * 8);
        strbuf_append(&src, "(%rbp)");
        const char *ptype = cg_type_of(cg, q->result);
        if (strcmp(ptype, "float") == 0) {
            asm_insn(cg, "movsd", strbuf_cstr(&src), "%xmm0", "data-move",
                     "argument");
            asm_store(cg, q->result);
        } else if (strcmp(ptype, "ptr") == 0) {
            asm_insn(cg, "movq", strbuf_cstr(&src), "%rax", "data-move",
                     "argument");
            asm_store(cg, q->result);
        } else {
            asm_insn(cg, "movl", strbuf_cstr(&src), "%eax", "data-move",
                     "argument");
            asm_store(cg, q->result);
        }
        strbuf_free(&src);
        param_no++;
    }

    /* body */
    ArgBuf pending = {0};
    for (size_t i = start + 1; i < end; i++) {
        const IrQuad *q = &quads->items[i];
        if (strcmp(q->op, "param") == 0 || strcmp(q->op, "endfunc") == 0) {
            continue;
        }
        cg_quad(cg, q, &pending);
    }
    argbuf_free(&pending);

    cg_epilogue(cg);

    cg_sym_free(&cg->sym);
}

/* ---------------------------------------------------------------- public API */

void codegen_generate(const IrQuadList *quads, AsmDoc *doc) {
    CG cg;
    memset(&cg, 0, sizeof(cg));
    cg.doc = doc;
    strbuf_init(&cg.text);

    doc->instructions = (AsmInstructionList){0};
    doc->slots = (AsmSlotList){0};
    doc->stack_size = 0;
    doc->text = NULL;

    /* pass 0: collect function signatures (handles forward references) */
    for (size_t i = 0; i < quads->len; i++) {
        const IrQuad *q = &quads->items[i];
        if (strcmp(q->op, "func") == 0) {
            func_add(&cg.funcs, q->result, q->arg1);
        }
    }

    collect_literals(&cg, quads);

    int native = cg.funcs.len > 0;

    text_line(&cg, "    .text");
    if (!native) {
        /* mini-c: the whole list is one implicit `main` function */
        cg.next_offset = 0;
        for (size_t i = 0; i < quads->len; i++) {
            layout_quad(&cg, &quads->items[i], quads, i);
        }
        emit_stack_slots(&cg, doc);
        text_line(&cg, "    .globl main");
        asm_label(&cg, "main");
        cg_prologue(&cg);
        ArgBuf pending = {0};
        for (size_t i = 0; i < quads->len; i++) {
            cg_quad(&cg, &quads->items[i], &pending);
        }
        argbuf_free(&pending);
        cg_epilogue(&cg);
    } else {
        size_t i = 0;
        while (i < quads->len) {
            if (strcmp(quads->items[i].op, "func") == 0) {
                size_t j = i + 1;
                while (j < quads->len && strcmp(quads->items[j].op, "endfunc") != 0) {
                    j++;
                }
                emit_function(&cg, quads, i, j);
                i = j + 1;
            } else {
                i++;
            }
        }
    }

    emit_rodata(&cg);

    doc->text = co1_strdup(strbuf_cstr(&cg.text));

    strbuf_free(&cg.text);
    free(cg.pending_label);
    cg_sym_free(&cg.sym);
    func_table_free(&cg.funcs);
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
