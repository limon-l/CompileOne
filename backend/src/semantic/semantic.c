#include "semantic.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
   Semantic analysis: scoped symbol table + expression typing.
   ============================================================ */

DARRAY_DEFINE(Symbol, SymbolList)
DARRAY_DEFINE(SemanticDiagnostic, SemanticDiagnosticList)

typedef struct SemCtx {
    SymbolList symbols;          /* resolution table (current scope chain) */
    size_t *scope_starts;        /* stack: symbols.len at each scope entry */
    size_t scope_depth;
    size_t scope_cap;
    SymbolList all_symbols;      /* output registry (never truncated) */
    SemanticDiagnosticList diagnostics;
    int has_error;
} SemCtx;

/* --------------------------- diagnostics --------------------------- */

static void diagv(SemCtx *ctx, const char *severity, const char *code,
                  int line, int column, const char *fmt, va_list ap) {
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);

    SemanticDiagnostic d;
    d.line = line;
    d.column = column;
    d.severity = severity;
    d.code = code;
    d.message = co1_strdup(buf);
    SemanticDiagnosticList_push(&ctx->diagnostics, d);

    if (strcmp(severity, "error") == 0) {
        ctx->has_error = 1;
    }
}

static void diag(SemCtx *ctx, const char *severity, const char *code,
                 int line, int column, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    diagv(ctx, severity, code, line, column, fmt, ap);
    va_end(ap);
}

/* --------------------------- scope stack --------------------------- */
/* Scope 0 is the implicit global scope (scope_starts[0] = 0). Each
   push_scope() opens a new block scope; pop_scope() restores the
   symbol table to the state at entry of that block. */

static void push_scope(SemCtx *ctx) {
    if (ctx->scope_depth + 1 >= ctx->scope_cap) {
        ctx->scope_cap = ctx->scope_cap == 0 ? 8 : ctx->scope_cap * 2;
        ctx->scope_starts = (size_t *)realloc(
            ctx->scope_starts, ctx->scope_cap * sizeof(size_t));
    }
    ctx->scope_depth++;
    ctx->scope_starts[ctx->scope_depth] = ctx->symbols.len;
}

static void pop_scope(SemCtx *ctx) {
    ctx->symbols.len = ctx->scope_starts[ctx->scope_depth--];
}

static int line_of(const ASTNode *n) {
    return (n && n->token) ? n->token->line : 0;
}

static int column_of(const ASTNode *n) {
    return (n && n->token) ? n->token->column : 0;
}

static const char *node_name(const ASTNode *n) {
    const char *name = ast_get_prop(n, "name");
    if (name) {
        return name;
    }
    if (n && n->token && n->token->lexeme) {
        return n->token->lexeme;
    }
    return "";
}

static const char *current_scope_name(SemCtx *ctx) {
    if (ctx->scope_depth == 0) {
        return "global";
    }
    static char buf[24];
    snprintf(buf, sizeof(buf), "block:%d", (int)ctx->scope_depth);
    return buf;
}

static Symbol *declare(SemCtx *ctx, const char *name, const char *type,
                       int is_const, int line, int column) {
    Symbol s;
    s.name = co1_strdup(name);
    s.type = type;
    s.scope = current_scope_name(ctx);
    s.scope_level = (int)ctx->scope_depth;
    s.is_const = is_const;
    s.line = line;
    s.column = column;
    s.used = 0;
    s.out_index = ctx->all_symbols.len;

    SymbolList_push(&ctx->symbols, s);
    /* duplicate into the output registry for the artifact */
    Symbol copy = s;
    copy.name = co1_strdup(name);
    SymbolList_push(&ctx->all_symbols, copy);
    return &ctx->symbols.items[ctx->symbols.len - 1];
}

/* Resolve `name` through the scope chain (innermost first). */
static Symbol *lookup(SemCtx *ctx, const char *name) {
    size_t scope = ctx->scope_depth;
    for (;;) {
        size_t start = ctx->scope_starts[scope];
        size_t end = (scope == ctx->scope_depth) ? ctx->symbols.len
                                                 : ctx->scope_starts[scope + 1];
        for (size_t i = end; i > start; i--) {
            if (strcmp(ctx->symbols.items[i - 1].name, name) == 0) {
                return &ctx->symbols.items[i - 1];
            }
        }
        if (scope == 0) {
            return NULL;
        }
        scope--;
    }
}

