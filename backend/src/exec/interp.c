/* ============================================================
   mini-c interpreter (the "run" phase).

   A small recursive-descent interpreter that consumes the token
   stream produced by the flex lexer and executes a useful subset of
   the study language:

     - top-level statements and blocks
     - variable declarations: int/float/bool/char, optional `const`
     - assignment (read-only checks on const variables)
     - print <expr>;  and  print <string>;
     - if/else, while, for
     - return (from the implicit top-level main / from any scope)
     - expressions: literals, identifiers, unary - and !,
       + - * / % < <= > >= == != && || with C precedence

   Function *definitions* are skipped (they are not callable yet in the
   study subset); any call site reports a clear "not implemented" error.

   The interpreter never looks at source text — it works exclusively on
   the same token array the lexer emits to the JSON artifact.
   ============================================================ */

#include "interp.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "darray.h"

/* ---------------------------------------------------------------- values */

typedef struct Value {
    int is_float;   /* 0: integer semantics, 1: float semantics */
    double d;
} Value;

static Value val_int(double v) {
    Value x;
    x.is_float = 0;
    x.d = v;
    return x;
}

static Value val_float(double v) {
    Value x;
    x.is_float = 1;
    x.d = v;
    return x;
}

static int val_truthy(Value v) {
    return v.d != 0.0;
}

/* ---------------------------------------------------------------- variables */

typedef struct Var {
    char *name;
    int is_float;
    int is_const;
    double value;
} Var;

DARRAY_DECLARE(Var, VarList)
DARRAY_DEFINE(Var, VarList)

/* ---------------------------------------------------------------- interpreter state */

typedef struct Interp {
    Token *tokens;
    size_t count;
    size_t pos;
    VarList vars;
    RunResult *result;
    int halted;
    int returning;
} Interp;

#define LOOP_LIMIT 1000000

/* ---------------------------------------------------------------- helpers */

static void interp_error(Interp *ip, Token *tok, const char *fmt, ...) {
    if (ip->halted) {
        return;
    }
    ip->halted = 1;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    free(ip->result->error.message);
    ip->result->error.message = co1_strdup(buf);
    ip->result->error.line = tok ? tok->line : 0;
    ip->result->error.column = tok ? tok->column : 0;
}

static void skip_comments(Interp *ip) {
    while (ip->pos < ip->count && ip->tokens[ip->pos].kind == TOK_COMMENT) {
        ip->pos++;
    }
}

static TokenKind peek_kind(Interp *ip) {
    skip_comments(ip);
    return ip->pos < ip->count ? ip->tokens[ip->pos].kind : TOK_EOF;
}

static Token *peek_token(Interp *ip) {
    skip_comments(ip);
    return ip->pos < ip->count ? &ip->tokens[ip->pos] : NULL;
}

static Token *next_token(Interp *ip) {
    skip_comments(ip);
    if (ip->pos >= ip->count) {
        return NULL;
    }
    return &ip->tokens[ip->pos++];
}

static int expect_kind(Interp *ip, TokenKind kind, const char *what) {
    if (peek_kind(ip) != kind) {
        Token *tok = peek_token(ip);
        interp_error(ip, tok, "expected %s", what);
        return 0;
    }
    next_token(ip);
    return 1;
}

static void count_step(Interp *ip) {
    ip->result->step_count++;
}

/* ---------------------------------------------------------------- variables */

static Var *find_var(Interp *ip, const char *name) {
    for (size_t i = ip->vars.len; i-- > 0;) {
        if (strcmp(ip->vars.items[i].name, name) == 0) {
            return &ip->vars.items[i];
        }
    }
    return NULL;
}

static Var *declare_var(Interp *ip, const char *name, int is_float, int is_const) {
    Var v;
    v.name = co1_strdup(name);
    v.is_float = is_float;
    v.is_const = is_const;
    v.value = 0;
    VarList_push(&ip->vars, v);
    return &ip->vars.items[ip->vars.len - 1];
}

/* ---------------------------------------------------------------- expressions */

