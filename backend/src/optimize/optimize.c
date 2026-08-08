/* ============================================================
   Optimization phase: local passes over the TAC quad list.

   Pass 1 — Constant Folding:  evaluate arithmetic/comparison/unary
     operations whose operands are integer literals at compile time
     (`t2 = 2 + 3` becomes `t2 = 5`). div/mod by zero are left alone
     so semantics are preserved.

   Pass 2 — Dead Code Elimination:  remove quads that compute a
     temporary which nothing ever reads again, iterated to a fixpoint
     so chains of dead temporaries collapse.

   Each pass records the quads it touched so the UI can render an
   evidence panel with before/after listings.
   ============================================================ */

#include "optimize.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "strbuf.h"

DARRAY_DEFINE(OptPassRecord, OptPassRecordList)

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

static long long parse_literal(const char *s) {
    return strtoll(s, NULL, 10);
}

static int is_arith_op(const char *op) {
    return strcmp(op, "add") == 0 || strcmp(op, "sub") == 0 ||
           strcmp(op, "mul") == 0 || strcmp(op, "div") == 0 ||
           strcmp(op, "mod") == 0;
}

static int is_compare_op(const char *op) {
    return strcmp(op, "lt") == 0 || strcmp(op, "le") == 0 ||
           strcmp(op, "gt") == 0 || strcmp(op, "ge") == 0 ||
           strcmp(op, "eq") == 0 || strcmp(op, "ne") == 0;
}

static int is_boolean_op(const char *op) {
    return strcmp(op, "and") == 0 || strcmp(op, "or") == 0;
}

static IrQuad ir_quad_dup(const IrQuad *q) {
    IrQuad copy;
    copy.index = q->index;
    copy.op = q->op;
    copy.arg1 = q->arg1 ? co1_strdup(q->arg1) : NULL;
    copy.arg2 = q->arg2 ? co1_strdup(q->arg2) : NULL;
    copy.result = q->result ? co1_strdup(q->result) : NULL;
    return copy;
}

static void ir_quad_swap(IrQuad *dst, IrQuad *src) {
    IrQuad tmp = *dst;
    *dst = *src;
    *src = tmp;
}

/* ---------------------------------------------------------------- pass 1: constant folding */

