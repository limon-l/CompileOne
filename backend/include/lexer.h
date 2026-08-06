#ifndef CO1_LEXER_H
#define CO1_LEXER_H

#include <stdio.h>

#include "token.h"

/* Runs the flex scanner over `input_path`, collecting every token into
   `tokens` and every lexical diagnostic into `errors`.
   Returns 0 on success (lexical errors are recorded inside the lists,
   not fatal), -1 on I/O failure. */
int lex_file(const char *input_path, TokenList *tokens, LexErrorList *errors);

/* When non-zero (default) TOK_COMMENT tokens are emitted into the token
   stream. When zero, comments are consumed but not recorded — used by the
   future parse phase, whose grammar must not see comments. */
extern int lex_collect_comments;

#endif /* CO1_LEXER_H */