static Symbol *lookup_current_scope(SemCtx *ctx, const char *name) {
    size_t start = ctx->scope_starts[ctx->scope_depth];
    for (size_t i = ctx->symbols.len; i > start; i--) {
        if (strcmp(ctx->symbols.items[i - 1].name, name) == 0) {
            return &ctx->symbols.items[i - 1];
        }
    }
    return NULL;
}

static void mark_used(SemCtx *ctx, Symbol *sym) {
    sym->used = 1;
    ctx->all_symbols.items[sym->out_index].used = 1;
}

/* --------------------------- type helpers --------------------------- */

static int is_numeric(const char *t) {
    return t && (strcmp(t, "int") == 0 || strcmp(t, "float") == 0 ||
                 strcmp(t, "char") == 0);
}

static int is_integer(const char *t) {
    return t && (strcmp(t, "int") == 0 || strcmp(t, "char") == 0);
}

static int is_unknown(const char *t) {
    return t && strcmp(t, "unknown") == 0;
}

/* Forward declaration (expressions can nest). */
static const char *analyze_expr(SemCtx *ctx, ASTNode *node);

/* ------------------------- assignment rules ------------------------ */

static void check_assign(SemCtx *ctx, const char *target, const char *src,
                         int line, int column) {
    if (!src || !target || strcmp(target, src) == 0 || is_unknown(src)) {
        return;
    }
    if (strcmp(target, "float") == 0) {
        if (strcmp(src, "int") == 0 || strcmp(src, "char") == 0 ||
            strcmp(src, "bool") == 0) {
            return; /* widening / bool-to-float allowed */
        }
    } else if (strcmp(target, "int") == 0) {
        if (strcmp(src, "char") == 0 || strcmp(src, "bool") == 0) {
            return;
        }
        if (strcmp(src, "float") == 0) {
            diag(ctx, "warning", "SEM007", line, column,
                 "implicit conversion from 'float' to 'int' may lose precision");
            return;
        }
    } else if (strcmp(target, "char") == 0) {
        if (strcmp(src, "int") == 0) {
            diag(ctx, "warning", "SEM007", line, column,
                 "implicit conversion from 'int' to 'char' may truncate");
            return;
        }
    } else if (strcmp(target, "bool") == 0) {
        if (strcmp(src, "int") == 0 || strcmp(src, "char") == 0) {
            diag(ctx, "warning", "SEM007", line, column,
                 "implicit conversion from '%s' to 'bool'", src);
            return;
        }
    }
    diag(ctx, "error", "SEM004", line, column,
         "cannot assign value of type '%s' to variable of type '%s'",
         src, target);
}

static const char *promote(const char *a, const char *b) {
    if (strcmp(a, "float") == 0 || strcmp(b, "float") == 0) {
        return "float";
    }
    return "int";
}

/* ------------------------- expression typing ------------------------ */

static const char *analyze_identifier(SemCtx *ctx, ASTNode *node) {
    const char *name = node_name(node);
    Symbol *sym = lookup(ctx, name);
    if (!sym) {
        diag(ctx, "error", "SEM001", line_of(node), column_of(node),
             "use of undeclared identifier '%s'", name);
        ast_annotate(node, "type", "unknown");
        return "unknown";
    }
    mark_used(ctx, sym);
    const char *t = sym->type;
    ast_annotate(node, "type", t);
    return t;
}

