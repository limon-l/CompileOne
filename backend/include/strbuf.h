#ifndef CO1_STRBUF_H
#define CO1_STRBUF_H

#include <stddef.h>

/* Growable, null-terminated char buffer used by the JSON writer
   and anywhere the backend needs to assemble text incrementally. */
typedef struct StrBuf {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void strbuf_init(StrBuf *sb);
void strbuf_free(StrBuf *sb);
void strbuf_clear(StrBuf *sb);
void strbuf_append(StrBuf *sb, const char *s);
void strbuf_append_len(StrBuf *sb, const char *s, size_t n);
void strbuf_append_char(StrBuf *sb, char c);
void strbuf_append_int(StrBuf *sb, long long v);
void strbuf_append_double(StrBuf *sb, double v, int max_decimals);
void strbuf_append_json_string(StrBuf *sb, const char *s);
const char *strbuf_cstr(StrBuf *sb);
size_t strbuf_len(StrBuf *sb);

/* strdup replacement (MinGW's string.h strdup can warn under -std=c99). */
char *co1_strdup(const char *s);

#endif /* CO1_STRBUF_H */
