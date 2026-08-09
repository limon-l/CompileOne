/* ============================================================
   Multi-language C-family front end (C / C++ / Java).

   One hand-written recursive-descent parser covers the common
   C-family subset plus the language-specific extensions, building
   a real Concrete Syntax Tree (CST) and Abstract Syntax Tree (AST)
   in a single pass exactly like the mini-c parser.

   The AST shapes re-use the mini-c conventions wherever possible
   (IntLit/FloatLit/BoolLit/StringLit/Identifier/BinaryOp/UnaryOp/
   IncDec/VarDecl/Assign/If/While/For/Return/Block/Program/Read/
   Print) so shared tooling treats native programs uniformly, and
   add native node types:

     FunctionDef {name, return_type, prototype?}
       children: ParamDecl*, Block?
     ParamDecl  {name, type_name}
     ClassDef   {name, base?}
       children: FieldDecl*, MethodDef*
     FieldDecl  {name, type_name}  + optional init child
     MethodDef  {name, return_type, is_static, is_public,
                 is_constructor, class_name, is_abstract?}
       children: ParamDecl*, CtorInit*, Block
     CtorInit   {member}           child: value expr
     DoWhile    children: body, cond
     Break / Continue / Empty
     Assign     {name, op}         child: value        (simple lvalue)
     AssignLvalue {op}             children: target, value
     Call       {name}             children: callee, args*
     MemberAccess {member}         child: base
     Index                          children: base, index
     NewObject  {class_name}       children: args*
     NewArray   {elem_type}        child: size expr
     Ternary                        children: cond, then, else
     TryCatch    children: tryBlock, CatchClause*
     CatchClause {var_name, var_type} child: block
     Throw       child: expr?
     Println / PrintNoLn    children: print items
     CoutStream  children: print items
     Printf     {format}           children: args*
     CharLit {value} / NullLit
     InitList (C++ ctor initializer list, represented as CtorInit)

   Compound assignment targets (a[i], obj.f, *p) are AssignLvalue.
   Built-in I/O is recognized: printf/scanf, std::cout/std::cin
   streams, System.out.println/print, and Java field initialisers
   are preserved for the IR phase to lower.
   ============================================================ */

#include "frontend.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_writer.h"
#include "strbuf.h"

/* --------------------------- language ids --------------------------- */

enum LangId { LANG_C, LANG_CPP, LANG_JAVA };

int lang_is_native(const char *language) {
    return language &&
           (strcmp(language, "c") == 0 || strcmp(language, "c++") == 0 ||
            strcmp(language, "java") == 0);
}

static int lang_id(const char *language) {
    if (strcmp(language, "c") == 0) {
        return LANG_C;
    }
    if (strcmp(language, "c++") == 0) {
        return LANG_CPP;
    }
    if (strcmp(language, "java") == 0) {
        return LANG_JAVA;
    }
    return LANG_C;
}

void lang_reclassify_tokens(TokenList *tokens, const char *language) {
    if (!lang_is_native(language)) {
        return;
    }
    /* print / read are mini-c keywords but plain identifiers in the
       C family; the lexer already emits identifiers in native mode,
       so this is a defensive demotion for hand-built token streams. */
    for (size_t i = 0; i < tokens->len; i++) {
        Token *t = &tokens->items[i];
        if (t->kind == TOK_PRINT || t->kind == TOK_READ) {
            t->kind = TOK_IDENTIFIER;
            t->category = token_category(TOK_IDENTIFIER);
            t->subtype = token_subtype(TOK_IDENTIFIER);
            t->color = token_color(TOK_IDENTIFIER);
            t->description = token_description(TOK_IDENTIFIER);
        }
    }
}

/* --------------------------- node helpers --------------------------- */

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

static int g_lang_ast_id = 1;

