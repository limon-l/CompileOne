#include "json_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
   JSON reader
   ============================================================ */

typedef struct JsonParser {
    const char *s;
    size_t pos;
} JsonParser;

static JsonValue *parse_value(JsonParser *p, const char **error);
static int is_hex(char c);

static void set_error(const char **error, const char *msg) {
    if (error) {
        *error = msg;
    }
}

static void skip_ws(JsonParser *p) {
    while (p->s[p->pos] == ' ' || p->s[p->pos] == '\t' ||
           p->s[p->pos] == '\n' || p->s[p->pos] == '\r') {
        p->pos++;
    }
}

static JsonValue *value_null(JsonParser *p) {
    (void)p;
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    v->type = JSON_NULL;
    return v;
}

static JsonValue *value_bool(JsonParser *p, int b) {
    (void)p;
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    v->type = JSON_BOOL;
    v->as.boolean = b;
    return v;
}

static JsonValue *value_number(JsonParser *p, size_t start, size_t end) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    v->type = JSON_NUMBER;
    v->as.number = (char *)malloc(end - start + 1);
    memcpy(v->as.number, p->s + start, end - start);
    v->as.number[end - start] = '\0';
    return v;
}

static JsonValue *value_string_owned(char *s) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    v->type = JSON_STRING;
    v->as.string = s;
    return v;
}

static JsonValue *parse_string(JsonParser *p, const char **error) {
    /* caller verified p->s[p->pos] == '"' */
    size_t i = p->pos + 1;
    size_t cap = 64;
    size_t len = 0;
    char *buf = (char *)malloc(cap);

    for (;;) {
        char c = p->s[i];
        if (c == '\0') {
            free(buf);
            set_error(error, "unterminated string in JSON");
            return NULL;
        }
        if (c == '"') {
            i++;
            break;
        }
        if (c == '\\') {
            char esc = p->s[i + 1];
            switch (esc) {
            case '"':  c = '"';  i += 2; break;
            case '\\': c = '\\'; i += 2; break;
            case '/':  c = '/';  i += 2; break;
            case 'b':  c = '\b'; i += 2; break;
            case 'f':  c = '\f'; i += 2; break;
            case 'n':  c = '\n'; i += 2; break;
            case 'r':  c = '\r'; i += 2; break;
            case 't':  c = '\t'; i += 2; break;
            case 'u': {
                char hex[5] = {0, 0, 0, 0, 0};
                unsigned int cp = 0;
                for (int k = 0; k < 4; k++) {
                    hex[k] = p->s[i + 2 + k];
                    if (!is_hex(hex[k])) {
                        free(buf);
                        set_error(error, "invalid \\u escape in JSON");
                        return NULL;
                    }
                }
                if (sscanf(hex, "%4x", &cp) != 1) {
                    free(buf);
                    set_error(error, "invalid \\u escape in JSON");
                    return NULL;
                }
                /* Only encode the BMP: surrogate pairs are not produced
                   by json_writer (it escapes non-ASCII as \\uXXXX). */
                if (cp < 0x80) {
                    c = (char)cp;
                } else {
                    c = '?'; /* non-ASCII: replaced, never emitted by us */
                }
                i += 6;
                break;
            }
            default:
                free(buf);
                set_error(error, "invalid escape in JSON string");
                return NULL;
            }
        } else {
            i++;
        }
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (char *)realloc(buf, cap);
        }
        buf[len++] = c;
    }
    buf[len] = '\0';
    p->pos = i;
    return value_string_owned(buf);
}

static int is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static JsonValue *parse_number(JsonParser *p, const char **error) {
    size_t start = p->pos;
    size_t i = p->pos;
    if (p->s[i] == '-') {
        i++;
    }
    while (p->s[i] >= '0' && p->s[i] <= '9') {
        i++;
    }
    if (p->s[i] == '.') {
        i++;
        while (p->s[i] >= '0' && p->s[i] <= '9') {
            i++;
        }
    }
    if (p->s[i] == 'e' || p->s[i] == 'E') {
        i++;
        if (p->s[i] == '+' || p->s[i] == '-') {
            i++;
        }
        while (p->s[i] >= '0' && p->s[i] <= '9') {
            i++;
        }
    }
    if (i == start) {
        set_error(error, "invalid number in JSON");
        return NULL;
    }
    JsonValue *v = value_number(p, start, i);
    p->pos = i;
    return v;
}

