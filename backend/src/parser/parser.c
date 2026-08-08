/* ============================================================
   mini-c recursive-descent parser.

   Consumes the token array and produces a Concrete Syntax Tree
   (every grammar reduction) and an Abstract Syntax Tree (the
   semantic structure) in a single pass, exactly as a hand-written
   front end does (Clang, GCC's C parser, Python's parser all use
   recursive descent / precedence climbing).

   Grammar (mini-c):

     program      := stmt*
     stmt         := declaration | assignment | read_stmt | print_stmt
                   | if_stmt | while_stmt | for_stmt | return_stmt
                   | incdec_stmt | block | ';'
     declaration  := 'const'? type IDENTIFIER ('=' expr)? ';'
     type         := 'int' | 'float' | 'bool' | 'char'
     assignment   := IDENTIFIER '=' expr ';'
     read_stmt    := 'read' IDENTIFIER ';'
     print_stmt   := 'print' (expr | STRING_LITERAL) ';'
     if_stmt      := 'if' '(' expr ')' stmt ('else' stmt)?
     while_stmt   := 'while' '(' expr ')' stmt
     for_stmt     := 'for' '(' for_init? ';' expr? ';' for_step? ')' stmt
     return_stmt  := 'return' expr? ';'
     incdec_stmt  := IDENTIFIER ('++' | '--') ';'
     block        := '{' stmt* '}'
     expr         := or (precedence climbing)
     unary        := '-' unary | '!' unary | ('++'|'--') unary | primary
     primary      := INT/FLOAT/BOOL/STRING literal | IDENTIFIER
                   | '(' expr ')' | postfix '++'/'--'
   ============================================================ */

#include "parser.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_writer.h"
#include "strbuf.h"

DARRAY_DEFINE(SyntaxError, SyntaxErrorList)

/* ============================================================
   CST node helpers
   ============================================================ */

static CSTNode *cst_rule(const char *name) {
    CSTNode *n = (CSTNode *)calloc(1, sizeof(CSTNode));
    n->rule_name = name;
    return n;
}

static CSTNode *cst_leaf(Token *tok) {
    CSTNode *n = (CSTNode *)calloc(1, sizeof(CSTNode));
    n->token = tok;
    return n;
}

static void cst_add(CSTNode *parent, CSTNode *child) {
    if (!child) {
        return;
    }
    parent->children = (CSTNode **)realloc(
        parent->children, (parent->child_count + 1) * sizeof(CSTNode *));
    parent->children[parent->child_count++] = child;
}

void cst_free(CSTNode *node) {
    if (!node) {
        return;
    }
    for (size_t i = 0; i < node->child_count; i++) {
        cst_free(node->children[i]);
    }
    free(node->children);
    free(node);
}

/* ============================================================
   AST node helpers
   ============================================================ */

static int g_ast_next_id = 1;

static ASTNode *ast_make(const char *type, Token *tok) {
    ASTNode *n = (ASTNode *)calloc(1, sizeof(ASTNode));
    n->id = g_ast_next_id++;
    n->node_type = type;
    n->token = tok;
    return n;
}

static void ast_add_prop(ASTNode *n, const char *key, const char *value, int is_string) {
    n->props = (ASTProp *)realloc(n->props, (n->nprops + 1) * sizeof(ASTProp));
    n->props[n->nprops].key = key;
    n->props[n->nprops].value = co1_strdup(value);
    n->props[n->nprops].is_string = is_string;
    n->nprops++;
}

static void ast_add_str(ASTNode *n, const char *key, const char *value) {
    ast_add_prop(n, key, value, 1);
}

static void ast_add_int(ASTNode *n, const char *key, long long value) {
    StrBuf sb;
    strbuf_init(&sb);
    strbuf_append_int(&sb, value);
    ast_add_prop(n, key, strbuf_cstr(&sb), 0);
    strbuf_free(&sb);
}

static void ast_add_bool(ASTNode *n, const char *key, int value) {
    ast_add_prop(n, key, value ? "true" : "false", 0);
}

