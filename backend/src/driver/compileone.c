/* ============================================================
   compileone — the CompileOne backend driver.

   A single executable exposing one subcommand per compiler phase.
   Each phase reads its input artifact (or the source file for the
   first phase) and writes the next artifact as validated JSON.

   Usage:
       compileone lex --input <source.mc> --output <tokens.json> \
                      [--language mini-c]
       compileone parse | ast | semantic | ir | opt | codegen | run
                     --input <prev.json> --output <next.json>
       compileone --version | --list-phases | --help
   ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "interp.h"
#include "json_writer.h"
#include "lexer.h"
#include "token.h"

#define CO1_VERSION "0.1.0"

static const char *const kPhases[] = {
    "lex", "parse", "ast", "semantic", "ir", "opt", "codegen", "run",
};
static const size_t kPhaseCount = sizeof(kPhases) / sizeof(kPhases[0]);

/* ------------------------------------------------------------------
   Phase: lex
   ------------------------------------------------------------------ */

static void write_scope_string(JsonWriter *w, int level) {
    if (level == 0) {
        jw_string(w, "global");
    } else {
        char buf[24];
        snprintf(buf, sizeof(buf), "block:%d", level);
        jw_string(w, buf);
    }
}

static int write_token_artifact(const char *output_path, const char *input_path,
                                const char *language, double duration_ms,
                                TokenList *tokens, LexErrorList *errors) {
    JsonWriter w;
    jw_init(&w);
    jw_begin_object(&w);

    jw_key(&w, "schema");
    jw_string(&w, "compileone/token-stream/1.0");
    jw_key(&w, "phase");
    jw_string(&w, "lexical");
    jw_key(&w, "language");
    jw_string(&w, language);
    jw_key(&w, "source_file");
    jw_string(&w, input_path);
    jw_key(&w, "generated_by");
    jw_string(&w, "compileone.exe v" CO1_VERSION " (flex)");
    jw_key(&w, "duration_ms");
    jw_double(&w, duration_ms, 4);

    jw_key(&w, "tokens");
    jw_begin_array(&w);
    for (size_t i = 0; i < tokens->len; i++) {
        Token *t = &tokens->items[i];
        jw_begin_object(&w);
        jw_key(&w, "id");       jw_int(&w, t->id);
        jw_key(&w, "line");     jw_int(&w, t->line);
        jw_key(&w, "column");   jw_int(&w, t->column);
        jw_key(&w, "lexeme");   jw_string(&w, t->lexeme);
        jw_key(&w, "token");    jw_string(&w, token_name(t->kind));
        jw_key(&w, "category"); jw_string(&w, token_category_name(t->category));
        jw_key(&w, "subtype");  jw_string(&w, t->subtype);
        jw_key(&w, "length");   jw_int(&w, (long long)t->length);
        jw_key(&w, "scope");    write_scope_string(&w, t->scope_level);
        jw_key(&w, "scope_level"); jw_int(&w, t->scope_level);
        jw_key(&w, "color");    jw_string(&w, t->color);
        jw_key(&w, "description"); jw_string(&w, t->description);
        jw_key(&w, "offset");
        jw_begin_object(&w);
        jw_key(&w, "start");    jw_int(&w, (long long)t->offset_start);
        jw_key(&w, "end");      jw_int(&w, (long long)t->offset_end);
        jw_end_object(&w);
        jw_end_object(&w);
    }
    jw_end_array(&w);

    jw_key(&w, "statistics");
    jw_begin_object(&w);
    jw_key(&w, "total");
    jw_int(&w, (long long)tokens->len);
    jw_key(&w, "by_category");
    jw_begin_object(&w);
    for (int c = 0; c < CAT_COUNT; c++) {
        size_t count = 0;
        for (size_t i = 0; i < tokens->len; i++) {
            if (tokens->items[i].category == (TokenCategory)c) {
                count++;
            }
        }
        jw_key(&w, token_category_name((TokenCategory)c));
        jw_int(&w, (long long)count);
    }
    jw_end_object(&w);
    jw_end_object(&w);

    jw_key(&w, "errors");
    jw_begin_array(&w);
    for (size_t i = 0; i < errors->len; i++) {
        LexError *e = &errors->items[i];
        jw_begin_object(&w);
        jw_key(&w, "line");     jw_int(&w, e->line);
        jw_key(&w, "column");   jw_int(&w, e->column);
        jw_key(&w, "lexeme");   jw_string(&w, e->lexeme);
        jw_key(&w, "message");  jw_string(&w, e->message);
        jw_end_object(&w);
    }
    jw_end_array(&w);

    jw_end_object(&w);

    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        fprintf(stderr, "error: cannot open output file '%s'\n", output_path);
        jw_free(&w);
        return -1;
    }
    fwrite(jw_cstr(&w), 1, jw_len(&w), fp);
    fclose(fp);
    jw_free(&w);
    return 0;
}

