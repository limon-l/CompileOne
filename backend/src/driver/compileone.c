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

#include "artifact_loader.h"
#include "codegen.h"
#include "interp.h"
#include "ir.h"
#include "json_writer.h"
#include "lexer.h"
#include "optimize.h"
#include "parser.h"
#include "semantic.h"
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

/* Serialise `w` to `output_path`. Returns 0 on success, -1 on failure. */
static int write_artifact_file(const char *output_path, JsonWriter *w) {
    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        fprintf(stderr, "error: cannot open output file '%s'\n", output_path);
        return -1;
    }
    fwrite(jw_cstr(w), 1, jw_len(w), fp);
    fclose(fp);
    return 0;
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
   Phases: parse, ast, semantic

   All three consume the token-stream artifact and re-run the front end
   in-process (the single parse pass yields both the CST and the AST).
   Each writes its own inspectable artifact.
   ------------------------------------------------------------------ */

static int load_input_tokens(const char *input, TokenList *tokens) {
    const char *err = NULL;
    if (load_token_stream(input, tokens, &err) != 0) {
        fprintf(stderr, "compileone: cannot load token-stream artifact "
                        "'%s' (%s)\n", input, err ? err : "unknown error");
        return -1;
    }
    return 0;
}

static void write_syntax_errors(JsonWriter *w, const SyntaxErrorList *errors) {
    jw_key(w, "errors");
    jw_begin_array(w);
    for (size_t i = 0; i < errors->len; i++) {
        const SyntaxError *e = &errors->items[i];
        jw_begin_object(w);
        jw_key(w, "line");     jw_int(w, e->line);
        jw_key(w, "column");   jw_int(w, e->column);
        jw_key(w, "message");  jw_string(w, e->message);
        jw_end_object(w);
    }
    jw_end_array(w);
}

static int cmd_parse(const char *input, const char *output, const char *language) {
    clock_t start = clock();

    TokenList tokens = {0};
    if (load_input_tokens(input, &tokens) != 0) {
        return 1;
    }

    ParseResult pr;
    parse_tokens(&tokens, &pr);

    double duration_ms = ((double)(clock() - start)) * 1000.0 / CLOCKS_PER_SEC;

    JsonWriter w;
    jw_init(&w);
    jw_begin_object(&w);
    jw_key(&w, "schema");
    jw_string(&w, "compileone/parse-tree/1.0");
    jw_key(&w, "phase");
    jw_string(&w, "parse");
    jw_key(&w, "language");
    jw_string(&w, language);
    jw_key(&w, "source_file");
    jw_string(&w, input);
    jw_key(&w, "generated_by");
    jw_string(&w, "compileone.exe v" CO1_VERSION " (recursive-descent parser)");
    jw_key(&w, "duration_ms");
    jw_double(&w, duration_ms, 4);
    jw_key(&w, "root");
    cst_to_json(&w, pr.cst_root);
    write_syntax_errors(&w, &pr.errors);
    jw_end_object(&w);

    int rc = write_artifact_file(output, &w);
    jw_free(&w);

    fprintf(stderr, "parse: %lu node(s), %lu errors, %.2f ms -> %s\n",
            (unsigned long)(pr.errors.len > 0 ? 0 : 1),
            (unsigned long)pr.errors.len, duration_ms, output);

    parse_result_free(&pr);
    for (size_t i = 0; i < tokens.len; i++) {
        token_free(&tokens.items[i]);
    }
    TokenList_free(&tokens);
    return rc;
}

static int cmd_ast(const char *input, const char *output, const char *language) {
    clock_t start = clock();

    TokenList tokens = {0};
    if (load_input_tokens(input, &tokens) != 0) {
        return 1;
    }

    ParseResult pr;
    parse_tokens(&tokens, &pr);

    double duration_ms = ((double)(clock() - start)) * 1000.0 / CLOCKS_PER_SEC;

    JsonWriter w;
    jw_init(&w);
    jw_begin_object(&w);
    jw_key(&w, "schema");
    jw_string(&w, "compileone/ast/1.0");
    jw_key(&w, "phase");
    jw_string(&w, "ast");
    jw_key(&w, "language");
    jw_string(&w, language);
    jw_key(&w, "source_file");
    jw_string(&w, input);
    jw_key(&w, "generated_by");
    jw_string(&w, "compileone.exe v" CO1_VERSION " (recursive-descent parser)");
    jw_key(&w, "duration_ms");
    jw_double(&w, duration_ms, 4);
    jw_key(&w, "root");
    ast_to_json(&w, pr.ast_root);
    write_syntax_errors(&w, &pr.errors);
    jw_end_object(&w);

    int rc = write_artifact_file(output, &w);
    jw_free(&w);

    fprintf(stderr, "ast: %lu error(s), %.2f ms -> %s\n",
            (unsigned long)pr.errors.len, duration_ms, output);

    parse_result_free(&pr);
    for (size_t i = 0; i < tokens.len; i++) {
        token_free(&tokens.items[i]);
    }
    TokenList_free(&tokens);
    return rc;
}