static void ast_add_child(ASTNode *parent, ASTNode *child) {
    if (!child) {
        return;
    }
    parent->children = (ASTNode **)realloc(
        parent->children, (parent->nchildren + 1) * sizeof(ASTNode *));
    parent->children[parent->nchildren++] = child;
}

void ast_free(ASTNode *node) {
    if (!node) {
        return;
    }
    for (size_t i = 0; i < node->nchildren; i++) {
        ast_free(node->children[i]);
    }
    free(node->children);
    for (size_t i = 0; i < node->nprops; i++) {
        free(node->props[i].value);
    }
    free(node->props);
    free(node);
}

/* ============================================================
   Token stream
   ============================================================ */

typedef struct Parser {
    Token *tokens;
    size_t count;
    size_t pos;
    SyntaxErrorList *errors;
} Parser;

static void skip_comments(Parser *p) {
    while (p->pos < p->count && p->tokens[p->pos].kind == TOK_COMMENT) {
        p->pos++;
    }
}

static TokenKind peek_kind(Parser *p) {
    skip_comments(p);
    if (p->pos < p->count) {
        return p->tokens[p->pos].kind;
    }
    return TOK_EOF;
}

static Token *peek_token(Parser *p) {
    skip_comments(p);
    return p->pos < p->count ? &p->tokens[p->pos] : NULL;
}

static Token *next_token(Parser *p) {
    skip_comments(p);
    return p->pos < p->count ? &p->tokens[p->pos++] : NULL;
}

static int peek_is(Parser *p, TokenKind kind) {
    return peek_kind(p) == kind;
}

static int is_type_kind(TokenKind k) {
    return k == TOK_INT || k == TOK_FLOAT || k == TOK_BOOL || k == TOK_CHAR;
}

/* Error reporting + light recovery (skip to the next ';' or '}'). */
static void syntax_error(Parser *p, Token *tok, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (p->errors->len < 32) { /* bound recovery noise */
        SyntaxError e;
        e.line = tok ? tok->line : 0;
        e.column = tok ? tok->column : 0;
        e.message = co1_strdup(buf);
        SyntaxErrorList_push(p->errors, e);
    }
}

static Token *expect_kind(Parser *p, TokenKind kind, const char *what) {
    if (peek_kind(p) != kind) {
        syntax_error(p, peek_token(p), "expected %s", what);
        return NULL;
    }
    return next_token(p);
}

/* ============================================================
   Forward declarations
   ============================================================ */

static ASTNode *parse_expr(Parser *p, CSTNode *cst_parent);
static ASTNode *parse_stmt(Parser *p, CSTNode *cst_parent);

/* ============================================================
   Expressions (precedence climbing; CST mirrors productions)
   ============================================================ */

static ASTNode *make_binary(Token *op, ASTNode *l, ASTNode *r) {
    ASTNode *n = ast_make("BinaryOp", op);
    ast_add_str(n, "op", op->lexeme);
    ast_add_child(n, l);
    ast_add_child(n, r);
    return n;
}