static int cmd_lex(const char *input, const char *output, const char *language) {
    clock_t start = clock();

    TokenList tokens = {0};
    LexErrorList errors = {0};

    if (lex_file(input, &tokens, &errors) != 0) {
        fprintf(stderr, "error: cannot open input file '%s'\n", input);
        return 1;
    }

    double duration_ms = ((double)(clock() - start)) * 1000.0 / CLOCKS_PER_SEC;
    int rc = write_token_artifact(output, input, language, duration_ms,
                                  &tokens, &errors);

    fprintf(stderr, "lex: %lu tokens, %lu errors, %.2f ms -> %s\n",
            (unsigned long)tokens.len, (unsigned long)errors.len,
            duration_ms, output);

    for (size_t i = 0; i < tokens.len; i++) {
        token_free(&tokens.items[i]);
    }
    for (size_t i = 0; i < errors.len; i++) {
        lex_error_free(&errors.items[i]);
    }
    TokenList_free(&tokens);
    LexErrorList_free(&errors);
    return rc;
}

/* ------------------------------------------------------------------
   Phase: run (mini-c interpreter)
   ------------------------------------------------------------------ */

static int write_execution_artifact(const char *output_path, const char *input_path,
                                    const char *language, double duration_ms,
                                    RunResult *r, const char *status,
                                    LexErrorList *lex_errors) {
    JsonWriter w;
    jw_init(&w);
    jw_begin_object(&w);

    jw_key(&w, "schema");
    jw_string(&w, "compileone/execution/1.0");
    jw_key(&w, "phase");
    jw_string(&w, "execution");
    jw_key(&w, "language");
    jw_string(&w, language);
    jw_key(&w, "source_file");
    jw_string(&w, input_path);
    jw_key(&w, "generated_by");
    jw_string(&w, "compileone.exe v" CO1_VERSION " (interpreter)");
    jw_key(&w, "duration_ms");
    jw_double(&w, duration_ms, 4);
    jw_key(&w, "status");
    jw_string(&w, status);
    jw_key(&w, "exit_code");
    jw_int(&w, r->exit_code);
    jw_key(&w, "steps");
    jw_int(&w, r->step_count);

    jw_key(&w, "output");
    jw_begin_array(&w);
    {
        StrBuf line;
        strbuf_init(&line);
        const char *out = strbuf_cstr(&r->output);
        size_t len = strbuf_len(&r->output);
        for (size_t i = 0; i < len; i++) {
            if (out[i] == '\n') {
                jw_string(&w, strbuf_cstr(&line));
                strbuf_clear(&line);
            } else {
                strbuf_append_char(&line, out[i]);
            }
        }
        if (strbuf_len(&line) > 0) {
            jw_string(&w, strbuf_cstr(&line));
        }
        strbuf_free(&line);
    }
    jw_end_array(&w);

    jw_key(&w, "errors");
    jw_begin_array(&w);
    if (lex_errors) {
        for (size_t i = 0; i < lex_errors->len; i++) {
            LexError *e = &lex_errors->items[i];
            jw_begin_object(&w);
            jw_key(&w, "line");     jw_int(&w, e->line);
            jw_key(&w, "column");   jw_int(&w, e->column);
            jw_key(&w, "message");  jw_string(&w, e->message);
            jw_end_object(&w);
        }
    } else if (r->error.line > 0) {
        jw_begin_object(&w);
        jw_key(&w, "line");     jw_int(&w, r->error.line);
        jw_key(&w, "column");   jw_int(&w, r->error.column);
        jw_key(&w, "message");  jw_string(&w, r->error.message);
        jw_end_object(&w);
    }
    jw_end_array(&w);

    jw_end_object(&w);

    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        fprintf(stderr, "error: cannot open output file '%s'\n", output_path);
        jw_free(&w);
        return -1;
    }
    fwrite(jw_cstr(&w), 1, jw_len(&w), fp);
    fclose(fp);
    jw_free(&w);
    return 0;
}