static int cmd_semantic(const char *input, const char *output, const char *language) {
    clock_t start = clock();

    TokenList tokens = {0};
    if (load_input_tokens(input, &tokens) != 0) {
        return 1;
    }

    ParseResult pr;
    parse_tokens(&tokens, &pr);

    SemanticResult sr;
    semantic_analyze(pr.ast_root, &sr);

    double duration_ms = ((double)(clock() - start)) * 1000.0 / CLOCKS_PER_SEC;

    JsonWriter w;
    jw_init(&w);
    jw_begin_object(&w);
    jw_key(&w, "schema");
    jw_string(&w, "compileone/semantic/1.0");
    jw_key(&w, "phase");
    jw_string(&w, "semantic");
    jw_key(&w, "language");
    jw_string(&w, language);
    jw_key(&w, "source_file");
    jw_string(&w, input);
    jw_key(&w, "generated_by");
    jw_string(&w, "compileone.exe v" CO1_VERSION " (semantic analyzer)");
    jw_key(&w, "duration_ms");
    jw_double(&w, duration_ms, 4);
    jw_key(&w, "valid");
    jw_bool(&w, sr.valid);

    jw_key(&w, "symbols");
    jw_begin_array(&w);
    for (size_t i = 0; i < sr.symbols.len; i++) {
        const Symbol *s = &sr.symbols.items[i];
        jw_begin_object(&w);
        jw_key(&w, "name");       jw_string(&w, s->name);
        jw_key(&w, "type");       jw_string(&w, s->type);
        jw_key(&w, "scope");      jw_string(&w, s->scope);
        jw_key(&w, "scope_level"); jw_int(&w, s->scope_level);
        jw_key(&w, "is_const");   jw_bool(&w, s->is_const);
        jw_key(&w, "line");       jw_int(&w, s->line);
        jw_key(&w, "column");     jw_int(&w, s->column);
        jw_end_object(&w);
    }
    jw_end_array(&w);

    jw_key(&w, "diagnostics");
    jw_begin_array(&w);
    for (size_t i = 0; i < sr.diagnostics.len; i++) {
        const SemanticDiagnostic *d = &sr.diagnostics.items[i];
        jw_begin_object(&w);
        jw_key(&w, "severity");   jw_string(&w, d->severity);
        jw_key(&w, "code");       jw_string(&w, d->code);
        jw_key(&w, "line");       jw_int(&w, d->line);
        jw_key(&w, "column");     jw_int(&w, d->column);
        jw_key(&w, "message");    jw_string(&w, d->message);
        jw_end_object(&w);
    }
    jw_end_array(&w);

    jw_end_object(&w);

    int rc = write_artifact_file(output, &w);
    jw_free(&w);

    fprintf(stderr, "semantic: valid=%d symbols=%lu diagnostics=%lu, "
                    "%.2f ms -> %s\n",
            sr.valid, (unsigned long)sr.symbols.len,
            (unsigned long)sr.diagnostics.len, duration_ms, output);

    semantic_result_free(&sr);
    parse_result_free(&pr);
    for (size_t i = 0; i < tokens.len; i++) {
        token_free(&tokens.items[i]);
    }
    TokenList_free(&tokens);
    return rc;
}

/* ------------------------------------------------------------------
   Phases: ir, opt, codegen
   ------------------------------------------------------------------ */

/* Serialise one quad as {"index","op","arg1","arg2","result"} (or the
   compact a/b/r keys when `abr` is set, matching the quadruple view). */
static void write_quad_json(JsonWriter *w, const IrQuad *q, int abr) {
    jw_begin_object(w);
    jw_key(w, "index");
    jw_int(w, q->index);
    jw_key(w, "op");
    jw_string(w, q->op);
    if (abr) {
        jw_key(w, "a");
        jw_string(w, q->arg1 ? q->arg1 : "");
        jw_key(w, "b");
        jw_string(w, q->arg2 ? q->arg2 : "");
        jw_key(w, "r");
        jw_string(w, q->result ? q->result : "");
    } else {
        jw_key(w, "arg1");
        jw_string(w, q->arg1 ? q->arg1 : "");
        jw_key(w, "arg2");
        jw_string(w, q->arg2 ? q->arg2 : "");
        jw_key(w, "result");
        jw_string(w, q->result ? q->result : "");
    }
    jw_end_object(w);
}