static const char *analyze_binary(SemCtx *ctx, ASTNode *node) {
    const ASTNode *l = ast_child(node, 0);
    const ASTNode *r = ast_child(node, 1);
    const char *op = ast_get_prop(node, "op");
    const char *lt = analyze_expr(ctx, (ASTNode *)l);
    const char *rt = analyze_expr(ctx, (ASTNode *)r);
    int line = line_of(node);
    int col = column_of(node);

    if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 ||
        strcmp(op, "*") == 0 || strcmp(op, "/") == 0) {
        if (!is_unknown(lt) && !is_unknown(rt) &&
            !(is_numeric(lt) && is_numeric(rt))) {
            diag(ctx, "error", "SEM005", line, col,
                 "operator '%s' expects numeric operands, got '%s' and '%s'",
                 op, lt, rt);
        }
        const char *t = (is_unknown(lt) || is_unknown(rt))
                            ? "unknown"
                            : promote(lt, rt);
        ast_annotate(node, "type", t);
        return t;
    }
    if (strcmp(op, "%") == 0) {
        if (!is_unknown(lt) && !is_unknown(rt)) {
            if (!(is_integer(lt) && is_integer(rt))) {
                diag(ctx, "error", "SEM005", line, col,
                     "operator '%%' expects integer operands, got '%s' and '%s'",
                     lt, rt);
            }
        }
        const char *t = (is_unknown(lt) || is_unknown(rt)) ? "unknown" : "int";
        ast_annotate(node, "type", t);
        return t;
    }
    if (strcmp(op, "<") == 0 || strcmp(op, "<=") == 0 ||
        strcmp(op, ">") == 0 || strcmp(op, ">=") == 0) {
        if (!is_unknown(lt) && !is_unknown(rt) &&
            !(is_numeric(lt) && is_numeric(rt))) {
            diag(ctx, "error", "SEM005", line, col,
                 "operator '%s' expects numeric operands, got '%s' and '%s'",
                 op, lt, rt);
        }
        ast_annotate(node, "type", "bool");
        return "bool";
    }
    if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) {
        if (!is_unknown(lt) && !is_unknown(rt) &&
            (is_numeric(lt) != is_numeric(rt))) {
            diag(ctx, "error", "SEM005", line, col,
                 "operator '%s' mixes numeric and non-numeric operands "
                 "('-%s' and '%s')", op, lt, rt);
        }
        ast_annotate(node, "type", "bool");
        return "bool";
    }
    if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
        if (!is_unknown(lt) && strcmp(lt, "bool") != 0) {
            diag(ctx, "warning", "SEM006", line, col,
                 "operand of '%s' is '%s', expected 'bool'", op, lt);
        }
        if (!is_unknown(rt) && strcmp(rt, "bool") != 0) {
            diag(ctx, "warning", "SEM006", line, col,
                 "operand of '%s' is '%s', expected 'bool'", op, rt);
        }
        ast_annotate(node, "type", "bool");
        return "bool";
    }
    ast_annotate(node, "type", "unknown");
    return "unknown";
}

static const char *analyze_unary(SemCtx *ctx, ASTNode *node) {
    const ASTNode *operand = ast_child(node, 0);
    const char *op = ast_get_prop(node, "op");
    const char *t = analyze_expr(ctx, (ASTNode *)operand);
    int line = line_of(node);
    int col = column_of(node);

    if (strcmp(op, "-") == 0) {
        if (!is_unknown(t) && !is_numeric(t)) {
            diag(ctx, "error", "SEM005", line, col,
                 "unary '-' expects a numeric operand, got '%s'", t);
        }
        const char *res = is_unknown(t) ? "unknown" : t;
        ast_annotate(node, "type", res);
        return res;
    }
    if (strcmp(op, "!") == 0) {
        if (!is_unknown(t) && strcmp(t, "string") == 0) {
            diag(ctx, "error", "SEM005", line, col,
                 "unary '!' cannot be applied to '%s'", t);
        }
        ast_annotate(node, "type", "bool");
        return "bool";
    }
    ast_annotate(node, "type", t);
    return t;
}

static const char *analyze_incdec(SemCtx *ctx, ASTNode *node) {
    const ASTNode *target = ast_child(node, 0);
    const char *name = node_name(target);
    const char *op = ast_get_prop(node, "op");
    Symbol *sym = lookup(ctx, name);
    if (!sym) {
        diag(ctx, "error", "SEM001", line_of(target), column_of(target),
             "use of undeclared identifier '%s'", name);
    } else {
        mark_used(ctx, sym);
        if (sym->is_const) {
            diag(ctx, "error", "SEM003", line_of(node), column_of(node),
                 "cannot '%s' const variable '%s'", op, name);
        }
    }
    const char *t = sym ? sym->type : "unknown";
    ast_annotate(node, "type", t);
    return t;
}

