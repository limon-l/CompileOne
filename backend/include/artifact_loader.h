#ifndef CO1_ARTIFACT_LOADER_H
#define CO1_ARTIFACT_LOADER_H

#include "token.h"

/* ============================================================
   Artifact loaders — read the JSON artifacts that feed the
   mid-pipeline phases.

   The pipeline is single-process: each phase command consumes
   the previous phase's artifact and re-derives the in-memory
   structures it needs. The token-stream artifact is the common
   input for parse / ast / semantic (it carries the full token
   array, which is enough to re-run the front end).
   ============================================================ */

/* Load a token-stream artifact (`compileone/token-stream/1.0`) from
   `path` into `tokens`. Returns 0 on success; on failure returns -1
   and writes a static message into *error (if non-NULL). On success
   the caller owns every token (call token_free + TokenList_free). */
int load_token_stream(const char *path, TokenList *tokens, const char **error);

#endif /* CO1_ARTIFACT_LOADER_H */