static void write_ir_artifact(const char *output_path, const char *input_path,
                              const char *language, double duration_ms,
                              const IrQuadList *quads, const SyntaxErrorList *errors) {
    JsonWriter w;
    jw_init(&w);
    jw_begin_object(&w);
    jw_key(&w, "schema");
    jw_string(&w, "compileone/ir/1.0");
    jw_key(&w, "phase");
    jw_string(&w, "ir");
    jw_key(&w, "language");
    jw_string(&w, language);
    jw_key(&w, "source_file");
    jw_string(&w, input_path);
    jw_key(&w, "generated_by");
    jw_string(&w, "compileone.exe v" CO1_VERSION " (three-address code)");
    jw_key(&w, "duration_ms");
    jw_double(&w, duration_ms, 4);

    jw_key(&w, "tac");
    jw_begin_array(&w);
    for (size_t i = 0; i < quads->len; i++) {
        write_quad_json(&w, &quads->items[i], 0);
    }
    jw_end_array(&w);

    jw_key(&w, "quadruples");
    jw_begin_array(&w);
    for (size_t i = 0; i < quads->len; i++) {
        write_quad_json(&w, &quads->items[i], 1);
    }
    jw_end_array(&w);

    /* temporaries and labels in program order */
    jw_key(&w, "temporaries");
    jw_begin_array(&w);
    for (size_t i = 0; i < quads->len; i++) {
        const IrQuad *q = &quads->items[i];
        if (q->result && q->result[0] == 't' && q->result[1] >= '0' &&
            q->result[1] <= '9') {
            jw_string(&w, q->result);
        }
    }
    jw_end_array(&w);

    jw_key(&w, "labels");
    jw_begin_array(&w);
    for (size_t i = 0; i < quads->len; i++) {
        const IrQuad *q = &quads->items[i];
        if (q->result && q->result[0] == 'L' &&
            (strcmp(q->op, "label") == 0 || strcmp(q->op, "goto") == 0 ||
             strcmp(q->op, "if_false") == 0)) {
            jw_string(&w, q->result);
        }
    }
    jw_end_array(&w);

    write_syntax_errors(&w, errors);
    jw_end_object(&w);

    int rc = write_artifact_file(output_path, &w);
    jw_free(&w);
    fprintf(stderr, "ir: %lu quad(s), %.2f ms -> %s\n",
            (unsigned long)quads->len, duration_ms, output_path);
    (void)rc;
}

static int cmd_ir(const char *input, const char *output, const char *language) {
    clock_t start = clock();

    TokenList tokens = {0};
    if (load_input_tokens(input, &tokens) != 0) {
        return 1;
    }

    ParseResult pr;
    parse_tokens(&tokens, &pr);

    IrQuadList quads;
    ir_build(pr.ast_root, &quads);

    double duration_ms = ((double)(clock() - start)) * 1000.0 / CLOCKS_PER_SEC;
    write_ir_artifact(output, input, language, duration_ms, &quads, &pr.errors);

    ir_list_free(&quads);
    parse_result_free(&pr);
    for (size_t i = 0; i < tokens.len; i++) {
        token_free(&tokens.items[i]);
    }
    TokenList_free(&tokens);
    return 0;
}