static ASTNode *parse_primary(Parser *p, CSTNode *cst_parent) {
    Token *tok = peek_token(p);
    if (!tok) {
        syntax_error(p, NULL, "unexpected end of input in expression");
        return NULL;
    }

    switch (tok->kind) {
    case TOK_INT_LITERAL: {
        Token *t = next_token(p);
        ASTNode *n = ast_make("IntLit", t);
        ast_add_int(n, "value", (long long)strtoll(t->lexeme, NULL, 10));
        if (cst_parent) {
            CSTNode *c = cst_rule("int_literal");
            cst_add(c, cst_leaf(t));
            cst_add(cst_parent, c);
        }
        return n;
    }
    case TOK_FLOAT_LITERAL: {
        Token *t = next_token(p);
        ASTNode *n = ast_make("FloatLit", t);
        ast_add_str(n, "value", t->lexeme);
        if (cst_parent) {
            CSTNode *c = cst_rule("float_literal");
            cst_add(c, cst_leaf(t));
            cst_add(cst_parent, c);
        }
        return n;
    }
    case TOK_TRUE:
    case TOK_FALSE: {
        Token *t = next_token(p);
        ASTNode *n = ast_make("BoolLit", t);
        ast_add_int(n, "value", t->kind == TOK_TRUE ? 1 : 0);
        if (cst_parent) {
            CSTNode *c = cst_rule("bool_literal");
            cst_add(c, cst_leaf(t));
            cst_add(cst_parent, c);
        }
        return n;
    }
    case TOK_STRING_LITERAL: {
        Token *t = next_token(p);
        ASTNode *n = ast_make("StringLit", t);
        if (cst_parent) {
            CSTNode *c = cst_rule("string_literal");
            cst_add(c, cst_leaf(t));
            cst_add(cst_parent, c);
        }
        return n;
    }
    case TOK_IDENTIFIER: {
        Token *t = next_token(p);
        ASTNode *n = ast_make("Identifier", t);
        ast_add_str(n, "name", t->lexeme);
        if (cst_parent) {
            CSTNode *c = cst_rule("identifier");
            cst_add(c, cst_leaf(t));
            cst_add(cst_parent, c);
        }

        /* postfix ++ / -- */
        if (peek_is(p, TOK_PLUSPLUS) || peek_is(p, TOK_MINUSMINUS)) {
            Token *op = next_token(p);
            ASTNode *dec = ast_make("IncDec", op);
            ast_add_str(dec, "op", op->lexeme);
            ast_add_child(dec, n);
            if (cst_parent) {
                CSTNode *u = cst_rule("incdec_expr");
                cst_add(u, cst_leaf(op));
                cst_add(cst_parent, u);
            }
            return dec;
        }
        return n;
    }
    case TOK_LPAREN: {
        Token *open = next_token(p);
        CSTNode *paren_cst = cst_parent ? cst_rule("paren_expr") : NULL;
        if (paren_cst) {
            cst_add(paren_cst, cst_leaf(open));
        }
        ASTNode *inner = parse_expr(p, paren_cst);
        Token *close = expect_kind(p, TOK_RPAREN, "')'");
        if (paren_cst) {
            cst_add(paren_cst, cst_leaf(close));
            cst_add(cst_parent, paren_cst);
        }
        return inner;
    }
    default:
        next_token(p);
        syntax_error(p, tok, "unexpected token '%s' in expression", tok->lexeme);
        if (cst_parent) {
            cst_add(cst_parent, cst_leaf(tok));
        }
        return NULL;
    }
}

static ASTNode *parse_unary(Parser *p, CSTNode *cst_parent) {
    TokenKind k = peek_kind(p);
    if (k == TOK_SUB || k == TOK_NOT || k == TOK_PLUSPLUS || k == TOK_MINUSMINUS) {
        Token *op = next_token(p);
        CSTNode *u = cst_parent ? cst_rule("unary_expr") : NULL;
        if (u) {
            cst_add(u, cst_leaf(op));
        }
        ASTNode *operand = parse_unary(p, u);
        ASTNode *n = ast_make("UnaryOp", op);
        ast_add_str(n, "op", op->lexeme);
        ast_add_child(n, operand);
        if (u) {
            cst_add(cst_parent, u);
        }
        return n;
    }
    return parse_primary(p, cst_parent);
}

static ASTNode *parse_binary_level(
    Parser *p, CSTNode *cst_parent,
    ASTNode *(*sub)(Parser *, CSTNode *),
    const TokenKind *ops, size_t nops) {
    ASTNode *l = sub(p, cst_parent);
    for (;;) {
        TokenKind k = peek_kind(p);
        int found = 0;
        for (size_t i = 0; i < nops; i++) {
            if (k == ops[i]) {
                found = 1;
                break;
            }
        }
        if (!found) {
            break;
        }
        Token *op = next_token(p);
        CSTNode *b = cst_parent ? cst_rule("binary_expr") : NULL;
        if (b) {
            cst_add(b, cst_leaf(op));
        }
        ASTNode *r = sub(p, b);
        l = make_binary(op, l, r);
        if (b) {
            cst_add(cst_parent, b);
        }
    }
    return l;
}