static Value eval_expr(Interp *ip);
static Value eval_or(Interp *ip);
static Value eval_and(Interp *ip);
static Value eval_eq(Interp *ip);
static Value eval_rel(Interp *ip);
static Value eval_add(Interp *ip);
static Value eval_mul(Interp *ip);
static Value eval_unary(Interp *ip);
static Value eval_primary(Interp *ip);

static Value eval_or(Interp *ip) {
    Value l = eval_and(ip);
    while (!ip->halted && peek_kind(ip) == TOK_OR) {
        next_token(ip);
        if (val_truthy(l)) {
            eval_and(ip);
            l = val_int(1);
        } else {
            l = val_int(val_truthy(eval_and(ip)));
        }
    }
    return l;
}

static Value eval_and(Interp *ip) {
    Value l = eval_eq(ip);
    while (!ip->halted && peek_kind(ip) == TOK_AND) {
        next_token(ip);
        if (!val_truthy(l)) {
            eval_eq(ip);
            l = val_int(0);
        } else {
            l = val_int(val_truthy(eval_eq(ip)));
        }
    }
    return l;
}

static Value eval_eq(Interp *ip) {
    Value l = eval_rel(ip);
    while (!ip->halted &&
           (peek_kind(ip) == TOK_EQ || peek_kind(ip) == TOK_NEQ)) {
        int is_eq = peek_kind(ip) == TOK_EQ;
        next_token(ip);
        Value r = eval_rel(ip);
        int eq = l.d == r.d;
        l = val_int(is_eq ? eq : !eq);
    }
    return l;
}

static Value eval_rel(Interp *ip) {
    Value l = eval_add(ip);
    while (!ip->halted) {
        TokenKind k = peek_kind(ip);
        if (k != TOK_LT && k != TOK_GT && k != TOK_LE && k != TOK_GE) {
            break;
        }
        next_token(ip);
        Value r = eval_add(ip);
        int res = 0;
        if (k == TOK_LT) {
            res = l.d < r.d;
        } else if (k == TOK_GT) {
            res = l.d > r.d;
        } else if (k == TOK_LE) {
            res = l.d <= r.d;
        } else {
            res = l.d >= r.d;
        }
        l = val_int(res);
    }
    return l;
}

static Value eval_add(Interp *ip) {
    Value l = eval_mul(ip);
    while (!ip->halted &&
           (peek_kind(ip) == TOK_ADD || peek_kind(ip) == TOK_SUB)) {
        int is_add = peek_kind(ip) == TOK_ADD;
        next_token(ip);
        Value r = eval_mul(ip);
        Value out;
        out.is_float = l.is_float || r.is_float;
        out.d = is_add ? l.d + r.d : l.d - r.d;
        l = out;
    }
    return l;
}

static Value eval_mul(Interp *ip) {
    Value l = eval_unary(ip);
    while (!ip->halted) {
        TokenKind k = peek_kind(ip);
        if (k != TOK_MUL && k != TOK_DIV && k != TOK_MOD) {
            break;
        }
        Token *op = peek_token(ip);
        next_token(ip);
        Value r = eval_unary(ip);
        if (r.d == 0.0 && k != TOK_MOD) {
            interp_error(ip, op, "division by zero");
            return l;
        }
        Value out;
        if (k == TOK_MOD) {
            if (l.is_float || r.is_float) {
                interp_error(ip, op, "modulo requires integer operands");
                return l;
            }
            out = val_int((double)((long long)l.d % (long long)r.d));
        } else {
            out.is_float = l.is_float || r.is_float;
            out.d = (k == TOK_MUL) ? l.d * r.d : l.d / r.d;
        }
        l = out;
    }
    return l;
}

static Value eval_unary(Interp *ip) {
    TokenKind k = peek_kind(ip);
    if (k == TOK_SUB) {
        next_token(ip);
        Value v = eval_unary(ip);
        v.d = -v.d;
        return v;
    }
    if (k == TOK_NOT) {
        next_token(ip);
        Value v = eval_unary(ip);
        return val_int(!val_truthy(v));
    }
    return eval_primary(ip);
}