static int fold_quad(IrQuad *q, char *reason, size_t reason_len) {
    if (is_arith_op(q->op) &&
        is_integer_literal(q->arg1) && is_integer_literal(q->arg2)) {
        long long a = parse_literal(q->arg1);
        long long b = parse_literal(q->arg2);
        long long r = 0;
        if (strcmp(q->op, "add") == 0) {
            r = a + b;
        } else if (strcmp(q->op, "sub") == 0) {
            r = a - b;
        } else if (strcmp(q->op, "mul") == 0) {
            r = a * b;
        } else if (strcmp(q->op, "div") == 0) {
            if (b == 0) {
                return 0;
            }
            r = a / b;
        } else if (strcmp(q->op, "mod") == 0) {
            if (b == 0) {
                return 0;
            }
            r = a % b;
        }
        snprintf(reason, reason_len, "%s = %I64d %s %I64d evaluated to %I64d",
                 q->result ? q->result : "?", a, q->op, b, r);
        free(q->arg2);
        q->arg2 = NULL;
        char buf[32];
        snprintf(buf, sizeof(buf), "%I64d", r);
        free(q->arg1);
        q->arg1 = co1_strdup(buf);
        q->op = "assign";
        return 1;
    }
    if (is_compare_op(q->op) &&
        is_integer_literal(q->arg1) && is_integer_literal(q->arg2)) {
        long long a = parse_literal(q->arg1);
        long long b = parse_literal(q->arg2);
        int r = 0;
        if (strcmp(q->op, "lt") == 0)  r = a < b;
        else if (strcmp(q->op, "le") == 0) r = a <= b;
        else if (strcmp(q->op, "gt") == 0) r = a > b;
        else if (strcmp(q->op, "ge") == 0) r = a >= b;
        else if (strcmp(q->op, "eq") == 0) r = a == b;
        else if (strcmp(q->op, "ne") == 0) r = a != b;
        snprintf(reason, reason_len, "%s = %I64d %s %I64d evaluated to %d",
                 q->result ? q->result : "?", a, q->op, b, r);
        free(q->arg2);
        q->arg2 = NULL;
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", r);
        free(q->arg1);
        q->arg1 = co1_strdup(buf);
        q->op = "assign";
        return 1;
    }
    if (strcmp(q->op, "neg") == 0 && is_integer_literal(q->arg1)) {
        long long a = parse_literal(q->arg1);
        snprintf(reason, reason_len, "%s = -%I64d evaluated to %I64d",
                 q->result ? q->result : "?", a, -a);
        char buf[32];
        snprintf(buf, sizeof(buf), "%I64d", -a);
        free(q->arg1);
        q->arg1 = co1_strdup(buf);
        q->op = "assign";
        return 1;
    }
    if (strcmp(q->op, "not") == 0 && is_integer_literal(q->arg1)) {
        long long a = parse_literal(q->arg1);
        snprintf(reason, reason_len, "%s = !%I64d evaluated to %d",
                 q->result ? q->result : "?", a, !a);
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", !a);
        free(q->arg1);
        q->arg1 = co1_strdup(buf);
        q->op = "assign";
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- pass 2: dead code elimination */

static int is_temp_name(const char *s) {
    return s && s[0] == 't' && s[1] >= '0' && s[1] <= '9';
}

static int is_pure_op(const char *op) {
    return strcmp(op, "assign") == 0 || is_arith_op(op) ||
           is_compare_op(op) || is_boolean_op(op) ||
           strcmp(op, "neg") == 0 || strcmp(op, "not") == 0;
}

/* Does any quad after position `pos` reference `name`? */
static int referenced_later(const IrQuadList *quads, size_t pos, const char *name) {
    for (size_t i = pos + 1; i < quads->len; i++) {
        const IrQuad *q = &quads->items[i];
        if ((q->arg1 && strcmp(q->arg1, name) == 0) ||
            (q->arg2 && strcmp(q->arg2, name) == 0) ||
            (q->result && strcmp(q->result, name) == 0)) {
            return 1;
        }
    }
    return 0;
}

static int dce_pass(IrQuadList *quads, IrQuadList *removed) {
    /* Iterate to a fixpoint so dead temps that only feed other dead
       temps are removed too. */
    int any = 0;
    int again = 1;
    while (again) {
        again = 0;
        for (size_t i = 0; i < quads->len; i++) {
            const IrQuad *q = &quads->items[i];
            if (is_pure_op(q->op) && is_temp_name(q->result) &&
                !referenced_later(quads, i, q->result)) {
                IrQuad copy = ir_quad_dup(q);
                IrQuadList_push(removed, copy);
                /* swap with tail, pop */
                ir_quad_swap(&quads->items[i], &quads->items[quads->len - 1]);
                quads->len--;
                i--;
                any = 1;
                again = 1;
            }
        }
    }
    return any;
}

/* ---------------------------------------------------------------- public API */

void optimize(const IrQuadList *input, IrQuadList *output, OptReport *report) {
    /* working copy */
    IrQuadList work = {0};
    for (size_t i = 0; i < input->len; i++) {
        IrQuad copy = ir_quad_dup(&input->items[i]);
        IrQuadList_push(&work, copy);
    }

    report->before_count = (int)input->len;
    report->after_count = (int)work.len;
    report->reduction_pct = 0.0;
    report->passes = (OptPassRecordList){0};

    /* ---- pass 1: constant folding ---- */
    {
        OptPassRecord rec;
        rec.name = co1_strdup("Constant Folding");
        rec.applied = 0;
        rec.instructions_removed = 0;
        rec.before = (IrQuadList){0};
        rec.after = (IrQuadList){0};

        char reason[256];
        StrBuf examples;
        strbuf_init(&examples);
        int folded = 0;
        for (size_t i = 0; i < work.len; i++) {
            IrQuad orig = ir_quad_dup(&work.items[i]);
            if (fold_quad(&work.items[i], reason, sizeof(reason))) {
                if (!rec.applied) {
                    rec.applied = 1;
                }
                IrQuadList_push(&rec.before, orig);
                IrQuadList_push(&rec.after, ir_quad_dup(&work.items[i]));
                folded++;
                if (folded <= 4) {
                    if (strbuf_len(&examples) > 0) {
                        strbuf_append(&examples, "; ");
                    }
                    strbuf_append(&examples, reason);
                }
            } else {
                free(orig.arg1);
                free(orig.arg2);
                free(orig.result);
            }
        }
        rec.instructions_removed = folded;

        char expl[512];
        if (rec.applied) {
            snprintf(expl, sizeof(expl),
                     "Folded %d constant expression(s) at compile time "
                     "(%s). The value is fixed at build time so no "
                     "runtime arithmetic is emitted.", folded,
                     strbuf_len(&examples) > 0 ? strbuf_cstr(&examples) : "");
        } else {
            snprintf(expl, sizeof(expl),
                     "No constant expressions found; every operation "
                     "already involves a runtime value.");
        }
        rec.explanation = co1_strdup(expl);
        strbuf_free(&examples);

        OptPassRecordList_push(&report->passes, rec);
    }

    /* ---- pass 2: dead code elimination ---- */
    {
        OptPassRecord rec;
        rec.name = co1_strdup("Dead Code Elimination");
        rec.applied = 0;
        rec.instructions_removed = 0;
        rec.before = (IrQuadList){0};
        rec.after = (IrQuadList){0};

        int removed = dce_pass(&work, &rec.before);
        rec.applied = removed > 0;
        rec.instructions_removed = removed;

        char expl[256];
        if (removed > 0) {
            snprintf(expl, sizeof(expl),
                     "Removed %d temporary computation(s) whose result "
                     "was never read; unused temporaries waste a stack "
                     "slot and register moves.", removed);
        } else {
            snprintf(expl, sizeof(expl),
                     "Every temporary produced is read by a later "
                     "instruction; nothing was dead.");
        }
        rec.explanation = co1_strdup(expl);

        OptPassRecordList_push(&report->passes, rec);
    }

    report->after_count = (int)work.len;
    if (report->before_count > 0) {
        report->reduction_pct =
            100.0 * (report->before_count - report->after_count) /
            (double)report->before_count;
    }

    *output = work;
}

void opt_report_free(OptReport *report) {
    for (size_t i = 0; i < report->passes.len; i++) {
        OptPassRecord *rec = &report->passes.items[i];
        free(rec->name);
        free(rec->explanation);
        ir_list_free(&rec->before);
        ir_list_free(&rec->after);
    }
    OptPassRecordList_free(&report->passes);
}