static ASTNode *parse_mul(Parser *p, CSTNode *c) {
    static const TokenKind ops[] = {TOK_MUL, TOK_DIV, TOK_MOD};
    return parse_binary_level(p, c, parse_unary, ops, 3);
}

static ASTNode *parse_add(Parser *p, CSTNode *c) {
    static const TokenKind ops[] = {TOK_ADD, TOK_SUB};
    return parse_binary_level(p, c, parse_mul, ops, 2);
}

static ASTNode *parse_rel(Parser *p, CSTNode *c) {
    static const TokenKind ops[] = {TOK_LT, TOK_GT, TOK_LE, TOK_GE};
    return parse_binary_level(p, c, parse_add, ops, 4);
}

static ASTNode *parse_eq(Parser *p, CSTNode *c) {
    static const TokenKind ops[] = {TOK_EQ, TOK_NEQ};
    return parse_binary_level(p, c, parse_rel, ops, 2);
}

static ASTNode *parse_and(Parser *p, CSTNode *c) {
    static const TokenKind ops[] = {TOK_AND};
    return parse_binary_level(p, c, parse_eq, ops, 1);
}

static ASTNode *parse_or(Parser *p, CSTNode *c) {
    static const TokenKind ops[] = {TOK_OR};
    return parse_binary_level(p, c, parse_and, ops, 1);
}

static ASTNode *parse_expr(Parser *p, CSTNode *cst_parent) {
    return parse_or(p, cst_parent);
}

/* ============================================================
   Statements
   ============================================================ */

static ASTNode *parse_declaration(Parser *p, CSTNode *cst_parent) {
    CSTNode *cst = cst_parent ? cst_rule("declaration") : NULL;
    ASTNode *n = ast_make("VarDecl", NULL);
    int is_const = 0;

    Token *kw = next_token(p);
    if (kw->kind == TOK_CONST) {
        is_const = 1;
        if (cst) {
            cst_add(cst, cst_leaf(kw));
        }
        kw = next_token(p);
    }
    if (cst) {
        cst_add(cst, cst_leaf(kw));
    }
    if (!is_type_kind(kw->kind)) {
        syntax_error(p, kw, "expected a type name, found '%s'", kw->lexeme);
        if (cst) {
            cst_add(cst_parent, cst);
        }
        return n;
    }
    n->token = kw;
    ast_add_str(n, "type_name", kw->lexeme);
    ast_add_bool(n, "const", is_const);

    Token *name = expect_kind(p, TOK_IDENTIFIER, "a variable name after the type");
    if (!name) {
        if (cst) {
            cst_add(cst_parent, cst);
        }
        return n;
    }
    ast_add_str(n, "name", name->lexeme);
    if (cst) {
        cst_add(cst, cst_leaf(name));
    }

    if (peek_is(p, TOK_ASSIGN)) {
        Token *eq = next_token(p);
        if (cst) {
            cst_add(cst, cst_leaf(eq));
        }
        ASTNode *init = parse_expr(p, cst);
        ast_add_child(n, init);
    }
    Token *semi = expect_kind(p, TOK_SEMICOLON, "';' after declaration");
    if (cst) {
        cst_add(cst, cst_leaf(semi));
        cst_add(cst_parent, cst);
    }
    return n;
}

static ASTNode *parse_assignment(Parser *p, CSTNode *cst_parent) {
    CSTNode *cst = cst_parent ? cst_rule("assignment") : NULL;
    Token *name = next_token(p);
    ASTNode *n = ast_make("Assign", name);
    ast_add_str(n, "name", name->lexeme);
    if (cst) {
        cst_add(cst, cst_leaf(name));
    }
    Token *eq = expect_kind(p, TOK_ASSIGN, "'=' after the variable name");
    if (!eq) {
        if (cst) {
            cst_add(cst_parent, cst);
        }
        return n;
    }
    if (cst) {
        cst_add(cst, cst_leaf(eq));
    }
    ASTNode *expr = parse_expr(p, cst);
    ast_add_child(n, expr);
    Token *semi = expect_kind(p, TOK_SEMICOLON, "';' after assignment");
    if (cst) {
        cst_add(cst, cst_leaf(semi));
        cst_add(cst_parent, cst);
    }
    return n;
}