static Value eval_primary(Interp *ip) {
    Token *tok = next_token(ip);
    if (!tok) {
        interp_error(ip, NULL, "unexpected end of input in expression");
        return val_int(0);
    }
    switch (tok->kind) {
    case TOK_INT_LITERAL:
        return val_int((double)strtol(tok->lexeme, NULL, 10));
    case TOK_FLOAT_LITERAL:
        return val_float(strtod(tok->lexeme, NULL));
    case TOK_TRUE:
        return val_int(1);
    case TOK_FALSE:
        return val_int(0);
    case TOK_IDENTIFIER:
        if (peek_kind(ip) == TOK_LPAREN) {
            interp_error(ip, tok,
                         "function call '%s(...)' is not supported yet "
                         "(roadmap Phase D)", tok->lexeme);
            return val_int(0);
        }
        {
            Var *v = find_var(ip, tok->lexeme);
            if (!v) {
                interp_error(ip, tok, "undefined variable '%s'", tok->lexeme);
                return val_int(0);
            }
            Value out;
            out.is_float = v->is_float;
            out.d = v->value;
            return out;
        }
    case TOK_LPAREN: {
        Value v = eval_expr(ip);
        expect_kind(ip, TOK_RPAREN, "')'");
        return v;
    }
    default:
        interp_error(ip, tok, "unexpected token '%s' in expression",
                     tok->lexeme);
        return val_int(0);
    }
}

static Value eval_expr(Interp *ip) {
    return eval_or(ip);
}

/* ---------------------------------------------------------------- statements */

static void exec_statement(Interp *ip, int top_level);
static void exec_body(Interp *ip);
static void skip_body(Interp *ip);

static void write_string_literal(Interp *ip, Token *tok) {
    /* lexeme includes the surrounding quotes */
    size_t len = tok->length;
    const char *s = tok->lexeme;
    if (len >= 2 && s[0] == '"') {
        s += 1;
        len -= 2;
    }
    for (size_t i = 0; i < len && !ip->halted; i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < len) {
            char esc = s[++i];
            switch (esc) {
            case 'n':
                c = '\n';
                break;
            case 't':
                c = '\t';
                break;
            case 'r':
                c = '\r';
                break;
            case '\\':
                c = '\\';
                break;
            case '"':
                c = '"';
                break;
            case '0':
                c = '\0';
                break;
            default:
                c = esc;
                break;
            }
        }
        strbuf_append_char(&ip->result->output, c);
    }
}

/* Declares a variable (optionally initialised). Does NOT consume ';'. */
static Var *parse_decl(Interp *ip, int is_const) {
    TokenKind k = peek_kind(ip);
    if (k != TOK_INT && k != TOK_FLOAT && k != TOK_BOOL && k != TOK_CHAR) {
        Token *tok = peek_token(ip);
        interp_error(ip, tok, "expected a type in declaration");
        return NULL;
    }
    int is_float = (k == TOK_FLOAT);
    next_token(ip);
    Token *name = next_token(ip);
    if (!name || name->kind != TOK_IDENTIFIER) {
        interp_error(ip, name, "expected a variable name");
        return NULL;
    }
    if (peek_kind(ip) == TOK_ASSIGN) {
        next_token(ip);
        Value v = eval_expr(ip);
        if (!ip->halted) {
            Var *var = declare_var(ip, name->lexeme, is_float, is_const);
            var->value = v.d;
            return var;
        }
        return NULL;
    }
    return declare_var(ip, name->lexeme, is_float, is_const);
}

/* Executes an assignment. Does NOT consume ';'. */
static void exec_assignment(Interp *ip) {
    Token *name = next_token(ip);
    if (!name || name->kind != TOK_IDENTIFIER) {
        interp_error(ip, name, "expected a variable name");
        return;
    }
    Var *v = find_var(ip, name->lexeme);
    if (!v) {
        interp_error(ip, name, "undefined variable '%s'", name->lexeme);
        return;
    }
    if (v->is_const) {
        interp_error(ip, name, "cannot assign to const variable '%s'",
                     name->lexeme);
        return;
    }
    if (peek_kind(ip) != TOK_ASSIGN) {
        interp_error(ip, name, "expected '=' after '%s'", name->lexeme);
        return;
    }
    next_token(ip);
    Value val = eval_expr(ip);
    if (!ip->halted) {
        v->value = val.d;
    }
}

