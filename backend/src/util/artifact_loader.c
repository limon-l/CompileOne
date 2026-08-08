#include "artifact_loader.h"

#include <stdlib.h>
#include <string.h>

#include "json_reader.h"
#include "strbuf.h"

int load_token_stream(const char *path, TokenList *tokens, const char **error) {
    if (error) {
        *error = NULL;
    }
    if (!tokens) {
        return -1;
    }

    const char *parse_err = NULL;
    JsonValue *root = json_parse_file(path, &parse_err);
    if (!root) {
        if (error) {
            *error = parse_err ? parse_err : "cannot read JSON artifact";
        }
        return -1;
    }

    JsonValue *tokens_v = json_object_get(root, "tokens");
    if (!tokens_v || !json_is_array(tokens_v)) {
        json_value_free(root);
        if (error) {
            *error = "artifact has no 'tokens' array";
        }
        return -1;
    }

    size_t n = json_array_len(tokens_v);
    for (size_t i = 0; i < n; i++) {
        JsonValue *raw = json_array_at(tokens_v, i);
        JsonValue *off = json_object_get(raw, "offset");

        const char *name = json_as_string(json_object_get(raw, "token"));
        const char *lexeme = json_as_string(json_object_get(raw, "lexeme"));
        if (!name || !lexeme) {
            json_value_free(root);
            if (error) {
                *error = "token entry missing 'token' or 'lexeme'";
            }
            return -1;
        }

        Token t;
        t.id = (int)strtol(json_as_number(json_object_get(raw, "id")) ? json_as_number(json_object_get(raw, "id")) : "0", NULL, 10);
        t.line = (int)strtol(json_as_number(json_object_get(raw, "line")) ? json_as_number(json_object_get(raw, "line")) : "1", NULL, 10);
        t.column = (int)strtol(json_as_number(json_object_get(raw, "column")) ? json_as_number(json_object_get(raw, "column")) : "1", NULL, 10);
        t.lexeme = co1_strdup(lexeme);
        t.kind = token_from_name(name);
        t.category = token_category(t.kind);
        t.subtype = token_subtype(t.kind);
        t.length = strlen(t.lexeme);
        t.scope_level = (int)strtol(json_as_number(json_object_get(raw, "scope_level")) ? json_as_number(json_object_get(raw, "scope_level")) : "0", NULL, 10);
        t.color = token_color(t.kind);
        t.description = token_description(t.kind);
        t.offset_start = (size_t)strtoull(json_as_number(json_object_get(off, "start")) ? json_as_number(json_object_get(off, "start")) : "0", NULL, 10);
        t.offset_end = (size_t)strtoull(json_as_number(json_object_get(off, "end")) ? json_as_number(json_object_get(off, "end")) : "0", NULL, 10);
        TokenList_push(tokens, t);
    }

    json_value_free(root);
    return 0;
}