static ASTNode *parse_read(Parser *p, CSTNode *cst_parent) {
    CSTNode *cst = cst_parent ? cst_rule("read_statement") : NULL;
    Token *kw = next_token(p); /* read */
    if (cst) {
        cst_add(cst, cst_leaf(kw));
    }
    Token *name = expect_kind(p, TOK_IDENTIFIER, "a variable name after 'read'");
    if (!name) {
        if (cst) {
            cst_add(cst_parent, cst);
        }
        return NULL;
    }
    ASTNode *n = ast_make("Read", name);
    ast_add_str(n, "name", name->lexeme);
    if (cst) {
        cst_add(cst, cst_leaf(name));
    }
    Token *semi = expect_kind(p, TOK_SEMICOLON, "';' after read statement");
    if (cst) {
        cst_add(cst, cst_leaf(semi));
        cst_add(cst_parent, cst);
    }
    return n;
}

static ASTNode *parse_print(Parser *p, CSTNode *cst_parent) {
    CSTNode *cst = cst_parent ? cst_rule("print_statement") : NULL;
    Token *kw = next_token(p); /* print */
    if (cst) {
        cst_add(cst, cst_leaf(kw));
    }
    ASTNode *n = ast_make("Print", kw);
    ASTNode *arg = parse_expr(p, cst);
    ast_add_child(n, arg);
    Token *semi = expect_kind(p, TOK_SEMICOLON, "';' after print statement");
    if (cst) {
        cst_add(cst, cst_leaf(semi));
        cst_add(cst_parent, cst);
    }
    return n;
}

static ASTNode *parse_if(Parser *p, CSTNode *cst_parent) {
    CSTNode *cst = cst_parent ? cst_rule("if_statement") : NULL;
    Token *kw = next_token(p); /* if */
    if (cst) {
        cst_add(cst, cst_leaf(kw));
    }
    ASTNode *n = ast_make("If", kw);
    Token *open = expect_kind(p, TOK_LPAREN, "'(' after if");
    if (!open) {
        if (cst) {
            cst_add(cst_parent, cst);
        }
        return n;
    }
    if (cst) {
        cst_add(cst, cst_leaf(open));
    }
    ASTNode *cond = parse_expr(p, cst);
    ast_add_child(n, cond);
    Token *close = expect_kind(p, TOK_RPAREN, "')' after if condition");
    if (!close) {
        if (cst) {
            cst_add(cst_parent, cst);
        }
        return n;
    }
    if (cst) {
        cst_add(cst, cst_leaf(close));
    }
    ASTNode *then_stmt = parse_stmt(p, cst);
    ast_add_child(n, then_stmt);
    if (peek_is(p, TOK_ELSE)) {
        Token *else_kw = next_token(p);
        if (cst) {
            cst_add(cst, cst_leaf(else_kw));
        }
        ASTNode *else_stmt = parse_stmt(p, cst);
        ast_add_child(n, else_stmt);
    }
    if (cst) {
        cst_add(cst_parent, cst);
    }
    return n;
}

static ASTNode *parse_while(Parser *p, CSTNode *cst_parent) {
    CSTNode *cst = cst_parent ? cst_rule("while_statement") : NULL;
    Token *kw = next_token(p); /* while */
    if (cst) {
        cst_add(cst, cst_leaf(kw));
    }
    ASTNode *n = ast_make("While", kw);
    Token *open = expect_kind(p, TOK_LPAREN, "'(' after while");
    if (!open) {
        if (cst) {
            cst_add(cst_parent, cst);
        }
        return n;
    }
    if (cst) {
        cst_add(cst, cst_leaf(open));
    }
    ASTNode *cond = parse_expr(p, cst);
    ast_add_child(n, cond);
    Token *close = expect_kind(p, TOK_RPAREN, "')' after while condition");
    if (!close) {
        if (cst) {
            cst_add(cst_parent, cst);
        }
        return n;
    }
    if (cst) {
        cst_add(cst, cst_leaf(close));
    }
    ASTNode *body = parse_stmt(p, cst);
    ast_add_child(n, body);
    if (cst) {
        cst_add(cst_parent, cst);
    }
    return n;
}