static void exec_print(Interp *ip) {
    next_token(ip); /* print */
    if (peek_kind(ip) == TOK_STRING_LITERAL) {
        Token *str = next_token(ip);
        if (peek_kind(ip) == TOK_SEMICOLON) {
            next_token(ip);
            write_string_literal(ip, str);
            strbuf_append_char(&ip->result->output, '\n');
            return;
        }
        interp_error(ip, str,
                     "string literals may only appear alone in print "
                     "statements");
        return;
    }
    Value v = eval_expr(ip);
    if (ip->halted) {
        return;
    }
    if (!expect_kind(ip, TOK_SEMICOLON, "';' after print")) {
        return;
    }
    if (v.is_float) {
        strbuf_append_double(&ip->result->output, v.d, 6);
    } else {
        strbuf_append_int(&ip->result->output, (long long)v.d);
    }
    strbuf_append_char(&ip->result->output, '\n');
}

static void exec_if(Interp *ip) {
    next_token(ip); /* if */
    if (!expect_kind(ip, TOK_LPAREN, "'(' after if")) {
        return;
    }
    Value cond = eval_expr(ip);
    if (ip->halted) {
        return;
    }
    if (!expect_kind(ip, TOK_RPAREN, "')' after if condition")) {
        return;
    }
    if (val_truthy(cond)) {
        exec_body(ip);
        if (peek_kind(ip) == TOK_ELSE) {
            next_token(ip);
            skip_body(ip);
        }
    } else {
        skip_body(ip);
        if (peek_kind(ip) == TOK_ELSE) {
            next_token(ip);
            exec_body(ip);
        }
    }
}

static void exec_while(Interp *ip) {
    next_token(ip); /* while */
    if (!expect_kind(ip, TOK_LPAREN, "'(' after while")) {
        return;
    }
    size_t cond_start = ip->pos;
    int iterations = 0;
    for (;;) {
        ip->pos = cond_start;
        Value cond = eval_expr(ip);
        if (ip->halted) {
            return;
        }
        if (!expect_kind(ip, TOK_RPAREN, "')' after while condition")) {
            return;
        }
        if (!val_truthy(cond)) {
            skip_body(ip);
            return;
        }
        if (++iterations > LOOP_LIMIT) {
            interp_error(ip, NULL, "loop iteration limit exceeded (%d)",
                         LOOP_LIMIT);
            return;
        }
        exec_body(ip);
        if (ip->halted || ip->returning) {
            return;
        }
    }
}

static void exec_for(Interp *ip) {
    next_token(ip); /* for */
    if (!expect_kind(ip, TOK_LPAREN, "'(' after for")) {
        return;
    }
    /* init clause: declaration, assignment, or empty */
    TokenKind k = peek_kind(ip);
    if (k == TOK_CONST) {
        next_token(ip);
        parse_decl(ip, 1);
    } else if (k == TOK_INT || k == TOK_FLOAT || k == TOK_BOOL || k == TOK_CHAR) {
        parse_decl(ip, 0);
    } else if (k == TOK_IDENTIFIER) {
        exec_assignment(ip);
    }
    if (!expect_kind(ip, TOK_SEMICOLON, "';' in for")) {
        return;
    }
    size_t cond_start = ip->pos;
    int iterations = 0;
    for (;;) {
        ip->pos = cond_start;
        Value cond = eval_expr(ip);
        if (ip->halted) {
            return;
        }
        if (!expect_kind(ip, TOK_SEMICOLON, "';' after for condition")) {
            return;
        }
        /* step clause (assignment or empty), then ')' */
        if (peek_kind(ip) == TOK_IDENTIFIER) {
            exec_assignment(ip);
        } else if (peek_kind(ip) != TOK_RPAREN) {
            /* bare expression step: evaluate and discard */
            eval_expr(ip);
        }
        if (ip->halted) {
            return;
        }
        if (!expect_kind(ip, TOK_RPAREN, "')' after for step")) {
            return;
        }
        if (!val_truthy(cond)) {
            skip_body(ip);
            return;
        }
        if (++iterations > LOOP_LIMIT) {
            interp_error(ip, NULL, "loop iteration limit exceeded (%d)",
                         LOOP_LIMIT);
            return;
        }
        exec_body(ip);
        if (ip->halted || ip->returning) {
            return;
        }
    }
}

