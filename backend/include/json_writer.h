#ifndef CO1_JSON_WRITER_H
#define CO1_JSON_WRITER_H

#include "strbuf.h"

/* A small, dependency-free JSON writer.
   Container correctness is the caller's responsibility:
   every begin_* must be matched by its end_*, and every key must be
   followed by exactly one value. The writer handles commas and nesting. */

typedef struct JsonWriter {
    StrBuf buf;
    char ctx[64];     /* stack of container kinds: 'O' object, 'A' array */
    int depth;        /* current nesting depth (-1 = root, nothing open) */
    int need_comma[64]; /* comma-pending flag per depth */
    int closed;       /* set after the root container is closed */
} JsonWriter;

void jw_init(JsonWriter *w);
void jw_free(JsonWriter *w);
void jw_begin_object(JsonWriter *w);
void jw_end_object(JsonWriter *w);
void jw_begin_array(JsonWriter *w);
void jw_end_array(JsonWriter *w);
void jw_key(JsonWriter *w, const char *key);
void jw_string(JsonWriter *w, const char *value);
void jw_int(JsonWriter *w, long long value);
void jw_double(JsonWriter *w, double value, int max_decimals);
void jw_bool(JsonWriter *w, int value);
void jw_null(JsonWriter *w);
const char *jw_cstr(JsonWriter *w);
size_t jw_len(JsonWriter *w);

#endif /* CO1_JSON_WRITER_H */