static const char *analyze_expr(SemCtx *ctx, ASTNode *node) {
    if (!node) {
        return "unknown";
    }
    const char *type = node->node_type;
    if (strcmp(type, "IntLit") == 0) {
        ast_annotate(node, "type", "int");
        return "int";
    }
    if (strcmp(type, "FloatLit") == 0) {
        ast_annotate(node, "type", "float");
        return "float";
    }
    if (strcmp(type, "BoolLit") == 0) {
        ast_annotate(node, "type", "bool");
        return "bool";
    }
    if (strcmp(type, "StringLit") == 0) {
        ast_annotate(node, "type", "string");
        return "string";
    }
    if (strcmp(type, "Identifier") == 0) {
        return analyze_identifier(ctx, node);
    }
    if (strcmp(type, "BinaryOp") == 0) {
        return analyze_binary(ctx, node);
    }
    if (strcmp(type, "UnaryOp") == 0) {
        return analyze_unary(ctx, node);
    }
    if (strcmp(type, "IncDec") == 0) {
        return analyze_incdec(ctx, node);
    }
    return "unknown";
}

/* --------------------------- statements ---------------------------- */

static void check_condition(SemCtx *ctx, const ASTNode *cond) {
    if (!cond) {
        return;
    }
    const char *t = analyze_expr(ctx, (ASTNode *)cond);
    if (!is_unknown(t) && strcmp(t, "string") == 0) {
        diag(ctx, "warning", "SEM006", line_of(cond), column_of(cond),
             "condition has type '%s', expected a boolean expression", t);
    }
}

static void analyze_stmt(SemCtx *ctx, ASTNode *node);

static void analyze_block(SemCtx *ctx, ASTNode *node) {
    push_scope(ctx);
    for (size_t i = 0; i < node->nchildren; i++) {
        analyze_stmt(ctx, node->children[i]);
    }
    pop_scope(ctx);
}

static void analyze_vardecl(SemCtx *ctx, ASTNode *node) {
    const char *name = node_name(node);
    const char *type = ast_get_prop(node, "type_name");
    int is_const = ast_get_prop(node, "const") != NULL &&
                   strcmp(ast_get_prop(node, "const"), "true") == 0;

    if (lookup_current_scope(ctx, name)) {
        diag(ctx, "error", "SEM002", line_of(node), column_of(node),
             "redeclaration of identifier '%s' in the same scope", name);
        return;
    }
    Symbol *sym = declare(ctx, name, type, is_const, line_of(node), column_of(node));

    const ASTNode *init = ast_child(node, 0);
    if (init) {
        const char *it = analyze_expr(ctx, (ASTNode *)init);
        if (!is_unknown(it)) {
            check_assign(ctx, type, it, line_of(init), column_of(init));
        }
        ast_annotate(node, "init_type", it);
    }
    ast_annotate(node, "type", type);
    (void)sym;
}

static void analyze_assign(SemCtx *ctx, ASTNode *node) {
    const char *name = node_name(node);
    Symbol *sym = lookup(ctx, name);
    if (!sym) {
        diag(ctx, "error", "SEM001", line_of(node), column_of(node),
             "assignment to undeclared identifier '%s'", name);
        ast_annotate(node, "type", "unknown");
        return;
    }
    mark_used(ctx, sym);
    if (sym->is_const) {
        diag(ctx, "error", "SEM003", line_of(node), column_of(node),
             "cannot assign to const variable '%s'", name);
    }
    const ASTNode *expr = ast_child(node, 0);
    const char *t = analyze_expr(ctx, (ASTNode *)expr);
    if (!is_unknown(t)) {
        check_assign(ctx, sym->type, t, line_of(node), column_of(node));
    }
    ast_annotate(node, "type", sym->type);
}

static void analyze_read(SemCtx *ctx, ASTNode *node) {
    const char *name = node_name(node);
    Symbol *sym = lookup(ctx, name);
    if (!sym) {
        diag(ctx, "error", "SEM001", line_of(node), column_of(node),
             "'read' into undeclared identifier '%s'", name);
        return;
    }
    mark_used(ctx, sym);
    if (sym->is_const) {
        diag(ctx, "error", "SEM003", line_of(node), column_of(node),
             "cannot 'read' into const variable '%s'", name);
    }
    ast_annotate(node, "type", sym->type);
}

static void analyze_print(SemCtx *ctx, ASTNode *node) {
    const ASTNode *arg = ast_child(node, 0);
    if (arg) {
        analyze_expr(ctx, (ASTNode *)arg);
    }
    ast_annotate(node, "type", "void");
}