static ASTNode *ast_make(const char *type, Token *tok) {
    ASTNode *n = (ASTNode *)calloc(1, sizeof(ASTNode));
    n->id = g_lang_ast_id++;
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

/* --------------------------- token stream --------------------------- */

typedef struct LangParser {
    Token *tokens;
    size_t count;
    size_t pos;
    SyntaxErrorList *errors;
    int language;
} LangParser;

static int lang_is_java(LangParser *p) { return p->language == LANG_JAVA; }
static int lang_is_cpp(LangParser *p) { return p->language == LANG_CPP; }

static void skip_comments(LangParser *p) {
    while (p->pos < p->count && p->tokens[p->pos].kind == TOK_COMMENT) {
        p->pos++;
    }
}

static TokenKind peek_kind(LangParser *p) {
    skip_comments(p);
    if (p->pos < p->count) {
        return p->tokens[p->pos].kind;
    }
    return TOK_EOF;
}

static TokenKind peek_kind_at(LangParser *p, size_t ahead) {
    skip_comments(p);
    size_t idx = p->pos;
    while (idx < p->count && p->tokens[idx].kind == TOK_COMMENT) {
        idx++;
    }
    while (ahead-- > 0 && idx < p->count) {
        idx++;
        while (idx < p->count && p->tokens[idx].kind == TOK_COMMENT) {
            idx++;
        }
    }
    return idx < p->count ? p->tokens[idx].kind : TOK_EOF;
}

static Token *peek_token(LangParser *p) {
    skip_comments(p);
    return p->pos < p->count ? &p->tokens[p->pos] : NULL;
}

static Token *peek_token_at(LangParser *p, size_t ahead) {
    skip_comments(p);
    size_t idx = p->pos;
    while (idx < p->count && p->tokens[idx].kind == TOK_COMMENT) {
        idx++;
    }
    while (ahead-- > 0 && idx < p->count) {
        idx++;
        while (idx < p->count && p->tokens[idx].kind == TOK_COMMENT) {
            idx++;
        }
    }
    return idx < p->count ? &p->tokens[idx] : NULL;
}

static Token *next_token(LangParser *p) {
    skip_comments(p);
    return p->pos < p->count ? &p->tokens[p->pos++] : NULL;
}

static int peek_is(LangParser *p, TokenKind kind) {
    return peek_kind(p) == kind;
}

static int accept(LangParser *p, TokenKind kind) {
    if (peek_kind(p) == kind) {
        next_token(p);
        return 1;
    }
    return 0;
}

static void syntax_error(LangParser *p, Token *tok, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (p->errors->len < 32) {
        SyntaxError e;
        e.line = tok ? tok->line : 0;
        e.column = tok ? tok->column : 0;
        e.message = co1_strdup(buf);
        SyntaxErrorList_push(p->errors, e);
    }
}

static Token *expect_kind(LangParser *p, TokenKind kind, const char *what) {
    if (peek_kind(p) != kind) {
        syntax_error(p, peek_token(p), "expected %s", what);
        return NULL;
    }
    return next_token(p);
}

/* --------------------------- forward decls --------------------------- */

static ASTNode *parse_expr(LangParser *p, CSTNode *cst_parent);
static ASTNode *parse_stmt(LangParser *p, CSTNode *cst_parent);
static ASTNode *parse_binary(LangParser *p, CSTNode *cst_parent, int min_prec);

/* --------------------------- builtin I/O detection --------------------------- */

static int is_ident(const Token *t, const char *name) {
    return t && t->kind == TOK_IDENTIFIER && strcmp(t->lexeme, name) == 0;
}

/* Detects "printf (", "scanf (", "System . out . println/print (" at the
   current position (returns the number of leading tokens consumed). */
static int match_printf(LangParser *p) {
    Token *t0 = peek_token_at(p, 0);
    if (is_ident(t0, "printf") && peek_kind_at(p, 1) == TOK_LPAREN) {
        return 1;
    }
    return 0;
}

static int match_scanf(LangParser *p) {
    Token *t0 = peek_token_at(p, 0);
    if (is_ident(t0, "scanf") && peek_kind_at(p, 1) == TOK_LPAREN) {
        return 1;
    }
    return 0;
}

/* Returns 1 when the stream begins System.out.println( / .print( */
static int match_system_out(LangParser *p, int *is_println) {
    Token *t0 = peek_token_at(p, 0);
    Token *t1 = peek_token_at(p, 1);
    Token *t2 = peek_token_at(p, 2);
    Token *t3 = peek_token_at(p, 3);
    Token *t4 = peek_token_at(p, 4);
    if (!is_ident(t0, "System") || !t1 || t1->kind != TOK_DOT ||
        !is_ident(t2, "out") || !t3 || t3->kind != TOK_DOT) {
        return 0;
    }
    if ((!is_ident(t4, "println") && !is_ident(t4, "print")) ||
        peek_kind_at(p, 5) != TOK_LPAREN) {
        return 0;
    }
    *is_println = is_ident(t4, "println");
    return 1;
}

/* --------------------------- C type specifier --------------------------- */

typedef struct CType {
    char base[64];       /* canonical base type name */
    int is_pointer;
    int is_array;
    char array_size[32]; /* "10", "[]" or "" */
    int is_const;
} CType;

static void ctype_init(CType *t) {
    memset(t, 0, sizeof(*t));
}

/* Try to parse a type specifier. Returns 1 and fills `t` on success,
   without consuming anything on failure. Only base types are matched
   here (modifier keywords + pointers/array suffixes belong to the
   declarator) so expression statements like `x = 5;` backtrack. */
static int try_parse_type(LangParser *p, CType *t) {
    size_t save = p->pos;
    ctype_init(t);

    /* leading storage/qualifier keywords that do not name a type */
    for (;;) {
        TokenKind k = peek_kind(p);
        if (k == TOK_CONST) {
            t->is_const = 1;
            next_token(p);
            continue;
        }
        if (k == TOK_SIGNED || k == TOK_UNSIGNED) {
            next_token(p);
            continue;
        }
        break;
    }

    Token *tok = peek_token(p);
    if (!tok) {
        p->pos = save;
        return 0;
    }

    switch (tok->kind) {
    case TOK_INT:
    case TOK_FLOAT:
    case TOK_BOOL:
    case TOK_CHAR:
    case TOK_DOUBLE:
    case TOK_VOID:
    case TOK_LONG:
    case TOK_SHORT:
    case TOK_BYTE:
    case TOK_STRING:
    case TOK_BOOLEAN:
        snprintf(t->base, sizeof(t->base), "%s", tok->lexeme);
        next_token(p);
        break;
    case TOK_STRUCT:
    case TOK_ENUM:
    case TOK_UNION:
    case TOK_CLASS:
        /* struct Foo / enum Color / union U / class C (as a type) */
        next_token(p);
        if (peek_kind(p) == TOK_IDENTIFIER) {
            Token *name = next_token(p);
            snprintf(t->base, sizeof(t->base), "%s", name->lexeme);
        } else {
            p->pos = save;
            return 0;
        }
        break;
    case TOK_IDENTIFIER:
        /* class name or typedef'd name — but an identifier followed by
           '(' or '.' or '[' or '=' or ';' is a call/expression, not a
           type. We accept the identifier; the caller backtracks if the
           declaration shape does not follow. */
        snprintf(t->base, sizeof(t->base), "%s", tok->lexeme);
        next_token(p);
        /* C++ scope-qualified type: std::string, my::ns::Foo */
        while (peek_is(p, TOK_SCOPE)) {
            Token *part = peek_token_at(p, 1);
            if (!part || part->kind != TOK_IDENTIFIER) {
                break;
            }
            StrBuf sb;
            strbuf_init(&sb);
            strbuf_append(&sb, t->base);
            strbuf_append(&sb, "::");
            strbuf_append(&sb, part->lexeme);
            snprintf(t->base, sizeof(t->base), "%s", strbuf_cstr(&sb));
            strbuf_free(&sb);
            next_token(p); /* :: */
            next_token(p); /* part */
        }
        break;
    default:
        p->pos = save;
        return 0;
    }

    /* pointer suffix: int *p */
    while (peek_is(p, TOK_MUL)) {
        t->is_pointer = 1;
        next_token(p);
    }

    return 1;
}

/* Parse a declarator: optional leading Java-style array suffixes
   (String[] args), the declared name, then optional C-style array
   suffixes (int a[10]). Returns 0 (backtracking) when no name follows. */
static int parse_declarator(LangParser *p, char *name, size_t name_n, CType *t) {
    size_t save = p->pos;

    /* Java: String[] args  => [] precedes the name */
    while (peek_is(p, TOK_LBRACKET)) {
        next_token(p);
        if (peek_is(p, TOK_RBRACKET)) {
            next_token(p);
            if (t->array_size[0] != '[') {
                snprintf(t->array_size, sizeof(t->array_size), "[]");
                t->is_array = 1;
            }
        } else {
            p->pos = save;
            return 0;
        }
    }

    Token *tok = peek_token(p);
    if (!tok || tok->kind != TOK_IDENTIFIER) {
        p->pos = save;
        return 0;
    }
    snprintf(name, name_n, "%s", tok->lexeme);
    next_token(p);

    /* int a[10]  or  int a[] */
    while (peek_is(p, TOK_LBRACKET)) {
        next_token(p);
        if (peek_is(p, TOK_INT_LITERAL)) {
            Token *sz = next_token(p);
            if (t->is_array && t->array_size[0] == '[') {
                /* Java int[][] — drop extra dims at study level */
            } else {
                snprintf(t->array_size, sizeof(t->array_size), "%s", sz->lexeme);
                t->is_array = 1;
            }
        } else if (peek_is(p, TOK_RBRACKET)) {
            if (t->is_array && t->array_size[0] != '[') {
                /* int a[][10] — ignore extra dim */
            } else {
                snprintf(t->array_size, sizeof(t->array_size), "[]");
                t->is_array = 1;
            }
            next_token(p);
        } else {
            syntax_error(p, peek_token(p), "expected array size");
            next_token(p);
        }
        if (peek_is(p, TOK_RBRACKET)) {
            next_token(p);
        }
    }
    return 1;
}

/* --------------------------- helpers --------------------------- */

/* Expression precedence (higher binds tighter). */
static int binop_prec(TokenKind k) {
    switch (k) {
    case TOK_OR:        return 1;
    case TOK_AND:       return 2;
    case TOK_PIPE:      return 3;
    case TOK_CARET:     return 4;
    case TOK_AMP:       return 5;
    case TOK_EQ:
    case TOK_NEQ:       return 6;
    case TOK_LT:
    case TOK_LE:
    case TOK_GT:
    case TOK_GE:        return 7;
    case TOK_SHL:
    case TOK_SHR:       return 8;
    case TOK_ADD:
    case TOK_SUB:       return 9;
    case TOK_MUL:
    case TOK_DIV:
    case TOK_MOD:       return 10;
    default:            return 0;
    }
}

static const char *assign_op_name(TokenKind k) {
    switch (k) {
    case TOK_ASSIGN:    return "=";
    case TOK_PLUSEQ:    return "+=";
    case TOK_MINUSEQ:   return "-=";
    case TOK_STAREQ:    return "*=";
    case TOK_SLASHEQ:   return "/=";
    case TOK_PERCENTEQ: return "%=";
    case TOK_ANDEQ:     return "&=";
    case TOK_OREQ:      return "|=";
    case TOK_XOREQ:     return "^=";
    case TOK_SHLASSIGN: return "<<=";
    case TOK_SHRASSIGN: return ">>=";
    default:            return "=";
    }
}

/* --------------------------- expressions --------------------------- */

static ASTNode *make_binary(LangParser *p, Token *op, ASTNode *l, ASTNode *r) {
    (void)p;
    ASTNode *n = ast_make("BinaryOp", op);
    ast_add_str(n, "op", op->lexeme);
    ast_add_child(n, l);
    ast_add_child(n, r);
    return n;
}

static ASTNode *make_unary(LangParser *p, Token *op, ASTNode *operand) {
    (void)p;
    ASTNode *n = ast_make("UnaryOp", op);
    ast_add_str(n, "op", op->lexeme);
    ast_add_child(n, operand);
    return n;
}

static ASTNode *make_identifier(Token *tok) {
    ASTNode *n = ast_make("Identifier", tok);
    ast_add_str(n, "name", tok->lexeme);
    return n;
}

/* True when the expression subtree contains a string literal, i.e. it
   is a Java string-concatenation expression. */
static int expr_has_string(ASTNode *n) {
    if (!n) {
        return 0;
    }
    if (strcmp(n->node_type, "StringLit") == 0) {
        return 1;
    }
    for (size_t i = 0; i < n->nchildren; i++) {
        if (expr_has_string(n->children[i])) {
            return 1;
        }
    }
    return 0;
}

/* Flatten a String + int expression into a list of print items so
   System.out.println("x=" + n) prints each piece in order. */
static int flatten_print_items(ASTNode *expr, ASTNode ***items, size_t *n) {
    *items = NULL;
    *n = 0;
    if (!expr) {
        return 0;
    }
    if (strcmp(expr->node_type, "BinaryOp") == 0 && expr->nchildren == 2 &&
        strcmp(ast_get_prop(expr, "op"), "+") == 0 &&
        expr_has_string(expr)) {
        ASTNode **left = NULL, **right = NULL;
        size_t ln = 0, rn = 0;
        flatten_print_items(expr->children[0], &left, &ln);
        flatten_print_items(expr->children[1], &right, &rn);
        ASTNode **out = (ASTNode **)malloc((ln + rn) * sizeof(ASTNode *));
        memcpy(out, left, ln * sizeof(ASTNode *));
        memcpy(out + ln, right, rn * sizeof(ASTNode *));
        free(left);
        free(right);
        *items = out;
        *n = ln + rn;
        return 1;
    }
    ASTNode **out = (ASTNode **)malloc(sizeof(ASTNode *));
    out[0] = expr;
    *items = out;
    *n = 1;
    return 1;
}

static ASTNode *parse_printf(LangParser *p, CSTNode *cst_parent) {
    Token *kw = next_token(p); /* printf */
    expect_kind(p, TOK_LPAREN, "'(' after printf");
    if (cst_parent) {
        CSTNode *c = cst_rule("printf_call");
        cst_add(c, cst_leaf(kw));
        cst_add(cst_parent, c);
    }
    ASTNode *n = ast_make("Printf", kw);

    Token *fmt = next_token(p);
    if (fmt && fmt->kind == TOK_STRING_LITERAL) {
        ast_add_str(n, "format", fmt->lexeme);
    } else {
        syntax_error(p, fmt, "printf format must be a string literal");
        ast_add_str(n, "format", "\"\"");
    }
    if (cst_parent && fmt) {
        cst_add(cst_parent->children[cst_parent->child_count - 1], cst_leaf(fmt));
    }

    while (peek_is(p, TOK_COMMA)) {
        next_token(p);
        ast_add_child(n, parse_expr(p, cst_parent));
    }
    expect_kind(p, TOK_RPAREN, "')' after printf arguments");
    return n;
}

static ASTNode *parse_scanf(LangParser *p, CSTNode *cst_parent) {
    Token *kw = next_token(p); /* scanf */
    expect_kind(p, TOK_LPAREN, "'(' after scanf");
    if (cst_parent) {
        CSTNode *c = cst_rule("scanf_call");
        cst_add(c, cst_leaf(kw));
        cst_add(cst_parent, c);
    }
    ASTNode *n = ast_make("Read", kw);

    /* format string (validated but otherwise ignored) */
    Token *fmt = next_token(p);
    if (!fmt || fmt->kind != TOK_STRING_LITERAL) {
        syntax_error(p, fmt, "scanf format must be a string literal");
    }
    if (cst_parent) {
        cst_add(cst_parent->children[cst_parent->child_count - 1], cst_leaf(fmt));
    }

    while (peek_is(p, TOK_COMMA)) {
        next_token(p);
        ASTNode *arg = parse_expr(p, cst_parent);
        /* unwrap &x to x */
        if (arg && strcmp(arg->node_type, "UnaryOp") == 0 &&
            strcmp(ast_get_prop(arg, "op"), "&") == 0 && arg->nchildren >= 1) {
            ast_add_child(n, arg->children[0]);
            arg->nchildren = 0;
            ast_free(arg);
        } else {
            ast_add_child(n, arg);
        }
    }
    expect_kind(p, TOK_RPAREN, "')' after scanf arguments");
    return n;
}

static ASTNode *parse_system_out(LangParser *p, CSTNode *cst_parent, int is_println) {
    Token *system_tok = next_token(p);  /* System */
    next_token(p);                      /* . */
    next_token(p);                      /* out */
    next_token(p);                      /* . */
    Token *method = next_token(p);      /* println | print */
    expect_kind(p, TOK_LPAREN, "'(' after print call");
    if (cst_parent) {
        CSTNode *c = cst_rule("system_out_call");
        cst_add(c, cst_leaf(system_tok));
        cst_add(c, cst_leaf(method));
        cst_add(cst_parent, c);
    }

    ASTNode *n = ast_make(is_println ? "Println" : "PrintNoLn", method);
    if (peek_is(p, TOK_RPAREN)) {
        next_token(p);
        return n;
    }
    ASTNode *arg = parse_expr(p, cst_parent);
    ASTNode **items = NULL;
    size_t nitems = 0;
    flatten_print_items(arg, &items, &nitems);
    for (size_t i = 0; i < nitems; i++) {
        ast_add_child(n, items[i]);
    }
    free(items);
    expect_kind(p, TOK_RPAREN, "')' after print arguments");
    return n;
}

/* True when the next tokens are `cout` or `std::cout` (in raw token
   form, before the C++ scope-resolution name is joined). */
static int is_stream_ident(LangParser *p, const char *name) {
    Token *t0 = peek_token_at(p, 0);
    if (is_ident(t0, name)) {
        return 1;
    }
    Token *t1 = peek_token_at(p, 1);
    Token *t2 = peek_token_at(p, 2);
    if (is_ident(t0, "std") && t1 && t1->kind == TOK_SCOPE &&
        is_ident(t2, name)) {
        return 1;
    }
    return 0;
}

/* A << operator that immediately follows a cout identifier marks a
   C++ stream-out statement. */
static int is_cout_stream(LangParser *p) {
    if (!is_stream_ident(p, "cout")) {
        return 0;
    }
    if (peek_kind_at(p, 1) == TOK_SHL) {
        return 1;
    }
    return peek_kind_at(p, 3) == TOK_SHL; /* std :: cout << */
}

static int is_cin_stream(LangParser *p) {
    if (!is_stream_ident(p, "cin")) {
        return 0;
    }
    if (peek_kind_at(p, 1) == TOK_SHR) {
        return 1;
    }
    return peek_kind_at(p, 3) == TOK_SHR; /* std :: cin >> */
}

static ASTNode *parse_cout_stream(LangParser *p, CSTNode *cst_parent) {
    Token *kw = next_token(p); /* cout */
    if (is_ident(kw, "std")) {
        next_token(p); /* :: */
        kw = next_token(p); /* cout */
    }
    if (cst_parent) {
        CSTNode *c = cst_rule("cout_stream");
        cst_add(c, cst_leaf(kw));
        cst_add(cst_parent, c);
    }
    ASTNode *n = ast_make("CoutStream", kw);
    while (peek_is(p, TOK_SHL)) {
        next_token(p);
        /* items bind tighter than '<<' so `cout << a + b` prints a+b */
        ASTNode *item = parse_binary(p, cst_parent, 9);
        ast_add_child(n, item);
    }
    expect_kind(p, TOK_SEMICOLON, "';' after cout stream");
    return n;
}

static ASTNode *parse_cin_stream(LangParser *p, CSTNode *cst_parent) {
    Token *kw = next_token(p); /* cin */
    if (is_ident(kw, "std")) {
        next_token(p); /* :: */
        kw = next_token(p); /* cin */
    }
    if (cst_parent) {
        CSTNode *c = cst_rule("cin_stream");
        cst_add(c, cst_leaf(kw));
        cst_add(cst_parent, c);
    }
    ASTNode *n = ast_make("Read", kw);
    while (peek_is(p, TOK_SHR)) {
        next_token(p);
        ASTNode *target = parse_binary(p, cst_parent, 9);
        if (target && strcmp(target->node_type, "UnaryOp") == 0 &&
            strcmp(ast_get_prop(target, "op"), "&") == 0 && target->nchildren >= 1) {
            ast_add_child(n, target->children[0]);
            target->nchildren = 0;
            ast_free(target);
        } else {
            ast_add_child(n, target);
        }
    }
    expect_kind(p, TOK_SEMICOLON, "';' after cin stream");
    return n;
}

static ASTNode *parse_new(LangParser *p, CSTNode *cst_parent) {
    Token *kw = next_token(p); /* new */
    if (cst_parent) {
        CSTNode *c = cst_rule("new_expression");
        cst_add(c, cst_leaf(kw));
        cst_add(cst_parent, c);
    }

    CType t;
    ctype_init(&t);
    if (!try_parse_type(p, &t)) {
        syntax_error(p, peek_token(p), "expected type after 'new'");
        return ast_make("NewObject", kw);
    }

    if (peek_is(p, TOK_LBRACKET)) {
        /* new int[10] */
        next_token(p);
        ASTNode *size = parse_expr(p, cst_parent);
        expect_kind(p, TOK_RBRACKET, "']' after array size");
        while (accept(p, TOK_LBRACKET)) {
            if (peek_is(p, TOK_RBRACKET)) {
                next_token(p);
            } else {
                ASTNode *d = parse_expr(p, cst_parent);
                ast_free(d);
                expect_kind(p, TOK_RBRACKET, "']'");
            }
        }
        ASTNode *n = ast_make("NewArray", kw);
        ast_add_str(n, "elem_type", t.base);
        ast_add_child(n, size);
        return n;
    }

    /* new Class(args) */
    ASTNode *n = ast_make("NewObject", kw);
    ast_add_str(n, "class_name", t.base);
    if (peek_is(p, TOK_LPAREN)) {
        next_token(p);
        if (!peek_is(p, TOK_RPAREN)) {
            do {
                ast_add_child(n, parse_expr(p, cst_parent));
            } while (accept(p, TOK_COMMA));
        }
        expect_kind(p, TOK_RPAREN, "')' after constructor arguments");
    }
    return n;
}

static ASTNode *parse_primary(LangParser *p, CSTNode *cst_parent);

static ASTNode *parse_postfix(LangParser *p, CSTNode *cst_parent) {
    ASTNode *n = parse_primary(p, cst_parent);
    if (!n) {
        return NULL;
    }

    for (;;) {
        Token *t = peek_token(p);
        if (!t) {
            break;
        }
        switch (t->kind) {
        case TOK_LPAREN: {
            /* call */
            next_token(p);
            ASTNode *call = ast_make("Call", t);
            ast_add_child(call, n);
            if (n && strcmp(n->node_type, "Identifier") == 0) {
                ast_add_str(call, "name", ast_get_prop(n, "name"));
            }
            if (n && strcmp(n->node_type, "MemberAccess") == 0) {
                const char *m = ast_get_prop(n, "member");
                ast_add_str(call, "name", m ? m : "?");
            }
            if (!peek_is(p, TOK_RPAREN)) {
                do {
                    ast_add_child(call, parse_expr(p, cst_parent));
                } while (accept(p, TOK_COMMA));
            }
            expect_kind(p, TOK_RPAREN, "')' after arguments");
            if (cst_parent) {
                CSTNode *c = cst_rule("call_expression");
                cst_add(c, cst_leaf(t));
                cst_add(cst_parent, c);
            }
            n = call;
            break;
        }
        case TOK_LBRACKET: {
            next_token(p);
            ASTNode *idx = parse_expr(p, cst_parent);
            expect_kind(p, TOK_RBRACKET, "']' after subscript");
            ASTNode *ix = ast_make("Index", t);
            ast_add_child(ix, n);
            ast_add_child(ix, idx);
            if (cst_parent) {
                CSTNode *c = cst_rule("subscript");
                cst_add(c, cst_leaf(t));
                cst_add(cst_parent, c);
            }
            n = ix;
            break;
        }
        case TOK_DOT:
        case TOK_ARROW: {
            next_token(p);
            Token *member = next_token(p);
            if (!member || member->kind != TOK_IDENTIFIER) {
                syntax_error(p, member, "expected member name after '%s'",
                             t->kind == TOK_DOT ? "." : "->");
                return n;
            }
            ASTNode *ma = ast_make("MemberAccess", member);
            ast_add_str(ma, "member", member->lexeme);
            ast_add_child(ma, n);
            if (cst_parent) {
                CSTNode *c = cst_rule("member_access");
                cst_add(c, cst_leaf(t));
                cst_add(c, cst_leaf(member));
                cst_add(cst_parent, c);
            }
            n = ma;
            break;
        }
        case TOK_PLUSPLUS:
        case TOK_MINUSMINUS: {
            next_token(p);
            ASTNode *dec = ast_make("IncDec", t);
            ast_add_str(dec, "op", t->lexeme);
            ast_add_child(dec, n);
            if (cst_parent) {
                CSTNode *c = cst_rule("incdec_expr");
                cst_add(c, cst_leaf(t));
                cst_add(cst_parent, c);
            }
            n = dec;
            break;
        }
        case TOK_INSTANCEOF: {
            syntax_error(p, t, "'instanceof' is not supported by CompileOne");
            next_token(p);
            if (peek_kind(p) == TOK_IDENTIFIER) {
                next_token(p);
            }
            break;
        }
        default:
            return n;
        }
    }
    return n;
}

static ASTNode *parse_unary(LangParser *p, CSTNode *cst_parent) {
    Token *t = peek_token(p);
    if (!t) {
        syntax_error(p, NULL, "unexpected end of input in expression");
        return NULL;
    }

    switch (t->kind) {
    case TOK_SUB:
    case TOK_NOT:
    case TOK_AMP:
    case TOK_TILDE:
    case TOK_ADD:
    case TOK_MUL:
    case TOK_PLUSPLUS:
    case TOK_MINUSMINUS: {
        if (t->kind == TOK_PLUSPLUS || t->kind == TOK_MINUSMINUS) {
            next_token(p);
            ASTNode *operand = parse_unary(p, cst_parent);
            ASTNode *n = ast_make("IncDec", t);
            ast_add_str(n, "op", t->lexeme);
            ast_add_str(n, "prefix", "true");
            ast_add_child(n, operand);
            return n;
        }
        next_token(p);
        ASTNode *operand = parse_unary(p, cst_parent);
        return make_unary(p, t, operand);
    }
    case TOK_SIZEOF: {
        next_token(p);
        ASTNode *n = ast_make("UnaryOp", t);
        ast_add_str(n, "op", "sizeof");
        if (peek_is(p, TOK_LPAREN)) {
            next_token(p);
            ASTNode *inner = parse_expr(p, cst_parent);
            expect_kind(p, TOK_RPAREN, "')' after sizeof");
            ast_add_child(n, inner);
        } else {
            ast_add_child(n, parse_unary(p, cst_parent));
        }
        return n;
    }
    default:
        return parse_postfix(p, cst_parent);
    }
}

static ASTNode *parse_binary(LangParser *p, CSTNode *cst_parent, int min_prec) {
    ASTNode *l = parse_unary(p, cst_parent);
    if (!l) {
        return NULL;
    }
    for (;;) {
        Token *op = peek_token(p);
        if (!op) {
            break;
        }
        int prec = binop_prec(op->kind);
        if (prec == 0 || prec < min_prec) {
            break;
        }
        next_token(p);
        ASTNode *r = parse_binary(p, cst_parent, prec + 1);
        l = make_binary(p, op, l, r);
    }
    return l;
}

static ASTNode *parse_ternary(LangParser *p, CSTNode *cst_parent) {
    ASTNode *cond = parse_binary(p, cst_parent, 1);
    if (!cond) {
        return NULL;
    }
    if (peek_is(p, TOK_QUESTION)) {
        Token *q = next_token(p);
        ASTNode *t = parse_ternary(p, cst_parent);
        expect_kind(p, TOK_COLON, "':' in ternary expression");
        ASTNode *f = parse_ternary(p, cst_parent);
        ASTNode *n = ast_make("Ternary", q);
        ast_add_child(n, cond);
        ast_add_child(n, t);
        ast_add_child(n, f);
        return n;
    }
    return cond;
}

static ASTNode *parse_expr(LangParser *p, CSTNode *cst_parent) {
    ASTNode *target = parse_ternary(p, cst_parent);
    if (!target) {
        return NULL;
    }

    Token *op = peek_token(p);
    if (op && (op->kind == TOK_ASSIGN || op->kind == TOK_PLUSEQ ||
               op->kind == TOK_MINUSEQ || op->kind == TOK_STAREQ ||
               op->kind == TOK_SLASHEQ || op->kind == TOK_PERCENTEQ ||
               op->kind == TOK_ANDEQ || op->kind == TOK_OREQ ||
               op->kind == TOK_XOREQ || op->kind == TOK_SHLASSIGN ||
               op->kind == TOK_SHRASSIGN)) {
        next_token(p);
        ASTNode *value = parse_expr(p, cst_parent);
        const char *opname = assign_op_name(op->kind);

        if (strcmp(target->node_type, "Identifier") == 0) {
            ASTNode *a = ast_make("Assign", op);
            ast_add_str(a, "name", ast_get_prop(target, "name"));
            ast_add_str(a, "op", opname);
            ast_add_child(a, value);
            return a;
        }
        ASTNode *a = ast_make("AssignLvalue", op);
        ast_add_str(a, "op", opname);
        ast_add_child(a, target);
        ast_add_child(a, value);
        return a;
    }
    return target;
}

static ASTNode *parse_primary(LangParser *p, CSTNode *cst_parent) {
    /* built-in I/O recognized at primary level so they work in both
       statement and expression positions */
    int is_println = 0;
    if (match_printf(p)) {
        return parse_printf(p, cst_parent);
    }
    if (match_scanf(p)) {
        return parse_scanf(p, cst_parent);
    }
    if (match_system_out(p, &is_println)) {
        return parse_system_out(p, cst_parent, is_println);
    }

    Token *tok = peek_token(p);
    if (!tok) {
        syntax_error(p, NULL, "unexpected end of input in expression");
        return NULL;
    }

    switch (tok->kind) {
    case TOK_INT_LITERAL: {
        next_token(p);
        ASTNode *n = ast_make("IntLit", tok);
        ast_add_int(n, "value", (long long)strtoll(tok->lexeme, NULL, 10));
        if (cst_parent) {
            CSTNode *c = cst_rule("literal");
            cst_add(c, cst_leaf(tok));
            cst_add(cst_parent, c);
        }
        return n;
    }
    case TOK_FLOAT_LITERAL: {
        next_token(p);
        ASTNode *n = ast_make("FloatLit", tok);
        ast_add_str(n, "value", tok->lexeme);
        if (cst_parent) {
            CSTNode *c = cst_rule("literal");
            cst_add(c, cst_leaf(tok));
            cst_add(cst_parent, c);
        }
        return n;
    }
    case TOK_CHAR_LITERAL: {
        next_token(p);
        ASTNode *n = ast_make("CharLit", tok);
        ast_add_str(n, "value", tok->lexeme);
        if (cst_parent) {
            CSTNode *c = cst_rule("char_literal");
            cst_add(c, cst_leaf(tok));
            cst_add(cst_parent, c);
        }
        return n;
    }
    case TOK_TRUE:
    case TOK_FALSE: {
        next_token(p);
        ASTNode *n = ast_make("BoolLit", tok);
        ast_add_int(n, "value", tok->kind == TOK_TRUE ? 1 : 0);
        if (cst_parent) {
            CSTNode *c = cst_rule("bool_literal");
            cst_add(c, cst_leaf(tok));
            cst_add(cst_parent, c);
        }
        return n;
    }
    case TOK_STRING_LITERAL: {
        next_token(p);
        ASTNode *n = ast_make("StringLit", tok);
        ast_add_str(n, "value", tok->lexeme);
        if (cst_parent) {
            CSTNode *c = cst_rule("string_literal");
            cst_add(c, cst_leaf(tok));
            cst_add(cst_parent, c);
        }
        return n;
    }
    case TOK_NULL: {
        next_token(p);
        ASTNode *n = ast_make("NullLit", tok);
        if (cst_parent) {
            CSTNode *c = cst_rule("null_literal");
            cst_add(c, cst_leaf(tok));
            cst_add(cst_parent, c);
        }
        return n;
    }
    case TOK_THIS: {
        next_token(p);
        return make_identifier(tok);
    }
    case TOK_SUPER: {
        next_token(p);
        return make_identifier(tok);
    }
    case TOK_IDENTIFIER: {
        next_token(p);
        /* C++ scope resolution joins the name, e.g. std::cout */
        StrBuf name;
        strbuf_init(&name);
        strbuf_append(&name, tok->lexeme);
        while (peek_is(p, TOK_SCOPE)) {
            next_token(p);
            Token *part = next_token(p);
            if (part && part->kind == TOK_IDENTIFIER) {
                strbuf_append_char(&name, ':');
                strbuf_append_char(&name, ':');
                strbuf_append(&name, part->lexeme);
            } else {
                syntax_error(p, part, "expected name after '::'");
                break;
            }
        }
        Token *ident = tok;
        ASTNode *n = ast_make("Identifier", ident);
        ast_add_str(n, "name", strbuf_cstr(&name));
        strbuf_free(&name);
        return n;
    }
    case TOK_NEW: {
        return parse_new(p, cst_parent);
    }
    case TOK_LPAREN: {
        next_token(p);
        ASTNode *inner = parse_expr(p, cst_parent);
        expect_kind(p, TOK_RPAREN, "')'");
        return inner;
    }
    default:
        syntax_error(p, tok, "unexpected token '%s' in expression", tok->lexeme);
        next_token(p);
        return NULL;
    }
}

/* --------------------------- declarations --------------------------- */

static int is_type_start_kind(TokenKind k) {
    switch (k) {
    case TOK_INT:
    case TOK_FLOAT:
    case TOK_BOOL:
    case TOK_CHAR:
    case TOK_DOUBLE:
    case TOK_VOID:
    case TOK_LONG:
    case TOK_SHORT:
    case TOK_BYTE:
    case TOK_STRING:
    case TOK_BOOLEAN:
    case TOK_CONST:
    case TOK_SIGNED:
    case TOK_UNSIGNED:
    case TOK_STRUCT:
    case TOK_ENUM:
    case TOK_UNION:
        return 1;
    default:
        return 0;
    }
}

static ASTNode *parse_parameter(LangParser *p, CSTNode *cst_parent) {
    CType t;
    ctype_init(&t);
    if (!try_parse_type(p, &t)) {
        syntax_error(p, peek_token(p), "expected parameter type");
        next_token(p);
        return NULL;
    }
    char name[128] = "";
    parse_declarator(p, name, sizeof(name), &t);

    ASTNode *n = ast_make("ParamDecl", NULL);
    ast_add_str(n, "type_name", t.base);
    if (t.is_pointer) {
        ast_add_str(n, "is_pointer", "true");
    }
    if (t.is_array) {
        ast_add_str(n, "is_array", "true");
        if (t.array_size[0] != '[') {
            ast_add_str(n, "array_size", t.array_size);
        }
    }
    ast_add_str(n, "name", name[0] ? name : "?");
    if (cst_parent) {
        CSTNode *c = cst_rule("parameter_declaration");
        cst_add(cst_parent, c);
    }
    return n;
}

/* Parse a parameter list "(" ... ")". Appends ParamDecl children to
   `fn`. Returns 0 on missing '('. */
static int parse_parameter_list(LangParser *p, CSTNode *cst_parent, ASTNode *fn) {
    if (!peek_is(p, TOK_LPAREN)) {
        return 0;
    }
    next_token(p);
    if (peek_is(p, TOK_RPAREN)) {
        next_token(p);
        return 1;
    }
    do {
        ASTNode *param = parse_parameter(p, cst_parent);
        if (param) {
            ast_add_child(fn, param);
        }
    } while (accept(p, TOK_COMMA));
    expect_kind(p, TOK_RPAREN, "')' after parameters");
    return 1;
}

static ASTNode *parse_function_def(LangParser *p, CSTNode *cst_parent,
                                   const CType *ret, Token *name_tok,
                                   int is_method, const char *class_name,
                                   int is_static, int is_public,
                                   int is_constructor) {
    ASTNode *fn = ast_make(is_constructor ? "MethodDef" : "FunctionDef", name_tok);
    ast_add_str(fn, "name", name_tok->lexeme);
    ast_add_str(fn, "return_type", ret->base);
    if (is_method) {
        ast_add_bool(fn, "is_method", 1);
        ast_add_bool(fn, "is_static", is_static);
        ast_add_bool(fn, "is_public", is_public);
        ast_add_bool(fn, "is_constructor", is_constructor);
        ast_add_str(fn, "class_name", class_name);
    }

    if (cst_parent) {
        CSTNode *c = cst_rule(is_constructor ? "constructor_definition"
                                             : "function_definition");
        if (name_tok) {
            cst_add(c, cst_leaf(name_tok));
        }
        cst_add(cst_parent, c);
    }

    parse_parameter_list(p, cst_parent, fn);

    /* Java: throws Exception, ... */
    while (peek_is(p, TOK_THROWS)) {
        next_token(p);
        while (peek_kind(p) == TOK_IDENTIFIER) {
            next_token(p);
            if (!accept(p, TOK_COMMA)) {
                break;
            }
        }
    }

    if (peek_is(p, TOK_SEMICOLON)) {
        /* prototype */
        next_token(p);
        ast_add_bool(fn, "prototype", 1);
        return fn;
    }

    if (peek_is(p, TOK_COLON)) {
        /* C++ constructor initializer list: Foo(int x) : _x(x) {} */
        next_token(p);
        while (peek_kind(p) == TOK_IDENTIFIER) {
            Token *member = next_token(p);
            expect_kind(p, TOK_LPAREN, "'(' in initializer list");
            ASTNode *init = ast_make("CtorInit", member);
            ast_add_str(init, "member", member->lexeme);
            ASTNode *val = parse_expr(p, cst_parent);
            ast_add_child(init, val);
            ast_add_child(fn, init);
            expect_kind(p, TOK_RPAREN, "')' in initializer list");
            if (!accept(p, TOK_COMMA)) {
                break;
            }
        }
    }

    ASTNode *body = parse_stmt(p, cst_parent);
    if (body && strcmp(body->node_type, "Block") == 0) {
        ast_add_child(fn, body);
    } else {
        if (body) {
            ast_free(body);
        }
        syntax_error(p, peek_token(p), "function body must be a block");
    }
    return fn;
}

/* Parse an initializer for a declaration. Supports plain expressions and
   C-style brace lists {1, 2, 3} which lower to per-element stores. */
static ASTNode *parse_initializer(LangParser *p, CSTNode *cst_parent) {
    if (!peek_is(p, TOK_LBRACE)) {
        return parse_expr(p, cst_parent);
    }
    Token *lb = next_token(p);
    ASTNode *list = ast_make("InitList", lb);
    if (cst_parent) {
        CSTNode *c = cst_rule("initializer_list");
        cst_add(c, cst_leaf(lb));
        cst_add(cst_parent, c);
    }
    if (!peek_is(p, TOK_RBRACE)) {
        do {
            ast_add_child(list, parse_initializer(p, cst_parent));
        } while (accept(p, TOK_COMMA));
    }
    expect_kind(p, TOK_RBRACE, "'}' to close initializer list");
    return list;
}

/* Parse a top-level or block-scope declaration. Returns NULL (and
   restores the position) when the stream does not start a
   declaration, letting the caller fall back to an expression
   statement. */
static ASTNode *parse_declaration(LangParser *p, CSTNode *cst_parent) {
    size_t save = p->pos;

    CType t;
    ctype_init(&t);
    if (!try_parse_type(p, &t)) {
        p->pos = save;
        return NULL;
    }

    Token *name_tok = peek_token(p);
    char name[128] = "";
    if (!parse_declarator(p, name, sizeof(name), &t)) {
        p->pos = save;
        return NULL;
    }

    Token *after = peek_token(p);
    if (!after) {
        p->pos = save;
        return NULL;
    }

    switch (after->kind) {
    case TOK_LPAREN: {
        /* function / method definition */
        return parse_function_def(p, cst_parent, &t, name_tok,
                                  /*is_method*/ 0, "", 0, 0, 0);
    }
    case TOK_ASSIGN:
    case TOK_SEMICOLON:
    case TOK_COMMA:
    case TOK_LBRACKET: {
        /* variable declaration */
        ASTNode *block = ast_make("Block", NULL);
        ASTNode *first = ast_make("VarDecl", NULL);
        ast_add_str(first, "name", name);
        ast_add_str(first, "type_name", t.base);
        if (t.is_const) {
            ast_add_bool(first, "const", 1);
        }
        if (t.is_pointer) {
            ast_add_str(first, "is_pointer", "true");
        }
        if (t.is_array) {
            ast_add_bool(first, "is_array", 1);
            ast_add_str(first, "array_size", t.array_size[0] == '[' ? "[]" : t.array_size);
        }
        if (cst_parent) {
            CSTNode *c = cst_rule("variable_declaration");
            cst_add(cst_parent, c);
        }

        if (accept(p, TOK_ASSIGN)) {
            ASTNode *init = parse_initializer(p, cst_parent);
            ast_add_child(first, init);
        }
        ast_add_child(block, first);

        while (accept(p, TOK_COMMA)) {
            char more[128] = "";
            CType mt;
            ctype_init(&mt);
            mt.base[0] = '\0';
            snprintf(mt.base, sizeof(mt.base), "%s", t.base);
            mt.is_const = t.is_const;
            if (!parse_declarator(p, more, sizeof(more), &mt)) {
                syntax_error(p, peek_token(p), "expected declarator");
                break;
            }
            ASTNode *d = ast_make("VarDecl", NULL);
            ast_add_str(d, "name", more);
            ast_add_str(d, "type_name", mt.base);
            if (mt.is_const) {
                ast_add_bool(d, "const", 1);
            }
            if (mt.is_pointer) {
                ast_add_str(d, "is_pointer", "true");
            }
            if (mt.is_array) {
                ast_add_bool(d, "is_array", 1);
                ast_add_str(d, "array_size",
                            mt.array_size[0] == '[' ? "[]" : mt.array_size);
            }
            if (accept(p, TOK_ASSIGN)) {
                ASTNode *init = parse_initializer(p, cst_parent);
                ast_add_child(d, init);
            }
            ast_add_child(block, d);
        }
        expect_kind(p, TOK_SEMICOLON, "';' after declaration");
        return block;
    }
    default:
        p->pos = save;
        return NULL;
    }
}

/* --------------------------- statements --------------------------- */

static ASTNode *parse_if(LangParser *p, CSTNode *cst_parent) {
    Token *kw = next_token(p);
    if (cst_parent) {
        CSTNode *c = cst_rule("if_statement");
        cst_add(c, cst_leaf(kw));
        cst_add(cst_parent, c);
    }
    ASTNode *n = ast_make("If", kw);
    expect_kind(p, TOK_LPAREN, "'(' after 'if'");
    ast_add_child(n, parse_expr(p, cst_parent));
    expect_kind(p, TOK_RPAREN, "')' after condition");
    ast_add_child(n, parse_stmt(p, cst_parent));
    if (accept(p, TOK_ELSE)) {
        ast_add_child(n, parse_stmt(p, cst_parent));
    }
    return n;
}

static ASTNode *parse_while(LangParser *p, CSTNode *cst_parent) {
    Token *kw = next_token(p);
    if (cst_parent) {
        CSTNode *c = cst_rule("while_statement");
        cst_add(c, cst_leaf(kw));
        cst_add(cst_parent, c);
    }
    ASTNode *n = ast_make("While", kw);
    expect_kind(p, TOK_LPAREN, "'(' after 'while'");
    ast_add_child(n, parse_expr(p, cst_parent));
    expect_kind(p, TOK_RPAREN, "')' after condition");
    ast_add_child(n, parse_stmt(p, cst_parent));
    return n;
}

static ASTNode *parse_do_while(LangParser *p, CSTNode *cst_parent) {
    Token *kw = next_token(p);
    if (cst_parent) {
        CSTNode *c = cst_rule("do_while_statement");
        cst_add(c, cst_leaf(kw));
        cst_add(cst_parent, c);
    }
    ASTNode *n = ast_make("DoWhile", kw);
    ast_add_child(n, parse_stmt(p, cst_parent));
    expect_kind(p, TOK_WHILE, "'while' after 'do' body");
    next_token(p);
    expect_kind(p, TOK_LPAREN, "'(' after 'while'");
    ast_add_child(n, parse_expr(p, cst_parent));
    expect_kind(p, TOK_RPAREN, "')' after condition");
    expect_kind(p, TOK_SEMICOLON, "';' after do-while");
    return n;
}

static ASTNode *parse_for(LangParser *p, CSTNode *cst_parent) {
    Token *kw = next_token(p);
    if (cst_parent) {
        CSTNode *c = cst_rule("for_statement");
        cst_add(c, cst_leaf(kw));
        cst_add(cst_parent, c);
    }
    ASTNode *n = ast_make("For", kw);
    expect_kind(p, TOK_LPAREN, "'(' after 'for'");

    /* init clause: declaration, assignment or nothing.
       parse_declaration consumes the terminating ';' itself, while an
       expression init (or an empty init) leaves the ';' for us below. */
    size_t save = p->pos;
    ASTNode *init = parse_declaration(p, cst_parent);
    int decl_init = (init != NULL);
    if (!init) {
        p->pos = save;
        if (!peek_is(p, TOK_SEMICOLON)) {
            init = parse_expr(p, cst_parent);
        }
    }
    if (init) {
        ast_add_child(n, init);
    }
    if (!decl_init) {
        expect_kind(p, TOK_SEMICOLON, "';' after for-init");
    }

    if (!peek_is(p, TOK_SEMICOLON)) {
        ast_add_child(n, parse_expr(p, cst_parent));
    }
    expect_kind(p, TOK_SEMICOLON, "';' after for-condition");

    if (!peek_is(p, TOK_RPAREN)) {
        ast_add_child(n, parse_expr(p, cst_parent));
    }
    expect_kind(p, TOK_RPAREN, "')' after for-step");

    ast_add_child(n, parse_stmt(p, cst_parent));
    return n;
}

static ASTNode *parse_try(LangParser *p, CSTNode *cst_parent) {
    Token *kw = next_token(p);
    if (cst_parent) {
        CSTNode *c = cst_rule("try_statement");
        cst_add(c, cst_leaf(kw));
        cst_add(cst_parent, c);
    }
    ASTNode *n = ast_make("TryCatch", kw);
    ast_add_child(n, parse_stmt(p, cst_parent)); /* try block */
    while (peek_is(p, TOK_CATCH)) {
        Token *catch_tok = next_token(p);
        ASTNode *cc = ast_make("CatchClause", catch_tok);
        expect_kind(p, TOK_LPAREN, "'(' after 'catch'");
        CType t;
        ctype_init(&t);
        if (try_parse_type(p, &t)) {
            ast_add_str(cc, "var_type", t.base);
        }
        char name[128] = "";
        parse_declarator(p, name, sizeof(name), &t);
        ast_add_str(cc, "var_name", name[0] ? name : "e");
        expect_kind(p, TOK_RPAREN, "')' after catch parameter");
        ast_add_child(cc, parse_stmt(p, cst_parent));
        ast_add_child(n, cc);
    }
    if (peek_is(p, TOK_FINALLY)) {
        next_token(p);
        ast_add_child(n, parse_stmt(p, cst_parent));
    }
    return n;
}

static ASTNode *parse_stmt(LangParser *p, CSTNode *cst_parent) {
    Token *tok = peek_token(p);
    if (!tok) {
        syntax_error(p, NULL, "unexpected end of input in statement");
        return NULL;
    }

    switch (tok->kind) {
    case TOK_LBRACE: {
        next_token(p);
        ASTNode *block = ast_make("Block", tok);
        if (cst_parent) {
            CSTNode *c = cst_rule("compound_statement");
            cst_add(c, cst_leaf(tok));
            cst_add(cst_parent, c);
        }
        while (!peek_is(p, TOK_RBRACE) && !peek_is(p, TOK_EOF)) {
            size_t errs = p->errors->len;
            size_t pos = p->pos;
            ASTNode *s = parse_stmt(p, cst_parent);
            if (s) {
                ast_add_child(block, s);
            }
            if (p->errors->len > errs && p->pos == pos) {
                next_token(p);
            }
        }
        expect_kind(p, TOK_RBRACE, "'}' to close block");
        return block;
    }
    case TOK_SEMICOLON:
        next_token(p);
        return ast_make("Empty", tok);
    case TOK_IF:
        return parse_if(p, cst_parent);
    case TOK_WHILE:
        return parse_while(p, cst_parent);
    case TOK_DO:
        return parse_do_while(p, cst_parent);
    case TOK_FOR:
        return parse_for(p, cst_parent);
    case TOK_RETURN: {
        next_token(p);
        ASTNode *n = ast_make("Return", tok);
        if (cst_parent) {
            CSTNode *c = cst_rule("return_statement");
            cst_add(c, cst_leaf(tok));
            cst_add(cst_parent, c);
        }
        if (!peek_is(p, TOK_SEMICOLON)) {
            ast_add_child(n, parse_expr(p, cst_parent));
        }
        expect_kind(p, TOK_SEMICOLON, "';' after return");
        return n;
    }
    case TOK_BREAK: {
        next_token(p);
        expect_kind(p, TOK_SEMICOLON, "';' after break");
        return ast_make("Break", tok);
    }
    case TOK_CONTINUE: {
        next_token(p);
        expect_kind(p, TOK_SEMICOLON, "';' after continue");
        return ast_make("Continue", tok);
    }
    case TOK_TRY:
        return parse_try(p, cst_parent);
    case TOK_THROW: {
        next_token(p);
        ASTNode *n = ast_make("Throw", tok);
        if (!peek_is(p, TOK_SEMICOLON)) {
            ast_add_child(n, parse_expr(p, cst_parent));
        }
        expect_kind(p, TOK_SEMICOLON, "';' after throw");
        return n;
    }
    case TOK_SWITCH:
    case TOK_CASE:
    case TOK_DEFAULT:
    case TOK_GOTO:
    case TOK_TYPEDEF:
    case TOK_EXTERN:
    case TOK_INLINE:
    case TOK_TEMPLATE:
        syntax_error(p, tok, "'%s' is not supported by CompileOne",
                     tok->lexeme);
        /* recover: skip to ';' or '{' ... '}' */
        while (!peek_is(p, TOK_SEMICOLON) && !peek_is(p, TOK_RBRACE) &&
               !peek_is(p, TOK_EOF)) {
            if (peek_is(p, TOK_LBRACE)) {
                int depth = 0;
                do {
                    if (peek_is(p, TOK_LBRACE)) {
                        depth++;
                    } else if (peek_is(p, TOK_RBRACE)) {
                        depth--;
                    }
                    next_token(p);
                } while (depth > 0 && !peek_is(p, TOK_EOF));
                break;
            }
            next_token(p);
        }
        accept(p, TOK_SEMICOLON);
        return NULL;
    case TOK_PREPROC:
        next_token(p); /* #include ... line */
        return NULL;
    case TOK_IMPORT:
    case TOK_PACKAGE:
    case TOK_USING:
        /* directive terminated by ';' — skipped at statement level too */
        while (!peek_is(p, TOK_SEMICOLON) && !peek_is(p, TOK_EOF)) {
            next_token(p);
        }
        accept(p, TOK_SEMICOLON);
        return NULL;
    case TOK_PUBLIC:
    case TOK_PRIVATE:
    case TOK_PROTECTED:
    case TOK_STATIC:
    case TOK_FINAL:
    case TOK_ABSTRACT:
    case TOK_SYNCHRONIZED:
    case TOK_VIRTUAL:
    case TOK_FRIEND:
    case TOK_OPERATOR:
        syntax_error(p, tok, "'%s' not allowed inside a function body",
                     tok->lexeme);
        next_token(p);
        return NULL;
    default:
        break;
    }

    if (is_cout_stream(p)) {
        return parse_cout_stream(p, cst_parent);
    }
    if (is_cin_stream(p)) {
        return parse_cin_stream(p, cst_parent);
    }

    /* declaration or expression statement */
    {
        size_t save = p->pos;
        ASTNode *decl = parse_declaration(p, cst_parent);
        if (decl) {
            return decl;
        }
        p->pos = save;
    }

    if (is_type_start_kind(peek_kind(p)) &&
        peek_kind_at(p, 1) == TOK_LPAREN) {
        /* `int (...) {` — a nested function-like declaration in a block;
           not supported; report and consume one token */
        syntax_error(p, peek_token(p), "nested function definitions are not supported");
        next_token(p);
        return NULL;
    }

    /* expression statement */
    {
        ASTNode *e = parse_expr(p, cst_parent);
        if (!e) {
            return NULL;
        }
        expect_kind(p, TOK_SEMICOLON, "';' after expression");
        ASTNode *es = ast_make("ExprStmt", e->token ? e->token : NULL);
        ast_add_child(es, e);
        return es;
    }
}

/* --------------------------- class / method definitions --------------------------- */

/* Skip a C++ initializer list for a field: `int x = 5;` inside a class
   is kept as a FieldDecl plus optional init child. */
static ASTNode *parse_field(LangParser *p, CSTNode *cst_parent, const CType *t,
                            const char *name) {
    ASTNode *n = ast_make("FieldDecl", NULL);
    ast_add_str(n, "name", name);
    ast_add_str(n, "type_name", t->base);
    if (t->is_array) {
        ast_add_bool(n, "is_array", 1);
        ast_add_str(n, "array_size", t->array_size[0] == '[' ? "[]" : t->array_size);
    }
    if (cst_parent) {
        CSTNode *c = cst_rule("field_declaration");
        cst_add(cst_parent, c);
    }
    if (accept(p, TOK_ASSIGN)) {
        ast_add_child(n, parse_expr(p, cst_parent));
    }
    expect_kind(p, TOK_SEMICOLON, "';' after field declaration");
    return n;
}

static ASTNode *parse_class_def(LangParser *p, CSTNode *cst_parent) {
    Token *kw = next_token(p); /* class */
    Token *name_tok = expect_kind(p, TOK_IDENTIFIER, "class name");
    if (cst_parent) {
        CSTNode *c = cst_rule("class_definition");
        cst_add(c, cst_leaf(kw));
        cst_add(c, cst_leaf(name_tok));
        cst_add(cst_parent, c);
    }

    ASTNode *cls = ast_make("ClassDef", name_tok);
    ast_add_str(cls, "name", name_tok->lexeme);

    if (peek_is(p, TOK_EXTENDS)) {
        next_token(p);
        Token *base = expect_kind(p, TOK_IDENTIFIER, "base class name");
        if (base) {
            ast_add_str(cls, "base", base->lexeme);
        }
    } else if (lang_is_cpp(p) && peek_is(p, TOK_COLON)) {
        /* C++: class Foo : public Bar */
        next_token(p);
        while (peek_kind(p) == TOK_IDENTIFIER ||
               peek_kind(p) == TOK_PUBLIC || peek_kind(p) == TOK_PRIVATE ||
               peek_kind(p) == TOK_PROTECTED || peek_kind(p) == TOK_VIRTUAL) {
            Token *bt = next_token(p);
            if (bt->kind == TOK_IDENTIFIER) {
                ast_add_str(cls, "base", bt->lexeme);
                break;
            }
        }
    }

    if (lang_is_java(p) && peek_is(p, TOK_IMPLEMENTS)) {
        next_token(p);
        while (peek_kind(p) == TOK_IDENTIFIER) {
            next_token(p);
            if (!accept(p, TOK_COMMA)) {
                break;
            }
        }
    }

    expect_kind(p, TOK_LBRACE, "'{' after class name");

    while (!peek_is(p, TOK_RBRACE) && !peek_is(p, TOK_EOF)) {
        int is_public = 0;
        int is_static = 0;
        int is_final = 0;
        size_t save = p->pos;

        for (;;) {
            TokenKind k = peek_kind(p);
            if (k == TOK_PUBLIC || k == TOK_PROTECTED) {
                is_public = 1;
                next_token(p);
            } else if (k == TOK_PRIVATE) {
                next_token(p);
            } else if (k == TOK_STATIC) {
                is_static = 1;
                next_token(p);
            } else if (k == TOK_FINAL) {
                is_final = 1;
                next_token(p);
            } else if (k == TOK_ABSTRACT) {
                next_token(p);
            } else if (k == TOK_SYNCHRONIZED) {
                next_token(p);
            } else {
                break;
            }
        }

        if (peek_is(p, TOK_SEMICOLON)) {
            next_token(p);
            continue;
        }
        if (peek_is(p, TOK_RBRACE)) {
            break;
        }
        /* static initializer block `static { ... }` — skip */
        if (is_static && peek_is(p, TOK_LBRACE)) {
            int depth = 0;
            do {
                if (peek_is(p, TOK_LBRACE)) {
                    depth++;
                } else if (peek_is(p, TOK_RBRACE)) {
                    depth--;
                }
                next_token(p);
            } while (depth > 0 && !peek_is(p, TOK_EOF));
            continue;
        }

        CType t;
        ctype_init(&t);
        if (!try_parse_type(p, &t)) {
            p->pos = save;
            syntax_error(p, peek_token(p), "expected class member declaration");
            next_token(p);
            continue;
        }

        /* constructor: Person(String n, int a) — the type itself is the
           name and '(' follows immediately */
        if (peek_is(p, TOK_LPAREN) && name_tok &&
            strcmp(t.base, name_tok->lexeme) == 0) {
            ASTNode *ctor = parse_function_def(p, cst_parent, &t, name_tok, 1,
                                               name_tok->lexeme, is_static,
                                               is_public, /*is_constructor*/ 1);
            ast_add_child(cls, ctor);
            continue;
        }

        char name[128] = "";
        Token *member_tok = peek_token(p);
        if (!parse_declarator(p, name, sizeof(name), &t)) {
            p->pos = save;
            syntax_error(p, peek_token(p), "expected member name");
            next_token(p);
            continue;
        }

        if (peek_is(p, TOK_LPAREN)) {
            /* method or constructor */
            int is_ctor = strcmp(name, name_tok->lexeme) == 0;
            ASTNode *m = parse_function_def(p, cst_parent, &t, member_tok, 1,
                                            name_tok->lexeme, is_static,
                                            is_public, is_ctor);
            ast_add_child(cls, m);
        } else {
            ASTNode *f = parse_field(p, cst_parent, &t, name);
            if (is_final) {
                ast_add_str(f, "final", "true");
            }
            if (is_static) {
                ast_add_str(f, "static", "true");
            }
            ast_add_child(cls, f);
        }
    }
    expect_kind(p, TOK_RBRACE, "'}' to close class");
    return cls;
}

static ASTNode *parse_interface_def(LangParser *p, CSTNode *cst_parent) {
    Token *kw = next_token(p); /* interface */
    Token *name_tok = expect_kind(p, TOK_IDENTIFIER, "interface name");
    ASTNode *cls = ast_make("ClassDef", name_tok);
    ast_add_str(cls, "name", name_tok->lexeme);
    ast_add_str(cls, "interface", "true");
    if (cst_parent) {
        CSTNode *c = cst_rule("interface_definition");
        cst_add(c, cst_leaf(kw));
        cst_add(cst_parent, c);
    }
    while (peek_is(p, TOK_IDENTIFIER)) {
        next_token(p);
        if (!accept(p, TOK_COMMA)) {
            break;
        }
    }
    expect_kind(p, TOK_LBRACE, "'{' after interface name");
    while (!peek_is(p, TOK_RBRACE) && !peek_is(p, TOK_EOF)) {
        if (accept(p, TOK_SEMICOLON)) {
            continue;
        }
        CType t;
        ctype_init(&t);
        if (!try_parse_type(p, &t)) {
            next_token(p);
            continue;
        }
        char name[128] = "";
        Token *member_tok = peek_token(p);
        if (!parse_declarator(p, name, sizeof(name), &t)) {
            next_token(p);
            continue;
        }
        if (peek_is(p, TOK_LPAREN)) {
            ASTNode *m = parse_function_def(p, cst_parent, &t, member_tok, 1,
                                            name_tok->lexeme, 0, 1, 0);
            ast_add_str(m, "is_abstract", "true");
            ast_add_child(cls, m);
        } else {
            ASTNode *f = parse_field(p, cst_parent, &t, name);
            ast_add_child(cls, f);
        }
    }
    expect_kind(p, TOK_RBRACE, "'}' to close interface");
    return cls;
}

/* --------------------------- top-level program --------------------------- */

/* Skip a C++ namespace body, template header, or Java annotation so the
   parser stays aligned on real-world sources. */
static void skip_balanced_block(LangParser *p) {
    int depth = 0;
    do {
        if (peek_is(p, TOK_LBRACE)) {
            depth++;
        } else if (peek_is(p, TOK_RBRACE)) {
            depth--;
        }
        next_token(p);
    } while (depth > 0 && !peek_is(p, TOK_EOF));
}

static ASTNode *parse_program_item(LangParser *p, CSTNode *cst_parent) {
    Token *tok = peek_token(p);
    if (!tok) {
        return NULL;
    }

    /* leading modifiers (Java: public class Foo, C++: static int helper) */
    while (peek_kind(p) == TOK_PUBLIC || peek_kind(p) == TOK_PRIVATE ||
           peek_kind(p) == TOK_PROTECTED || peek_kind(p) == TOK_STATIC ||
           peek_kind(p) == TOK_FINAL || peek_kind(p) == TOK_ABSTRACT ||
           peek_kind(p) == TOK_SYNCHRONIZED) {
        next_token(p);
    }
    tok = peek_token(p);
    if (!tok) {
        return NULL;
    }

    switch (tok->kind) {
    case TOK_PREPROC:
        next_token(p);
        return NULL;
    case TOK_IMPORT:
    case TOK_PACKAGE:
        while (!peek_is(p, TOK_SEMICOLON) && !peek_is(p, TOK_EOF)) {
            next_token(p);
        }
        accept(p, TOK_SEMICOLON);
        return NULL;
    case TOK_USING:
        /* using namespace std;  or  using std::cout; */
        while (!peek_is(p, TOK_SEMICOLON) && !peek_is(p, TOK_EOF)) {
            next_token(p);
        }
        accept(p, TOK_SEMICOLON);
        return NULL;
    case TOK_NAMESPACE: {
        next_token(p);
        if (peek_kind(p) == TOK_IDENTIFIER) {
            next_token(p);
        }
        if (peek_is(p, TOK_ASSIGN)) {
            /* namespace alias Foo = Bar; */
            next_token(p);
            while (!peek_is(p, TOK_SEMICOLON) && !peek_is(p, TOK_EOF)) {
                next_token(p);
            }
            accept(p, TOK_SEMICOLON);
            return NULL;
        }
        skip_balanced_block(p);
        return NULL;
    }
    case TOK_TEMPLATE: {
        /* template <...> class/function — drop the header, parse the rest */
        next_token(p);
        int depth = 0;
        while (!peek_is(p, TOK_EOF)) {
            if (peek_is(p, TOK_LT)) {
                depth++;
            } else if (peek_is(p, TOK_GT)) {
                if (depth == 0) {
                    next_token(p);
                    break;
                }
                depth--;
            }
            next_token(p);
        }
        return NULL; /* the following declaration is consumed next loop */
    }
    case TOK_CLASS:
        return parse_class_def(p, cst_parent);
    case TOK_INTERFACE:
        return parse_interface_def(p, cst_parent);
    case TOK_TYPEDEF: {
        next_token(p);
        while (!peek_is(p, TOK_SEMICOLON) && !peek_is(p, TOK_EOF)) {
            if (peek_is(p, TOK_LBRACE)) {
                skip_balanced_block(p);
                break;
            }
            next_token(p);
        }
        accept(p, TOK_SEMICOLON);
        return NULL;
    }
    case TOK_STRUCT:
    case TOK_ENUM:
    case TOK_UNION: {
        /* struct Foo { ... };  or struct Foo; */
        next_token(p);
        if (peek_kind(p) == TOK_IDENTIFIER) {
            next_token(p);
        }
        if (peek_is(p, TOK_LBRACE)) {
            skip_balanced_block(p);
        }
        accept(p, TOK_SEMICOLON);
        return NULL;
    }
    case TOK_SEMICOLON:
        next_token(p);
        return NULL;
    case TOK_EXTERN:
    case TOK_INLINE:
        next_token(p);
        return NULL;
    default:
        break;
    }

    /* function or global variable declaration */
    {
        size_t save = p->pos;
        ASTNode *decl = parse_declaration(p, cst_parent);
        if (decl) {
            return decl;
        }
        p->pos = save;
    }

    /* stray statement at top level (e.g. a loose expression) */
    syntax_error(p, tok, "unexpected token '%s' at top level", tok->lexeme);
    next_token(p);
    return NULL;
}

/* --------------------------- entry point --------------------------- */

int lang_parse_tokens(TokenList *tokens, const char *language, ParseResult *out) {
    out->cst_root = NULL;
    out->ast_root = NULL;
    out->errors = (SyntaxErrorList){0};

    LangParser p;
    p.tokens = tokens->items;
    p.count = tokens->len;
    p.pos = 0;
    p.errors = &out->errors;
    p.language = lang_id(language);

    g_lang_ast_id = 1;

    CSTNode *cst_prog = cst_rule("program");
    ASTNode *ast_prog = ast_make("Program", NULL);

    while (peek_kind(&p) != TOK_EOF) {
        size_t errs_before = p.errors->len;
        size_t pos_before = p.pos;
        ASTNode *item = parse_program_item(&p, cst_prog);
        if (item) {
            if (strcmp(item->node_type, "FunctionDef") == 0 ||
                strcmp(item->node_type, "ClassDef") == 0) {
                ast_add_child(ast_prog, item);
            } else if (strcmp(item->node_type, "Block") == 0 &&
                       item->nchildren > 0) {
                /* multi-variable declaration at top level */
                for (size_t i = 0; i < item->nchildren; i++) {
                    ast_add_child(ast_prog, item->children[i]);
                }
                item->nchildren = 0;
                ast_free(item);
            } else {
                ast_add_child(ast_prog, item);
            }
        }
        if (p.errors->len > errs_before && p.pos == pos_before) {
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
