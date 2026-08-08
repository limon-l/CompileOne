#include "json_writer.h"

#include <stdio.h>
#include <stdlib.h>

void jw_init(JsonWriter *w) {
    strbuf_init(&w->buf);
    w->depth = -1;
    w->closed = 0;
}

void jw_free(JsonWriter *w) {
    strbuf_free(&w->buf);
}

/* Called before entering a nested container or writing a bare value.
   Emits the inter-element comma when the enclosing container is an
   array; object members handle their comma inside jw_key(). */
static void jw_before_child(JsonWriter *w) {
    if (w->depth >= 0 && w->ctx[w->depth] == '[' && w->need_comma[w->depth]) {
        strbuf_append_char(&w->buf, ',');
    }
    if (w->depth >= 0) {
        w->need_comma[w->depth] = 1;
    }
}

static void jw_enter(JsonWriter *w, char kind) {
    if (w->closed) {
        return;
    }
    jw_before_child(w);
    strbuf_append_char(&w->buf, kind);
    w->depth++;
    if (w->depth >= 64) {
        fprintf(stderr, "json_writer: nesting too deep\n");
        exit(1);
    }
    w->ctx[w->depth] = kind;
    w->need_comma[w->depth] = 0;
}

static void jw_leave(JsonWriter *w, char open_char, char close_char) {
    if (w->depth < 0 || w->ctx[w->depth] != open_char) {
        fprintf(stderr, "json_writer: unbalanced container (expected '%c')\n",
                open_char);
        exit(1);
    }
    strbuf_append_char(&w->buf, close_char);
    w->depth--;
    if (w->depth < 0) {
        w->closed = 1;
    }
}

void jw_begin_object(JsonWriter *w) {
    jw_enter(w, '{');
}

void jw_end_object(JsonWriter *w) {
    jw_leave(w, '{', '}');
}

void jw_begin_array(JsonWriter *w) {
    jw_enter(w, '[');
}

void jw_end_array(JsonWriter *w) {
    jw_leave(w, '[', ']');
}

void jw_key(JsonWriter *w, const char *key) {
    if (w->closed || w->depth < 0 || w->ctx[w->depth] != '{') {
        fprintf(stderr, "json_writer: key outside object\n");
        exit(1);
    }
    if (w->need_comma[w->depth]) {
        strbuf_append_char(&w->buf, ',');
    }
    w->need_comma[w->depth] = 1;
    strbuf_append_json_string(&w->buf, key);
    strbuf_append_char(&w->buf, ':');
}

void jw_string(JsonWriter *w, const char *value) {
    jw_before_child(w);
    strbuf_append_json_string(&w->buf, value);
}

void jw_int(JsonWriter *w, long long value) {
    jw_before_child(w);
    strbuf_append_int(&w->buf, value);
}

void jw_double(JsonWriter *w, double value, int max_decimals) {
    jw_before_child(w);
    strbuf_append_double(&w->buf, value, max_decimals);
}

void jw_bool(JsonWriter *w, int value) {
    jw_before_child(w);
    strbuf_append(&w->buf, value ? "true" : "false");
}

void jw_null(JsonWriter *w) {
    jw_before_child(w);
    strbuf_append(&w->buf, "null");
}

void jw_append_raw(JsonWriter *w, const char *value) {
    jw_before_child(w);
    strbuf_append(&w->buf, value);
}

const char *jw_cstr(JsonWriter *w) {
    return strbuf_cstr(&w->buf);
}

size_t jw_len(JsonWriter *w) {
    return strbuf_len(&w->buf);
}