static int cmd_run(const char *input, const char *output, const char *language) {
    clock_t start = clock();
    if (strcmp(language, "mini-c") != 0) {
        fprintf(stderr,
                "compileone: run phase supports language 'mini-c' only "
                "(got '%s')\n", language);
        return 1;
    }

    TokenList tokens = {0};
    LexErrorList errors = {0};
    if (lex_file(input, &tokens, &errors) != 0) {
        fprintf(stderr, "error: cannot open input file '%s'\n", input);
        return 1;
    }

    RunResult result;
    run_result_init(&result);
    const char *status = "ok";
    if (errors.len > 0) {
        status = "lex-error";
        result.exit_code = 1;
    } else {
        run_program(&tokens, &result);
        if (result.error.line > 0) {
            status = "runtime-error";
            result.exit_code = 1;
        }
    }

    double duration_ms = ((double)(clock() - start)) * 1000.0 / CLOCKS_PER_SEC;
    int rc = write_execution_artifact(output, input, language, duration_ms,
                                      &result, status,
                                      errors.len > 0 ? &errors : NULL);

    fprintf(stderr, "run: status=%s exit=%d steps=%d, %.2f ms -> %s\n",
            status, result.exit_code, result.step_count, duration_ms, output);

    run_result_free(&result);
    for (size_t i = 0; i < tokens.len; i++) {
        token_free(&tokens.items[i]);
    }
    for (size_t i = 0; i < errors.len; i++) {
        lex_error_free(&errors.items[i]);
    }
    TokenList_free(&tokens);
    LexErrorList_free(&errors);
    return rc;
}

/* ------------------------------------------------------------------
   Driver
   ------------------------------------------------------------------ */

static void print_version(void) {
    printf("compileone v%s\n", CO1_VERSION);
}

static void list_phases(void) {
    for (size_t i = 0; i < kPhaseCount; i++) {
        printf("%s\n", kPhases[i]);
    }
}

static void print_usage(void) {
    printf(
        "compileone — CompileOne compiler backend\n"
        "usage: compileone <phase> --input <path> --output <path> [--language <lang>]\n"
        "\n"
        "phases (in pipeline order):\n");
    for (size_t i = 0; i < kPhaseCount; i++) {
        printf("  %s\n", kPhases[i]);
    }
    printf(
        "\noptions:\n"
        "  --input <path>      source file (lex, run) or prior artifact (all later phases)\n"
        "  --output <path>     artifact JSON file to write\n"
        "  --language <lang>   source language (default: mini-c)\n"
        "  --version           print version\n"
        "  --list-phases       list registered phases\n"
        "  --help              this message\n");
}

int main(int argc, char **argv) {
    const char *phase = NULL;
    const char *input = NULL;
    const char *output = NULL;
    const char *language = "mini-c";

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--version") == 0) {
            print_version();
            return 0;
        } else if (strcmp(a, "--list-phases") == 0) {
            list_phases();
            return 0;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(a, "--input") == 0 && i + 1 < argc) {
            input = argv[++i];
        } else if (strcmp(a, "--output") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (strcmp(a, "--language") == 0 && i + 1 < argc) {
            language = argv[++i];
        } else if (a[0] == '-') {
            fprintf(stderr, "compileone: unknown option '%s'\n", a);
            print_usage();
            return 1;
        } else {
            phase = a;
        }
    }

    if (!phase) {
        print_usage();
        return 1;
    }

    if (strcmp(phase, "lex") == 0) {
        if (!input || !output) {
            fprintf(stderr, "compileone: lex requires --input and --output\n");
            return 1;
        }
        return cmd_lex(input, output, language);
    }

    if (strcmp(phase, "run") == 0) {
        if (!input || !output) {
            fprintf(stderr, "compileone: run requires --input and --output\n");
            return 1;
        }
        return cmd_run(input, output, language);
    }

    /* Phases registered for the full pipeline but not implemented yet. */
    for (size_t i = 0; i < kPhaseCount; i++) {
        if (strcmp(phase, kPhases[i]) == 0) {
            fprintf(stderr,
                    "compileone: phase '%s' is registered but not implemented "
                    "yet (roadmap Phase C+)\n", phase);
            return 2;
        }
    }

    fprintf(stderr, "compileone: unknown phase '%s'\n", phase);
    return 1;
}