static int cmd_opt(const char *input, const char *output, const char *language) {
    clock_t start = clock();

    TokenList tokens = {0};
    if (load_input_tokens(input, &tokens) != 0) {
        return 1;
    }

    ParseResult pr;
    parse_tokens(&tokens, &pr);

    IrQuadList quads;
    ir_build(pr.ast_root, &quads);

    IrQuadList optimized;
    OptReport report;
    optimize(&quads, &optimized, &report);

    double duration_ms = ((double)(clock() - start)) * 1000.0 / CLOCKS_PER_SEC;

    JsonWriter w;
    jw_init(&w);
    jw_begin_object(&w);
    jw_key(&w, "schema");
    jw_string(&w, "compileone/optimization/1.0");
    jw_key(&w, "phase");
    jw_string(&w, "optimization");
    jw_key(&w, "language");
    jw_string(&w, language);
    jw_key(&w, "source_file");
    jw_string(&w, input);
    jw_key(&w, "generated_by");
    jw_string(&w, "compileone.exe v" CO1_VERSION " (optimizer)");
    jw_key(&w, "duration_ms");
    jw_double(&w, duration_ms, 4);

    jw_key(&w, "before_instruction_count");
    jw_int(&w, report.before_count);
    jw_key(&w, "after_instruction_count");
    jw_int(&w, report.after_count);
    jw_key(&w, "instruction_reduction_pct");
    jw_double(&w, report.reduction_pct, 2);

    jw_key(&w, "passes");
    jw_begin_array(&w);
    for (size_t i = 0; i < report.passes.len; i++) {
        const OptPassRecord *rec = &report.passes.items[i];
        jw_begin_object(&w);
        jw_key(&w, "name");
        jw_string(&w, rec->name);
        jw_key(&w, "applied");
        jw_bool(&w, rec->applied);
        jw_key(&w, "explanation");
        jw_string(&w, rec->explanation);
        jw_key(&w, "instruction_reduction");
        jw_int(&w, rec->instructions_removed);

        jw_key(&w, "removed_instructions");
        jw_begin_array(&w);
        for (size_t j = 0; j < rec->before.len; j++) {
            write_quad_json(&w, &rec->before.items[j], 0);
        }
        jw_end_array(&w);

        jw_key(&w, "before");
        jw_begin_array(&w);
        for (size_t j = 0; j < rec->before.len; j++) {
            char *t = ir_quad_text(&rec->before.items[j]);
            jw_string(&w, t);
            free(t);
        }
        jw_end_array(&w);

        jw_key(&w, "after");
        jw_begin_array(&w);
        for (size_t j = 0; j < rec->after.len; j++) {
            char *t = ir_quad_text(&rec->after.items[j]);
            jw_string(&w, t);
            free(t);
        }
        jw_end_array(&w);

        jw_end_object(&w);
    }
    jw_end_array(&w);

    write_syntax_errors(&w, &pr.errors);
    jw_end_object(&w);

    int rc = write_artifact_file(output, &w);
    jw_free(&w);

    fprintf(stderr, "opt: %d -> %d instructions (%.1f%% reduction), %.2f ms -> %s\n",
            report.before_count, report.after_count, report.reduction_pct,
            duration_ms, output);

    opt_report_free(&report);
    ir_list_free(&optimized);
    ir_list_free(&quads);
    parse_result_free(&pr);
    for (size_t i = 0; i < tokens.len; i++) {
        token_free(&tokens.items[i]);
    }
    TokenList_free(&tokens);
    return rc;
}

static void render_insn(const AsmInstruction *ins, char *buf, size_t n) {
    snprintf(buf, n, "%s", ins->mnemonic);
    for (size_t i = 0; i < ins->noperands && i < 2; i++) {
        strncat(buf, i == 0 ? " " : ", ", n - strlen(buf) - 1);
        strncat(buf, ins->operands[i], n - strlen(buf) - 1);
    }
}