static void exec_return(Interp *ip) {
    next_token(ip); /* return */
    Value v = val_int(0);
    if (peek_kind(ip) != TOK_SEMICOLON) {
        v = eval_expr(ip);
    }
    if (ip->halted) {
        return;
    }
    if (!expect_kind(ip, TOK_SEMICOLON, "';' after return")) {
        return;
    }
    ip->returning = 1;
    ip->result->exit_code = (int)v.d;
}

/* Skips a function definition: name ( params ) { body }. */
static void skip_function(Interp *ip, Token *name) {
    if (peek_kind(ip) == TOK_LPAREN) {
        next_token(ip);
        int depth = 1;
        while (!ip->halted && depth > 0) {
            TokenKind k = peek_kind(ip);
            if (k == TOK_EOF) {
                break;
            }
            next_token(ip);
            if (k == TOK_LPAREN) {
                depth++;
            } else if (k == TOK_RPAREN) {
                depth--;
            }
        }
    }
    if (peek_kind(ip) == TOK_LBRACE) {
        next_token(ip);
        int depth = 1;
        while (!ip->halted && depth > 0) {
            TokenKind k = peek_kind(ip);
            if (k == TOK_EOF) {
                break;
            }
            next_token(ip);
            if (k == TOK_LBRACE) {
                depth++;
            } else if (k == TOK_RBRACE) {
                depth--;
            }
        }
    } else {
        interp_error(ip, name,
                     "expected '{' in function definition '%s'", name->lexeme);
    }
}

/* ---------------------------------------------------------------- bodies */

static void exec_block(Interp *ip) {
    if (!expect_kind(ip, TOK_LBRACE, "'{'")) {
        return;
    }
    while (!ip->halted) {
        TokenKind k = peek_kind(ip);
        if (k == TOK_EOF || k == TOK_RBRACE) {
            break;
        }
        exec_statement(ip, 0);
        if (ip->returning) {
            break;
        }
    }
    if (!ip->halted) {
        expect_kind(ip, TOK_RBRACE, "'}'");
    }
}

static void exec_body(Interp *ip) {
    if (peek_kind(ip) == TOK_LBRACE) {
        exec_block(ip);
    } else {
        exec_statement(ip, 0);
    }
}

static void skip_statement(Interp *ip) {
    TokenKind k = peek_kind(ip);
    switch (k) {
    case TOK_EOF:
        return;
    case TOK_LBRACE:
        skip_body(ip);
        return;
    case TOK_IF:
        next_token(ip);
        if (peek_kind(ip) == TOK_LPAREN) {
            next_token(ip);
            int depth = 1;
            while (!ip->halted && depth > 0) {
                TokenKind t = peek_kind(ip);
                if (t == TOK_EOF) {
                    break;
                }
                next_token(ip);
                if (t == TOK_LPAREN) {
                    depth++;
                } else if (t == TOK_RPAREN) {
                    depth--;
                }
            }
        }
        skip_body(ip);
        if (peek_kind(ip) == TOK_ELSE) {
            next_token(ip);
            skip_body(ip);
        }
        return;
    case TOK_WHILE:
    case TOK_FOR:
        next_token(ip);
        if (peek_kind(ip) == TOK_LPAREN) {
            next_token(ip);
            int depth = 1;
            while (!ip->halted && depth > 0) {
                TokenKind t = peek_kind(ip);
                if (t == TOK_EOF) {
                    break;
                }
                next_token(ip);
                if (t == TOK_LPAREN) {
                    depth++;
                } else if (t == TOK_RPAREN) {
                    depth--;
                }
            }
        }
        skip_body(ip);
        return;
    default:
        /* declaration / assignment / print / return / empty / block:
           skip to the terminating ';' */
        if (k == TOK_CONST || k == TOK_INT || k == TOK_FLOAT ||
            k == TOK_BOOL || k == TOK_CHAR || k == TOK_IDENTIFIER ||
            k == TOK_PRINT || k == TOK_RETURN || k == TOK_SEMICOLON) {
            next_token(ip);
            int depth = 0;
            while (!ip->halted) {
                TokenKind t = peek_kind(ip);
                if (t == TOK_EOF) {
                    break;
                }
                if (t == TOK_LPAREN) {
                    depth++;
                } else if (t == TOK_RPAREN) {
                    if (depth > 0) {
                        depth--;
                    }
                } else if (t == TOK_SEMICOLON && depth == 0) {
                    next_token(ip);
                    return;
                }
                next_token(ip);
            }
            return;
        }
        next_token(ip);
        return;
    }
}