static ASTNode *parse_for(Parser *p, CSTNode *cst_parent) {
    CSTNode *cst = cst_parent ? cst_rule("for_statement") : NULL;
    Token *kw = next_token(p); /* for */
    if (cst) {
        cst_add(cst, cst_leaf(kw));
    }
    ASTNode *n = ast_make("For", kw);
    Token *open = expect_kind(p, TOK_LPAREN, "'(' after for");
    if (!open) {
        if (cst) {
            cst_add(cst_parent, cst);
        }
        return n;
    }
    if (cst) {
        cst_add(cst, cst_leaf(open));
    }

    /* init clause: declaration, assignment, or empty */
    TokenKind k = peek_kind(p);
    if (k == TOK_CONST || is_type_kind(k)) {
        ASTNode *init = parse_declaration(p, cst); /* consumes ';' */
        ast_add_child(n, init);
    } else if (k == TOK_IDENTIFIER) {
        ASTNode *init = parse_assignment(p, cst); /* consumes ';' */
        ast_add_child(n, init);
    } else {
        Token *semi = expect_kind(p, TOK_SEMICOLON, "';' in for init clause");
        if (cst) {
            cst_add(cst, cst_leaf(semi));
        }
    }

    /* condition (optional) */
    if (!peek_is(p, TOK_SEMICOLON)) {
        ASTNode *cond = parse_expr(p, cst);
        ast_add_child(n, cond);
    }
    Token *cond_semi = expect_kind(p, TOK_SEMICOLON, "';' after for condition");
    if (!cond_semi) {
        if (cst) {
            cst_add(cst_parent, cst);
        }
        return n;
    }
    if (cst) {
        cst_add(cst, cst_leaf(cond_semi));
    }

    /* step clause (optional) */
    if (!peek_is(p, TOK_RPAREN)) {
        ASTNode *step = parse_expr(p, cst);
        ast_add_child(n, step);
    }
    Token *close = expect_kind(p, TOK_RPAREN, "')' after for step");
    if (!close) {
        if (cst) {
            cst_add(cst_parent, cst);
        }
        return n;
    }
    if (cst) {
        cst_add(cst, cst_leaf(close));
    }
    ASTNode *body = parse_stmt(p, cst);
    ast_add_child(n, body);
    if (cst) {
        cst_add(cst_parent, cst);
    }
    return n;
}

static ASTNode *parse_return(Parser *p, CSTNode *cst_parent) {
    CSTNode *cst = cst_parent ? cst_rule("return_statement") : NULL;
    Token *kw = next_token(p); /* return */
    if (cst) {
        cst_add(cst, cst_leaf(kw));
    }
    ASTNode *n = ast_make("Return", kw);
    if (!peek_is(p, TOK_SEMICOLON)) {
        ASTNode *expr = parse_expr(p, cst);
        ast_add_child(n, expr);
    }
    Token *semi = expect_kind(p, TOK_SEMICOLON, "';' after return");
    if (cst) {
        cst_add(cst, cst_leaf(semi));
        cst_add(cst_parent, cst);
    }
    return n;
}

static ASTNode *parse_block(Parser *p, CSTNode *cst_parent) {
    CSTNode *cst = cst_parent ? cst_rule("block_statement") : NULL;
    Token *open = next_token(p); /* { */
    if (cst) {
        cst_add(cst, cst_leaf(open));
    }
    ASTNode *n = ast_make("Block", open);
    while (peek_kind(p) != TOK_RBRACE) {
        if (peek_kind(p) == TOK_EOF) {
            syntax_error(p, NULL, "unexpected end of input in block");
            break;
        }
        ASTNode *stmt = parse_stmt(p, cst);
        ast_add_child(n, stmt);
    }
    Token *close = expect_kind(p, TOK_RBRACE, "'}'");
    if (cst) {
        cst_add(cst, cst_leaf(close));
        cst_add(cst_parent, cst);
    }
    return n;
}

