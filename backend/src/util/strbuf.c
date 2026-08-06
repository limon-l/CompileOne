#include "strbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void strbuf_reserve(StrBuf *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) {
        return;
    }
    size_t new_cap = sb->cap ? sb->cap : 64;
    while (new_cap < sb->len + extra + 1) {
        new_cap *= 2;
    }
    sb->data = (char *)realloc(sb->data, new_cap);
    if (!sb->data) {
        fprintf(stderr, "strbuf: out of memory\n");
        exit(1);
    }
    sb->cap = new_cap;
}

void strbuf_init(StrBuf *sb) {
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void strbuf_free(StrBuf *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void strbuf_clear(StrBuf *sb) {
    if (sb->data) {
        sb->data[0] = '\0';
    }
    sb->len = 0;
}

void strbuf_append_len(StrBuf *sb, const char *s, size_t n) {
    if (n == 0) {
        return;
    }
    strbuf_reserve(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void strbuf_append(StrBuf *sb, const char *s) {
    if (!s) {
        return;
    }
    strbuf_append_len(sb, s, strlen(s));
}

void strbuf_append_char(StrBuf *sb, char c) {
    strbuf_reserve(sb, 1);
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

void strbuf_append_int(StrBuf *sb, long long v) {
    char tmp[24];
    size_t n = 0;
    if (v == 0) {
        strbuf_append_char(sb, '0');
        return;
    }
    int neg = 0;
    unsigned long long u;
    if (v < 0) {
        neg = 1;
        u = (unsigned long long)(-(v + 1)) + 1; /* avoid UB on LLONG_MIN */
    } else {
        u = (unsigned long long)v;
    }
    while (u > 0) {
        tmp[n++] = (char)('0' + (int)(u % 10));
        u /= 10;
    }
    if (neg) {
        strbuf_append_char(sb, '-');
    }
    while (n > 0) {
        strbuf_append_char(sb, tmp[--n]);
    }
}

void strbuf_append_double(StrBuf *sb, double v, int max_decimals) {
    char tmp[64];
    if (max_decimals < 0) {
        max_decimals = 0;
    }
    if (max_decimals > 12) {
        max_decimals = 12;
    }
    snprintf(tmp, sizeof(tmp), "%.*f", max_decimals, v);
    /* strip trailing zeros and a possible trailing '.' for clean numbers */
    size_t n = strlen(tmp);
    if (n > 0) {
        char *dot = strchr(tmp, '.');
        if (dot) {
            size_t last = n - 1;
            while (last > (size_t)(dot - tmp) && tmp[last] == '0') {
                tmp[last--] = '\0';
            }
            if (tmp[last] == '.') {
                tmp[last] = '\0';
            }
        }
    }
    strbuf_append(sb, tmp);
}

void strbuf_append_json_string(StrBuf *sb, const char *s) {
    if (!s) {
        strbuf_append(sb, "\"\"");
        return;
    }
    strbuf_append_char(sb, '"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        switch (c) {
        case '"':
            strbuf_append(sb, "\\\"");
            break;
        case '\\':
            strbuf_append(sb, "\\\\");
            break;
        case '\b':
            strbuf_append(sb, "\\b");
            break;
        case '\f':
            strbuf_append(sb, "\\f");
            break;
        case '\n':
            strbuf_append(sb, "\\n");
            break;
        case '\r':
            strbuf_append(sb, "\\r");
            break;
        case '\t':
            strbuf_append(sb, "\\t");
            break;
        default:
            if (c < 0x20) {
                char esc[8];
                snprintf(esc, sizeof(esc), "\\u%04x", c);
                strbuf_append(sb, esc);
            } else {
                strbuf_append_char(sb, (char)c);
            }
        }
    }
    strbuf_append_char(sb, '"');
}

const char *strbuf_cstr(StrBuf *sb) {
    return sb->data ? sb->data : "";
}

size_t strbuf_len(StrBuf *sb) {
    return sb->len;
}

char *co1_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t n = strlen(s) + 1;
    char *copy = (char *)malloc(n);
    if (copy) {
        memcpy(copy, s, n);
    }
    return copy;
}