static void analyze_return(SemCtx *ctx, ASTNode *node) {
    const ASTNode *expr = ast_child(node, 0);
    if (expr) {
        analyze_expr(ctx, (ASTNode *)expr);
    }
    diag(ctx, "warning", "SEM008", line_of(node), column_of(node),
         "'return' outside a function has no effect (mini-c has no functions)");
    ast_annotate(node, "type", "void");
}

static void analyze_stmt(SemCtx *ctx, ASTNode *node) {
    if (!node) {
        return;
    }
    const char *type = node->node_type;

    if (strcmp(type, "Block") == 0) {
        analyze_block(ctx, node);
    } else if (strcmp(type, "VarDecl") == 0) {
        analyze_vardecl(ctx, node);
    } else if (strcmp(type, "Assign") == 0) {
        analyze_assign(ctx, node);
    } else if (strcmp(type, "Read") == 0) {
        analyze_read(ctx, node);
    } else if (strcmp(type, "Print") == 0) {
        analyze_print(ctx, node);
    } else if (strcmp(type, "If") == 0) {
        check_condition(ctx, ast_child(node, 0));
        analyze_stmt(ctx, (ASTNode *)ast_child(node, 1));
        if (node->nchildren > 2) {
            analyze_stmt(ctx, node->children[2]);
        }
        ast_annotate(node, "type", "void");
    } else if (strcmp(type, "While") == 0) {
        check_condition(ctx, ast_child(node, 0));
        analyze_stmt(ctx, (ASTNode *)ast_child(node, 1));
        ast_annotate(node, "type", "void");
    } else if (strcmp(type, "For") == 0) {
        push_scope(ctx);
        if (node->nchildren > 0) {
            analyze_stmt(ctx, node->children[0]); /* init */
        }
        if (node->nchildren > 1) {
            check_condition(ctx, ast_child(node, 1)); /* cond */
        }
        if (node->nchildren > 2) {
            analyze_expr(ctx, node->children[2]); /* step */
        }
        if (node->nchildren > 3) {
            analyze_stmt(ctx, node->children[3]); /* body */
        }
        pop_scope(ctx);
        ast_annotate(node, "type", "void");
    } else if (strcmp(type, "Return") == 0) {
        analyze_return(ctx, node);
    } else if (strcmp(type, "IncDec") == 0) {
        analyze_incdec(ctx, node);
    }
}

/* --------------------------- entry point --------------------------- */

void semantic_analyze(const ASTNode *program, SemanticResult *out) {
    out->symbols = (SymbolList){0};
    out->diagnostics = (SemanticDiagnosticList){0};
    out->valid = 1;

    SemCtx ctx = {0};
    ctx.scope_cap = 8;
    ctx.scope_starts = (size_t *)malloc(ctx.scope_cap * sizeof(size_t));
    ctx.scope_starts[0] = 0; /* implicit global scope */

    if (program) {
        for (size_t i = 0; i < program->nchildren; i++) {
            analyze_stmt(&ctx, program->children[i]);
        }
    }

    /* unused-variable warnings */
    for (size_t i = 0; i < ctx.all_symbols.len; i++) {
        Symbol *s = &ctx.all_symbols.items[i];
        if (!s->used) {
            diag(&ctx, "warning", "SEM009", s->line, s->column,
                 "variable '%s' is declared but never used", s->name);
        }
    }

    out->symbols = ctx.all_symbols;
    out->diagnostics = ctx.diagnostics;
    out->valid = !ctx.has_error;

    for (size_t i = 0; i < ctx.symbols.len; i++) {
        free((char *)ctx.symbols.items[i].name);
    }
    free(ctx.symbols.items);
    free(ctx.scope_starts);
    ctx.symbols = (SymbolList){0};
    ctx.scope_starts = NULL;
    ctx.scope_depth = 0;
}

void semantic_result_free(SemanticResult *r) {
    for (size_t i = 0; i < r->symbols.len; i++) {
        free((char *)r->symbols.items[i].name);
    }
    SymbolList_free(&r->symbols);

    for (size_t i = 0; i < r->diagnostics.len; i++) {
        free(r->diagnostics.items[i].message);
    }
    SemanticDiagnosticList_free(&r->diagnostics);
}
