#ifndef CO1_JSON_READER_H
#define CO1_JSON_READER_H

#include <stddef.h>

/* ============================================================
   Minimal DOM-style JSON reader for artifact files.

   The backend emits JSON via json_writer.h but also needs to
   consume artifact JSON (e.g. the token-stream artifact that
   feeds the parse / ast / semantic phases). This is a small,
   non-recursive-friendly value model plus a parser covering the
   JSON subset the artifacts use: objects, arrays, strings,
   numbers, booleans and null.

   Values form a tree of JsonValue nodes. The caller owns the
   tree and must free it with json_value_free().
   ============================================================ */

typedef enum JsonValueType {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,    /* stored verbatim as a string token */
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} JsonValueType;

typedef struct JsonValue JsonValue;

typedef struct JsonMember {
    char *key;              /* owned */
    JsonValue *value;       /* owned */
} JsonMember;

struct JsonValue {
    JsonValueType type;
    union {
        int boolean;                    /* JSON_BOOL */
        char *number;                   /* JSON_NUMBER: raw digits text */
        char *string;                   /* JSON_STRING: unescaped */
        struct {
            JsonValue **items;          /* JSON_ARRAY */
            size_t len;
        } array;
        struct {
            JsonMember *members;        /* JSON_OBJECT */
            size_t len;
        } object;
    } as;
};

/* Parse `text` (must be NUL-terminated). On success returns the root
   value (owned by caller) and sets *error to NULL. On failure returns
   NULL and sets *error to a static message (not owned). */
JsonValue *json_parse(const char *text, const char **error);

/* Look up an object member by key; returns NULL when absent. */
JsonValue *json_object_get(const JsonValue *obj, const char *key);

/* Convenience accessors. All return NULL/0 when the value is NULL or
   the type does not match. */
const char *json_as_string(const JsonValue *v);
const char *json_as_number(const JsonValue *v);
int json_as_bool(const JsonValue *v);
int json_is_array(const JsonValue *v);
size_t json_array_len(const JsonValue *v);
JsonValue *json_array_at(const JsonValue *v, size_t idx);
int json_is_object(const JsonValue *v);
size_t json_object_len(const JsonValue *v);
const JsonMember *json_object_at(const JsonValue *v, size_t idx);

/* Free the whole tree rooted at `v`. */
void json_value_free(JsonValue *v);

/* Parse the JSON file at `path`. Returns root on success, NULL on
   failure (with *error set to a static message). */
JsonValue *json_parse_file(const char *path, const char **error);

#endif /* CO1_JSON_READER_H */
