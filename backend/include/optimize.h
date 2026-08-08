#ifndef CO1_OPTIMIZE_H
#define CO1_OPTIMIZE_H

#include <stddef.h>

#include "ir.h"

/* ============================================================
   Optimization phase for the mini-c study language.

   Runs a small suite of classic local passes over the TAC quad
   list and records, per pass, the instructions it touched so the
   UI can show a before/after evidence view (optimization.json,
   schema "compileone/optimization/1.0").
   ============================================================ */

typedef struct OptPassRecord {
    char *name;               /* owned, e.g. "Constant Folding" */
    int applied;              /* 0 => pass found nothing to do */
    char *explanation;        /* owned prose */
    int instructions_removed; /* quads the pass eliminated */
    IrQuadList before;        /* quads as they looked before the pass */
    IrQuadList after;         /* quads the pass produced in their place */
} OptPassRecord;

DARRAY_DECLARE(OptPassRecord, OptPassRecordList)

typedef struct OptReport {
    int before_count;
    int after_count;
    double reduction_pct;     /* 0..100 */
    OptPassRecordList passes;
} OptReport;

/* Optimize `input` into `output` (a fresh, optimised quad list) and
   fill `report` with the pass evidence. Both are owned by the caller. */
void optimize(const IrQuadList *input, IrQuadList *output, OptReport *report);

void opt_report_free(OptReport *report);

#endif /* CO1_OPTIMIZE_H */