static JsonValue *parse_array(JsonParser *p, const char **error) {
    p->pos++; /* '[' */
    JsonValue *arr = (JsonValue *)calloc(1, sizeof(JsonValue));
    arr->type = JSON_ARRAY;
    size_t cap = 8;
    arr->as.array.items = (JsonValue **)malloc(cap * sizeof(JsonValue *));
    arr->as.array.len = 0;

    skip_ws(p);
    if (p->s[p->pos] == ']') {
        p->pos++;
        return arr;
    }
    for (;;) {
        JsonValue *item = parse_value(p, error);
        if (!item) {
            json_value_free(arr);
            return NULL;
        }
        if (arr->as.array.len >= cap) {
            cap *= 2;
            arr->as.array.items = (JsonValue **)realloc(
                arr->as.array.items, cap * sizeof(JsonValue *));
        }
        arr->as.array.items[arr->as.array.len++] = item;

        skip_ws(p);
        char c = p->s[p->pos];
        if (c == ']') {
            p->pos++;
            return arr;
        }
        if (c != ',') {
            json_value_free(arr);
            set_error(error, "expected ',' or ']' in JSON array");
            return NULL;
        }
        p->pos++;
        skip_ws(p);
    }
}

static JsonValue *parse_object(JsonParser *p, const char **error) {
    p->pos++; /* '{' */
    JsonValue *obj = (JsonValue *)calloc(1, sizeof(JsonValue));
    obj->type = JSON_OBJECT;
    size_t cap = 8;
    obj->as.object.members = (JsonMember *)malloc(cap * sizeof(JsonMember));
    obj->as.object.len = 0;

    skip_ws(p);
    if (p->s[p->pos] == '}') {
        p->pos++;
        return obj;
    }
    for (;;) {
        skip_ws(p);
        if (p->s[p->pos] != '"') {
            json_value_free(obj);
            set_error(error, "expected string key in JSON object");
            return NULL;
        }
        JsonValue *keyv = parse_string(p, error);
        if (!keyv) {
            json_value_free(obj);
            return NULL;
        }
        skip_ws(p);
        if (p->s[p->pos] != ':') {
            json_value_free(obj);
            json_value_free(keyv);
            set_error(error, "expected ':' in JSON object");
            return NULL;
        }
        p->pos++;
        JsonValue *val = parse_value(p, error);
        if (!val) {
            json_value_free(obj);
            json_value_free(keyv);
            return NULL;
        }

        if (obj->as.object.len >= cap) {
            cap *= 2;
            obj->as.object.members = (JsonMember *)realloc(
                obj->as.object.members, cap * sizeof(JsonMember));
        }
        JsonMember *m = &obj->as.object.members[obj->as.object.len++];
        m->key = keyv->as.string;
        free(keyv); /* steal the string */
        m->value = val;

        skip_ws(p);
        char c = p->s[p->pos];
        if (c == '}') {
            p->pos++;
            return obj;
        }
        if (c != ',') {
            json_value_free(obj);
            set_error(error, "expected ',' or '}' in JSON object");
            return NULL;
        }
        p->pos++;
    }
}