static void synchronize(Parser *p) {
    while (p->pos < p->count) {
        TokenKind k = peek_kind(p);
        if (k == TOK_SEMICOLON) {
            next_token(p);
            return;
        }
        if (k == TOK_RBRACE || k == TOK_EOF) {
            return;
        }
        next_token(p);
    }
}

static ASTNode *parse_stmt(Parser *p, CSTNode *cst_parent) {
    TokenKind k = peek_kind(p);

    switch (k) {
    case TOK_CONST:
    case TOK_INT:
    case TOK_FLOAT:
    case TOK_BOOL:
    case TOK_CHAR: {
        /* function definitions are out of scope for v1 of the study
           language: `type name (` is reported and skipped */
        if (p->pos + 2 < p->count &&
            p->tokens[p->pos + 1].kind == TOK_IDENTIFIER &&
            p->tokens[p->pos + 2].kind == TOK_LPAREN) {
            Token *tok = &p->tokens[p->pos + 1];
            syntax_error(p, tok,
                         "function definitions are not supported in the "
                         "mini-c study language");
            synchronize(p);
            return NULL;
        }
        return parse_declaration(p, cst_parent);
    }
    case TOK_IDENTIFIER: {
        /* `ident++` / `ident--` */
        if (p->pos + 1 < p->count &&
            (p->tokens[p->pos + 1].kind == TOK_PLUSPLUS ||
             p->tokens[p->pos + 1].kind == TOK_MINUSMINUS)) {
            Token *name = next_token(p);
            Token *op = next_token(p);
            ASTNode *dec = ast_make("IncDec", op);
            ast_add_str(dec, "op", op->lexeme);
            ast_add_child(dec, ast_make("Identifier", name));
            expect_kind(p, TOK_SEMICOLON, "';' after increment");
            return dec;
        }
        return parse_assignment(p, cst_parent);
    }
    case TOK_PRINT:
        return parse_print(p, cst_parent);
    case TOK_READ:
        return parse_read(p, cst_parent);
    case TOK_IF:
        return parse_if(p, cst_parent);
    case TOK_WHILE:
        return parse_while(p, cst_parent);
    case TOK_FOR:
        return parse_for(p, cst_parent);
    case TOK_RETURN:
        return parse_return(p, cst_parent);
    case TOK_SEMICOLON:
        next_token(p);
        return NULL;
    case TOK_LBRACE:
        return parse_block(p, cst_parent);
    case TOK_EOF:
        syntax_error(p, NULL, "unexpected end of input in statement");
        return NULL;
    default: {
        Token *tok = peek_token(p);
        syntax_error(p, tok, "unexpected token '%s' in statement", tok->lexeme);
        next_token(p);
        return NULL;
    }
    }
}

/* ============================================================
   Entry point
   ============================================================ */

int parse_tokens(TokenList *tokens, ParseResult *out) {
    out->cst_root = NULL;
    out->ast_root = NULL;
    out->errors = (SyntaxErrorList){0};

    Parser p;
    p.tokens = tokens->items;
    p.count = tokens->len;
    p.pos = 0;
    p.errors = &out->errors;

    g_ast_next_id = 1;

    CSTNode *cst_prog = cst_rule("program");
    ASTNode *ast_prog = ast_make("Program", NULL);

    while (peek_kind(&p) != TOK_EOF) {
        size_t errs_before = p.errors->len;
        size_t pos_before = p.pos;
        ASTNode *stmt = parse_stmt(&p, cst_prog);
        if (stmt) {
            ast_add_child(ast_prog, stmt);
        }
        if (p.errors->len > errs_before && p.pos == pos_before) {
            /* error with no progress: force-consume one token */
            next_token(&p);
        }
        if (p.errors->len >= 32) {
            break;
        }
    }

    out->cst_root = cst_prog;
    out->ast_root = ast_prog;
    return 0;
}