static int cmd_codegen(const char *input, const char *output, const char *language) {
    clock_t start = clock();

    TokenList tokens = {0};
    if (load_input_tokens(input, &tokens) != 0) {
        return 1;
    }

    ParseResult pr;
    parse_tokens(&tokens, &pr);

    IrQuadList quads;
    ir_build(pr.ast_root, &quads);

    AsmDoc doc;
    codegen_generate(&quads, &doc);

    double duration_ms = ((double)(clock() - start)) * 1000.0 / CLOCKS_PER_SEC;

    JsonWriter w;
    jw_init(&w);
    jw_begin_object(&w);
    jw_key(&w, "schema");
    jw_string(&w, "compileone/assembly/1.0");
    jw_key(&w, "phase");
    jw_string(&w, "codegen");
    jw_key(&w, "arch");
    jw_string(&w, "x86_64");
    jw_key(&w, "syntax");
    jw_string(&w, "att");
    jw_key(&w, "language");
    jw_string(&w, language);
    jw_key(&w, "source_file");
    jw_string(&w, input);
    jw_key(&w, "generated_by");
    jw_string(&w, "compileone.exe v" CO1_VERSION " (x86-64 codegen)");
    jw_key(&w, "duration_ms");
    jw_double(&w, duration_ms, 4);

    jw_key(&w, "text");
    jw_string(&w, doc.text);

    jw_key(&w, "instructions");
    jw_begin_array(&w);
    for (size_t i = 0; i < doc.instructions.len; i++) {
        const AsmInstruction *ins = &doc.instructions.items[i];
        jw_begin_object(&w);
        jw_key(&w, "address");
        {
            char hex[16];
            snprintf(hex, sizeof(hex), "0x%04x", ins->address);
            jw_string(&w, hex);
        }
        jw_key(&w, "label");
        if (ins->label) {
            jw_string(&w, ins->label);
        } else {
            jw_null(&w);
        }
        jw_key(&w, "mnemonic");
        jw_string(&w, ins->mnemonic);
        jw_key(&w, "operands");
        jw_begin_array(&w);
        for (size_t j = 0; j < ins->noperands; j++) {
            jw_string(&w, ins->operands[j]);
        }
        jw_end_array(&w);
        jw_key(&w, "comment");
        if (ins->comment) {
            jw_string(&w, ins->comment);
        } else {
            jw_null(&w);
        }
        jw_key(&w, "class");
        jw_string(&w, ins->class_name);
        jw_end_object(&w);
    }
    jw_end_array(&w);

    jw_key(&w, "prologue");
    jw_begin_array(&w);
    for (size_t i = 0; i < doc.instructions.len && i < 3; i++) {
        char buf[128];
        render_insn(&doc.instructions.items[i], buf, sizeof(buf));
        jw_string(&w, buf);
    }
    jw_end_array(&w);

    jw_key(&w, "epilogue");
    jw_begin_array(&w);
    {
        size_t n = doc.instructions.len;
        for (size_t i = n > 3 ? n - 3 : 0; i < n; i++) {
            char buf[128];
            render_insn(&doc.instructions.items[i], buf, sizeof(buf));
            jw_string(&w, buf);
        }
    }
    jw_end_array(&w);

    jw_key(&w, "stack_layout");
    jw_begin_object(&w);
    jw_key(&w, "base_pointer");
    jw_string(&w, "%rbp");
    jw_key(&w, "total_size");
    jw_int(&w, doc.stack_size);
    jw_key(&w, "slots");
    jw_begin_array(&w);
    for (size_t i = 0; i < doc.slots.len; i++) {
        const AsmSlot *s = &doc.slots.items[i];
        jw_begin_object(&w);
        jw_key(&w, "name");
        jw_string(&w, s->name);
        jw_key(&w, "offset");
        jw_int(&w, s->offset);
        jw_key(&w, "size");
        jw_int(&w, s->size);
        jw_end_object(&w);
    }
    jw_end_array(&w);
    jw_end_object(&w);

    write_syntax_errors(&w, &pr.errors);
    jw_end_object(&w);

    int rc = write_artifact_file(output, &w);
    jw_free(&w);

    fprintf(stderr, "codegen: %lu instruction(s), stack %d bytes, %.2f ms -> %s\n",
            (unsigned long)doc.instructions.len, doc.stack_size, duration_ms, output);

    asm_doc_free(&doc);
    ir_list_free(&quads);
    parse_result_free(&pr);
    for (size_t i = 0; i < tokens.len; i++) {
        token_free(&tokens.items[i]);
    }
    TokenList_free(&tokens);
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

    if (strcmp(phase, "parse") == 0) {
        if (!input || !output) {
            fprintf(stderr, "compileone: parse requires --input and --output\n");
            return 1;
        }
        return cmd_parse(input, output, language);
    }

    if (strcmp(phase, "ast") == 0) {
        if (!input || !output) {
            fprintf(stderr, "compileone: ast requires --input and --output\n");
            return 1;
        }
        return cmd_ast(input, output, language);
    }

    if (strcmp(phase, "semantic") == 0) {
        if (!input || !output) {
            fprintf(stderr, "compileone: semantic requires --input and --output\n");
            return 1;
        }
        return cmd_semantic(input, output, language);
    }

    if (strcmp(phase, "ir") == 0) {
        if (!input || !output) {
            fprintf(stderr, "compileone: ir requires --input and --output\n");
            return 1;
        }
        return cmd_ir(input, output, language);
    }

    if (strcmp(phase, "opt") == 0) {
        if (!input || !output) {
            fprintf(stderr, "compileone: opt requires --input and --output\n");
            return 1;
        }
        return cmd_opt(input, output, language);
    }

    if (strcmp(phase, "codegen") == 0) {
        if (!input || !output) {
            fprintf(stderr, "compileone: codegen requires --input and --output\n");
            return 1;
        }
        return cmd_codegen(input, output, language);
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
