#ifndef CO1_INTERP_H
#define CO1_INTERP_H

#include <stddef.h>

#include "strbuf.h"
#include "token.h"

/* A runtime diagnostic produced while executing a mini-c program. */
typedef struct RunError {
    int line;
    int column;
    char *message;   /* owned */
} RunError;

/* Captured outcome of executing a mini-c program. */
typedef struct RunResult {
    StrBuf output;       /* captured stdout, newline-terminated lines */
    int exit_code;       /* value of the final return / 0 on completion */
    int step_count;      /* statements executed */
    RunError error;      /* error.line == 0 => no runtime error */
} RunResult;

void run_result_init(RunResult *r);
void run_result_free(RunResult *r);

/* Interpret the token stream. Fills `r` (owned strings must be freed by
   the caller via run_result_free). Returns 0 on success, 1 if a runtime
   error was reported (details in r->error). */
int run_program(TokenList *tokens, RunResult *r);

#endif /* CO1_INTERP_H */