void parse_result_free(ParseResult *r) {
    cst_free(r->cst_root);
    ast_free(r->ast_root);
    for (size_t i = 0; i < r->errors.len; i++) {
        free(r->errors.items[i].message);
    }
    SyntaxErrorList_free(&r->errors);
}

/* ============================================================
   JSON serializers
   ============================================================ */

static void token_to_json(JsonWriter *w, const Token *t) {
    jw_begin_object(w);
    jw_key(w, "id");        jw_int(w, t->id);
    jw_key(w, "line");      jw_int(w, t->line);
    jw_key(w, "column");    jw_int(w, t->column);
    jw_key(w, "lexeme");    jw_string(w, t->lexeme);
    jw_key(w, "token");     jw_string(w, token_name(t->kind));
    jw_key(w, "category");  jw_string(w, token_category_name(t->category));
    jw_key(w, "subtype");   jw_string(w, t->subtype);
    jw_key(w, "length");    jw_int(w, (long long)t->length);
    jw_key(w, "scope");     jw_string(w, t->scope_level == 0 ? "global" : "block");
    jw_key(w, "scope_level"); jw_int(w, t->scope_level);
    jw_key(w, "color");     jw_string(w, t->color);
    jw_key(w, "description"); jw_string(w, t->description);
    jw_key(w, "offset");
    jw_begin_object(w);
    jw_key(w, "start");     jw_int(w, (long long)t->offset_start);
    jw_key(w, "end");       jw_int(w, (long long)t->offset_end);
    jw_end_object(w);
    jw_end_object(w);
}

void cst_to_json(JsonWriter *w, const CSTNode *node) {
    if (!node) {
        jw_null(w);
        return;
    }
    if (node->token) {
        jw_begin_object(w);
        jw_key(w, "token");
        token_to_json(w, node->token);
        jw_end_object(w);
        return;
    }
    jw_begin_object(w);
    jw_key(w, "rule_name");
    jw_string(w, node->rule_name);
    if (node->child_count > 0) {
        jw_key(w, "children");
        jw_begin_array(w);
        for (size_t i = 0; i < node->child_count; i++) {
            cst_to_json(w, node->children[i]);
        }
        jw_end_array(w);
    }
    jw_end_object(w);
}

void ast_to_json(JsonWriter *w, const ASTNode *node) {
    if (!node) {
        jw_null(w);
        return;
    }
    jw_begin_object(w);
    jw_key(w, "id");
    jw_int(w, node->id);
    jw_key(w, "node_type");
    jw_string(w, node->node_type);
    if (node->token) {
        jw_key(w, "token");
        token_to_json(w, node->token);
    }
    if (node->nprops > 0) {
        jw_key(w, "attributes");
        jw_begin_object(w);
        for (size_t i = 0; i < node->nprops; i++) {
            jw_key(w, node->props[i].key);
            if (node->props[i].is_string) {
                jw_string(w, node->props[i].value);
            } else {
                jw_append_raw(w, node->props[i].value);
            }
        }
        jw_end_object(w);
    }
    if (node->nchildren > 0) {
        jw_key(w, "children");
        jw_begin_array(w);
        for (size_t i = 0; i < node->nchildren; i++) {
            ast_to_json(w, node->children[i]);
        }
        jw_end_array(w);
    }
    jw_end_object(w);
}

const char *ast_get_prop(const ASTNode *node, const char *key) {
    if (!node) {
        return NULL;
    }
    for (size_t i = 0; i < node->nprops; i++) {
        if (strcmp(node->props[i].key, key) == 0) {
            return node->props[i].value;
        }
    }
    return NULL;
}

const char *ast_child_name(const ASTNode *node) {
    return ast_get_prop(node, "name");
}

const ASTNode *ast_child(const ASTNode *node, size_t idx) {
    if (!node || idx >= node->nchildren) {
        return NULL;
    }
    return node->children[idx];
}

void ast_annotate(ASTNode *node, const char *key, const char *value) {
    if (node) {
        ast_add_str(node, key, value);
    }
}