static JsonValue *parse_value(JsonParser *p, const char **error) {
    skip_ws(p);
    char c = p->s[p->pos];
    if (c == '\0') {
        set_error(error, "unexpected end of JSON input");
        return NULL;
    }
    if (c == '{') {
        return parse_object(p, error);
    }
    if (c == '[') {
        return parse_array(p, error);
    }
    if (c == '"') {
        return parse_string(p, error);
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        return parse_number(p, error);
    }
    if (strncmp(p->s + p->pos, "true", 4) == 0) {
        p->pos += 4;
        return value_bool(p, 1);
    }
    if (strncmp(p->s + p->pos, "false", 5) == 0) {
        p->pos += 5;
        return value_bool(p, 0);
    }
    if (strncmp(p->s + p->pos, "null", 4) == 0) {
        p->pos += 4;
        return value_null(p);
    }
    set_error(error, "unexpected character in JSON");
    return NULL;
}

JsonValue *json_parse(const char *text, const char **error) {
    if (error) {
        *error = NULL;
    }
    JsonParser p = {text, 0};
    JsonValue *root = parse_value(&p, error);
    if (!root) {
        return NULL;
    }
    skip_ws(&p);
    if (p.s[p.pos] != '\0') {
        json_value_free(root);
        set_error(error, "trailing characters after JSON value");
        return NULL;
    }
    return root;
}

JsonValue *json_object_get(const JsonValue *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT) {
        return NULL;
    }
    for (size_t i = 0; i < obj->as.object.len; i++) {
        if (strcmp(obj->as.object.members[i].key, key) == 0) {
            return obj->as.object.members[i].value;
        }
    }
    return NULL;
}

const char *json_as_string(const JsonValue *v) {
    return (v && v->type == JSON_STRING) ? v->as.string : NULL;
}

const char *json_as_number(const JsonValue *v) {
    return (v && v->type == JSON_NUMBER) ? v->as.number : NULL;
}

int json_as_bool(const JsonValue *v) {
    return (v && v->type == JSON_BOOL) ? v->as.boolean : 0;
}

int json_is_array(const JsonValue *v) {
    return v && v->type == JSON_ARRAY;
}

size_t json_array_len(const JsonValue *v) {
    return (v && v->type == JSON_ARRAY) ? v->as.array.len : 0;
}

JsonValue *json_array_at(const JsonValue *v, size_t idx) {
    if (!v || v->type != JSON_ARRAY || idx >= v->as.array.len) {
        return NULL;
    }
    return v->as.array.items[idx];
}

int json_is_object(const JsonValue *v) {
    return v && v->type == JSON_OBJECT;
}

size_t json_object_len(const JsonValue *v) {
    return (v && v->type == JSON_OBJECT) ? v->as.object.len : 0;
}

const JsonMember *json_object_at(const JsonValue *v, size_t idx) {
    if (!v || v->type != JSON_OBJECT || idx >= v->as.object.len) {
        return NULL;
    }
    return &v->as.object.members[idx];
}

void json_value_free(JsonValue *v) {
    if (!v) {
        return;
    }
    switch (v->type) {
    case JSON_NUMBER:
        free(v->as.number);
        break;
    case JSON_STRING:
        free(v->as.string);
        break;
    case JSON_ARRAY:
        for (size_t i = 0; i < v->as.array.len; i++) {
            json_value_free(v->as.array.items[i]);
        }
        free(v->as.array.items);
        break;
    case JSON_OBJECT:
        for (size_t i = 0; i < v->as.object.len; i++) {
            free(v->as.object.members[i].key);
            json_value_free(v->as.object.members[i].value);
        }
        free(v->as.object.members);
        break;
    default:
        break;
    }
    free(v);
}

JsonValue *json_parse_file(const char *path, const char **error) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        set_error(error, "cannot open file");
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        set_error(error, "cannot seek in file");
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        set_error(error, "cannot read file size");
        return NULL;
    }
    rewind(fp);
    char *text = (char *)malloc((size_t)size + 1);
    if (!text) {
        fclose(fp);
        set_error(error, "out of memory");
        return NULL;
    }
    size_t rd = fread(text, 1, (size_t)size, fp);
    fclose(fp);
    text[rd] = '\0';

    const char *parse_err = NULL;
    JsonValue *root = json_parse(text, &parse_err);
    free(text);
    if (error) {
        *error = parse_err;
    }
    return root;
}