static void skip_body(Interp *ip) {
    if (peek_kind(ip) == TOK_LBRACE) {
        next_token(ip);
        int depth = 1;
        while (!ip->halted && depth > 0) {
            TokenKind k = peek_kind(ip);
            if (k == TOK_EOF) {
                break;
            }
            next_token(ip);
            if (k == TOK_LBRACE) {
                depth++;
            } else if (k == TOK_RBRACE) {
                depth--;
            }
        }
    } else {
        skip_statement(ip);
    }
}

/* ---------------------------------------------------------------- dispatch */

static void exec_statement(Interp *ip, int top_level) {
    count_step(ip);
    TokenKind k = peek_kind(ip);
    Token *tok = peek_token(ip);

    if (k == TOK_CONST) {
        next_token(ip);
        parse_decl(ip, 1);
        expect_kind(ip, TOK_SEMICOLON, "';' after declaration");
        return;
    }
    if (k == TOK_INT || k == TOK_FLOAT || k == TOK_BOOL || k == TOK_CHAR) {
        if (top_level && !ip->halted) {
            /* possible function definition: type name '(' ... */
            if (ip->pos + 1 < ip->count &&
                ip->tokens[ip->pos + 1].kind == TOK_IDENTIFIER &&
                ip->pos + 2 < ip->count &&
                ip->tokens[ip->pos + 2].kind == TOK_LPAREN) {
                Token *name = &ip->tokens[ip->pos + 1];
                next_token(ip); /* type */
                next_token(ip); /* name */
                skip_function(ip, name);
                return;
            }
        }
        parse_decl(ip, 0);
        expect_kind(ip, TOK_SEMICOLON, "';' after declaration");
        return;
    }
    if (k == TOK_IDENTIFIER) {
        exec_assignment(ip);
        expect_kind(ip, TOK_SEMICOLON, "';' after assignment");
        return;
    }
    if (k == TOK_PRINT) {
        exec_print(ip);
        return;
    }
    if (k == TOK_IF) {
        exec_if(ip);
        return;
    }
    if (k == TOK_WHILE) {
        exec_while(ip);
        return;
    }
    if (k == TOK_FOR) {
        exec_for(ip);
        return;
    }
    if (k == TOK_RETURN) {
        exec_return(ip);
        return;
    }
    if (k == TOK_SEMICOLON) {
        next_token(ip);
        return;
    }
    if (k == TOK_LBRACE) {
        exec_block(ip);
        return;
    }
    interp_error(ip, tok, "unexpected token '%s'",
                 tok ? tok->lexeme : "end of file");
}

static void run_program_body(Interp *ip) {
    while (!ip->halted && !ip->returning) {
        TokenKind k = peek_kind(ip);
        if (k == TOK_EOF) {
            break;
        }
        exec_statement(ip, 1);
    }
}

/* ---------------------------------------------------------------- public API */

void run_result_init(RunResult *r) {
    r->output.data = NULL;
    r->output.len = 0;
    r->output.cap = 0;
    r->exit_code = 0;
    r->step_count = 0;
    r->error.line = 0;
    r->error.column = 0;
    r->error.message = NULL;
}

void run_result_free(RunResult *r) {
    strbuf_free(&r->output);
    free(r->error.message);
    r->error.message = NULL;
}

int run_program(TokenList *tokens, RunResult *r) {
    Interp ip;
    ip.tokens = tokens->items;
    ip.count = tokens->len;
    ip.pos = 0;
    ip.vars = (VarList){0};
    ip.result = r;
    ip.halted = 0;
    ip.returning = 0;

    run_program_body(&ip);

    for (size_t i = 0; i < ip.vars.len; i++) {
        free(ip.vars.items[i].name);
    }
    VarList_free(&ip.vars);
    return ip.halted ? 1 : 0;
}
